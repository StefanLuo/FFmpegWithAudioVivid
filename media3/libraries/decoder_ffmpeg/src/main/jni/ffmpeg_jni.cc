/*
 * FFmpeg JNI Wrapper - Ultimate Stability Version for AVS3 10ch
 */
#include <android/log.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <unistd.h>
#include <sys/types.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}

#define LOG_TAG "ffmpeg_jni"
#define LOGE(...) ((void)__android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__))

static const AVSampleFormat OUTPUT_FORMAT_PCM_16BIT = AV_SAMPLE_FMT_S16;
static const int AUDIO_DECODER_ERROR_INVALID_DATA = -1;
static const int AUDIO_DECODER_ERROR_OTHER = -2;

static jmethodID growOutputBufferMethod;
static jmethodID initForYuvFrameMethod;

struct JniDecoderContext {
    SwrContext* swrContext;
    struct SwsContext* swsContext;
    int targetSampleRate;
    int targetChannelCount;
    int lastInSampleRate;
    AVChannelLayout lastInLayout;
    bool inLayoutValid;
    AVChannelLayout targetLayout;
    bool targetLayoutValid;

    JniDecoderContext() : swrContext(NULL), swsContext(NULL),
                          targetSampleRate(48000), targetChannelCount(2),
                          lastInSampleRate(0), inLayoutValid(false),
                          targetLayoutValid(false) {
        memset(&lastInLayout, 0, sizeof(AVChannelLayout));
        memset(&targetLayout, 0, sizeof(AVChannelLayout));
    }
};

const AVCodec* getCodecByName(JNIEnv* env, jstring codecName) {
  if (!codecName) return NULL;
  const char* name = env->GetStringUTFChars(codecName, NULL);
  const AVCodec* codec = avcodec_find_decoder_by_name(name);
  env->ReleaseStringUTFChars(codecName, name);
  return codec;
}

// void logError(const char* functionName, int errorNumber) {
//   char buffer[256];
//   av_strerror(errorNumber, buffer, 256);
//   LOGE("Error in %s: %s", functionName, buffer);
// }

void releaseContext(AVCodecContext* context) {
  if (!context) return;
  if (avcodec_is_open(context)) avcodec_flush_buffers(context);
  JniDecoderContext* jniCtx = (JniDecoderContext*)context->opaque;
  if (jniCtx) {
      if (jniCtx->swrContext) swr_free(&jniCtx->swrContext);
      if (jniCtx->swsContext) sws_freeContext(jniCtx->swsContext);
      if (jniCtx->inLayoutValid) av_channel_layout_uninit(&jniCtx->lastInLayout);
      if (jniCtx->targetLayoutValid) av_channel_layout_uninit(&jniCtx->targetLayout);
      delete jniCtx;
  }
  context->opaque = NULL;
  avcodec_free_context(&context);
}

AVCodecContext* createContext(JNIEnv* env, const AVCodec* codec,
                              jbyteArray extraData, jboolean outputFloat,
                              jint rawSampleRate, jint rawChannelCount,
                              jstring modelPath) {
  AVCodecContext* context = avcodec_alloc_context3(codec);
  if (!context) return NULL;
  context->flags |= AV_CODEC_FLAG_LOW_DELAY;
  if (strcmp(codec->name, "avs3_audio") == 0) context->flags2 |= AV_CODEC_FLAG2_FAST;

  AVChannelLayout targetLayout;
  int targetChannelCount = rawChannelCount;
  if (targetChannelCount <= 0) targetChannelCount = 2;
  av_channel_layout_default(&targetLayout, targetChannelCount);
  av_channel_layout_copy(&context->ch_layout, &targetLayout);
  av_channel_layout_uninit(&targetLayout);
  JniDecoderContext* jniCtx = new JniDecoderContext();
  jniCtx->targetSampleRate = (rawSampleRate > 0) ? rawSampleRate : 48000;
  jniCtx->targetChannelCount = targetChannelCount;
  context->opaque = jniCtx;

  if (codec->type == AVMEDIA_TYPE_AUDIO) context->request_sample_fmt = OUTPUT_FORMAT_PCM_16BIT;

  if (extraData) {
    jsize size = env->GetArrayLength(extraData);
    context->extradata = (uint8_t*)av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (context->extradata) {
        context->extradata_size = size;
        env->GetByteArrayRegion(extraData, 0, size, (jbyte*)context->extradata);
    }
  }

  AVDictionary* options = NULL;
  if (modelPath) {
    const char* path = env->GetStringUTFChars(modelPath, NULL);
    av_dict_set(&options, "av3a_model_path", path, 0);
    env->ReleaseStringUTFChars(modelPath, path);
  }

  if (avcodec_open2(context, codec, &options) < 0) {
    if (options) av_dict_free(&options);
    releaseContext(context);
    return NULL;
  }

  if (options) av_dict_free(&options);

  if (strcmp(codec->name, "avs3_audio") == 0) {
    setpriority(PRIO_PROCESS, 0, -16);
  }
  return context;
}

