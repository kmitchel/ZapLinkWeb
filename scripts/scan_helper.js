const https = require('https');
const fs = require('fs');
const readline = require('readline');
const { spawn } = require('child_process');

const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout
});

function calculateCenterFrequency(channel) {
    const ch = parseInt(channel, 10);
    if (ch >= 2 && ch <= 4) return (57 + (ch - 2) * 6) * 1000000;
    if (ch >= 5 && ch <= 6) return (79 + (ch - 5) * 6) * 1000000;
    if (ch >= 7 && ch <= 13) return (177 + (ch - 7) * 6) * 1000000;
    if (ch >= 14 && ch <= 69) return (473 + (ch - 14) * 6) * 1000000; // Updated UHF start
    return 0;
}

function fetchRabbitEars(zip) {
    return new Promise((resolve, reject) => {
        // Use the tabular search query which is easier to parse
        const url = `https://www.rabbitears.info/search.php?request=zip_search&zipcode=${zip}`;
        console.log(`Querying RabbitEars for ${zip}...`);

        const req = https.get(url, (res) => {
            let data = '';
            res.on('data', (chunk) => data += chunk);
            res.on('end', () => resolve(data));
        });

        req.on('error', (e) => reject(e));
        req.end();
    });
}

function parseChannels(html) {
    const channels = [];

    // Naive HTML table parser
    // We look for rows: <tr>...</tr>
    // We expect 7 columns. Col 0 is Channel, Col 5 is Distance.

    const rowRegex = /<tr[^>]*>(.*?)<\/tr>/gs;
    let rowMatch;

    while ((rowMatch = rowRegex.exec(html)) !== null) {
        const rowContent = rowMatch[1];
        // Split by <td> (this is rough but usually works for simple tables)
        const cols = rowContent.split(/<td[^>]*>/).slice(1); // slice(1) to remove content before first td

        if (cols.length >= 6) {
            // content of col 0
            const col0 = cols[0].replace(/<\/td>/g, '').trim();
            // content of col 5 (Distance)
            const col5 = cols[5].replace(/<\/td>/g, '').trim();

            // Channel often looks like "2-1 (36)" or just "36" or "<strong>2-1</strong> (36)"
            // Logic: look for number in parens, OR just number
            let physCh = null;
            const parenMatch = col0.match(/\((\d+)\)/);
            if (parenMatch) {
                physCh = parseInt(parenMatch[1]);
            } else {
                // Fallback: see if the whole thing is a number, or starts with one?
                // Often virtual and physical are same?
                // Let's strip tags strings:
                const cleanText = col0.replace(/<[^>]+>/g, '').trim();
                // If text is like "36", use it.
                if (/^\d+$/.test(cleanText)) physCh = parseInt(cleanText);
            }

            // Distance: "10.5 miles"
            let dist = 999;
            const distMatch = col5.match(/([\d\.]+)/);
            if (distMatch) {
                dist = parseFloat(distMatch[1]);
            }

            if (physCh && !isNaN(physCh)) {
                // Filter duplicates? Or keep them (maybe different towers?)
                // Usually one physical channel per tower.
                channels.push({ ch: physCh, dist: dist });
            }
        }
    }
    return channels;
}

