/**
 * @file transcode.c
 * @brief FFmpeg-based video transcoding pipeline
 *
 * Provides real-time transcoding of video streams for browser playback.
 * The pipeline:
 * 1. Spawns FFmpeg as a child process
 * 2. Pipes FFmpeg stdout to the client socket
 * 3. Manages process lifecycle (cleanup on disconnect)
 *
 * Supports multiple hardware acceleration backends:
 * - Software (libx264, libx265, libsvtav1)
 * - Intel QSV
 * - NVIDIA NVENC
 * - VA-API (AMD/Intel on Linux)
 *
 * Output format is fragmented MP4 for H.264/HEVC, WebM for AV1.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>

#include "transcode.h"
#include "log.h"

/* Default audio bitrates */
static const char *default_audio_bitrate = "128k";         /**< Stereo AAC/Opus */
static const char *default_aac_surround_bitrate = "384k";  /**< 5.1 AAC */
static const char *default_surround_bitrate = "384k";      /**< 5.1 Opus */

static char **build_ffmpeg_args(const char *input_url, TranscodeConfig config, int *argc_out) {
    int capacity = 64;
    char **argv = malloc(sizeof(char*) * capacity);
    int argc = 0;

    argv[argc++] = "ffmpeg";
    
    // HW Accel Enums: 
    // VAAPI: -init_hw_device vaapi=gpu:/dev/dri/renderD128 -filter_hw_device gpu
    // QSV:   -init_hw_device qsv=hw -filter_hw_device hw
    // NVENC: (none for init, just codec selection) (Reference code handles this differently for nvenc?)
    // Reference check: 
    // if (engine === 'qsv') ffmpegArgs.push('-init_hw_device', 'qsv=hw', '-filter_hw_device', 'hw');
    // else if (engine === 'vaapi') ffmpegArgs.push('-init_hw_device', 'vaapi=gpu:/dev/dri/renderD128', '-filter_hw_device', 'gpu');

    if (config.backend == TRANSCODE_BACKEND_VAAPI) {
        argv[argc++] = "-init_hw_device";
        argv[argc++] = "vaapi=gpu:/dev/dri/renderD128";
        argv[argc++] = "-filter_hw_device";
        argv[argc++] = "gpu";
    } else if (config.backend == TRANSCODE_BACKEND_QSV) {
        argv[argc++] = "-init_hw_device";
        argv[argc++] = "qsv=hw";
        argv[argc++] = "-filter_hw_device";
        argv[argc++] = "hw";
    }

    argv[argc++] = "-re"; // Read input at native frame rate (important for live streams?) 
    // Actually, for transcoding, usually -re is for pushing to RTMP, but if we are pulling live, we don't strictly need it 
    // effectively, but lets stick to reference or safe defaults. Input is http live stream, so it flows at live rate anyway.
    
    argv[argc++] = "-i";
    argv[argc++] = (char*)input_url;

    // Video Codec
    if (config.codec == TRANSCODE_CODEC_COPY) {
        argv[argc++] = "-c:v";
        argv[argc++] = "copy";
    } else {
        // Encoder Selection & Filters
        if (config.backend == TRANSCODE_BACKEND_SOFTWARE) {
            argv[argc++] = "-c:v";
            if (config.codec == TRANSCODE_CODEC_HEVC) argv[argc++] = "libx265";
            else if (config.codec == TRANSCODE_CODEC_AV1) argv[argc++] = "libsvtav1";
            else argv[argc++] = "libx264";

            argv[argc++] = "-preset";
            argv[argc++] = "fast"; // ultrafast?
            argv[argc++] = "-crf";
            argv[argc++] = "23";

        } else if (config.backend == TRANSCODE_BACKEND_NVENC) {
            argv[argc++] = "-c:v";
            if (config.codec == TRANSCODE_CODEC_HEVC) argv[argc++] = "hevc_nvenc";
            else if (config.codec == TRANSCODE_CODEC_AV1) argv[argc++] = "av1_nvenc";
            else argv[argc++] = "h264_nvenc";

            argv[argc++] = "-preset";
            argv[argc++] = "p4"; // medium
            argv[argc++] = "-rc";
            argv[argc++] = "constqp"; // or vbr
            argv[argc++] = "-qp"; // cq
            argv[argc++] = "23"; // 18?

        } else if (config.backend == TRANSCODE_BACKEND_QSV) {
            // Filter
            // -vf yadif=0:-1:0,format=nv12,hwupload=extra_hw_frames=64,format=qsv
            argv[argc++] = "-vf";
            argv[argc++] = "yadif=0:-1:0,format=nv12,hwupload=extra_hw_frames=64,format=qsv";

            argv[argc++] = "-c:v";
            if (config.codec == TRANSCODE_CODEC_HEVC) argv[argc++] = "hevc_qsv";
            else if (config.codec == TRANSCODE_CODEC_AV1) argv[argc++] = "av1_qsv";
            else argv[argc++] = "h264_qsv";
            
            argv[argc++] = "-global_quality";
            argv[argc++] = "23";

        } else if (config.backend == TRANSCODE_BACKEND_VAAPI) {
            // Filter: format=nv12,hwupload
            argv[argc++] = "-vf";
            argv[argc++] = "format=nv12,hwupload";

            argv[argc++] = "-c:v";
            if (config.codec == TRANSCODE_CODEC_HEVC) argv[argc++] = "hevc_vaapi";
            else if (config.codec == TRANSCODE_CODEC_AV1) argv[argc++] = "av1_vaapi"; // experimental?
            else argv[argc++] = "h264_vaapi";

            argv[argc++] = "-qp";
            argv[argc++] = "23";
        }

        // Audio Codec
         if (config.codec == TRANSCODE_CODEC_AV1) {
            // Opus for AV1/WebM
            if (config.surround51) {
                argv[argc++] = "-af";
                argv[argc++] = "channelmap=channel_layout=5.1";
                argv[argc++] = "-c:a";
                argv[argc++] = "libopus";
                argv[argc++] = "-mapping_family";
                argv[argc++] = "1";
                argv[argc++] = "-b:a";
                argv[argc++] = (char*)default_surround_bitrate;
            } else {
                argv[argc++] = "-ac";
                argv[argc++] = "2";
                argv[argc++] = "-c:a";
                argv[argc++] = "libopus";
                argv[argc++] = "-b:a";
                argv[argc++] = (char*)default_audio_bitrate;
            }
        } else {
            // AAC for H264/HEVC/MPEGTS
            if (config.surround51) {
                argv[argc++] = "-af";
                argv[argc++] = "channelmap=channel_layout=5.1";
                argv[argc++] = "-c:a";
                argv[argc++] = "aac";
                argv[argc++] = "-b:a";
                argv[argc++] = (char*)default_aac_surround_bitrate;
            } else {
                argv[argc++] = "-ac";
                argv[argc++] = "2";
                argv[argc++] = "-c:a";
                argv[argc++] = "aac";
                argv[argc++] = "-b:a";
                argv[argc++] = (char*)default_audio_bitrate;
            }
        }
    }

    // Format
    argv[argc++] = "-f";
    if (config.codec == TRANSCODE_CODEC_AV1) {
        argv[argc++] = "webm";
    } else {
        // Use fragmented MP4 for better browser compatibility than MPEG-TS
        argv[argc++] = "mp4";
        argv[argc++] = "-movflags";
        argv[argc++] = "frag_keyframe+empty_moov+default_base_moof";
    }

    // Output to stdout
    argv[argc++] = "pipe:1";
    argv[argc] = NULL;
    
    *argc_out = argc;
    return argv;
}

