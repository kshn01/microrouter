# 🌐 MicroRouter

**Transform any $5 ESP32-S3 board into a smart, enterprise-grade companion for your home Wi-Fi router.**

MicroRouter adds advanced network capabilities—such as **DNS ad & tracker shielding**, **automated bedtime curfews**, **75+ vendor device discovery**, and **dual-bank failsafe OTA firmware updates**—to standard home routers (TP-Link, Netgear, D-Link, ZTE, ISP fiber routers, etc.) without requiring costly enterprise hardware or custom router firmware flashing.

---

## 💡 How It Works (For Beginners)

```
                       ┌─────────────────────────┐
                       │   Main Internet Modem   │
                       │   (ISP / Home Router)   │
                       └───────────┬─────────────┘
                                   │
                    ┌──────────────┴──────────────┐
                    │                             │
                    ▼                             ▼
       ┌─────────────────────────┐   ┌─────────────────────────┐
       │   MicroRouter (ESP32)   │   │     Home Devices        │
       │  • Zero-Heap DNS Engine │   │  • Phones, Laptops      │
       │  • Content / Ad Shield  │◄──┼── • Smart TVs, Consoles │
       │  • Curfew Scheduler     │   │  (Send DNS queries to   │
       │  • 75+ OUI Inventory    │   │   MicroRouter for       │
       │  • Web Dashboard        │   │   inspection & filtering│
       └─────────────────────────┘   └─────────────────────────┘
```

You **do not** need to replace your existing home router or flash custom firmware like OpenWrt onto it. The ESP32 plugs into any standard USB charger and acts as an intelligent network companion:

1. **Plugs In Anywhere**: The ESP32-S3 connects wirelessly to your home Wi-Fi.
2. **Handles Network DNS (Port 53)**: By pointing your router or devices' DNS server to the ESP32's IP address, all web requests are instantly filtered, blocking ads, trackers, phishing, and unwanted apps.
3. **Inspects & Identifies**: Identifies 75+ hardware manufacturers (Apple, Samsung, Sony, Nintendo, Xiaomi, etc.) and Windows hostnames (NetBIOS).
4. **Enforces Wi-Fi Curfews**: Automatically cuts off internet access for children's devices at bedtime using accurate NTP network time.
5. **Modern Dashboard**: Manage everything from any phone, tablet, or laptop at `http://microrouter.local`.

> 📘 **New to ESP32?** Read our step-by-step [Novice Setup Guide](SETUP_GUIDE.md) for pictures, router settings walkthroughs, and beginner tips.

---

## ✨ Key Features

### 🛡️ DNS Shield & Network Spyglass
- **UDP Port 53 Zero-Heap Proxy**: Ultra-fast DNS resolver running on FreeRTOS Core 0 with dedicated wire buffers.
- **One-Click Upstream Profiles**: Switch instantly between **Cloudflare** (`1.1.1.1`), **Cloudflare Family** (malware & adult content blocking), **AdGuard** (network-wide ad blocking), **Google DNS**, or your own custom servers.
- **DoH Canary Sinkhole**: Blocks `use-application-dns.net` to prevent browsers (Chrome, Firefox, Safari) from bypassing your gateway rules.
- **Social & Content Shields**: Sinkholes Meta (Facebook, Instagram, WhatsApp) and TikTok algorithmic CDNs.
- **Network Spyglass**: Real-time 40-entry circular log inspecting live domains and client requests.

### 📱 Connected Client Inventory & 75+ OUI Database
- **Hardware Vendor Recognition**: Built-in database recognizing Apple, Samsung, Intel, Dell, HP, Lenovo, ASUS, Xiaomi, OnePlus, Sony PlayStation, Nintendo Switch, Amazon FireTV, Tuya IoT, and more.
- **Private MAC Detection**: Distinguishes randomized MAC addresses (used by iOS and Android privacy settings).
- **NetBIOS Name Query**: Discovers Windows PC and Samba hostnames over UDP 137.
- **Instant Device Access Control**: One-click Block / Allow policy with persistent flash memory (NVS).

### 🌙 Parental Controls & Curfew Scheduler
- **NTP-Synced Bedtime Curfews**: Automatically restricts station access during scheduled sleeping hours (e.g. 10:00 PM to 6:30 AM), handling overnight crossovers smoothly.
- **Bandwidth Quota Management**: Sets daily and hourly data caps (MB) for guest stations.
- **Temporary Emergency Waivers**: Grant 15m, 30m, 1h, or 2h access passes with a live countdown timer.

### 🚀 Enterprise ESP32 Architecture
- **Dual-Bank Safe OTA Updates**: Two flash partitions (`ota_0` and `ota_1`). If an update fails, it automatically rolls back to the working version.
- **Svelte 5 + Tailwind CSS v4 Portal**: Glassmorphism UI built with modern runes, responsive layouts, and offline simulation mode.
- **LittleFS Web Delivery**: Entire SPA compressed into LittleFS flash (~59 KB gzipped).