struct GrowOutputBufferCallback {
  uint8_t* operator()(int requiredSize) const;
  JNIEnv* env;
  jobject thiz;
  jobject decoderOutputBuffer;
};

int receiveFrames(AVCodecContext* context, uint8_t** outputBufferPtr, int* outputSizePtr,
                 GrowOutputBufferCallback growBuffer, int outTotalSize) {
  JniDecoderContext* jniCtx = (JniDecoderContext*)context->opaque;
  uint8_t* outputBuffer = *outputBufferPtr;
  int outputSize = *outputSizePtr;

  while (true) {
    AVFrame* frame = av_frame_alloc();
    int ret = avcodec_receive_frame(context, frame);
    if (ret < 0) {
      av_frame_free(&frame);
      if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
      return AUDIO_DECODER_ERROR_OTHER;
    }

    bool needsRebuild = !jniCtx->swrContext ||
                        frame->sample_rate != jniCtx->lastInSampleRate ||
                        !jniCtx->inLayoutValid ||
                        av_channel_layout_compare(&frame->ch_layout, &jniCtx->lastInLayout) != 0;

    if (needsRebuild) {
      if (jniCtx->swrContext) swr_free(&jniCtx->swrContext);

      /*
       * Determine the Android/Media3 target layout from the actual
       * decoded AVS3 channel layout.
       */
      if (!jniCtx->targetLayoutValid) {
        AVChannelLayout detectedTarget;

        memset(&detectedTarget, 0, sizeof(detectedTarget));

        AVChannelLayout layout512 = AV_CHANNEL_LAYOUT_5POINT1POINT2_BACK;
        AVChannelLayout layout514 = AV_CHANNEL_LAYOUT_5POINT1POINT4_BACK;
        AVChannelLayout layout712 = AV_CHANNEL_LAYOUT_7POINT1POINT2;
        AVChannelLayout layout714 = AV_CHANNEL_LAYOUT_7POINT1POINT4_BACK;

        bool is51 = av_channel_layout_compare(&frame->ch_layout, &layout512) == 0 || av_channel_layout_compare(&frame->ch_layout, &layout514) == 0;

        bool is71 = av_channel_layout_compare(&frame->ch_layout, &layout712) == 0 || av_channel_layout_compare(&frame->ch_layout, &layout714) == 0;

        if (is51) {
          av_channel_layout_from_mask(&detectedTarget, AV_CH_LAYOUT_5POINT1);
        } else if (is71) {
          av_channel_layout_from_mask(&detectedTarget, AV_CH_LAYOUT_7POINT1);
        } else {
          /*
           * Other channel layouts are passed through unchanged.
           */
          av_channel_layout_copy(&detectedTarget, &frame->ch_layout);
        }

        av_channel_layout_copy(&jniCtx->targetLayout, &detectedTarget);
        av_channel_layout_uninit(&detectedTarget);

        jniCtx->targetLayoutValid = true;
        jniCtx->targetChannelCount = jniCtx->targetLayout.nb_channels;
      }

      AVChannelLayout inLayout;
      AVChannelLayout outLayout;

      memset(&inLayout, 0, sizeof(inLayout));
      memset(&outLayout, 0, sizeof(outLayout));

      av_channel_layout_copy(&inLayout, &frame->ch_layout);

      av_channel_layout_copy(&outLayout, &jniCtx->targetLayout);

      int ret = swr_alloc_set_opts2(&jniCtx->swrContext, &outLayout, context->request_sample_fmt, jniCtx->targetSampleRate,
                &inLayout, (AVSampleFormat)frame->format, frame->sample_rate, 0, NULL);

      av_channel_layout_uninit(&inLayout);
      av_channel_layout_uninit(&outLayout);

      if (ret < 0 || !jniCtx->swrContext || swr_init(jniCtx->swrContext) < 0) {
        av_frame_free(&frame);
        return AUDIO_DECODER_ERROR_OTHER;
      }

      jniCtx->lastInSampleRate = frame->sample_rate;

      if (jniCtx->inLayoutValid) av_channel_layout_uninit(&jniCtx->lastInLayout);

      av_channel_layout_copy(&jniCtx->lastInLayout, &frame->ch_layout);

      jniCtx->inLayoutValid = true;
    }

    int produce_samples = swr_get_out_samples(jniCtx->swrContext, frame->nb_samples);
    int frameBufferSize = av_samples_get_buffer_size(NULL, jniCtx->targetChannelCount, produce_samples, context->request_sample_fmt, 1);

    if (outTotalSize + frameBufferSize > outputSize) {
      outputSize = outTotalSize + frameBufferSize + 8192;
      uint8_t* newBuf = growBuffer(outputSize);
      if (!newBuf) { av_frame_free(&frame); return -2; }
      outputBuffer = newBuf;
    }

    uint8_t* writePtr = outputBuffer + outTotalSize;
    int converted = swr_convert(jniCtx->swrContext, &writePtr, produce_samples, (const uint8_t**)frame->extended_data, frame->nb_samples);
    if (converted > 0) outTotalSize += converted * jniCtx->targetChannelCount * av_get_bytes_per_sample(context->request_sample_fmt);

    av_frame_free(&frame);
  }
  *outputBufferPtr = outputBuffer;
  *outputSizePtr = outputSize;
  return outTotalSize;
}

