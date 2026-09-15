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

package androidx.media3.extractor;

import androidx.annotation.Nullable;
import androidx.media3.common.util.ParsableBitArray;
import androidx.media3.common.util.UnstableApi;

/**
 * Utility methods for parsing AV3A (AVS3 Audio) AATF frame headers.
 *
 * <p>The implementation mirrors the current FFmpeg AV3A header parser
 * used by libavcodec/av3a.c in this project.
 */
@UnstableApi
public final class Av3aUtil {

    /**
     * Same value as MAX_NBYTES_FRAME_HEADER in the current FFmpeg av3a.h.
     */
    public static final int AV3A_HEADER_SIZE = 9;

    /** Number of PCM samples represented by one AVS3 audio frame. */
    private static final int AVS3_AUDIO_FRAME_SIZE = 1024;

    /** AVS3 frame sync word: 12 bits of 1. */
    private static final int AVS3_AUDIO_SYNC_WORD = 0xFFF;

    /**
     * FFmpeg avpriv_avs3_samplingrate_table[].
     */
    private static final int[] SAMPLING_RATE_TABLE = {
            192000,
            96000,
            48000,
            44100,
            32000,
            24000,
            22050,
            16000,
            8000
    };

    /*
     * ================================================================
     * Channel configuration constants.
     *
     * These values match AVS3AChannelConfig in av3a.h / avs3_stat_com.h.
     * ================================================================
     */

    private static final int CHANNEL_CONFIG_MONO = 0;
    private static final int CHANNEL_CONFIG_STEREO = 1;
    private static final int CHANNEL_CONFIG_MC_5_1 = 2;
    private static final int CHANNEL_CONFIG_MC_7_1 = 3;
    private static final int CHANNEL_CONFIG_MC_10_2 = 4;
    private static final int CHANNEL_CONFIG_MC_22_2 = 5;
    private static final int CHANNEL_CONFIG_MC_4_0 = 6;
    private static final int CHANNEL_CONFIG_MC_5_1_2 = 7;
    private static final int CHANNEL_CONFIG_MC_5_1_4 = 8;
    private static final int CHANNEL_CONFIG_MC_7_1_2 = 9;
    private static final int CHANNEL_CONFIG_MC_7_1_4 = 10;
    private static final int CHANNEL_CONFIG_HOA_ORDER1 = 11;
    private static final int CHANNEL_CONFIG_HOA_ORDER2 = 12;
    private static final int CHANNEL_CONFIG_HOA_ORDER3 = 13;

    /**
     * Bitrate table for mono.
     *
     * <p>Matches the current bitrateTableMono[] in avs3_rom_com.c.
     */
    private static final int[] BITRATE_TABLE_MONO = {
            16000,
            32000,
            44000,
            56000,
            64000,
            72000,
            80000,
            96000,
            128000,
            144000,
            164000,
            192000
    };

    /**
     * Bitrate table for stereo.
     */
    private static final int[] BITRATE_TABLE_STEREO = {
            24000,
            32000,
            48000,
            64000,
            80000,
            96000,
            128000,
            144000,
            192000,
            256000,
            320000
    };

    /**
     * Bitrate table for MC 5.1.
     *
     * <p>BRTABLE_ALIGN/BITRATE_EXT entries are intentionally not included
     * here because Java cannot see the C preprocessor configuration.
     *
     * <p>The base table exactly matches the source shown in
     * avs3_rom_com.c.
     */
    private static final int[] BITRATE_TABLE_MC_5_1 = {
            192000,
            256000,
            320000,
            384000,
            448000,
            512000,
            640000,
            720000
    };

    /**
     * Bitrate table for MC 7.1.
     */
    private static final int[] BITRATE_TABLE_MC_7_1 = {
            192000,
            480000,
            256000,
            384000,
            576000,
            640000
    };

    /**
     * Bitrate table for MC 4.0.
     */
    private static final int[] BITRATE_TABLE_MC_4_0 = {
            48000,
            96000,
            128000,
            192000,
            256000
    };

