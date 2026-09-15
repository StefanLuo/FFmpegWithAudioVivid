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

import androidx.annotation.Nullable;
import androidx.media3.common.C;
import androidx.media3.common.Format;
import androidx.media3.common.MimeTypes;
import androidx.media3.common.ParserException;
import androidx.media3.common.util.ParsableByteArray;
import androidx.media3.common.util.UnstableApi;
import androidx.media3.extractor.Av3aUtil;
import androidx.media3.extractor.ExtractorOutput;
import androidx.media3.extractor.TrackOutput;
import org.checkerframework.checker.nullness.qual.MonotonicNonNull;

/**
 * Parses an AV3A (AVS3 Audio) elementary stream and extracts individual AV3A frames.
 *
 * <p>The reader is intentionally implemented as an {@link ElementaryStreamReader}, so that
 * {@link PesReader} owns PES header parsing and PTS extraction, matching the normal Media3
 * AC3/E-AC3 reader architecture.
 *
 * <p>Each complete AV3A frame is emitted as exactly one Media3 sample. AV3A frames may span
 * PES boundaries.
 */
@UnstableApi
public final class Av3aReader implements ElementaryStreamReader {

    private static final String TAG = "Av3aReader";

    /** AVS3 audio uses 1024 PCM samples per coded frame. */
    private static final int AV3A_SAMPLES_PER_FRAME = 1024;

    /** Maximum amount of elementary-stream data retained while waiting for a frame. */
    private static final int MAX_BUFFER_SIZE = 1024 * 1024;

    private final ParsableByteArray masterBuffer;
    private final byte[] headerBuffer;

    @Nullable private final String language;
    private final @C.RoleFlags int roleFlags;

    private @MonotonicNonNull String formatId;
    private @MonotonicNonNull TrackOutput output;

    private boolean hasOutputFormat;

    /**
     * Timestamp for the next AV3A sample.
     *
     * <p>When a PES PTS is available, this is initialized from that PTS and then advanced by one
     * AV3A frame duration for each subsequent frame.
     */
    private long nextSamplePtsUs = C.TIME_UNSET;
    private boolean hasOutputSample;

    /** Cached frame size in bytes. Re-read from every AV3A header. */
    private int frameSize;

    /** Current sample rate used to calculate AV3A sample duration. */
    private int sampleRate;

    /** Duration of one AV3A frame in microseconds. */
    private long sampleDurationUs;

    public Av3aReader(@Nullable String language, @C.RoleFlags int roleFlags) {
        this.language = language;
        this.roleFlags = roleFlags;

        this.masterBuffer = new ParsableByteArray(16 * 1024);
        this.headerBuffer = new byte[Av3aUtil.AV3A_HEADER_SIZE];

        this.nextSamplePtsUs = C.TIME_UNSET;
        this.frameSize = C.LENGTH_UNSET;
        this.sampleRate = Format.NO_VALUE;
        this.sampleDurationUs = C.TIME_UNSET;
    }

    @Override
    public void seek() {
        masterBuffer.reset(0);

        nextSamplePtsUs = C.TIME_UNSET;
        frameSize = C.LENGTH_UNSET;
        sampleRate = Format.NO_VALUE;
        sampleDurationUs = C.TIME_UNSET;

        hasOutputFormat = false;
    }

    @Override
    public void createTracks(
            ExtractorOutput extractorOutput, TsPayloadReader.TrackIdGenerator idGenerator) {
        idGenerator.generateNewId();
        formatId = idGenerator.getFormatId();
        output = extractorOutput.track(idGenerator.getTrackId(), C.TRACK_TYPE_AUDIO);
    }

