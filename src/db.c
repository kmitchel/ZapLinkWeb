/**
 * @file db.c
 * @brief SQLite database operations for DVR functionality
 *
 * Manages two primary tables:
 * - timers: Scheduled recording entries
 * - recordings: Completed/in-progress recording metadata
 *
 * The database is created automatically if it doesn't exist.
 * All times are stored in milliseconds since Unix epoch.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sqlite3.h>
#include <time.h>
#include "db.h"
#include "config.h"

/** Module-level database connection handle */
static sqlite3 *db = NULL;

int db_init() {
    int rc = sqlite3_open(DB_PATH, &db);
    if (rc) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        return 0;
    }
    return 1;
}

void db_close() {
    if (db) sqlite3_close(db);
}

// Simple JSON Escape helper
static void json_escape(char *dest, const char *src, size_t size) {
    size_t i = 0;
    while (*src && i < size - 2) {
        if (*src == '"') {
            dest[i++] = '\\';
            dest[i++] = '"';
        } else if (*src == '\\') {
            dest[i++] = '\\';
            dest[i++] = '\\';
        } else if (*src == '\n') {
            dest[i++] = '\\';
            dest[i++] = 'n';
        } else {
            dest[i++] = *src;
        }
        src++;
    }
    dest[i] = '\0';
}

// Helper: Append string with reallocation
static void append_str(char **buf, size_t *cap, size_t *len, const char *str) {
    size_t slen = strlen(str);
    while (*len + slen + 1 > *cap) {
        *cap *= 2;
        *buf = realloc(*buf, *cap);
    }
    strcpy(*buf + *len, str);
    *len += slen;
}

char *db_get_channels_json() {
    // For now, we unfortunately would need to read channels.conf or DB if we stored channels there.
    // The node app read channels.conf.
    // However, the DB probably has program data but maybe not the channel list itself if it relies on channels.conf.
    // Checking lib/routes.js -> Channels comes from lib/channels.js which parses channels.conf.
    // We should implement a basic channel parser or fetch from DB if available. 
    // BUT checking the Node code: "const Channels = require('./lib/channels');" -> it parses the file.
    
    // For this migration, we will focus on DB APIS (Timers/Recordings). 
    // Guide API requires merging Channels + Programs.
    // We will stub this for now with a simple empty array to check connectivity, 
    // but ideally we need to parse channels.conf in C too (ZapLinkCore does this).
    
    return strdup("{\"channels\": []}");
}

// Helper to execute query and return generic JSON array of objects
static char *query_to_json(const char *sql) {
    sqlite3_stmt *stmt;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) return strdup("[]");

    size_t cap = 4096;
    size_t len = 0;
    char *json = malloc(cap);
    strcpy(json, "[");
    len = 1;

    int first = 1;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (!first) append_str(&json, &cap, &len, ",");
        first = 0;
        append_str(&json, &cap, &len, "{");
        
        int cols = sqlite3_column_count(stmt);
        for (int i = 0; i < cols; i++) {
            if (i > 0) append_str(&json, &cap, &len, ",");
            
            const char *name = sqlite3_column_name(stmt, i);
            const char *val = (const char *)sqlite3_column_text(stmt, i);
            char escaped[2048]; 
            json_escape(escaped, val ? val : "", sizeof(escaped));
            
            char buf[4096];
            snprintf(buf, sizeof(buf), "\"%s\":\"%s\"", name, escaped);
            append_str(&json, &cap, &len, buf);
        }
        append_str(&json, &cap, &len, "}");
    }
    append_str(&json, &cap, &len, "]");
    sqlite3_finalize(stmt);
    return json;
}

char *db_get_recordings_json() {
    return query_to_json("SELECT * FROM recordings ORDER BY start_time DESC");
}

char *db_get_timers_json() {
    return query_to_json("SELECT * FROM timers ORDER BY created_at DESC");
}