    /**
     * Bitrate table for MC 5.1.2.
     */
    private static final int[] BITRATE_TABLE_MC_5_1_2 = {
            152000,
            320000,
            480000,
            576000
    };

    /**
     * Bitrate table for MC 5.1.4.
     */
    private static final int[] BITRATE_TABLE_MC_5_1_4 = {
            176000,
            384000,
            576000,
            704000,
            256000,
            448000
    };

    /**
     * Bitrate table for MC 7.1.2.
     */
    private static final int[] BITRATE_TABLE_MC_7_1_2 = {
            216000,
            480000,
            576000,
            384000,
            768000
    };

    /**
     * Bitrate table for MC 7.1.4.
     */
    private static final int[] BITRATE_TABLE_MC_7_1_4 = {
            240000,
            608000,
            384000,
            512000,
            832000
    };

    /**
     * FOA bitrate table.
     *
     * <p>Only available in the C implementation when
     * AVS3_HOA_FULL_SUPPORT is enabled.
     */
    private static final int[] BITRATE_TABLE_FOA = {
            48000,
            96000,
            128000,
            192000,
            256000
    };

    /**
     * HOA order 2 bitrate table.
     *
     * <p>Only available in the C implementation when
     * AVS3_HOA_FULL_SUPPORT is enabled.
     */
    private static final int[] BITRATE_TABLE_HOA_2 = {
            192000,
            256000,
            320000,
            384000,
            480000,
            512000,
            640000
    };

    /**
     * HOA order 3 bitrate table.
     *
     * <p>This is the non-BITRATE_EXT version from the current C source.
     *
     * <p>If the Android FFmpeg build uses BITRATE_EXT, this table must
     * be changed to the corresponding C configuration.
     */
    private static final int[] BITRATE_TABLE_HOA_3 = {
            192000,
            256000,
            320000,
            512000,
            608000,
            896000
    };

    /** Holds AV3A stream/frame information. */
    public static final class Info {

        /** Sampling rate in Hz. */
        public final int sampleRate;

        /** Total output channel count signalled by the AV3A header. */
        public final int channelCount;

        /**
         * Complete coded AV3A frame size in bytes.
         *
         * <p>This is the value calculated by the FFmpeg parser:
         *
         * <pre>
         * ceil(total_bitrate * 1024 / (sampling_rate * 8))
         * </pre>
         */
        public final int frameSize;

        public Info(int sampleRate, int channelCount, int frameSize) {
            this.sampleRate = sampleRate;
            this.channelCount = channelCount;
            this.frameSize = frameSize;
        }
    }

    /**
     * Parses a complete AV3A header starting at offset 0.
     */
    public static Info parseHeader(byte[] data) {
        if (data == null || data.length < AV3A_HEADER_SIZE) {
            return null;
        }

        return parseHeaderAtOffset(data, 0, data.length);
    }