    /**
     * Called by {@link PesReader} when a new PES packet starts.
     *
     * <p>PES header parsing and PTS extraction are intentionally not performed here. {@link
     * PesReader} has already parsed the PES header and supplies the adjusted PES timestamp.
     */
    @Override
    public void packetStarted(long pesTimeUs, @TsPayloadReader.Flags int flags) {
        /*
         * Normally a PES starts at an AV3A frame boundary. If there is no unfinished frame in our
         * buffer, use the PES PTS as the timestamp of the next AV3A frame.
         *
         * If a frame is already partially buffered, that frame may span the PES boundary. In that
         * case its timestamp must not be replaced by the PTS of the following PES.
         */
        if (pesTimeUs != C.TIME_UNSET) {
            if (!hasOutputSample || masterBuffer.limit() == 0) {
                nextSamplePtsUs = pesTimeUs;
            }
        }
    }

    @Override
    public void consume(ParsableByteArray data) throws ParserException {
        if (output == null || formatId == null) {
            data.skipBytes(data.bytesLeft());
            return;
        }

        int bytesToAppend = data.bytesLeft();
        if (bytesToAppend <= 0) {
            return;
        }

        ensureMasterBufferCapacity(bytesToAppend);

        data.readBytes(
                masterBuffer.getData(),
                masterBuffer.limit(),
                bytesToAppend);

        masterBuffer.setLimit(masterBuffer.limit() + bytesToAppend);

        processBuffer();
    }

    @Override
    public void packetFinished() {
        /*
         * Do not flush an incomplete AV3A frame here.
         *
         * AV3A frames are allowed to cross PES boundaries, so the remaining bytes stay in
         * masterBuffer and are completed by the next packet.
         */
    }

    @Override
    public void endOfInputReached() {
        /*
         * Do not emit an incomplete frame.
         */
    }

    private void processBuffer() {
        while (masterBuffer.limit() > 0) {
            /*
             * 1. Make sure the beginning of masterBuffer is aligned to an AV3A sync word.
             */
            if (!alignToSync()) {
                return;
            }

            /*
             * 2. We need a complete AV3A header before we can determine the frame size.
             */
            if (masterBuffer.limit() < Av3aUtil.AV3A_HEADER_SIZE) {
                return;
            }

            System.arraycopy(
                    masterBuffer.getData(),
                    0,
                    headerBuffer,
                    0,
                    Av3aUtil.AV3A_HEADER_SIZE);

            Av3aUtil.Info info = Av3aUtil.parseHeader(headerBuffer);

            /*
             * 3. Invalid header: lose one byte and search for the next sync word.
             *
             * This is deliberately done before outputting anything, so one bad byte cannot
             * shift the entire stream framing.
             */
            if (info == null || info.frameSize < Av3aUtil.AV3A_HEADER_SIZE) {
                discardBytes(1);
                continue;
            }

            /*
             * Re-check the frame size on every frame instead of assuming that the stream bitrate
             * can never change.
             */
            frameSize = info.frameSize;

            if (frameSize <= 0 || frameSize > MAX_BUFFER_SIZE) {
                discardBytes(1);
                continue;
            }

            updateFormatIfNeeded(info);

            /*
             * 4. Wait until a complete AV3A frame has arrived.
             *
             * A frame may span multiple PES packets, so masterBuffer is retained until all
             * frameSize bytes are present.
             */
            if (masterBuffer.limit() < frameSize) {
                return;
            }

            /*
             * 5. Output EXACTLY ONE AV3A frame as ONE Media3 sample.
             */
            outputOneFrame(frameSize);
            hasOutputSample = true;
        }
    }

    /**
     * Ensures that the first bytes of masterBuffer begin with an AV3A sync word.
     *
     * <p>AV3A sync is the 12-bit 0xFFF prefix used by Av3aUtil.parseHeader().
     */
    private boolean alignToSync() {
        byte[] data = masterBuffer.getData();
        int limit = masterBuffer.limit();

        /*
         * Already aligned.
         */
        if (limit >= 2
                && (data[0] & 0xFF) == 0xFF
                && (data[1] & 0xF0) == 0xF0) {
            return true;
        }

        /*
         * Search for the next candidate sync position.
         */
        for (int i = 1; i < limit - 1; i++) {
            if ((data[i] & 0xFF) == 0xFF
                    && (data[i + 1] & 0xF0) == 0xF0) {

                discardBytes(i);
                return true;
            }
        }

        /*
         * Keep the final byte because it could be the first half of a sync word split
         * across two consume() calls.
         */
        if (limit > 1) {
            discardBytes(limit - 1);
        }

        return false;
    }

