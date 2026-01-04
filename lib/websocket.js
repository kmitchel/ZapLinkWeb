const { WebSocketServer } = require('ws');
const logger = require('./logger');

let wss;

const WebSocketManager = {
    init(server) {
        wss = new WebSocketServer({ server });

        wss.on('connection', (ws) => {
            logger.info('[WebSocket] Client connected');
            ws.on('close', () => logger.info('[WebSocket] Client disconnected'));
        });

        logger.info('[WebSocket] Server initialized');
    },

    broadcast(type, data) {
        if (!wss) return;

        const message = JSON.stringify({ type, data, timestamp: Date.now() });
        wss.clients.forEach((client) => {
            if (client.readyState === 1) {
                // OPEN
                client.send(message);
            }
        });
    }
};

module.exports = WebSocketManager;
