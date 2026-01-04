const path = require('path');
const rootDir = path.resolve(__dirname, '..');

module.exports = {
    PORT: process.env.PORT || 3000,
    dbPath: path.resolve(rootDir, 'epg.db'),
    CHANNELS_CONF: process.env.CHANNELS_CONF || path.resolve(rootDir, 'channels.conf'),
    LOGOS_JSON: process.env.LOGOS_JSON || path.resolve(rootDir, 'logos.json'),
    ENABLE_PREEMPTION: process.env.ENABLE_PREEMPTION === 'true',
    TRANSCODE_MODE: process.env.TRANSCODE_MODE || 'none', // none, soft, qsv, nvenc, vaapi
    TRANSCODE_CODEC: process.env.TRANSCODE_CODEC || 'h264', // h264, h265, av1
    VERBOSE_LOGGING: process.env.VERBOSE_LOGGING === 'true',
    ENABLE_EPG: process.env.ENABLE_EPG !== 'false', // Default: true
    RECORDINGS_DIR: process.env.RECORDINGS_DIR || path.resolve(rootDir, 'recordings')
};