    private void updateFormatIfNeeded(Av3aUtil.Info info) {
        if (hasOutputFormat) {
            /*
             * Keep the current track format stable. Frame size is still re-evaluated for every
             * frame above.
             */
            return;
        }

        Format format =
                new Format.Builder()
                        .setId(formatId)
                        .setSampleMimeType(MimeTypes.AUDIO_AV3A)
                        .setSampleRate(info.sampleRate)
                        .setChannelCount(info.channelCount)
                        .setLanguage(language)
                        .setRoleFlags(roleFlags)
                        .build();

        output.format(format);

        sampleRate = info.sampleRate;

        if (sampleRate > 0) {
            sampleDurationUs =
                    (C.MICROS_PER_SECOND * AV3A_SAMPLES_PER_FRAME) / sampleRate;
        } else {
            sampleDurationUs = C.TIME_UNSET;
        }

        hasOutputFormat = true;
    }

    private void outputOneFrame(int size) {
        long ptsUs = nextSamplePtsUs;

        masterBuffer.setPosition(0);

        output.sampleData(masterBuffer, size);

        output.sampleMetadata(
                ptsUs,
                C.BUFFER_FLAG_KEY_FRAME,
                size,
                0,
                null);

        /*
         * Advance timestamp by exactly one AV3A coded frame.
         */
        if (ptsUs != C.TIME_UNSET && sampleDurationUs != C.TIME_UNSET) {
            nextSamplePtsUs = ptsUs + sampleDurationUs;
        }

        /*
         * Remove exactly the frame that was just emitted.
         */
        discardBytes(size);
    }

    private void ensureMasterBufferCapacity(int bytesToAdd) {
        int currentLimit = masterBuffer.limit();
        int requiredCapacity = currentLimit + bytesToAdd;

        if (requiredCapacity <= masterBuffer.capacity()) {
            return;
        }

        int newCapacity = masterBuffer.capacity();

        while (newCapacity < requiredCapacity) {
            newCapacity *= 2;

            if (newCapacity >= MAX_BUFFER_SIZE) {
                newCapacity = MAX_BUFFER_SIZE;
                break;
            }
        }

        if (newCapacity < requiredCapacity) {
            /*
             * This should only happen for pathological/corrupt input.
             * Discard old data rather than allowing unbounded memory growth.
             */
            int keep = Math.min(currentLimit, Av3aUtil.AV3A_HEADER_SIZE - 1);
            if (keep > 0) {
                System.arraycopy(
                        masterBuffer.getData(),
                        currentLimit - keep,
                        masterBuffer.getData(),
                        0,
                        keep);
            }

            masterBuffer.setLimit(keep);

            /*
             * Re-evaluate after truncation.
             */
            currentLimit = keep;
            requiredCapacity = currentLimit + bytesToAdd;

            if (requiredCapacity > masterBuffer.capacity()) {
                newCapacity = Math.min(
                        MAX_BUFFER_SIZE,
                        Math.max(masterBuffer.capacity() * 2, requiredCapacity));
            } else {
                return;
            }
        }

        byte[] newBuffer = new byte[newCapacity];

        System.arraycopy(
                masterBuffer.getData(),
                0,
                newBuffer,
                0,
                currentLimit);

        masterBuffer.reset(newBuffer, currentLimit);
    }

    private void discardBytes(int count) {
        if (count <= 0) {
            return;
        }

        int limit = masterBuffer.limit();

        if (count >= limit) {
            masterBuffer.reset(0);
            return;
        }

        int remaining = limit - count;

        System.arraycopy(
                masterBuffer.getData(),
                count,
                masterBuffer.getData(),
                0,
                remaining);

        masterBuffer.setLimit(remaining);
        masterBuffer.setPosition(0);
    }
}