const sqlite3 = require('sqlite3').verbose();
const fs = require('fs');
const { dbPath } = require('./config');

const dbExists = fs.existsSync(dbPath);
const db = new sqlite3.Database(dbPath);

db.serialize(() => {
    db.run(`CREATE TABLE IF NOT EXISTS programs (
        frequency TEXT,
        channel_service_id TEXT,
        start_time INTEGER,
        end_time INTEGER,
        title TEXT,
        description TEXT,
        event_id INTEGER,
        source_id INTEGER,
        PRIMARY KEY (frequency, channel_service_id, start_time)
    )`);
    db.run(`CREATE INDEX IF NOT EXISTS idx_end_time ON programs(end_time)`);

    // DVR Tables
    db.run(`CREATE TABLE IF NOT EXISTS timers (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        type TEXT, -- 'once', 'series'
        title TEXT,
        channel_num TEXT,
        start_time INTEGER, -- for 'once'
        end_time INTEGER,   -- for 'once'
        created_at INTEGER DEFAULT (strftime('%s','now')*1000)
    )`);

    db.run(`CREATE TABLE IF NOT EXISTS recordings (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        title TEXT,
        description TEXT,
        channel_name TEXT,
        channel_num TEXT,
        start_time INTEGER,
        end_time INTEGER,
        file_path TEXT,
        status TEXT, -- 'recording', 'completed', 'failed'
        timer_id INTEGER,
        FOREIGN KEY(timer_id) REFERENCES timers(id) ON DELETE SET NULL
    )`);
});

module.exports = {
    db,
    dbExists
};
