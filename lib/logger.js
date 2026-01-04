const pino = require('pino');
const { VERBOSE_LOGGING } = require('./config');

// Use pino-pretty if running in a terminal for better readability,
// otherwise (e.g. systemd/docker without TTY) use defaults (JSON) or standard formatting.
// For now, we'll keep it simple. If you want pretty logs in systemd, you can pipe to pino-pretty or configure it.
// We will default to JSON for "structured logging" but enable pretty printing if the env var PRETTY_LOGS is set,
// or if we are in a TTY.

const usePretty = process.env.PRETTY_LOGS === 'true' || process.stdout.isTTY;

const logger = pino({
    level: VERBOSE_LOGGING ? 'debug' : 'info',
    transport: usePretty
        ? {
              target: 'pino-pretty',
              options: {
                  colorize: true,
                  translateTime: 'YYYY-MM-DD HH:mm:ss',
                  ignore: 'pid,hostname'
              }
          }
        : undefined,
    base: { pid: process.pid } // Keep pid in JSON logs
});

module.exports = logger;
