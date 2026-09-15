/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
package androidx.media3.decoder.ffmpeg;

import androidx.annotation.Nullable;
import androidx.media3.common.C;
import androidx.media3.common.MediaLibraryInfo;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.util.LibraryLoader;
import androidx.media3.common.util.Log;
import androidx.media3.common.util.UnstableApi;
import org.checkerframework.checker.nullness.qual.MonotonicNonNull;

/** Configures and queries the underlying native library. */
@UnstableApi
public final class FfmpegLibrary {

  static {
    MediaLibraryInfo.registerModule("media3.decoder.ffmpeg");
  }

  private static final String TAG = "FfmpegLibrary";

  private static final LibraryLoader LOADER =
      new LibraryLoader("avutil", "swresample", "avcodec", "avformat", "swscale", "ffmpegJNI") {
        @Override
        protected void loadLibrary(String name) {
          try {
            System.loadLibrary(name);
          } catch (UnsatisfiedLinkError e) {
            android.util.Log.e("FfmpegLibrary", "JNI_TRACE: System.loadLibrary FAILED: " + name + " | Error: " + e.getMessage());
            throw e;
          }
        }
      };

  private static @MonotonicNonNull String version;
  private static int inputBufferPaddingSize = C.LENGTH_UNSET;
  private static @MonotonicNonNull String modelPath;

  private FfmpegLibrary() {}

  /**
   * Sets the path to the model file used by some decoders (e.g., AVS3 audio).
   */
  public static void setModelPath(String path) {
    modelPath = path;
  }

  /**
   * Returns the path to the model file, or null if not set.
   */
  @Nullable
  public static String getModelPath() {
    return modelPath;
  }

  /**
   * Override the names of the FFmpeg native libraries. If an application wishes to call this
   * method, it must do so before calling any other method defined by this class, and before
   * instantiating a {@link FfmpegAudioRenderer} or {@link ExperimentalFfmpegVideoRenderer}
   * instance.
   *
   * @param libraries The names of the FFmpeg native libraries.
   */
  public static void setLibraries(String... libraries) {
    LOADER.setLibraries(libraries);
  }

  /** Returns whether the underlying library is available, loading it if necessary. */
  public static boolean isAvailable() {
    boolean available = LOADER.isAvailable();
    return available;
  }

  /** Returns the version of the underlying library if available, or null otherwise. */
  @Nullable
  public static String getVersion() {
    if (!isAvailable()) {
      return null;
    }
    if (version == null) {
      version = ffmpegGetVersion();
    }
    return version;
  }

  /**
   * Returns the required amount of padding for input buffers in bytes, or {@link C#LENGTH_UNSET} if
   * the underlying library is not available.
   */
  public static int getInputBufferPaddingSize() {
    if (!isAvailable()) {
      return C.LENGTH_UNSET;
    }
    if (inputBufferPaddingSize == C.LENGTH_UNSET) {
      inputBufferPaddingSize = ffmpegGetInputBufferPaddingSize();
    }
    return inputBufferPaddingSize;
  }

  /**
   * Returns whether the underlying library supports the specified MIME type.
   *
   * @param mimeType The MIME type to check.
   */
  public static boolean supportsFormat(String mimeType) {
    if (!isAvailable()) {
      return false;
    }
    @Nullable String codecName = getCodecName(mimeType);
    if (codecName == null) {
      return false;
    }
    // For custom formats like avs3_audio, libdavs2, libuavs3d, cavs,
    // we bypass the native hasDecoder check if we know we've mapped it correctly
    // and the user has integrated it into their FFmpeg build.
    if (codecName.equals("avs3_audio") || codecName.startsWith("libdavs") || 
        codecName.startsWith("libuavs") || codecName.equals("cavs")) {
        return true;
    }
    if (!ffmpegHasDecoder(codecName)) {
      Log.w(TAG, "No " + codecName + " decoder available. Check the FFmpeg build configuration.");
      return false;
    }
    return true;
  }

  /**
   * Returns the name of the FFmpeg decoder that could be used to decode the format, or {@code null}
   * if it's unsupported.
   */
  @Nullable
  /* package */ static String getCodecName(String mimeType) {
    if (mimeType == null) return null;
    String lowMime = mimeType.toLowerCase();
    if (lowMime.equals("audio/av3a") || lowMime.equals("audio/avs3-audio")) {
        return "avs3_audio";
    }
    
    switch (mimeType) {
      case MimeTypes.AUDIO_AAC:
        return "aac";
      case MimeTypes.AUDIO_MPEG:
      case MimeTypes.AUDIO_MPEG_L1:
        return "mp3";
      case MimeTypes.AUDIO_MPEG_L2:
        return "mp2";
      case MimeTypes.AUDIO_AC3:
        return "ac3";
      case MimeTypes.AUDIO_E_AC3:
      case MimeTypes.AUDIO_E_AC3_JOC:
        return "eac3";
      case MimeTypes.AUDIO_TRUEHD:
        return "truehd";
      case MimeTypes.AUDIO_DTS:
      case MimeTypes.AUDIO_DTS_EXPRESS:
      case MimeTypes.AUDIO_DTS_HD:
        return "dca";
      case MimeTypes.AUDIO_VORBIS:
        return "vorbis";
      case MimeTypes.AUDIO_OPUS:
        return "opus";
      case MimeTypes.AUDIO_AMR_NB:
        return "amrnb";
      case MimeTypes.AUDIO_AMR_WB:
        return "amrwb";
      case MimeTypes.AUDIO_FLAC:
        return "flac";
      case MimeTypes.AUDIO_ALAC:
        return "alac";
      case MimeTypes.AUDIO_MLAW:
        return "pcm_mulaw";
      case MimeTypes.AUDIO_ALAW:
        return "pcm_alaw";
      case MimeTypes.AUDIO_AV3A:
      case MimeTypes.AUDIO_AVS3_AUDIO:
        return "avs3_audio";
      case MimeTypes.AUDIO_APE:
        return "ape";
      case MimeTypes.AUDIO_WAVPACK:
        return "wavpack";
      case MimeTypes.AUDIO_TTA:
        return "tta";
      case MimeTypes.AUDIO_WMA:
        return "wmapro";
      case MimeTypes.AUDIO_REALAUDIO:
        return "cook";
      case MimeTypes.AUDIO_ADPCM:
        return "adpcm_ms";
      case MimeTypes.VIDEO_H264:
        return "h264";
      case MimeTypes.VIDEO_H265:
        return "hevc";
      case MimeTypes.VIDEO_AVS2:
        return "libdavs2";
      case MimeTypes.VIDEO_AVS3:
      case MimeTypes.VIDEO_AVS3D:
        return "libuavs3d";
      case MimeTypes.VIDEO_AVSPLUS:
        return "cavs";
      case MimeTypes.VIDEO_MPEG2:
        return "mpeg2video";
      case MimeTypes.VIDEO_VC1:
        return "vc1";
      case MimeTypes.VIDEO_WMV:
        return "wmv2";
      case MimeTypes.VIDEO_REALVIDEO:
        return "rv40";
      case MimeTypes.VIDEO_MP4V:
        return "mpeg4";
      case MimeTypes.VIDEO_MSMPEG4:
        return "msmpeg4v3";
      case MimeTypes.VIDEO_FLV:
        return "flv1";
      default:
        return null;
    }
  }

  private static native String ffmpegGetVersion();

  private static native int ffmpegGetInputBufferPaddingSize();

  private static native boolean ffmpegHasDecoder(String codecName);
}