    /**
     * Parses an AV3A header at a specific byte offset without allocating
     * a temporary byte array.
     *
     * @param data Source buffer.
     * @param offset Byte offset where the AV3A sync word starts.
     * @param length Number of bytes available starting at {@code offset}.
     * @return Parsed AV3A information, or {@code null} when the header is
     *     invalid/unsupported.
     */
    public static Info parseHeaderAtOffset(
            byte[] data,
            int offset,
            int length) {

        if (data == null) {
            return null;
        }

        if (offset < 0 || length < AV3A_HEADER_SIZE) {
            return null;
        }

        if (offset > data.length - length) {
            return null;
        }

        if (length > data.length - offset) {
            return null;
        }

        /*
         * The common FFmpeg header reader requires 9 bytes of input.
         */
        if (length < AV3A_HEADER_SIZE) {
            return null;
        }

        ParsableBitArray gb = new ParsableBitArray(data);
        gb.setPosition(offset * 8);

        /*
         * ------------------------------------------------------------
         * syncword: 12 bits
         * ------------------------------------------------------------
         */
        if (gb.bitsLeft() < 12) {
            return null;
        }

        if (gb.readBits(12) != AVS3_AUDIO_SYNC_WORD) {
            return null;
        }

        /*
         * ------------------------------------------------------------
         * audio_codec_id: 4 bits
         * ------------------------------------------------------------
         *
         * Current FFmpeg av3a.c accepts only codec_id == 2.
         */
        if (gb.bitsLeft() < 4) {
            return null;
        }

        int codecId = gb.readBits(4);

        if (codecId != 2) {
            return null;
        }

        /*
         * ------------------------------------------------------------
         * anc_data_index: 1 bit
         * ------------------------------------------------------------
         */
        if (gb.bitsLeft() < 1) {
            return null;
        }

        if (gb.readBit()) {
            return null;
        }

        /*
         * ------------------------------------------------------------
         * nn_type: 3 bits
         * ------------------------------------------------------------
         */
        if (gb.bitsLeft() < 3) {
            return null;
        }

        gb.skipBits(3);

        /*
         * ------------------------------------------------------------
         * coding_profile: 3 bits
         * ------------------------------------------------------------
         */
        if (gb.bitsLeft() < 3) {
            return null;
        }

        int codingProfile = gb.readBits(3);

        /*
         * The current FFmpeg implementation only handles profiles 0, 1, 2.
         */
        if (codingProfile < 0 || codingProfile > 2) {
            return null;
        }

        /*
         * ------------------------------------------------------------
         * sampling_frequency_index: 4 bits
         * ------------------------------------------------------------
         */
        if (gb.bitsLeft() < 4) {
            return null;
        }

        int samplingIndex = gb.readBits(4);

        if (samplingIndex < 0
                || samplingIndex >= SAMPLING_RATE_TABLE.length) {
            return null;
        }

        int sampleRate = SAMPLING_RATE_TABLE[samplingIndex];

        /*
         * ------------------------------------------------------------
         * First 8-bit CRC field
         *
         * Current FFmpeg av3a.c:
         *
         *   skip_bits(&gb, 8);
         * ------------------------------------------------------------
         */
        if (gb.bitsLeft() < 8) {
            return null;
        }

        gb.skipBits(8);

        int totalChannels;
        int totalBitrate;

        /*
         * ============================================================
         * coding_profile == 0
         * ============================================================
         *
         * Mono / stereo / MC.
         */
        if (codingProfile == 0) {

            /*
             * channel_number_index: 7 bits
             */
            if (gb.bitsLeft() < 7) {
                return null;
            }

            int channelConfig = gb.readBits(7);

            /*
             * Match the enum values used by av3a.c.
             *
             * 0..10 are MC/channel configurations.
             * HOA configs 11..13 belong to coding_profile == 2.
             */
            if (channelConfig < CHANNEL_CONFIG_MONO
                    || channelConfig > CHANNEL_CONFIG_MC_7_1_4) {
                return null;
            }

            totalChannels = getChannelCount(channelConfig);

            if (totalChannels <= 0) {
                return null;
            }

            /*
             * resolution_index: 2 bits
             *
             * Current C implementation:
             *
             *   0 -> 8 bit
             *   1 -> 16 bit
             *   2 -> 24 bit
             *   3 -> invalid
             *
             * Resolution does not affect frame byte length.
             */
            if (gb.bitsLeft() < 2) {
                return null;
            }

            int resolutionIndex = gb.readBits(2);

            if (resolutionIndex > 2) {
                return null;
            }

            /*
             * bitrate_index: 4 bits
             */
            if (gb.bitsLeft() < 4) {
                return null;
            }

            int bitrateIndex = gb.readBits(4);

            int[] bitrateTable =
                    getBitrateTable(channelConfig);

            /*
             * MC_10_2 and MC_22_2 have NULL bitrate tables in the
             * current FFmpeg source.
             */
            if (bitrateTable == null) {
                return null;
            }

            if (bitrateIndex < 0
                    || bitrateIndex >= bitrateTable.length) {
                return null;
            }

            totalBitrate = bitrateTable[bitrateIndex];

            if (totalBitrate <= 0) {
                return null;
            }
        }

        /*
         * ============================================================
         * coding_profile == 1
         * ============================================================
         *
         * Object only / MC + Objects.
         *
         * This follows the exact logic shown in av3a.c.
         */
        else if (codingProfile == 1) {

            /*
             * soundBedType: 2 bits
             */
            if (gb.bitsLeft() < 2) {
                return null;
            }

            int soundBedType = gb.readBits(2);

            /*
             * ----------------------------------------------------------
             * soundBedType == 0
             *
             * Objects only:
             *
             *   object number: 7 bits + 1
             *   bitrate index per object: 4 bits
             *
             * FFmpeg:
             *
             *   total_bitrate =
             *       bitrateTableMono[obj_brt_idx] * objects;
             *
             *   total_channels = objects;
             * ----------------------------------------------------------
             */
            if (soundBedType == 0) {

                if (gb.bitsLeft() < 7) {
                    return null;
                }

                int objects =
                        gb.readBits(7) + 1;

                if (objects <= 0) {
                    return null;
                }

                if (gb.bitsLeft() < 4) {
                    return null;
                }

                int objectBitrateIndex =
                        gb.readBits(4);

                int bitratePerObject =
                        getBitrate(
                                BITRATE_TABLE_MONO,
                                objectBitrateIndex);

                if (bitratePerObject <= 0) {
                    return null;
                }

                /*
                 * Protect the multiplication from overflowing int.
                 */
                long bitrate =
                        (long) bitratePerObject * objects;

                if (bitrate <= 0 || bitrate > Integer.MAX_VALUE) {
                    return null;
                }

                totalBitrate = (int) bitrate;
                totalChannels = objects;
            }

            /*
             * ----------------------------------------------------------
             * soundBedType == 1
             *
             * MC + objects:
             *
             *   channel_number_index: 7
             *   bed bitrate index: 4
             *   object number: 7 + 1
             *   object bitrate index: 4
             *
             * FFmpeg:
             *
             *   total_bitrate =
             *       bitrateBedMc + bitratePerObj * objects;
             *
             *   total_channels =
             *       channels + objects;
             * ----------------------------------------------------------
             */
            else if (soundBedType == 1) {

                if (gb.bitsLeft() < 7) {
                    return null;
                }

                int channelConfig =
                        gb.readBits(7);

                /*
                 * Same channel configuration table as profile 0.
                 */
                if (channelConfig < CHANNEL_CONFIG_MONO
                        || channelConfig > CHANNEL_CONFIG_MC_7_1_4) {
                    return null;
                }

                if (gb.bitsLeft() < 4) {
                    return null;
                }

                int bedBitrateIndex =
                        gb.readBits(4);

                if (gb.bitsLeft() < 7) {
                    return null;
                }

                int objects =
                        gb.readBits(7) + 1;

                if (objects <= 0) {
                    return null;
                }

                if (gb.bitsLeft() < 4) {
                    return null;
                }

                int objectBitrateIndex =
                        gb.readBits(4);

                int bedChannels =
                        getChannelCount(channelConfig);

                if (bedChannels <= 0) {
                    return null;
                }

                int[] bedBitrateTable =
                        getBitrateTable(channelConfig);

                /*
                 * Current C table contains NULL for MC_10_2 / MC_22_2.
                 */
                if (bedBitrateTable == null) {
                    return null;
                }

                int bitrateBedMc =
                        getBitrate(
                                bedBitrateTable,
                                bedBitrateIndex);

                if (bitrateBedMc <= 0) {
                    return null;
                }

                int bitratePerObj =
                        getBitrate(
                                BITRATE_TABLE_MONO,
                                objectBitrateIndex);

                if (bitratePerObj <= 0) {
                    return null;
                }

                long bitrate =
                        (long) bitrateBedMc
                                + (long) bitratePerObj * objects;

                if (bitrate <= 0 || bitrate > Integer.MAX_VALUE) {
                    return null;
                }

                totalBitrate = (int) bitrate;

                /*
                 * Match:
                 *
                 *   hdf->total_channels = channels + objects;
                 */
                long channels =
                        (long) bedChannels + objects;

                if (channels <= 0 || channels > Integer.MAX_VALUE) {
                    return null;
                }

                totalChannels = (int) channels;
            }

            /*
             * Current av3a.c does not implement soundBedType 2/3.
             */
            else {
                return null;
            }

            /*
             * Profile 1 then reaches the common resolution field.
             *
             * There is NO generic bitrate_index here because total_bitrate
             * has already been calculated above.
             */
        }

        /*
         * ============================================================
         * coding_profile == 2
         * ============================================================
         *
         * HOA.
         */
        else {
            /*
             * hoa_order: 4 bits + 1
             */
            if (gb.bitsLeft() < 4) {
                return null;
            }

            int hoaOrder =
                    gb.readBits(4) + 1;

            int channelConfig;

            switch (hoaOrder) {
                case 1:
                    channelConfig = CHANNEL_CONFIG_HOA_ORDER1;
                    totalChannels = 4;
                    break;

                case 2:
                    channelConfig = CHANNEL_CONFIG_HOA_ORDER2;
                    totalChannels = 9;
                    break;

                case 3:
                    channelConfig = CHANNEL_CONFIG_HOA_ORDER3;
                    totalChannels = 16;
                    break;

                default:
                    return null;
            }

            /*
             * resolution_index: 2 bits
             */
            if (gb.bitsLeft() < 2) {
                return null;
            }

            int resolutionIndex =
                    gb.readBits(2);

            if (resolutionIndex > 2) {
                return null;
            }

            /*
             * bitrate_index: 4 bits
             */
            if (gb.bitsLeft() < 4) {
                return null;
            }

            int bitrateIndex =
                    gb.readBits(4);

            int[] bitrateTable =
                    getBitrateTable(channelConfig);

            /*
             * HOA1 / HOA2 may be NULL in the C build when
             * AVS3_HOA_FULL_SUPPORT is disabled.
             */
            if (bitrateTable == null) {
                return null;
            }

            totalBitrate =
                    getBitrate(
                            bitrateTable,
                            bitrateIndex);

            if (totalBitrate <= 0) {
                return null;
            }
        }

        /*
         * ============================================================
         * Common resolution validation for profile 1
         * ============================================================
         *
         * Profile 0 and profile 2 already consumed resolution above.
         * Profile 1 reaches it here.
         */
        if (codingProfile == 1) {

            if (gb.bitsLeft() < 2) {
                return null;
            }

            int resolutionIndex =
                    gb.readBits(2);

            if (resolutionIndex > 2) {
                return null;
            }
        }

        /*
         * ============================================================
         * Second 8-bit CRC field
         * ============================================================
         *
         * Matches:
         *
         *   skip_bits(&gb, 8);
         *
         * in the current av3a.c.
         */
        if (gb.bitsLeft() < 8) {
            return null;
        }

        gb.skipBits(8);

        /*
         * ============================================================
         * Frame size
         * ============================================================
         *
         * Must exactly match av3a_parser.c:
         *
         *   ceil(total_bitrate * 1024 /
         *        (sampling_rate * 8))
         */
        int frameSize =
                calculateFrameSize(
                        totalBitrate,
                        sampleRate);

        if (frameSize <= 0) {
            return null;
        }

        return new Info(
                sampleRate,
                totalChannels,
                frameSize);
    }