int decodePacket(AVCodecContext* context, AVPacket* packet,
                 uint8_t* outputBuffer, int outputSize,
                 GrowOutputBufferCallback growBuffer) {
  if (!context || !packet) {
      return AUDIO_DECODER_ERROR_OTHER;
  }

  /*
   * Av3aReader 已经保证 Media3 inputBuffer 是一个完整的 AV3A frame。
   * 这里直接将该 frame 交给 FFmpeg decoder。
   */
  int ret = avcodec_send_packet(context, packet);

  if (ret == AVERROR_INVALIDDATA) {
      /*
       * 保持与 Java 层现有逻辑兼容：
       * 当前 AV3A sample 无效时跳过这个 sample，而不是让整个 decoder 失败。
       */
      return AUDIO_DECODER_ERROR_INVALID_DATA;
  }

  if (ret < 0) {
      return AUDIO_DECODER_ERROR_OTHER;
  }

  /*
   * FFmpeg decoder 可能一次 send_packet 后产生一个或多个 frame，
   * receiveFrames() 会一直取到 EAGAIN/EOF。
   */
  return receiveFrames(context, &outputBuffer, &outputSize, growBuffer, 0);
}

uint8_t* GrowOutputBufferCallback::operator()(int requiredSize) const {
  jobject newOutputData = env->CallObjectMethod(thiz, growOutputBufferMethod, decoderOutputBuffer, requiredSize);
  return (env->ExceptionCheck()) ? NULL : (uint8_t*)env->GetDirectBufferAddress(newOutputData);
}

