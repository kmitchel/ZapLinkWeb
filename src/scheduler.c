/**
 * @file scheduler.c
 * @brief DVR recording scheduler
 *
 * Manages automatic recording based on scheduled timers:
 * - Background thread polls database every POLL_INTERVAL seconds
 * - Starts FFmpeg processes when timer start times are reached
 * - Stops recordings when end times are reached or manually requested
 *
 * Recordings are saved to the "recordings/" directory as MP4 files.
 * The scheduler uses the local /stream/ endpoint to fetch content,
 * ensuring proper discovery resolution.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>

#include "scheduler.h"
#include "db.h"
#include "config.h"
#include "web.h"
#include "log.h"

/** Seconds between database polls for pending timers */
#define POLL_INTERVAL 10

/**
 * Tracks an active recording session
 */
typedef struct {
    int timer_id;           /**< Associated timer ID */
    int recording_id;       /**< Database recording ID */
    pid_t pid;              /**< FFmpeg process ID (0 = slot empty) */
    long long end_time;     /**< Scheduled end time (ms since epoch) */
    char path[256];         /**< Output file path */
} ActiveRecording;

/** Maximum concurrent recordings */
#define MAX_ACTIVE_RECORDINGS 16

/** Active recording slots */
static ActiveRecording active_recordings[MAX_ACTIVE_RECORDINGS];

/** Mutex for thread-safe access to active_recordings */
static pthread_mutex_t active_mutex = PTHREAD_MUTEX_INITIALIZER;

void *scheduler_thread(void *arg) {
    (void)arg;
    LOG_INFO("DVR", "Scheduler thread started");

    while (1) {
        time_t now_sec = time(NULL);
        long long now_ms = (long long)now_sec * 1000;
        
        // 1. Check pending timers
        Timer *timers = NULL;
        int count = 0;
        if (db_get_pending_timers(now_ms, &timers, &count)) {
            for (int i = 0; i < count; i++) {
                // Check if already active
                int is_active = 0;
                pthread_mutex_lock(&active_mutex);
                for (int j = 0; j < MAX_ACTIVE_RECORDINGS; j++) {
                    if (active_recordings[j].timer_id == timers[i].id) {
                        is_active = 1;
                        break;
                    }
                }
                pthread_mutex_unlock(&active_mutex);

                if (!is_active) {
                    // START RECORDING
                    LOG_INFO("DVR", "Starting recording: %s", timers[i].title);
                    
                    // Generate Path
                    char filename[256];
                    // Sanitize title
                    char safe_title[128];
                    strncpy(safe_title, timers[i].title, 127);
                    safe_title[127] = 0;
                    for(int c=0; safe_title[c]; c++) {
                        if (safe_title[c] == '/' || safe_title[c] == '\\' || safe_title[c] == ' ') safe_title[c] = '_';
                    }
                    
                    snprintf(filename, sizeof(filename), "recordings/%s-%lld.mp4", safe_title, now_ms);
                    
                    // Ensure directory exists (quick hack, ideally done at startup)
                    mkdir("recordings", 0777);

                    // Insert into DB first to get ID
                    int rec_id = db_add_recording_entry(timers[i].title,  timers[i].channel_num, now_ms, 0, filename);
                    if (rec_id == -1) {
                        LOG_ERROR("DVR", "Failed to create recording DB entry");
                        continue;
                    }

                    // Fork FFmpeg
                    pid_t pid = fork();
                    if (pid == 0) {
                        // Child
                        // Redirect stderr/stdout to /dev/null to hide ffmpeg output
                        int devnull = open("/dev/null", O_WRONLY);
                        if (devnull >= 0) {
                            dup2(devnull, STDERR_FILENO);
                            dup2(devnull, STDOUT_FILENO);
                            close(devnull);
                        }
                        
                        // Use our own stream endpoint to ensure we get the resolved stream
                        char stream_url[128];
                        snprintf(stream_url, sizeof(stream_url), "http://127.0.0.1:%d/stream/%s", WEB_PORT, timers[i].channel_num);
                        
                        // FFmpeg args: Input stream, copy codec (or transcode if needed), output file
                        execlp("ffmpeg", "ffmpeg", 
                            "-i", stream_url, 
                            "-c", "copy", 
                            "-bsf:a", "aac_adtstoasc",
                            "-movflags", "faststart",
                            "-y", 
                            filename, 
                            NULL);
                        
                        // If exec fails (won't print because stderr is closed)
                        _exit(1);
                    } else if (pid > 0) {
                        // Parent
                        pthread_mutex_lock(&active_mutex);
                        for (int j = 0; j < MAX_ACTIVE_RECORDINGS; j++) {
                            if (active_recordings[j].pid == 0) {
                                active_recordings[j].timer_id = timers[i].id;
                                active_recordings[j].recording_id = rec_id;
                                active_recordings[j].pid = pid;
                                active_recordings[j].end_time = timers[i].end_time;
                                strncpy(active_recordings[j].path, filename, 255);
                                break;
                            }
                        }
                        pthread_mutex_unlock(&active_mutex);
                    }
                }
            }
            if (timers) free(timers);
        }

        // 2. Check for finished recordings
        pthread_mutex_lock(&active_mutex);
        for (int j = 0; j < MAX_ACTIVE_RECORDINGS; j++) {
            if (active_recordings[j].pid != 0) {
                // Check if time is up
                if (now_ms >= active_recordings[j].end_time) {
                    LOG_INFO("DVR", "Stopping recording ID %d (time reached)", active_recordings[j].recording_id);
                    kill(active_recordings[j].pid, SIGTERM);
                    waitpid(active_recordings[j].pid, NULL, 0);
                    
                    // Update End Time in DB (Implement helper if verifying duration matters, or just leave as is)
                    // Reset slot
                    active_recordings[j].pid = 0;
                    active_recordings[j].timer_id = 0;
                    
                    // TODO: We should probably delete the "Once" timer or update its status so it doesn't trigger again immediately if loops logic was different.
                    // But db_get_pending_timers checks for valid range. If end_time is passed, it won't be returned by DB.
                    // However, we should clean up "active" "once" timers. 
                    db_delete_timer(active_recordings[j].timer_id); // Simple approach: delete "once" timers when done.
                } else {
                    // Check if process is still alive
                    int status;
                    if (waitpid(active_recordings[j].pid, &status, WNOHANG) != 0) {
                        LOG_WARN("DVR", "FFmpeg process %d died unexpectedly", active_recordings[j].pid);
                        active_recordings[j].pid = 0;
                        active_recordings[j].timer_id = 0;
                    }
                }
            }
        }
        pthread_mutex_unlock(&active_mutex);

        sleep(POLL_INTERVAL);
    }
    return NULL;
}

