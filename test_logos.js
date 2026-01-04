const { LOGOS_JSON } = require('./lib/config');
const Channels = require('./lib/channels');
const fs = require('fs');

console.log('LOGOS_JSON Path:', LOGOS_JSON);
console.log('File Exists?', fs.existsSync(LOGOS_JSON));

if (fs.existsSync(LOGOS_JSON)) {
    try {
        const content = fs.readFileSync(LOGOS_JSON, 'utf8');
        console.log('File size:', content.length);
        const parsed = JSON.parse(content);
        console.log('Key count:', Object.keys(parsed).length);
        console.log('Sample keys:', Object.keys(parsed).slice(0, 5));

        const logos = Channels.getLatestLogos();
        console.log('getLatestLogos() count:', Object.keys(logos).length);

        const testChan = { number: '12.1', name: 'WXYZ' };
        console.log(`Match for 12.1:`, Channels.matchIcon(logos, testChan));
    } catch (e) {
        console.error('Error reading/parsing:', e);
    }
} else {
    console.error('LOGOS_JSON not found');
}