char *db_get_guide_json(long long start_time, long long end_time) {
    char sql[512];
    snprintf(sql, sizeof(sql), 
        "SELECT * FROM programs WHERE end_time > %lld AND start_time < %lld ORDER BY start_time", 
        start_time, end_time);
    return query_to_json(sql);
}

int db_add_timer(const char *type, const char *title, const char *channel_num, long long start, long long end) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO timers (type, title, channel_num, start_time, end_time, created_at) VALUES (?, ?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, 0);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, type, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, channel_num, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, start);
    sqlite3_bind_int64(stmt, 5, end);
    sqlite3_bind_int64(stmt, 6, (long long)time(NULL) * 1000);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

int db_delete_timer(int id) {
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM timers WHERE id = %d", id);
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
        return 0;
    }
    return 1;
}

int db_delete_recording(int id) {
    char sql[128];
    snprintf(sql, sizeof(sql), "DELETE FROM recordings WHERE id = %d", id);
    char *err_msg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &err_msg);
    if (rc != SQLITE_OK) {
        if (err_msg) sqlite3_free(err_msg);
        return 0;
    }
    return 1;
}

char *db_get_recording_path(int id) {
    sqlite3_stmt *stmt;
    const char *sql = "SELECT file_path FROM recordings WHERE id = ?";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) return NULL;
    
    sqlite3_bind_int(stmt, 1, id);
    
    char *path = NULL;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char *text = (const char *)sqlite3_column_text(stmt, 0);
        if (text) path = strdup(text);
    }
    
    sqlite3_finalize(stmt);
    return path;
}

int db_get_pending_timers(long long now, Timer **out_timers, int *out_count) {
    sqlite3_stmt *stmt;
    // Find timers that are currently running or about to start (buffer handled by caller usually, but here we just check raw times)
    // We want timers where start_time <= now AND end_time > now
    const char *sql = "SELECT id, type, title, channel_num, start_time, end_time FROM timers WHERE start_time <= ? AND end_time > ?";
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) return 0;
    
    sqlite3_bind_int64(stmt, 1, now);
    sqlite3_bind_int64(stmt, 2, now);
    
    int capacity = 10;
    int count = 0;
    Timer *timers = malloc(sizeof(Timer) * capacity);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            timers = realloc(timers, sizeof(Timer) * capacity);
        }
        
        timers[count].id = sqlite3_column_int(stmt, 0);
        
        const char *type = (const char *)sqlite3_column_text(stmt, 1);
        strncpy(timers[count].type, type ? type : "once", sizeof(timers[count].type)-1);
        
        const char *title = (const char *)sqlite3_column_text(stmt, 2);
        strncpy(timers[count].title, title ? title : "Unknown", sizeof(timers[count].title)-1);
        
        const char *chn = (const char *)sqlite3_column_text(stmt, 3);
        strncpy(timers[count].channel_num, chn ? chn : "0", sizeof(timers[count].channel_num)-1);
        
        timers[count].start_time = sqlite3_column_int64(stmt, 4);
        timers[count].end_time = sqlite3_column_int64(stmt, 5);
        
        count++;
    }
    
    sqlite3_finalize(stmt);
    *out_timers = timers;
    *out_count = count;
    return 1;
}

int db_add_recording_entry(const char *title, const char *channel_name, long long start, long long end, const char *path) {
    sqlite3_stmt *stmt;
    const char *sql = "INSERT INTO recordings (title, channel_name, start_time, end_time, file_path) VALUES (?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) != SQLITE_OK) return -1;
    
    sqlite3_bind_text(stmt, 1, title, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, channel_name, -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, start);
    sqlite3_bind_int64(stmt, 4, end);
    sqlite3_bind_text(stmt, 5, path, -1, SQLITE_STATIC);
    
    int id = -1;
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        id = (int)sqlite3_last_insert_rowid(db);
    }
    sqlite3_finalize(stmt);
    return id;
}
