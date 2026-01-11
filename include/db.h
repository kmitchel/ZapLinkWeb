/**
 * @file db.h
 * @brief SQLite database interface for recordings and timers
 *
 * Provides all database operations for the DVR functionality including:
 * - Timer management (scheduling recordings)
 * - Recording metadata storage
 * - JSON serialization for API responses
 *
 * The database file location is defined by DB_PATH in config.h.
 * Tables are created automatically on first run.
 */

#ifndef DB_H
#define DB_H

#include <sqlite3.h>
#include <time.h>

/**
 * Timer structure representing a scheduled recording
 */
typedef struct {
    int id;                    /**< Unique timer ID */
    char type[16];             /**< Timer type: "once", "daily", "weekly" */
    char title[128];           /**< Program title */
    char channel_num[16];      /**< Channel number (e.g., "15.1") */
    long long start_time;      /**< Start time in milliseconds since epoch */
    long long end_time;        /**< End time in milliseconds since epoch */
} Timer;

/* ============================================================================
 * Database Lifecycle
 * ============================================================================ */

/**
 * Initialize database connection and create tables if needed
 * @return 1 on success, 0 on failure
 */
int db_init(void);

/**
 * Close database connection and release resources
 */
void db_close(void);

/* ============================================================================
 * JSON API Helpers
 * ============================================================================ */

/**
 * Get all channels as JSON array
 * @return Heap-allocated JSON string (caller must free)
 */
char *db_get_channels_json(void);

/**
 * Get EPG guide data as JSON for a time range
 * @param start_time Start of range (ms since epoch)
 * @param end_time End of range (ms since epoch)
 * @return Heap-allocated JSON string (caller must free)
 */
char *db_get_guide_json(long long start_time, long long end_time);

/**
 * Get all recordings as JSON array
 * @return Heap-allocated JSON string (caller must free)
 */
char *db_get_recordings_json(void);

/**
 * Get all timers as JSON array
 * @return Heap-allocated JSON string (caller must free)
 */
char *db_get_timers_json(void);

/* ============================================================================
 * Timer Management
 * ============================================================================ */

/**
 * Add a new recording timer
 * @param type Timer type ("once", "daily", "weekly")
 * @param title Program title
 * @param channel_num Channel number string
 * @param start Start time (ms since epoch)
 * @param end End time (ms since epoch)
 * @return New timer ID on success, -1 on failure
 */
int db_add_timer(const char *type, const char *title, const char *channel_num,
                 long long start, long long end);

/**
 * Delete a timer by ID
 * @param id Timer ID to delete
 * @return 1 on success, 0 on failure
 */
int db_delete_timer(int id);

/* ============================================================================
 * Recording Management
 * ============================================================================ */

/**
 * Delete a recording by ID (also removes file from disk)
 * @param id Recording ID to delete
 * @return 1 on success, 0 on failure
 */
int db_delete_recording(int id);

/**
 * Get the file path for a recording
 * @param id Recording ID
 * @return Heap-allocated path string (caller must free), NULL if not found
 */
char *db_get_recording_path(int id);

/* ============================================================================
 * Scheduler Support
 * ============================================================================ */

/**
 * Get all timers that should be active now
 * @param now Current time (ms since epoch)
 * @param timers Output: array of Timer structs (caller must free)
 * @param count Output: number of timers returned
 * @return 1 on success, 0 on failure
 */
int db_get_pending_timers(long long now, Timer **timers, int *count);

/**
 * Create a new recording entry when recording starts
 * @param title Recording title
 * @param channel_name Channel identifier
 * @param start Start time (ms since epoch)
 * @param end End time (ms since epoch), 0 if unknown
 * @param path File path where recording is saved
 * @return New recording ID on success, -1 on failure
 */
int db_add_recording_entry(const char *title, const char *channel_name,
                           long long start, long long end, const char *path);

/**
 * Update recording end time when recording completes
 * @param id Recording ID
 * @param end Actual end time (ms since epoch)
 * @return 1 on success, 0 on failure
 */
int db_update_recording_end_time(int id, long long end);

#endif