jint JNI_OnLoad(JavaVM* vm, void* reserved) {
  JNIEnv* env;
  if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) != JNI_OK) return -1;
  jclass clazz = env->FindClass("androidx/media3/decoder/ffmpeg/FfmpegAudioDecoder");
  growOutputBufferMethod = env->GetMethodID(clazz, "growOutputBuffer", "(Landroidx/media3/decoder/SimpleDecoderOutputBuffer;I)Ljava/nio/ByteBuffer;");
  jclass videoClazz = env->FindClass("androidx/media3/decoder/VideoDecoderOutputBuffer");
  initForYuvFrameMethod = env->GetMethodID(videoClazz, "initForYuvFrame", "(IIIII)Z");
  return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT jlong JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegAudioDecoder_ffmpegInitialize(JNIEnv* env, jobject thiz, jstring codecName, jbyteArray extraData, jboolean outputFloat, jint rawSampleRate, jint rawChannelCount, jstring modelPath) {
  const AVCodec* codec = getCodecByName(env, codecName);
  return codec ? (jlong)createContext(env, codec, extraData, outputFloat, rawSampleRate, rawChannelCount, modelPath) : 0L;
}
extern "C" JNIEXPORT jint JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegAudioDecoder_ffmpegDecode(JNIEnv* env, jobject thiz, jlong context, jobject inputData, jint inputSize, jobject decoderOutputBuffer, jobject outputData, jint outputSize) {
  if (!context) return -1;

  uint8_t *in = (uint8_t*)env->GetDirectBufferAddress(inputData), *out = (uint8_t*)env->GetDirectBufferAddress(outputData);
  if (!in || !out) return -1;
  AVPacket* pkt = av_packet_alloc();
  pkt->data = in; pkt->size = inputSize;

  int ret = decodePacket((AVCodecContext*)context, pkt, out, outputSize, {env, thiz, decoderOutputBuffer});

  av_packet_free(&pkt);
  return ret;
}
extern "C" JNIEXPORT jint JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegAudioDecoder_ffmpegGetChannelCount(JNIEnv* env, jobject thiz, jlong context) {
  return context ? ((JniDecoderContext*)((AVCodecContext*)context)->opaque)->targetChannelCount : -1;
}
extern "C" JNIEXPORT jint JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegAudioDecoder_ffmpegGetSampleRate(JNIEnv* env, jobject thiz, jlong context) {
  return context ? ((JniDecoderContext*)((AVCodecContext*)context)->opaque)->targetSampleRate : -1;
}
extern "C" JNIEXPORT jlong JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegAudioDecoder_ffmpegReset(JNIEnv* env, jobject thiz, jlong jContext, jbyteArray extraData) {
  AVCodecContext* context = (AVCodecContext*)jContext;
  if (!context) {
    return 0;
  }

  // Reset FFmpeg decoder state.
  avcodec_flush_buffers(context);
  return (jlong)context;
}
extern "C" JNIEXPORT void JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegAudioDecoder_ffmpegRelease(JNIEnv* env, jobject thiz, jlong context) {
  if (context) releaseContext((AVCodecContext*)context);
}

