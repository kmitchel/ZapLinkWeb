const puppeteer = require('puppeteer');
const fs = require('fs');
const path = require('path');

(async () => {
    const browser = await puppeteer.launch({
        args: ['--no-sandbox', '--disable-setuid-sandbox']
    });
    const page = await browser.newPage();
    await page.setViewport({ width: 1280, height: 800 });

    try {
        console.log('Navigating to dashboard...');
        await page.goto('http://localhost:3000', { waitUntil: 'networkidle0' });

        // 1. Main Dashboard
        console.log('Capturing Main Dashboard...');
        await page.waitForSelector('.guide-row');
        await page.screenshot({ path: 'screenshots/01_dashboard.png' });

        // 2. Favorites
        console.log('Toggling Favorite...');
        // Find a star and click it. Ideally one that isn't already active, but toggling is fine.
        await page.click('.guide-row:nth-child(3) .fav-star');
        // Wait for scroll snap animation (approx 500ms)
        await new Promise(r => setTimeout(r, 1000));
        await page.screenshot({ path: 'screenshots/02_favorites.png' });

        // 3. Channel Detail
        console.log('Opening Channel Detail...');
        // Click the channel column (not the star)
        // We need to be specific to avoid clicking the star again if it's in the way, 
        // but the star has stopPropagation, so clicking the parent .channel-col outside the star is safe.
        // Let's click the .channel-info part
        await page.click('.guide-row:nth-child(1) .channel-info');
        await page.waitForSelector('#channel-detail-overlay', { visible: true });
        // Wait for grid to render
        await new Promise(r => setTimeout(r, 1000));
        await page.screenshot({ path: 'screenshots/03_channel_detail.png' });

        // 4. Program Detail
        console.log('Opening Program Detail...');
        await page.click('.detail-program-tile'); // Click the first tile in the detail grid
        await page.waitForSelector('#details-panel', { visible: true });
        await new Promise(r => setTimeout(r, 500));
        await page.screenshot({ path: 'screenshots/04_program_detail.png' });

        console.log('Done.');
    } catch (e) {
        console.error('Error:', e);
    } finally {
        await browser.close();
    }
})();
