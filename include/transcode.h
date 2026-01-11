/**
 * @file transcode.h
 * @brief Video transcoding via FFmpeg
 *
 * Provides on-the-fly transcoding of video streams for browser compatibility.
 * Supports multiple hardware acceleration backends and codecs.
 *
 * The transcoding pipeline:
 * 1. Fetch source stream from ZapLinkCore (or file for playback)
 * 2. Transcode using configured backend/codec
 * 3. Output fragmented MP4 (or WebM for AV1) to client socket
 */

#ifndef TRANSCODE_H
#define TRANSCODE_H

#include <stdio.h>

/**
 * Hardware acceleration backend for transcoding
 */
typedef enum {
    TRANSCODE_BACKEND_SOFTWARE,  /**< CPU-only encoding (libx264/libx265/libsvtav1) */
    TRANSCODE_BACKEND_QSV,       /**< Intel Quick Sync Video */
    TRANSCODE_BACKEND_NVENC,     /**< NVIDIA NVENC */
    TRANSCODE_BACKEND_VAAPI      /**< VA-API (AMD/Intel on Linux) */
} TranscodeBackend;

/**
 * Video codec for transcoding output
 */
typedef enum {
    TRANSCODE_CODEC_H264,  /**< H.264/AVC - widest compatibility */
    TRANSCODE_CODEC_HEVC,  /**< H.265/HEVC - better compression */
    TRANSCODE_CODEC_AV1,   /**< AV1 - best compression, limited HW support */
    TRANSCODE_CODEC_COPY   /**< Stream copy - no transcoding, passthrough */
} TranscodeCodec;

/**
 * Transcoding configuration
 */
typedef struct {
    TranscodeBackend backend;  /**< Hardware acceleration backend */
    TranscodeCodec codec;      /**< Output video codec */
    int bitrate_kbps;          /**< Video bitrate in kbps (0 = default 10000) */
    int surround51;            /**< Enable 5.1 surround audio (0 or 1) */
} TranscodeConfig;

/**
 * Transcode a live stream and write to client socket
 *
 * Fetches from ZapLinkCore and transcodes in real-time.
 *
 * @param client_socket Socket to write HTTP response to
 * @param core_url Base URL of ZapLinkCore (e.g., "http://127.0.0.1:18392")
 * @param channel_id Channel number (e.g., "15.1")
 * @param config Transcoding configuration
 * @return 0 on success, -1 on error
 */
int transcode_stream(int client_socket, const char *core_url,
                     const char *channel_id, TranscodeConfig config);

/**
 * Transcode any input source and write to client socket
 *
 * Lower-level function that accepts any FFmpeg-compatible input
 * (URL or file path).
 *
 * @param client_socket Socket to write HTTP response to
 * @param input_source URL or file path to transcode
 * @param config Transcoding configuration
 * @return 0 on success, -1 on error
 */
int transcode_source(int client_socket, const char *input_source,
                     TranscodeConfig config);

#endif
