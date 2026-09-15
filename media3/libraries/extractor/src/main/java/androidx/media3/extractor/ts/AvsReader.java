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
package androidx.media3.extractor.ts;

import static com.google.common.base.Preconditions.checkNotNull;

import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.util.ParsableByteArray;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.extractor.ExtractorOutput;
import androidx.media3.extractor.TrackOutput;
import androidx.media3.extractor.ts.TsPayloadReader.TrackIdGenerator;
import org.checkerframework.checker.nullness.qual.MonotonicNonNull;

/**
 * Parses AVS (AVS2, AVS3, CAVS) PES packets and extracts samples.
 */
@UnstableApi
public final class AvsReader implements ElementaryStreamReader {

  private final String mimeType;
  private @MonotonicNonNull String formatId;
  private @MonotonicNonNull TrackOutput output;

  private boolean hasOutputFormat;
  private long timeUs;

  public AvsReader(String mimeType) {
    this.mimeType = mimeType;
    this.timeUs = C.TIME_UNSET;
  }

  @Override
  public void seek() {
    timeUs = C.TIME_UNSET;
  }

  @Override
  public void createTracks(ExtractorOutput extractorOutput, TrackIdGenerator idGenerator) {
    idGenerator.generateNewId();
    formatId = idGenerator.getFormatId();
    output = extractorOutput.track(idGenerator.getTrackId(), C.TRACK_TYPE_VIDEO);
  }

  @Override
  public void packetStarted(long pesTimeUs, @TsPayloadReader.Flags int flags) {
    if (pesTimeUs != C.TIME_UNSET) {
      timeUs = pesTimeUs;
    }
  }

  @Override
  public void consume(ParsableByteArray data) {
    checkNotNull(output);
    if (!hasOutputFormat) {
      output.format(new Format.Builder().setId(formatId).setSampleMimeType(mimeType).build());
      hasOutputFormat = true;
    }
    int bytesLength = data.bytesLeft();
    output.sampleData(data, bytesLength);
    if (timeUs != C.TIME_UNSET) {
      output.sampleMetadata(timeUs, C.BUFFER_FLAG_KEY_FRAME, bytesLength, 0, null);
      timeUs = C.TIME_UNSET;
    }
  }

  @Override
  public void packetFinished() {
    // Do nothing.
  }
}
