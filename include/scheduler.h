/**
 * @file scheduler.h
 * @brief DVR recording scheduler
 *
 * The scheduler runs in a background thread and automatically:
 * - Starts recordings when timer start times are reached
 * - Stops recordings when timer end times are reached
 * - Manages FFmpeg processes for each recording
 *
 * Recordings are saved to the "recordings/" directory and metadata
 * is stored in the database.
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

/**
 * Start the DVR scheduler background thread
 *
 * This function spawns a thread that polls the database every 10 seconds
 * for pending timers and manages active recordings.
 */
void start_scheduler(void);

/**
 * Manually stop an active recording
 *
 * Sends SIGTERM to the FFmpeg process and updates the database.
 *
 * @param recording_id The ID of the recording to stop
 * @return 1 if recording was found and stopped, 0 if not active
 */
int stop_recording(int recording_id);

/**
 * Get the number of currently active recordings
 *
 * @return Count of recordings currently being written
 */
int get_active_recording_count(void);

/**
 * Get array of active recording IDs
 *
 * @param count Output: number of IDs in returned array
 * @return Heap-allocated array of recording IDs (caller must free),
 *         NULL if no active recordings
 */
int *get_active_recording_ids(int *count);

#endif
