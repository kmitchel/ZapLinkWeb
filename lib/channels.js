const fs = require('fs');
const path = require('path');
const https = require('https');
const { CHANNELS_CONF, LOGOS_JSON, ICONS_DIR } = require('./config');

let CHANNELS = [];
let LOCAL_ICON_MAP = {};

function getLatestLogos() {
    try {
        if (fs.existsSync(LOGOS_JSON)) {
            const remoteLogos = JSON.parse(fs.readFileSync(LOGOS_JSON, 'utf8'));
            // Overlay local icons if available
            return { ...remoteLogos, ...LOCAL_ICON_MAP };
        }
    } catch (e) {
        console.warn('[Config] Failed to parse logos.json:', e);
    }
    return {};
}

async function downloadImages() {
    console.log('[Icons] Starting icon download check...');
    try {
        if (!fs.existsSync(ICONS_DIR)) fs.mkdirSync(ICONS_DIR, { recursive: true });

        const logos = getLatestLogos(); // Actually gets remote ones primarily initially
        const downloads = Object.entries(logos).map(([key, url]) => {
            if (!url || !url.startsWith('http')) return Promise.resolve();
            // Since we merged LOCAL_ICON_MAP above, we might be looking at a local path starting with /logos/
            // Skip if it looks like a local path already or invalid
            if (url.startsWith('/logos/')) return Promise.resolve();

            return new Promise((resolve) => {
                const ext = path.extname(url).split('?')[0] || '.png';
                const safeKey = key.replace(/[^a-z0-9\.\-_]/gi, '_');
                const filename = `${safeKey}${ext}`;
                const localPath = path.join(ICONS_DIR, filename);
                const webPath = `/logos/${filename}`;

                if (fs.existsSync(localPath)) {
                    LOCAL_ICON_MAP[key] = webPath;
                    resolve();
                    return;
                }

                const file = fs.createWriteStream(localPath);
                https.get(url, (response) => {
                    if (response.statusCode !== 200) {
                        console.warn(`[Icons] Failed to download ${url}: Status ${response.statusCode}`);
                        fs.unlink(localPath, () => { }); // Verify blank cleanup
                        resolve();
                        return;
                    }
                    response.pipe(file);
                    file.on('finish', () => {
                        file.close(() => {
                            LOCAL_ICON_MAP[key] = webPath;
                            console.log(`[Icons] Cached: ${key} -> ${webPath}`);
                            resolve();
                        });
                    });
                }).on('error', (err) => {
                    console.error(`[Icons] Error downloading ${url}:`, err.message);
                    fs.unlink(localPath, () => { });
                    resolve();
                });
            });
        });

        await Promise.all(downloads);
        console.log(`[Icons] Download check complete. ${Object.keys(LOCAL_ICON_MAP).length} icons locally cached.`);
    } catch (e) {
        console.error('[Icons] Critical error during download:', e);
    }
}

function matchIcon(logos, channel) {
    const channelNum = (channel.number || '').toString().trim();
    const channelName = (channel.name || '').trim();
    const hyphenated = channelNum.replace('.', '-');
    const natural = channelNum.replace('-', '.');

    // Debugging: Log what's being looked up
    // console.log(`matchIcon: checking for ${channelNum} / ${channelName} in ${Object.keys(logos).length} logos`);

    return logos[natural] ||
        logos[hyphenated] ||
        logos[channelNum] ||
        logos[channelName] ||
        Object.entries(logos).find(([k, v]) => k.toLowerCase() === channelName.toLowerCase())?.[1] ||
        channel.icon ||
        null;
}

function loadChannels() {
    console.log(`Loading channels from ${CHANNELS_CONF}...`);
    try {
        if (!fs.existsSync(CHANNELS_CONF)) {
            console.warn('Channels config not found, using empty list.');
            return;
        }

        const data = fs.readFileSync(CHANNELS_CONF, 'utf8');
        const entries = data.split('[');

        // Load logos if logos.json exists
        const logos = getLatestLogos();
        console.log(`[Config] Loaded ${Object.keys(logos).length} icons from logos.json`);

        CHANNELS = [];

        entries.forEach(entry => {
            if (!entry.trim()) return;
            const lines = entry.split('\n');
            const name = lines[0].replace(']', '').trim();
            const serviceIdLine = lines.find(l => l.trim().startsWith('SERVICE_ID'));
            const vChannelLine = lines.find(l => l.trim().startsWith('VCHANNEL'));
            const freqLine = lines.find(l => l.trim().startsWith('FREQUENCY'));

            if (serviceIdLine && vChannelLine) {
                let serviceId = serviceIdLine.split('=')[1].trim();
                const vChannel = vChannelLine.split('=')[1].trim();
                const frequency = freqLine ? freqLine.split('=')[1].trim() : null;

                // Normalize serviceId to decimal string (handles 0x hex if present)
                serviceId = parseInt(serviceId, serviceId.startsWith('0x') ? 16 : 10).toString();

                const natural = vChannel.replace('-', '.');
                const hyphenated = vChannel.replace('.', '-');

                CHANNELS.push({
                    number: vChannel,
                    naturalNumber: natural,
                    name: name,
                    serviceId: serviceId,
                    frequency: frequency,
                    icon: matchIcon(logos, { number: vChannel, name: name }),
                    rawConfig: `[${name}]\n${entry.substring(entry.indexOf(']') + 1).trim()}`
                });
            }
        });

        const iconCount = CHANNELS.filter(c => c.icon).length;
        console.log(`[Config] Matched ${iconCount} icons in channels.conf`);

        console.log(`Loaded ${CHANNELS.length} channels:`);
        CHANNELS.forEach(c => console.log(`  - ${c.name} (Service ID: ${c.serviceId}, Chan: ${c.number})${c.icon ? ' [ICON FOUND]' : ''}`));

        if (iconCount > 0) {
            const sample = CHANNELS.find(c => c.icon);
            console.log(`[Config] Icon sample: ${sample.number} -> ${sample.icon}`);
        }

        // Sort by channel number (Natural TV Sort: 12.2 comes before 12.10)
        CHANNELS.sort((a, b) => {
            const partsA = a.number.split(/[\.-]/).map(Number);
            const partsB = b.number.split(/[\.-]/).map(Number);

            for (let i = 0; i < Math.max(partsA.length, partsB.length); i++) {
                const numA = partsA[i] || 0;
                const numB = partsB[i] || 0;
                if (numA !== numB) return numA - numB;
            }
            return 0;
        });

    } catch (e) {
        console.error('Failed to parse channels.conf:', e);
    }
}

// Initial load
loadChannels();

let lastLogoLoad = 0;

module.exports = {
    get CHANNELS() {
        // Reload logos if the file exists and we haven't checked in a while (e.g. 1 minute)
        // or if we have no icons populated at all yet.
        const now = Date.now();
        const hasIcons = CHANNELS.some(c => c.icon);

        if (fs.existsSync(LOGOS_JSON) && (!hasIcons || (now - lastLogoLoad > 60000))) {
            lastLogoLoad = now;
            loadChannels();
        }
        return CHANNELS;
    },
    loadChannels,
    getLatestLogos,
    matchIcon,
    downloadImages
};
