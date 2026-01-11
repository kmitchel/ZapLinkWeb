/**
 * @file web.c
 * @brief HTTP server implementation
 *
 * Single-threaded-per-connection HTTP server providing:
 * - Static file serving from PUBLIC_DIR
 * - REST API endpoints (/api/...)
 * - Live stream proxying (/stream/)
 * - Transcoded streaming (/transcode/)
 * - Recording playback (/api/play/)
 *
 * Each incoming connection spawns a new pthread for handling.
 * The server supports basic HTTP/1.1 with Connection: close.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <ctype.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "web.h"
#include "config.h"
#include "db.h"
#include "app_config.h"
#include "discovery.h"
#include "transcode.h"
#include "scheduler.h"
#include "channels.h"
#include "log.h"

// MIME type helper
static const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0) return "image/jpeg";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    return "application/octet-stream";
}

// Send HTTP response headers
static void send_headers(int client_socket, int status_code, const char *status_text, const char *content_type, long content_length) {
    char buffer[1024];
    int len = snprintf(buffer, sizeof(buffer),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text, content_type, content_length);
    write(client_socket, buffer, len);
}

// Serve static file
static void serve_file(int client_socket, const char *path) {
    // Basic security: prevent directory traversal
    if (strstr(path, "..")) {
        send_headers(client_socket, 403, "Forbidden", "text/plain", 9);
        write(client_socket, "Forbidden", 9);
        return;
    }

    char full_path[512];
    snprintf(full_path, sizeof(full_path), "%s%s", PUBLIC_DIR, path);
    // Remove query params if any
    char *q = strchr(full_path, '?');
    if (q) *q = '\0';
    
    // If directory, try index.html
    struct stat st;
    if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        strncat(full_path, "/index.html", sizeof(full_path) - strlen(full_path) - 1);
    }

    int fd = open(full_path, O_RDONLY);
    if (fd < 0) {
        // Try falling back to index.html for SPA routing
        // Only if it doesn't look like a static asset request (js, css, png)
        if (!strstr(path, ".js") && !strstr(path, ".css") && !strstr(path, ".png") && !strstr(path, ".jpg")) {
             snprintf(full_path, sizeof(full_path), "%s/index.html", PUBLIC_DIR);
             fd = open(full_path, O_RDONLY);
        }
    }

    if (fd < 0) {
        const char *msg = "404 Not Found";
        send_headers(client_socket, 404, "Not Found", "text/plain", strlen(msg));
        write(client_socket, msg, strlen(msg));
        return;
    }

    fstat(fd, &st);
    send_headers(client_socket, 200, "OK", get_mime_type(full_path), st.st_size);

    char buffer[4096];
    ssize_t bytes_read;
    while ((bytes_read = read(fd, buffer, sizeof(buffer))) > 0) {
        write(client_socket, buffer, bytes_read);
    }
    close(fd);
}

static void *client_handler(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[4096];
    ssize_t bytes_read = read(client_socket, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) {
        close(client_socket);
        return NULL;
    }
    buffer[bytes_read] = '\0';

    // Simple parser
    char method[16], path[1024];
    // This sscanf stops at space so query params are part of path if not handled carefully, 
    // but typically %s stops at whitespace.
    sscanf(buffer, "%15s %1023s", method, path);

    LOG_DEBUG("HTTP", "%s %s", method, path);

    if (strncmp(path, "/api/", 5) == 0) {
        char *json = NULL;
        int status = 200;

        if (strcmp(path, "/api/status") == 0) {
            char status_json[1024];
            int count = 0;
            int *ids = get_active_recording_ids(&count);
            
            // Build ID list string "[1,2]"
            char ids_str[256] = "[";
            for (int i=0; i<count; i++) {
                char num[16];
                snprintf(num, sizeof(num), "%d%s", ids[i], (i<count-1) ? "," : "");
                strncat(ids_str, num, sizeof(ids_str)-strlen(ids_str)-1);
            }
            strncat(ids_str, "]", sizeof(ids_str)-strlen(ids_str)-1);
            if (ids) free(ids);

            snprintf(status_json, sizeof(status_json), 
                "{\"status\":\"ok\",\"version\":\"2.1-c\",\"backend\":\"%s\",\"codec\":\"%s\",\"active_recordings\":%d,\"active_ids\":%s}",
                app_config.backend, app_config.codec, get_active_recording_count(), ids_str);
            json = strdup(status_json);
        } else if (strcmp(path, "/api/config") == 0) {
            if (strcmp(method, "POST") == 0) {
                char *body = strstr(buffer, "\r\n\r\n");
                if (body) {
                    body += 4;
                    char *b = strstr(body, "\"backend\":\"");
                    if (b) {
                        b += 11;
                        char *end = strchr(b, '"');
                        if (end) {
                            *end = '\0';
                            strncpy(app_config.backend, b, sizeof(app_config.backend)-1);
                            *end = '"';
                        }
                    }
                    char *c = strstr(body, "\"codec\":\"");
                    if (c) {
                        c += 9;
                        char *end = strchr(c, '"');
                        if (end) {
                            *end = '\0';
                            strncpy(app_config.codec, c, sizeof(app_config.codec)-1);
                            *end = '"';
                        }
                    }
                    config_save();
                    json = strdup("{\"success\":true}");
                }
            } else {
                char conf_json[512];
                snprintf(conf_json, sizeof(conf_json), 
                    "{\"backend\":\"%s\",\"codec\":\"%s\"}",
                    app_config.backend, app_config.codec);
                json = strdup(conf_json);
            }
        } else if (strcmp(path, "/api/recordings") == 0) {
            json = db_get_recordings_json();
        } else if (strncmp(path, "/api/recordings/", 16) == 0) {
            // Check for /stop suffix
            char *stop_suffix = strstr(path + 16, "/stop");
            if (stop_suffix && strcmp(method, "POST") == 0) {
                 // Format: /api/recordings/123/stop
                 *stop_suffix = '\0'; // Temporarily terminate string to parse ID
                 int id = atoi(path + 16);
                 *stop_suffix = '/'; // Restore if needed (not really)
                 
                 if (stop_recording(id)) json = strdup("{\"success\":true}");
                 else {
                     status = 404;
                     json = strdup("{\"error\":\"Recording not found or not active\"}");
                 }
            }
            else if (strcmp(method, "DELETE") == 0) {
                int id = atoi(path + 16);
                char *fpath = db_get_recording_path(id);
                if (fpath) {
                    unlink(fpath);
                    free(fpath);
                }
                if (db_delete_recording(id)) json = strdup("{\"success\":true}");
                else status = 500;
            }
            // Removed stub
        } else if (strcmp(path, "/api/timers") == 0) {
            if (strcmp(method, "POST") == 0) {
                char *body = strstr(buffer, "\r\n\r\n");
                if (body) {
                    body += 4;
                    char type[32] = "", title[256] = "", channel[32] = "";
                    long long start = 0, end = 0;
                    
                    char *p;
                    if ((p = strstr(body, "\"type\":\""))) sscanf(p + 8, "%31[^\"]", type);
                    if ((p = strstr(body, "\"title\":\""))) sscanf(p + 9, "%255[^\"]", title);
                    if ((p = strstr(body, "\"channel_num\":\""))) sscanf(p + 15, "%31[^\"]", channel);
                    if ((p = strstr(body, "\"start_time\":"))) start = atoll(p + 13);
                    if ((p = strstr(body, "\"end_time\":"))) end = atoll(p + 11);
                    
                    if (db_add_timer(type, title, channel, start, end)) json = strdup("{\"success\":true}");
                    else status = 500;
                }
            } else {
                json = db_get_timers_json();
            }
        } else if (strncmp(path, "/api/timers/", 12) == 0) {
            if (strcmp(method, "DELETE") == 0) {
                int id = atoi(path + 12);
                if (db_delete_timer(id)) json = strdup("{\"success\":true}");
                else status = 500;
            }
        } else if (strncmp(path, "/api/play/", 10) == 0) {
            // Recording Playback: /api/play/[id]/[format]/[codec]
            // Example: /api/play/123/mp4/h264
            
            int id = 0;
            TranscodeConfig tc;
            tc.backend = TRANSCODE_BACKEND_SOFTWARE; // Default
            tc.codec = TRANSCODE_CODEC_H264;         // Default
            tc.bitrate_kbps = 0;
            tc.surround51 = 0;

            char *p = strdup(path + 10);
            char *token = strtok(p, "/");
            
            // First token is ID
            if (token && isdigit(token[0])) {
                id = atoi(token);
                token = strtok(NULL, "/");
            }

            // Remaining tokens: format, codec, options
            while (token) {
                // Codec
                if (strcmp(token, "h264") == 0) tc.codec = TRANSCODE_CODEC_H264;
                else if (strcmp(token, "hevc") == 0) tc.codec = TRANSCODE_CODEC_HEVC;
                else if (strcmp(token, "av1") == 0) tc.codec = TRANSCODE_CODEC_AV1;
                else if (strcmp(token, "copy") == 0) tc.codec = TRANSCODE_CODEC_COPY;

                // Backend
                else if (strcmp(token, "qsv") == 0) tc.backend = TRANSCODE_BACKEND_QSV;
                else if (strcmp(token, "nvenc") == 0) tc.backend = TRANSCODE_BACKEND_NVENC;
                else if (strcmp(token, "vaapi") == 0) tc.backend = TRANSCODE_BACKEND_VAAPI;
                
                // Audio
                else if (strcmp(token, "ac6") == 0) tc.surround51 = 1;

                // Bitrate
                else if ((token[0] == 'b' || token[0] == 'B') && isdigit(token[1])) {
                    tc.bitrate_kbps = atoi(token + 1);
                }

                token = strtok(NULL, "/");
            }
            free(p);
            
            if (id > 0) {
                char *fpath = db_get_recording_path(id);
                if (fpath) {
                    printf("[PLAY] Playing Rec %d: %s (Backend=%d Codec=%d)\n", id, fpath, tc.backend, tc.codec);
                    
                    if (transcode_source(client_socket, fpath, tc) < 0) {
                        printf("[PLAY] Transcode startup failed\n");
                    }
                    free(fpath);
                    
                    // Route handled, socket closed by transcode logic or below
                    close(client_socket);
                    return NULL;
                } else {
                    json = strdup("{\"error\":\"Recording not found\"}");
                    status = 404;
                }
            } else {
                json = strdup("{\"error\":\"Invalid ID\"}");
                status = 400;
            }

        } else if (strcmp(path, "/api/version") == 0) {
            json = strdup("{\"version\":\"2.1.0-c\"}");
        } else {
            json = strdup("{\"error\":\"Not Implemented\"}");
            status = 501;
        }
        
        if (json) {
            send_headers(client_socket, status, "OK", "application/json", strlen(json));
            write(client_socket, json, strlen(json));
            free(json);
        } else {
            const char *err = "{\"error\":\"Internal Server Error\"}";
            send_headers(client_socket, 500, "Internal Server Error", "application/json", strlen(err));
            write(client_socket, err, strlen(err));
        }
    } else if (strncmp(path, "/stream/", 8) == 0) {
        // Streaming Proxy / Transcode
        const char *chan = path + 8;
        const char *core = get_core_base_url();
        
        if (!core) {
            const char *err = "{\"error\":\"ZapLinkCore not discovered yet\"}";
            send_headers(client_socket, 503, "Service Unavailable", "application/json", strlen(err));
            write(client_socket, err, strlen(err));
        } else {
            // Map configuration
            TranscodeConfig tc;
            tc.bitrate_kbps = 0; // Default
            tc.surround51 = 0;   // Default

            if (strcmp(app_config.backend, "qsv") == 0) tc.backend = TRANSCODE_BACKEND_QSV;
            else if (strcmp(app_config.backend, "nvenc") == 0) tc.backend = TRANSCODE_BACKEND_NVENC;
            else if (strcmp(app_config.backend, "vaapi") == 0) tc.backend = TRANSCODE_BACKEND_VAAPI;
            else tc.backend = TRANSCODE_BACKEND_SOFTWARE;

            if (strcmp(app_config.codec, "hevc") == 0) tc.codec = TRANSCODE_CODEC_HEVC;
            else if (strcmp(app_config.codec, "av1") == 0) tc.codec = TRANSCODE_CODEC_AV1;
            else if (strcmp(app_config.codec, "copy") == 0) tc.codec = TRANSCODE_CODEC_COPY;
            else tc.codec = TRANSCODE_CODEC_H264;

            printf("[WEB] Starting Transcode from %s (Backend=%s, Codec=%s)\n", core, app_config.backend, app_config.codec);
            
            if (transcode_stream(client_socket, core, chan, tc) < 0) {
                // If transcode failed immediately
                printf("[WEB] Transcode startup failed\n");
            }
        }
        close(client_socket);
        return NULL;
    } else if (strncmp(path, "/transcode/", 11) == 0) {
        // Flexible Transcoding Endpoint
        // /transcode/[backend]/[codec]/[options]/[channel]
        
        TranscodeConfig tc;
        tc.backend = TRANSCODE_BACKEND_SOFTWARE; // Default
        tc.codec = TRANSCODE_CODEC_H264;         // Default
        tc.bitrate_kbps = 0;                     // Default
        tc.surround51 = 0;                       // Default
        char channel_id[64] = {0};

        // Make a copy of path segments after /transcode/
        char *p = strdup(path + 11);
        char *token = strtok(p, "/");
        while (token) {
            // Backend
            if (strcmp(token, "software") == 0) tc.backend = TRANSCODE_BACKEND_SOFTWARE;
            else if (strcmp(token, "qsv") == 0) tc.backend = TRANSCODE_BACKEND_QSV;
            else if (strcmp(token, "nvenc") == 0) tc.backend = TRANSCODE_BACKEND_NVENC;
            else if (strcmp(token, "vaapi") == 0) tc.backend = TRANSCODE_BACKEND_VAAPI;
            
            // Codec
            else if (strcmp(token, "h264") == 0) tc.codec = TRANSCODE_CODEC_H264;
            else if (strcmp(token, "hevc") == 0) tc.codec = TRANSCODE_CODEC_HEVC;
            else if (strcmp(token, "av1") == 0) tc.codec = TRANSCODE_CODEC_AV1;
            else if (strcmp(token, "copy") == 0) tc.codec = TRANSCODE_CODEC_COPY;

            // Audio (ac6 = 5.1 surround)
            else if (strcmp(token, "ac6") == 0) tc.surround51 = 1;

            // Bitrate (bXXXX)
            else if ((token[0] == 'b' || token[0] == 'B') && isdigit(token[1])) {
                tc.bitrate_kbps = atoi(token + 1);
            }

            // Channel ID (Fallback if not a keyword)
            else {
                strncpy(channel_id, token, sizeof(channel_id) - 1);
            }

            token = strtok(NULL, "/");
        }
        free(p);

        const char *core = get_core_base_url();
        if (!core) {
            const char *err = "{\"error\":\"ZapLinkCore not discovered yet\"}";
            send_headers(client_socket, 503, "Service Unavailable", "application/json", strlen(err));
            write(client_socket, err, strlen(err));
        } else if (strlen(channel_id) == 0) {
            const char *err = "{\"error\":\"No channel specified\"}";
            send_headers(client_socket, 400, "Bad Request", "application/json", strlen(err));
            write(client_socket, err, strlen(err));
        } else {
            printf("[TRANSCODE] Req: Chan=%s Backend=%d Codec=%d Bitrate=%d 5.1=%d\n", 
                   channel_id, tc.backend, tc.codec, tc.bitrate_kbps, tc.surround51);
                   
            if (transcode_stream(client_socket, core, channel_id, tc) < 0) {
                printf("[TRANSCODE] Startup failed\n");
            }
        }
        close(client_socket);
        return NULL;

    } else if (strncmp(path, "/playlist.m3u", 13) == 0) {
        /* ================================================================
         * M3U Playlist Generation
         * Supports query params: ?backend=X&codec=Y&bitrate=Z&ac6=1
         * Generates URLs using /transcode/ endpoint format
         * ================================================================ */
        
        /* Parse query parameters */
        char backend[32] = "";
        char codec[32] = "";
        char bitrate[16] = "";
        int ac6 = 0;
        
        char *query = strchr(path, '?');
        if (query) {
            query++;  /* Skip the '?' */
            char *param = strtok(query, "&");
            while (param) {
                if (strncmp(param, "backend=", 8) == 0) {
                    strncpy(backend, param + 8, sizeof(backend) - 1);
                } else if (strncmp(param, "codec=", 6) == 0) {
                    strncpy(codec, param + 6, sizeof(codec) - 1);
                } else if (strncmp(param, "bitrate=", 8) == 0) {
                    strncpy(bitrate, param + 8, sizeof(bitrate) - 1);
                } else if (strncmp(param, "ac6=", 4) == 0) {
                    ac6 = atoi(param + 4);
                }
                param = strtok(NULL, "&");
            }
        }
        
        /* Build transcode path prefix */
        char transcode_path[128] = "";
        if (strlen(backend) > 0) {
            strcat(transcode_path, "/");
            strcat(transcode_path, backend);
        }
        if (strlen(codec) > 0) {
            strcat(transcode_path, "/");
            strcat(transcode_path, codec);
        }
        if (strlen(bitrate) > 0) {
            strcat(transcode_path, "/b");
            strcat(transcode_path, bitrate);
        }
        if (ac6) {
            strcat(transcode_path, "/ac6");
        }
        
        /* Load channels */
        int chan_count = 0;
        Channel *channels = channels_load(&chan_count);
        
        if (!channels || chan_count == 0) {
            const char *err = "# No channels found in channels.conf\n";
            send_headers(client_socket, 200, "OK", "audio/x-mpegurl", strlen(err));
            write(client_socket, err, strlen(err));
            if (channels) channels_free(channels, chan_count);
            close(client_socket);
            return NULL;
        }
        
        /* Get Host header for absolute URLs */
        char *host_header = strstr(buffer, "Host:");
        char host[256] = "localhost:3000";  /* Default */
        if (host_header) {
            host_header += 5;  /* Skip "Host:" */
            while (*host_header == ' ') host_header++;
            char *end = strpbrk(host_header, "\r\n");
            if (end) {
                int len = end - host_header;
                if (len > 0 && len < (int)sizeof(host)) {
                    strncpy(host, host_header, len);
                    host[len] = '\0';
                }
            }
        }
        
        /* Build M3U playlist */
        size_t buf_cap = 4096;
        size_t buf_len = 0;
        char *m3u = malloc(buf_cap);
        
        /* Header */
        buf_len += snprintf(m3u + buf_len, buf_cap - buf_len, "#EXTM3U\n");
        
        /* Each channel */
        for (int i = 0; i < chan_count; i++) {
            /* Ensure buffer capacity */
            while (buf_len + 512 > buf_cap) {
                buf_cap *= 2;
                m3u = realloc(m3u, buf_cap);
            }
            
            buf_len += snprintf(m3u + buf_len, buf_cap - buf_len,
                "#EXTINF:-1 tvg-id=\"%s\" tvg-name=\"%s\",%s\n"
                "http://%s/transcode%s/%s\n",
                channels[i].number, channels[i].name, channels[i].name,
                host, transcode_path, channels[i].number);
        }
        
        channels_free(channels, chan_count);
        
        /* Send response */
        send_headers(client_socket, 200, "OK", "audio/x-mpegurl", buf_len);
        write(client_socket, m3u, buf_len);
        free(m3u);
        
        close(client_socket);
        return NULL;

    } else {
        serve_file(client_socket, path);
    }

    close(client_socket);
    return NULL;
}

void start_web_server(int port) {
    int server_socket, client_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(1);
    }

    listen(server_socket, 10);
    printf("ZapLinkWeb (C) listening on port %d\n", port);

    while (1) {
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if (client_socket < 0) continue;

        pthread_t thread;
        int *arg = malloc(sizeof(int));
        *arg = client_socket;
        pthread_create(&thread, NULL, client_handler, arg);
        pthread_detach(thread);
    }
}

