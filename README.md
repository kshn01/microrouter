# 🌐 MicroRouter

**ESP32-S3 powered advanced WiFi router management portal** — transforms a basic WiFi router into a feature-rich smart network management system.

## ✨ Features (Phase 1)

- 🔄 **Dual-bank OTA updates** — safe wireless firmware updates with automatic rollback
- 🌐 **Premium Web Portal** — dark glassmorphism SPA served from flash
- 📡 **WiFi Management** — scan, connect, auto-reconnect with captive portal setup
- 📊 **Real-time Dashboard** — live system stats via WebSocket
- 🔒 **Secure OTA** — password-protected firmware uploads
- 📱 **Fully Responsive** — works beautifully on desktop and mobile

## 🛠 Hardware

- **Board**: ESP32-S3-DevKitC-1 (8MB Flash)
- **Framework**: Arduino (via PlatformIO)

## 🚀 Getting Started

### Prerequisites
- [PlatformIO](https://platformio.org/) (VS Code extension recommended)
- ESP32-S3 board connected via USB

### Initial Flash (USB)
```bash
# Flash firmware
pio run -e serial -t upload

# Flash web UI to LittleFS
pio run -e serial -t uploadfs
```

### Access the Portal
1. **First boot**: Connect to WiFi `MicroRouter-Setup` (password: `setup1234`)
2. Open `http://192.168.4.1` in your browser
3. Connect to your home WiFi via the portal
4. After restart, access at `http://microrouter.local`

### OTA Updates (Wireless)
```bash
# Via PlatformIO (ArduinoOTA)
pio run -e ota -t upload

# Via Browser (ElegantOTA)
# Open http://microrouter.local/update
# Login: admin / microrouter
```

## 📁 Project Structure

```
├── platformio.ini          # Build configuration
├── partitions.csv          # Dual-OTA partition table
├── src/
│   ├── main.cpp            # Boot sequence
│   ├── config.h            # Configuration constants
│   ├── wifi_manager.h/cpp  # WiFi STA/AP management
│   ├── ota_manager.h/cpp   # OTA with rollback
│   └── web_server.h/cpp    # HTTP API + WebSocket
└── data/                   # Web UI (LittleFS)
    ├── index.html
    ├── css/app.css
    └── js/app.js
```

## 🔌 API Endpoints

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/system/info` | Chip, firmware, storage info |
| GET | `/api/system/health` | Uptime, memory, boot count |
| POST | `/api/system/restart` | Restart device |
| GET | `/api/wifi/status` | WiFi connection state |
| GET | `/api/wifi/scan` | Scan available networks |
| POST | `/api/wifi/connect` | Connect to network `{ssid, password}` |
| POST | `/api/wifi/disconnect` | Forget & restart in AP mode |
| WS | `/ws` | Real-time stats (2s interval) |

## 📋 Roadmap

- [x] Phase 1: OTA + Web Portal + WiFi Management
- [ ] Phase 2: Network Intelligence (device scanning, ARP monitoring)
- [ ] Phase 3: DNS Proxy + Ad Blocking
- [ ] Phase 4: Parental Controls + QoS

## 📄 License

MIT
