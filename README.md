# Express M3U Tuner

A simple Node.js Express application to bridge DVB tuners to Jellyfin/Emby via M3U. It utilizes `dvbv5-zap` for tuning and `ffmpeg` for stream cleanup/piping.

## Prerequisites (Arch Linux)

Ensure your server has the necessary hardware drivers and utilities installed.

```bash
sudo pacman -S nodejs npm ffmpeg v4l-utils
```

- **v4l-utils**: Provides `dvbv5-zap` and `dvbv5-scan`.
- **ffmpeg**: Used for stream container repair and piping.

## Setup

1.  **Clone and Install**
    ```bash
    git clone https://github.com/kmitchel/jellyfin-tuner.git
    cd jellyfin-tuner
    npm install
    ```

2.  **Channel Configuration**
    Generate a compatible channels file using `dvbv5-scan`. 
    *Example for US ATSC:*
    ```bash
    dvbv5-scan /usr/share/dvb/atsc/us-ATSC-center-frequencies-8VSB -o channels.conf
    ```
    Move this file to `/etc/dvb/channels.conf` or set the `CHANNELS_CONF` environment variable.

3.  **Application Config**
    Edit `index.js` to match your available Tuners and Channel list. The `name` in the `CHANNELS` array **MUST** match the channel name inside your `channels.conf`.

    ```javascript
    // index.js
    const CHANNELS = [
        { number: '1.1', name: 'WXYZ-HD', ... }, 
        // ...
    ];
    ```

## Usage

Start the server:
```bash
node index.js
```
*Port defaults to 3000.*

### Jellyfin Setup

1.  Go to **Dashboard** -> **Live TV**.
2.  Add a **Tuner Device** (Select "M3U Tuner").
3.  **File or URL**: `http://<your-server-ip>:3000/lineup.m3u`
4.  Save and scan for channels.

## Troubleshooting

-   **Logs**: Check the console output for `Zap [Tuner X]` and `FFmpeg [Tuner X]` errors.
-   **Permissions**: Ensure the user running the app is in the `video` group (e.g., `sudo usermod -aG video <user>`).
