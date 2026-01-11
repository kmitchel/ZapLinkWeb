# ZapLinkWeb ⚡

A high-performance C server that provides a web dashboard and streaming proxy for ZapLinkCore ATSC tuners.

## 🚀 Features

- **Pure C Implementation**: Lightweight, fast, no runtime dependencies.
- **mDNS Discovery**: Automatically discovers ZapLinkCore instances on the network.
- **Flexible Transcoding**: Hardware (QSV, NVENC, VA-API) and software transcoding.
- **Multiple Codecs**: H.264, HEVC, AV1, or passthrough copy.
- **DVR Recording**: Schedule recordings with automatic start/stop.
- **M3U Playlist**: Generate playlists with custom transcode parameters.
- **Modern Dashboard**: Web interface for channel browsing, EPG, and playback.
- **Pretty Logging**: Colored, timestamped output with verbose mode.

## 📸 Dashboard Preview

| **Main Guide** | **Channel Detail** |
|:---:|:---:|
| <img src="screenshots/01_dashboard.png" width="400" /> | <img src="screenshots/03_channel_detail.png" width="400" /> |

## 🛠️ Prerequisites

- **GCC**: C compiler with C99 support
- **FFmpeg**: For transcoding (must be in PATH)
- **SQLite3**: Development headers
- **Avahi**: mDNS/DNS-SD library
- **ZapLinkCore**: Running on localhost or network

```bash
# Arch Linux
sudo pacman -S gcc sqlite avahi ffmpeg

# Ubuntu/Debian
sudo apt install build-essential libsqlite3-dev libavahi-client-dev ffmpeg
```

## 📦 Installation

### Development Build

```bash
git clone https://github.com/kmitchel/ZapLinkWeb.git
cd ZapLinkWeb
make
./build/zaplinkweb
```

### Production Install

```bash
# Build and install to /opt/zaplink
sudo make install

# Enable and start service
sudo systemctl enable --now zaplinkweb
```

The install target:
- Creates `/opt/zaplink` directory
- Creates `zaplink` system user
- Installs binary, public assets, database
- Configures systemd service

### Uninstall

```bash
sudo make uninstall
```
> Note: Database and recordings in `/opt/zaplink` are preserved.

## ⚙️ Configuration

Runtime settings are stored in `zaplink.conf`:

```ini
TRANSCODE_BACKEND=software
TRANSCODE_CODEC=h264
```

These can be changed via the web dashboard Settings panel.

### Command Line Options

```bash
./build/zaplinkweb [-v] [-h]

  -v    Enable verbose/debug logging
  -h    Show help
```

## 🔗 Endpoints

### 📺 Media Endpoints

| Endpoint | Description |
| :--- | :--- |
| `/playlist.m3u` | M3U playlist for Jellyfin/VLC |
| `/stream/:channel` | Live stream (uses dashboard config) |
| `/transcode/.../:channel` | Custom transcode stream |

### M3U Playlist Parameters

Generate playlists with specific transcode settings:

```bash
# Default (software h264)
curl http://localhost:3000/playlist.m3u

# Hardware HEVC at 8Mbps with 5.1 audio
curl "http://localhost:3000/playlist.m3u?backend=vaapi&codec=hevc&bitrate=8000&ac6=1"
```

| Parameter | Values | Description |
|-----------|--------|-------------|
| `backend` | software, qsv, nvenc, vaapi | Hardware acceleration |
| `codec` | h264, hevc, av1, copy | Video codec |
| `bitrate` | integer | Video bitrate in kbps |
| `ac6` | 1 | Enable 5.1 surround audio |

### Transcode URL Format

Direct transcode URLs use path segments:

```
/transcode/{backend}/{codec}/{bitrate}/{audio}/{channel}

Examples:
/transcode/vaapi/hevc/15.1
/transcode/qsv/h264/b8000/21.1
/transcode/software/av1/ac6/33.1
/transcode/copy/15.1
```

### 🛠️ JSON API

| Endpoint | Method | Description |
| :--- | :--- | :--- |
| `/api/status` | GET | Server status and active recordings |
| `/api/channels` | GET | Channel list from channels.conf |
| `/api/guide` | GET | EPG data (proxied from ZapLinkCore) |
| `/api/recordings` | GET | List all recordings |
| `/api/recordings/:id/stop` | POST | Stop an active recording |
| `/api/timers` | GET | List scheduled recordings |
| `/api/timers` | POST | Schedule a new recording |
| `/api/play/:id/...` | GET | Play recording with transcode options |
| `/api/config` | GET/POST | Get/set transcode configuration |

## 🎮 Hardware Acceleration

### Intel Quick Sync (QSV)

Requires Intel iGPU (Gen 7+). User needs `video` and `render` group access.

```bash
sudo usermod -aG video,render $USER
```

### NVIDIA NVENC

Requires NVIDIA GPU with NVENC support and proper drivers.

### VA-API

Works with Intel and AMD GPUs on Linux. Requires `vainfo` to verify support.

```bash
vainfo  # Check VA-API support
```

## 🧠 Architecture

```
┌─────────────────┐     mDNS      ┌──────────────┐
│   ZapLinkWeb    │◄─────────────►│  ZapLinkCore │
│   (Port 3000)   │               │ (Port 18392) │
└────────┬────────┘               └──────┬───────┘
         │                               │
         │ HTTP/Transcode                │ Raw Streams
         ▼                               ▼
    ┌─────────┐                    ┌──────────┐
    │ Browser │                    │ USB Tuner│
    │   VLC   │                    │  /dev/dvb│
    │ Jellyfin│                    └──────────┘
    └─────────┘
```

### Key Components

| File | Purpose |
|------|---------|
| `main.c` | Entry point, signal handling |
| `web.c` | HTTP server and routing |
| `transcode.c` | FFmpeg process management |
| `scheduler.c` | DVR recording scheduler |
| `discovery.c` | mDNS service discovery |
| `channels.c` | channels.conf parser |
| `db.c` | SQLite database operations |

## 📁 Project Structure

```
ZapLinkWeb/
├── include/          # Header files
├── src/              # C source files
├── public/           # Web dashboard (HTML/CSS/JS)
├── build/            # Compiled output
├── recordings/       # DVR recordings
├── channels.conf     # Channel configuration
├── zaplinkweb.db     # SQLite database
└── zaplink.conf      # Runtime configuration
```

## 🔧 Troubleshooting

### Port Already in Use

```bash
fuser -k 3000/tcp
```

### ZapLinkCore Not Discovered

Check that ZapLinkCore is running and advertising via mDNS:

```bash
avahi-browse -r _http._tcp
```

### FFmpeg Errors

Run with `-v` flag to see debug output. Verify FFmpeg is in PATH:

```bash
which ffmpeg
ffmpeg -version
```

## 📄 License

ISC

## 🧪 Status

**Version 2.0** - Complete C rewrite of the original Node.js application.

- ✅ Transcoding with hardware acceleration
- ✅ DVR scheduling and recording
- ✅ M3U playlist generation
- ✅ Web dashboard with EPG
- ✅ mDNS service discovery
- ✅ Pretty logging system