// Function to handle the actual scanning process
function runScanPrompt(scanFile) {
    rl.question('Do you want to run the scan now? (Y/n): ', (ans) => {
        // Default to YES if empty or starts with y
        if (ans.trim() === '' || ans.toLowerCase().startsWith('y')) {
            console.log('Starting scan... (this may take a minute)');
            // -F: Only scan frequencies in the file (don't trust NIT to add more)
            // -T 0.5: Timeout faster on bad signals
            // -v: Verbose (to catch events)
            // -C us: US country code
            const scan = spawn('/usr/bin/dvbv5-scan', [scanFile, '-o', 'channels.conf', '-F', '-T', '0.5', '-v', '-C', 'us']);

            // dvbv5-scan writes logs to stderr, but some versions/contexts might use stdout
            // We'll capture both just to be safe.

            let buffer = '';

            const processOutput = (d) => {
                buffer += d.toString();
                let lines = buffer.split('\n');

                // Keep the last incomplete line in the buffer
                buffer = lines.pop(); // Remove the last element (potential partial line)

                lines.forEach(line => {
                    // Strip ANSI color codes and whitespace
                    // eslint-disable-next-line no-control-regex
                    const cleanLine = line.replace(/\x1b\[[0-9;]*m/g, '').trim();
                    if (!cleanLine) return;

                    // Example: Scanning frequency #1 57000000
                    let match = cleanLine.match(/Scanning frequency #\d+ (\d+)/);
                    if (match) {
                        const freq = (parseInt(match[1]) / 1000000).toFixed(2);
                        console.log(`🔭 Scanning ${freq} MHz...`);
                        return;
                    }

                    // Example: Lock   (0x1f) Signal= -39.00dBm C/N= 32.77dB
                    match = cleanLine.match(/Lock\s+\(0x[\da-f]+\)\s+Signal=\s+([-\d.]+)dBm\s+C\/N=\s+([-\d.]+)dB/);
                    if (match) {
                        console.log(`🔒 LOCK! Signal: ${match[1]}dBm (SNR: ${match[2]}dB)`);
                        return;
                    }

                    // Example: Virtual channel 55.1, name = WFFT-TV
                    // Relaxed regex to catch various formats
                    match = cleanLine.match(/Virtual channel ([\d.]+), name = (.+)/);
                    if (match) {
                        let name = match[2].trim();
                        // Fix concatenated error strings (e.g. "BounceERROR...")
                        const errorIndex = name.indexOf('ERROR');
                        if (errorIndex !== -1) {
                            name = name.substring(0, errorIndex).trim();
                        }

                        console.log(`📺 Found: [${match[1]}] ${name}`);
                        return;
                    }
                });
            };

            scan.stderr.on('data', processOutput);
            scan.stdout.on('data', processOutput);


            // stdout might just be empty if -o is used, or contain the file content if printed
            // We mainly rely on stderr for logs in verbose mode.
            // scan.stdout.pipe(process.stdout);

            scan.on('close', (code) => {
                if (code === 0) {
                    console.log('Scan complete! Saved to channels.conf');
                    console.log('You will need to copy channels.conf and restart ZapLink to apply changes.');
                } else {
                    console.log('Scan failed.');
                }
                rl.close();
            });
        } else {
            console.log('Scan skipped. You can run it later with:');
            console.log(`dvbv5-scan ${scanFile} -o channels.conf`);
            rl.close();
        }
    });
}

// Shared function to filter channels, generate config, and run scan
function processChannelList(finalListOfChans, scanFile, showStats = true) {
    if (showStats) {
        console.log(`Found ${finalListOfChans.length} potential channels.`);
        console.log('Top 5 closest:', finalListOfChans.slice(0, 5).map(c => `${c.ch} (${c.dist}mi)`).join(', '));
    }

    rl.question('Do you want to skip VHF channels (2-13)? (Y/n): ', (ans) => {
        if (ans.trim() === '' || ans.toLowerCase().startsWith('y')) {
            const originalCount = finalListOfChans.length;
            // VHF is 2-13. Higher is UHF.
            finalListOfChans = finalListOfChans.filter(c => c.ch >= 14);
            console.log(`Filtered out ${originalCount - finalListOfChans.length} VHF channels.`);
        }

        let confContent = '';

        finalListOfChans.forEach(item => {
            const freq = calculateCenterFrequency(item.ch);
            if (freq > 0) {
                confContent += `[CHANNEL_${item.ch}]\n`;
                confContent += `\tDELIVERY_SYSTEM = ATSC\n`;
                confContent += `\tFREQUENCY = ${freq}\n`;
                confContent += `\tMODULATION = VSB/8\n`;
                confContent += `\tINVERSION = AUTO\n\n`;
            }
        });

        fs.writeFileSync(scanFile, confContent);
        console.log(`Saved scan configuration to ${scanFile}`);
        runScanPrompt(scanFile);
    });
}

async function processRabbitEars(zip, scanFile) {
    try {
        const html = await fetchRabbitEars(zip);
        let foundChannels = parseChannels(html);

        if (foundChannels.length === 0) {
            console.log('Could not automatically detect channels (parsing failed).');
            console.log('Generating standard US ATSC scan list...');
            let channels = [];
            for (let i = 2; i <= 36; i++) channels.push(i);
            foundChannels = channels.map(c => ({ ch: c, dist: 0 }));
        }

        if (foundChannels.length === 0) {
            console.error('No channels found.');
            rl.close();
            return;
        }

        // UNIQUE filter then SORT
        const uniqueMap = new Map();
        foundChannels.forEach(item => {
            if (!uniqueMap.has(item.ch)) {
                uniqueMap.set(item.ch, item.dist);
            } else {
                // Keep closer one?
                if (item.dist < uniqueMap.get(item.ch)) {
                    uniqueMap.set(item.ch, item.dist);
                }
            }
        });

        // Convert back to array
        let finalListOfChans = Array.from(uniqueMap.entries()).map(([ch, dist]) => ({ ch, dist }));

        // SORT BY DISTANCE
        finalListOfChans.sort((a, b) => a.dist - b.dist);

        processChannelList(finalListOfChans, scanFile, true);

    } catch (e) {
        console.error('Error:', e);
        rl.close();
    }
}

rl.question('Enter your Zip Code: ', async (zip) => {
    const scanFile = `atsc_scan_${zip}.conf`;

    if (fs.existsSync(scanFile)) {
        rl.question(`Configuration file ${scanFile} already exists.\nFetch fresh data from RabbitEars? (y/N): `, (ans) => {
            if (ans.toLowerCase().startsWith('y')) {
                processRabbitEars(zip, scanFile);
            } else {
                console.log('Using existing configuration file...');
                // Parse existing file to support filtering
                try {
                    const content = fs.readFileSync(scanFile, 'utf8');
                    const matches = [...content.matchAll(/\[CHANNEL_(\d+)\]/g)];
                    if (matches.length > 0) {
                        const channels = matches.map(m => ({ ch: parseInt(m[1]), dist: 0 }));
                        processChannelList(channels, scanFile, false);
                    } else {
                        // Fallback if parsing fails
                        console.log('Could not parse existing file channels. Skipping filter step.');
                        runScanPrompt(scanFile);
                    }
                } catch (e) {
                    console.error('Error reading existing file:', e);
                    runScanPrompt(scanFile);
                }
            }
        });
    } else {
        processRabbitEars(zip, scanFile);
    }
});