    /**
     * Returns the channel count corresponding to AVS3AChannelConfig.
     */
    private static int getChannelCount(int channelConfig) {
        switch (channelConfig) {
            case CHANNEL_CONFIG_MONO:
                return 1;

            case CHANNEL_CONFIG_STEREO:
                return 2;

            case CHANNEL_CONFIG_MC_5_1:
                return 6;

            case CHANNEL_CONFIG_MC_7_1:
                return 8;

            case CHANNEL_CONFIG_MC_10_2:
                return 12;

            case CHANNEL_CONFIG_MC_22_2:
                return 24;

            case CHANNEL_CONFIG_MC_4_0:
                return 4;

            case CHANNEL_CONFIG_MC_5_1_2:
                return 8;

            case CHANNEL_CONFIG_MC_5_1_4:
                return 10;

            case CHANNEL_CONFIG_MC_7_1_2:
                return 10;

            case CHANNEL_CONFIG_MC_7_1_4:
                return 12;

            case CHANNEL_CONFIG_HOA_ORDER1:
                return 4;

            case CHANNEL_CONFIG_HOA_ORDER2:
                return 9;

            case CHANNEL_CONFIG_HOA_ORDER3:
                return 16;

            default:
                return -1;
        }
    }

    /**
     * Returns the bitrate table corresponding to an AVS3 channel config.
     *
     * <p>Null exactly where the current C table contains NULL.
     */
    @Nullable
    private static int[] getBitrateTable(int channelConfig) {
        switch (channelConfig) {
            case CHANNEL_CONFIG_MONO:
                return BITRATE_TABLE_MONO;

            case CHANNEL_CONFIG_STEREO:
                return BITRATE_TABLE_STEREO;

            case CHANNEL_CONFIG_MC_5_1:
                return BITRATE_TABLE_MC_5_1;

            case CHANNEL_CONFIG_MC_7_1:
                return BITRATE_TABLE_MC_7_1;

            case CHANNEL_CONFIG_MC_10_2:
                return null;

            case CHANNEL_CONFIG_MC_22_2:
                return null;

            case CHANNEL_CONFIG_MC_4_0:
                return BITRATE_TABLE_MC_4_0;

            case CHANNEL_CONFIG_MC_5_1_2:
                return BITRATE_TABLE_MC_5_1_2;

            case CHANNEL_CONFIG_MC_5_1_4:
                return BITRATE_TABLE_MC_5_1_4;

            case CHANNEL_CONFIG_MC_7_1_2:
                return BITRATE_TABLE_MC_7_1_2;

            case CHANNEL_CONFIG_MC_7_1_4:
                return BITRATE_TABLE_MC_7_1_4;

            case CHANNEL_CONFIG_HOA_ORDER1:
                return BITRATE_TABLE_FOA;

            case CHANNEL_CONFIG_HOA_ORDER2:
                return BITRATE_TABLE_HOA_2;

            case CHANNEL_CONFIG_HOA_ORDER3:
                return BITRATE_TABLE_HOA_3;

            default:
                return null;
        }
    }

    /**
     * Returns a bitrate table entry or -1 if the index is invalid.
     */
    private static int getBitrate(
            int[] bitrateTable,
            int index) {

        if (bitrateTable == null
                || index < 0
                || index >= bitrateTable.length) {
            return -1;
        }

        return bitrateTable[index];
    }

    /**
     * Calculates the complete coded frame size exactly like the FFmpeg
     * AV3A parser.
     */
    private static int calculateFrameSize(
            int totalBitrate,
            int sampleRate) {

        if (totalBitrate <= 0
                || sampleRate <= 0) {
            return -1;
        }

        long numerator =
                (long) totalBitrate
                        * AVS3_AUDIO_FRAME_SIZE;

        long denominator =
                (long) sampleRate * 8L;

        long frameSize =
                (numerator + denominator - 1L)
                        / denominator;

        if (frameSize <= 0
                || frameSize > Integer.MAX_VALUE) {
            return -1;
        }

        return (int) frameSize;
    }

    private Av3aUtil() {}
}