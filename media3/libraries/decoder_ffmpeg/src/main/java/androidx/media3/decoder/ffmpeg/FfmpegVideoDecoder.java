/*
 * Copyright (C) 2020 The Android Open Source Project
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

import android.view.Surface;
import androidx.annotation.Nullable;
import androidx.media3.common.Format;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.common.util.Util;
import androidx.media3.decoder.DecoderInputBuffer;
import androidx.media3.decoder.SimpleDecoder;
import androidx.media3.decoder.VideoDecoderOutputBuffer;
import java.nio.ByteBuffer;

/** FFmpeg video decoder. */
@UnstableApi
public final class FfmpegVideoDecoder
    extends SimpleDecoder<DecoderInputBuffer, VideoDecoderOutputBuffer, FfmpegDecoderException> {

  private final String codecName;
  @Nullable private final byte[] extraData;

  private long nativeContext;

  public FfmpegVideoDecoder(
      Format format,
      int numInputBuffers,
      int numOutputBuffers,
      int initialInputBufferSize)
      throws FfmpegDecoderException {
    super(new DecoderInputBuffer[numInputBuffers], new VideoDecoderOutputBuffer[numOutputBuffers]);
    if (!FfmpegLibrary.isAvailable()) {
      throw new FfmpegDecoderException("Failed to load decoder native libraries.");
    }
    codecName = FfmpegLibrary.getCodecName(format.sampleMimeType);
    if (codecName == null) {
      throw new FfmpegDecoderException("Unsupported MIME type: " + format.sampleMimeType);
    }
    extraData = format.initializationData.isEmpty() ? null : format.initializationData.get(0);
    nativeContext = ffmpegInitialize(codecName, extraData);
    if (nativeContext == 0) {
      throw new FfmpegDecoderException("Initialization failed.");
    }
    setInitialInputBufferSize(initialInputBufferSize);
  }

  @Override
  public String getName() {
    return "ffmpeg" + FfmpegLibrary.getVersion() + "-" + codecName;
  }

  @Override
  protected DecoderInputBuffer createInputBuffer() {
    return new DecoderInputBuffer(
        DecoderInputBuffer.BUFFER_REPLACEMENT_MODE_DIRECT,
        FfmpegLibrary.getInputBufferPaddingSize());
  }

  @Override
  protected VideoDecoderOutputBuffer createOutputBuffer() {
    return new VideoDecoderOutputBuffer(this::releaseOutputBuffer);
  }

  @Override
  protected FfmpegDecoderException createUnexpectedDecodeException(Throwable error) {
    return new FfmpegDecoderException("Unexpected decode error", error);
  }

  @Override
  @Nullable
  protected FfmpegDecoderException decode(
      DecoderInputBuffer inputBuffer, VideoDecoderOutputBuffer outputBuffer, boolean reset) {
    if (reset) {
      nativeContext = ffmpegReset(nativeContext, extraData);
      if (nativeContext == 0) {
        return new FfmpegDecoderException("Error resetting (see logcat).");
      }
    }
    ByteBuffer inputData = Util.castNonNull(inputBuffer.data);
    int inputSize = inputData.limit();
    outputBuffer.init(inputBuffer.timeUs, VideoDecoderOutputBuffer.COLORSPACE_UNKNOWN, null);
    int result = ffmpegDecode(nativeContext, inputData, inputSize, outputBuffer);
    if (result == -2) {
      return new FfmpegDecoderException("Error decoding (see logcat).");
    } else if (result == -1 || result == 0) {
      outputBuffer.shouldBeSkipped = true;
      return null;
    }
    return null;
  }

  @Override
  public void release() {
    super.release();
    ffmpegRelease(nativeContext);
    nativeContext = 0;
  }

  public void setOutputMode(int outputMode) {
    // For now, we only support YUV.
  }

  public void renderToSurface(VideoDecoderOutputBuffer outputBuffer, Surface surface)
      throws FfmpegDecoderException {
    ffmpegRenderToSurface(nativeContext, surface, outputBuffer);
  }

  private native long ffmpegInitialize(String codecName, @Nullable byte[] extraData);

  private native int ffmpegDecode(
      long context, ByteBuffer inputData, int inputSize, VideoDecoderOutputBuffer outputBuffer);

  private native void ffmpegRenderToSurface(
      long context, Surface surface, VideoDecoderOutputBuffer outputBuffer);

  private native long ffmpegReset(long context, @Nullable byte[] extraData);

  private native void ffmpegRelease(long context);
}