static void send_headers(int client_socket, const char *content_type) {
    char buffer[1024];
    int len = snprintf(buffer, sizeof(buffer),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        content_type);
    write(client_socket, buffer, len);
}

int transcode_source(int client_socket, const char *input_source, TranscodeConfig config) {
    // Pipe for ffmpeg stdout -> parent
    int pipe_fd[2];
    if (pipe(pipe_fd) < 0) {
        perror("pipe failed");
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork failed");
        return -1;
    }

    if (pid == 0) {
        // Child: FFmpeg
        close(pipe_fd[0]);
        
        // Redirect stdout to pipe write end
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);

        // Close all other FDs to be safe
        // (Assuming standard setup, not strictly iterating all)
        
        int argc;
        char **argv = build_ffmpeg_args(input_source, config, &argc);

        // Redirect stderr to /dev/null to hide ffmpeg debug output
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }

        execvp("ffmpeg", argv);
        perror("execvp ffmpeg failed");
        exit(1);
    }

    // Parent
    close(pipe_fd[1]); // Close write end

    // Send HTTP Headers to Client first
    // Determine content type
    const char *ctype = (config.codec == TRANSCODE_CODEC_AV1) ? "video/webm" : "video/mp4";
    send_headers(client_socket, ctype);

    // Relay loop
    char buffer[8192];
    ssize_t n;
    while ((n = read(pipe_fd[0], buffer, sizeof(buffer))) > 0) {
        if (write(client_socket, buffer, n) < 0) {
            // Client likely disconnected
            break;
        }
    }

    LOG_DEBUG("TRANSCODE", "Client disconnected, stopping ffmpeg pid=%d", pid);
    
    // Cleanup
    kill(pid, SIGTERM);
    close(pipe_fd[0]);
    int status;
    waitpid(pid, &status, 0);

    return 0;
}

int transcode_stream(int client_socket, const char *core_url, const char *channel_id, TranscodeConfig config) {
    char input_url[512];
    snprintf(input_url, sizeof(input_url), "%s/stream/%s", core_url, channel_id);
    return transcode_source(client_socket, input_url, config);
}