void start_scheduler() {
    // Clear active list
    memset(active_recordings, 0, sizeof(active_recordings));
    
    pthread_t th;
    if (pthread_create(&th, NULL, scheduler_thread, NULL) != 0) {
        fprintf(stderr, "Failed to create scheduler thread\n");
    } else {
        pthread_detach(th);
    }
}

int stop_recording(int recording_id) {
    int found = 0;
    pthread_mutex_lock(&active_mutex);
    for (int j = 0; j < MAX_ACTIVE_RECORDINGS; j++) {
        if (active_recordings[j].recording_id == recording_id && active_recordings[j].pid != 0) {
            kill(active_recordings[j].pid, SIGTERM);
            waitpid(active_recordings[j].pid, NULL, 0);
            active_recordings[j].pid = 0;
            // Don't delete timer here necessarily, depends on logic, but for now we just stop the recording.
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&active_mutex);
    return found;
}

int get_active_recording_count() {
    int count = 0;
    pthread_mutex_lock(&active_mutex);
    for (int j = 0; j < MAX_ACTIVE_RECORDINGS; j++) {
        if (active_recordings[j].pid != 0) count++;
    }
    pthread_mutex_unlock(&active_mutex);
    return count;
}

int *get_active_recording_ids(int *count) {
    *count = 0;
    pthread_mutex_lock(&active_mutex);
    // First count
    for (int j = 0; j < MAX_ACTIVE_RECORDINGS; j++) {
        if (active_recordings[j].pid != 0) (*count)++;
    }
    
    if (*count == 0) {
        pthread_mutex_unlock(&active_mutex);
        return NULL;
    }

    int *ids = malloc(sizeof(int) * (*count));
    int idx = 0;
    for (int j = 0; j < MAX_ACTIVE_RECORDINGS; j++) {
        if (active_recordings[j].pid != 0) {
            ids[idx++] = active_recordings[j].recording_id;
        }
    }
    pthread_mutex_unlock(&active_mutex);
    return ids;
}
