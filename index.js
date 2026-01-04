const express = require('express');
const path = require('path');
const { execSync } = require('child_process');
const http = require('http');
const { PORT: CONFIG_PORT, ENABLE_EPG } = require('./lib/config');
const { dbExists } = require('./lib/db');
const { TUNERS } = require('./lib/tuner');
const Channels = require('./lib/channels');
const EPG = require('./lib/epg');
const DVR = require('./lib/dvr');
const { setupRoutes } = require('./lib/routes');
const logger = require('./lib/logger');
const wsManager = require('./lib/websocket');
const { RECORDINGS_DIR } = require('./lib/config');

const app = express();
const server = http.createServer(app);
const PORT = process.env.PORT || CONFIG_PORT;
app.use(express.json());

// Initialize WS
wsManager.init(server);

// Initialize DVR
DVR.init();

// Start background download of channel icons
Channels.downloadImages();

// Block all requests until EPG scan is complete
// Get build version (count + short hash)
let buildVersion = 'v1.0.0';
try {
    const count = execSync('git rev-list --count HEAD', { stdio: ['ignore', 'pipe', 'ignore'] })
        .toString()
        .trim();
    const hash = execSync('git rev-parse --short HEAD', { stdio: ['ignore', 'pipe', 'ignore'] })
        .toString()
        .trim();
    buildVersion = `v1.0.0 (Build ${count}-${hash})`;
} catch {
    logger.warn('Could not determine build version from git');
}

// Block all requests until EPG scan is complete
app.use((req, res, next) => {
    if (!EPG.isInitialScanDone) {
        res.set('Retry-After', '30');
        // Serve the static HTML file
        return res.status(503).sendFile('initializing.html', {
            root: path.join(__dirname, 'public')
        });
    }
    next();
});

// Serve static files
app.use(express.static('public'));
app.use('/recordings', express.static(RECORDINGS_DIR));

// Set up routes
setupRoutes(app, buildVersion);

if (ENABLE_EPG) {
    // Schedule EPG grab every 15 minutes with a shorter 15s-per-mux timeout for background updates
    setInterval(() => EPG.grab(15000), 15 * 60 * 1000);

    // Priority: Initial grab on startup ONLY if database is missing
    if (!dbExists) {
        logger.info('Database not found, performing initial deep EPG scan...');
        // Small delay to ensure tuners are ready
        // Deep scan (30s per mux)
        setTimeout(() => EPG.grab(30000), 2000);
    } else {
        // If DB exists, we are ready immediately. Periodic background scan will update data later.
        logger.info('Database found, skipping initial EPG scan.');
        EPG.isInitialScanDone = true;
    }
} else {
    logger.info('EPG scanning is disabled.');
    EPG.isInitialScanDone = true;
}

server.listen(PORT, () => {
    logger.info(`ZapLink (Build ${buildVersion}) listening at http://localhost:${PORT}`);
});

// Global Cleanup on App Exit
/* eslint-disable n/no-process-exit */
function cleanExit() {
    logger.info('\nApp stopping, ensuring all tuners are released...');
    TUNERS.forEach((tuner) => {
        if (tuner.inUse && tuner.processes) {
            logger.info(`Killing processes for Tuner ${tuner.id}`);
            if (tuner.processes.zap) {
                try {
                    tuner.processes.zap.kill('SIGKILL');
                } catch {
                    /* Ignore error */
                }
            }
            if (tuner.processes.ffmpeg) {
                try {
                    tuner.processes.ffmpeg.kill('SIGKILL');
                } catch {
                    /* Ignore */
                }
            }
        }
    });
    process.exit();
}

process.on('SIGINT', cleanExit);
process.on('SIGTERM', cleanExit);