---

## ⚡ Quick Start Guide (5 Minutes)

### Step 1: Equipment Needed
1. Any **ESP32-S3** board with 8MB Flash (e.g., ESP32-S3-DevKitC-1 or similar).
2. A standard **USB-C cable** plugged into your router's USB port or a 5V phone charger.

### Step 2: Flash the Firmware & Web UI
Connect the ESP32 to your computer via USB, then run:

```bash
# 1. Flash the ESP32 C++ firmware
pio run -e serial -t upload

# 2. Flash the web portal to LittleFS
pio run -e serial -t uploadfs
```

### Step 3: Connect to Wi-Fi
1. On your phone or laptop, look for the Wi-Fi network named **`MicroRouter-Setup`**.
2. Connect using the setup password: `setup1234`
3. A setup screen will appear (or navigate to `http://192.168.4.1`).
4. Select your home Wi-Fi network, type your password, and click **Connect**.
5. The ESP32 will connect to your router and display its assigned IP address (e.g. `192.168.1.142`).

### Step 4: Open the Web Portal
From any device connected to your home Wi-Fi, open your browser and go to:
👉 **`http://microrouter.local`** (or `http://192.168.1.142`)

---

## 🔧 Configuring Your Router to Use MicroRouter DNS

To have MicroRouter protect **every device in your home automatically**:

1. Log into your home router's admin page (usually `192.168.1.1` or `192.168.0.1`).
2. Navigate to **DHCP Server** or **LAN Settings**.
3. Find the **Primary DNS** setting.
4. Replace the default DNS with your **ESP32 IP address** (e.g., `192.168.1.142`).
5. Save and restart your router.

*Now all phones, laptops, and smart TVs on your home Wi-Fi will automatically route their DNS through MicroRouter!*

> 💡 *Prefer to protect only specific devices (like your child's phone or smart TV)? You can configure DNS manually in that single device's Wi-Fi settings without changing your main router.*

---

## 💻 Offline Development & UI Simulation

You don't even need an ESP32 connected to test and develop the web interface! The frontend includes an offline telemetry and DNS simulator:

```bash
cd frontend
npm install
npm run dev
```

Open `http://localhost:5173` to see simulated live metrics, DNS queries, and devices.

---

## 📁 Repository Structure

```
├── src/                      # ESP32-S3 C++ Firmware (Arduino / FreeRTOS)
│   ├── main.cpp              # System initialization & FreeRTOS loop
│   ├── dns_engine.h/cpp      # UDP port 53 DNS proxy & Spyglass ring buffer
│   ├── device_manager.h/cpp  # 75+ OUI DB, NetBIOS resolver, client table
│   ├── scheduler.h/cpp       # NTP curfew schedules & quota tracker
│   ├── router_client.h       # Modular gateway abstraction
│   ├── wifi_manager.h/cpp    # AP/STA captive portal manager
│   ├── ota_manager.h/cpp     # Dual-bank OTA rollback engine
│   └── web_server.h/cpp      # REST endpoints & WebSocket broadcaster
├── frontend/                 # Svelte 5 + Tailwind CSS v4 Web Portal
│   ├── src/
│   │   ├── views/            # Dashboard, DNS, Devices, Parental, WiFi, Diagnostics
│   │   ├── utils/oui.js      # 75+ IEEE OUI hardware vendor database
│   │   ├── services/         # REST API, WebSocket, and offline simulator
│   │   └── stores/           # Svelte 5 reactive telemetry stores
├── data/                     # LittleFS partition bundle (HTML, CSS, JS)
├── platformio.ini            # PlatformIO build configuration
└── partitions.csv            # Dual-OTA (2x 3MB) + LittleFS (2MB) partition map
```

---

## 📡 REST API Summary

| Category | Endpoint | Method | Description |
|---|---|---|---|
| **DNS** | `/api/dns/get` | GET | Retrieve active upstream profile and shield statuses |
| **DNS** | `/api/dns/set` | POST | Set upstream profile (Cloudflare/AdGuard/Custom) & shields |
| **DNS** | `/api/dns/queries` | GET | Fetch live Network Spyglass circular query log |
| **Devices** | `/api/devices` | GET | List connected stations with vendor, NetBIOS, RSSI, bytes |
| **Devices** | `/api/device/block` | POST | Block station MAC address |
| **Devices** | `/api/device/allow` | POST | Allow station MAC address |
| **Devices** | `/api/device/waiver` | POST | Grant temporary access waiver (minutes) |
| **Curfew** | `/api/guest/limit/get` | GET | Fetch curfew schedule and bandwidth consumption |
| **Curfew** | `/api/guest/limit/set` | POST | Set bedtime curfew start/end hours and enabled status |
| **Curfew** | `/api/guest/quota/set` | POST | Set daily and hourly MB quotas |
| **WebSocket** | `/ws` | WS | Real-time bi-directional telemetry broadcast |

---

## 📄 License

MIT License. Feel free to use, modify, and extend!
