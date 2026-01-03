const path = require('path');

module.exports = {
    PORT: process.env.PORT || 3000,
    dbPath: 'epg.db',
    CHANNELS_CONF: process.env.CHANNELS_CONF || path.resolve(process.cwd(), 'channels.conf'),
    ENABLE_PREEMPTION: process.env.ENABLE_PREEMPTION === 'true',
    TRANSCODE_MODE: process.env.TRANSCODE_MODE || 'none', // none, soft, qsv, nvenc, vaapi
    TRANSCODE_CODEC: process.env.TRANSCODE_CODEC || 'h264', // h264, h265, av1
    VERBOSE_LOGGING: process.env.VERBOSE_LOGGING === 'true',
    ENABLE_EPG: process.env.ENABLE_EPG !== 'false' // Default: true
};