extern "C" JNIEXPORT jlong JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegVideoDecoder_ffmpegInitialize(JNIEnv* env, jobject thiz, jstring codecName, jbyteArray extraData) {
  const AVCodec* codec = getCodecByName(env, codecName);
  return codec ? (jlong)createContext(env, codec, extraData, false, -1, -1, NULL) : 0L;
}
extern "C" JNIEXPORT jint JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegVideoDecoder_ffmpegDecode(JNIEnv* env, jobject thiz, jlong context, jobject inputData, jint inputSize, jobject outputBuffer) {
  if (!context) return -2;
  AVCodecContext* ctx = (AVCodecContext*)context;
  uint8_t* in = (uint8_t*)env->GetDirectBufferAddress(inputData);
  if (!in) return -2;
  AVPacket* pkt = av_packet_alloc();
  pkt->data = in; pkt->size = inputSize;
  if (avcodec_send_packet(ctx, pkt) < 0) { av_packet_free(&pkt); return -2; }
  av_packet_free(&pkt);
  AVFrame* frame = av_frame_alloc();
  int ret = avcodec_receive_frame(ctx, frame);
  if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) { av_frame_free(&frame); return 0; }
  if (ret < 0) { av_frame_free(&frame); return -2; }
  if (frame->format != AV_PIX_FMT_YUV420P) {
      AVFrame* swsFrame = av_frame_alloc();
      swsFrame->format = AV_PIX_FMT_YUV420P; swsFrame->width = frame->width; swsFrame->height = frame->height;
      if (av_frame_get_buffer(swsFrame, 0) < 0) { av_frame_free(&swsFrame); av_frame_free(&frame); return -2; }
      JniDecoderContext* jniCtx = (JniDecoderContext*)ctx->opaque;
      jniCtx->swsContext = sws_getCachedContext(jniCtx->swsContext, frame->width, frame->height, (AVPixelFormat)frame->format, frame->width, frame->height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
      sws_scale(jniCtx->swsContext, frame->data, frame->linesize, 0, frame->height, swsFrame->data, swsFrame->linesize);
      av_frame_free(&frame); frame = swsFrame;
  }
  if (!env->CallBooleanMethod(outputBuffer, initForYuvFrameMethod, frame->width, frame->height, frame->linesize[0], frame->linesize[1], 0)) { av_frame_free(&frame); return -2; }
  jobjectArray yp = (jobjectArray)env->GetObjectField(outputBuffer, env->GetFieldID(env->GetObjectClass(outputBuffer), "yuvPlanes", "[Ljava/nio/ByteBuffer;"));
  for (int i = 0; i < 3; i++) {
    jobject pb = env->GetObjectArrayElement(yp, i);
    uint8_t* pd = (uint8_t*)env->GetDirectBufferAddress(pb);
    int h = (i == 0) ? frame->height : (frame->height + 1) / 2;
    for (int y = 0; y < h; y++) memcpy(pd + y * frame->linesize[i], frame->data[i] + y * frame->linesize[i], frame->linesize[i]);
    env->DeleteLocalRef(pb);
  }
  av_frame_free(&frame);
  return 1;
}
extern "C" JNIEXPORT jlong JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegVideoDecoder_ffmpegReset(JNIEnv* env, jobject thiz, jlong jContext, jbyteArray extraData) {
  AVCodecContext* context = (AVCodecContext*)jContext;
  if (context) avcodec_flush_buffers(context);
  return (jlong)context;
}
extern "C" JNIEXPORT void JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegVideoDecoder_ffmpegRenderToSurface(JNIEnv* env, jobject thiz, jlong context, jobject surface, jobject outputBuffer) {
  ANativeWindow* nw = ANativeWindow_fromSurface(env, surface);
  if (!nw) return;
  jclass clz = env->GetObjectClass(outputBuffer);
  int w = env->GetIntField(outputBuffer, env->GetFieldID(clz, "width", "I"));
  int h = env->GetIntField(outputBuffer, env->GetFieldID(clz, "height", "I"));
  jobjectArray yp = (jobjectArray)env->GetObjectField(outputBuffer, env->GetFieldID(clz, "yuvPlanes", "[Ljava/nio/ByteBuffer;"));
  jintArray ys = (jintArray)env->GetObjectField(outputBuffer, env->GetFieldID(clz, "yuvStrides", "[I"));
  jint* s = env->GetIntArrayElements(ys, NULL);
  ANativeWindow_setBuffersGeometry(nw, w, h, 0x32315659);
  ANativeWindow_Buffer buffer;
  if (ANativeWindow_lock(nw, &buffer, NULL) == 0) {
    jobject yP = env->GetObjectArrayElement(yp, 0);
    uint8_t* yS = (uint8_t*)env->GetDirectBufferAddress(yP);
    uint8_t* yD = (uint8_t*)buffer.bits;
    for (int y = 0; y < h; y++) memcpy(yD + y * buffer.stride, yS + y * s[0], w);
    int uvH = (h + 1) / 2, uvW = (w + 1) / 2, uvS = buffer.stride / 2;
    jobject uP = env->GetObjectArrayElement(yp, 1), vP = env->GetObjectArrayElement(yp, 2);
    uint8_t *uS = (uint8_t*)env->GetDirectBufferAddress(uP), *vS = (uint8_t*)env->GetDirectBufferAddress(vP);
    uint8_t *vD = yD + buffer.stride * buffer.height, *uD = vD + uvS * uvH;
    for (int y = 0; y < uvH; y++) { memcpy(uD + y * uvS, uS + y * s[1], uvW); memcpy(vD + y * uvS, vS + y * s[2], uvW); }
    ANativeWindow_unlockAndPost(nw);
    env->DeleteLocalRef(yP); env->DeleteLocalRef(uP); env->DeleteLocalRef(vP);
  }
  env->ReleaseIntArrayElements(ys, s, 0);
  ANativeWindow_release(nw);
}
extern "C" JNIEXPORT void JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegVideoDecoder_ffmpegRelease(JNIEnv* env, jobject thiz, jlong context) {
  if (context) releaseContext((AVCodecContext*)context);
}
extern "C" JNIEXPORT jstring JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegLibrary_ffmpegGetVersion(JNIEnv* env, jobject thiz) {
  return env->NewStringUTF(LIBAVCODEC_IDENT);
}
extern "C" JNIEXPORT jint JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegLibrary_ffmpegGetInputBufferPaddingSize(JNIEnv* env, jobject thiz) {
  return (jint)AV_INPUT_BUFFER_PADDING_SIZE;
}
extern "C" JNIEXPORT jboolean JNICALL Java_androidx_media3_decoder_ffmpeg_FfmpegLibrary_ffmpegHasDecoder(JNIEnv* env, jobject thiz, jstring codecName) {
  return getCodecByName(env, codecName) != NULL;
}
