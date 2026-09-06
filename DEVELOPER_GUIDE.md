# 🛠️ MicroRouter Developer Handbook

A comprehensive guide for developers on the architecture, how to add new features across the firmware and frontend stacks, how to build and deploy, and best practices.

---

## 🏗️ 1. High-Level Architecture

MicroRouter is split into two cleanly separated layers:

```
┌────────────────────────────────────────────────────────────────────────┐
│                   ESP32-S3 Firmware (C++ / FreeRTOS)                   │
├───────────────────┬───────────────────┬────────────────────────────────┤
│ Core Subsystems   │ Networking        │ Web Server & API Layer         │
│ • DNS Proxy (P53) │ • WiFiManager     │ • ESPAsyncWebServer (Port 80) │
│ • DeviceManager   │ • NetBIOS (P137)  │ • AsyncWebSocket (/ws)         │
│ • Scheduler (NTP) │ • ArduinoOTA      │ • LittleFS Static File Server  │
│ • RouterClient    │ • NVS Flash       │ • REST Endpoints (/api/*)      │
└───────────────────┴───────────────────┴────────────────────────────────┘
                                  ▲
                                  │ HTTP / REST / WebSocket (/ws)
                                  ▼
┌────────────────────────────────────────────────────────────────────────┐
│             Single-Page Application Frontend (Svelte 5 + Vite)         │
├────────────────────────────────────────────────────────────────────────┤
│ • Svelte 5 Runes ($state, $derived, $props)                            │
│ • Tailwind CSS v4 Glassmorphism Theme                                  │
│ • Dual-Mode Service Layer (Live Hardware vs Mock Simulation)           │
│ • Compiled Bundle: ~214 KB raw, ~59 KB gzipped                         │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 🗺️ 2. Where Things Live (Directory Map)

| Directory / File | Role |
|---|---|
| **`src/main.cpp`** | Hardware initialization, subsystem setup, and main FreeRTOS loop. |
| **`src/config.h`** | Global constants (pins, partition sizes, default Wi-Fi passwords, timeouts). |
| **`src/web_server.h/cpp`** | Registers all `/api/*` REST routes, serves LittleFS, broadcasts `/ws` WebSocket stats. |
| **`src/dns_engine.h/cpp`** | FreeRTOS Core 0 UDP port 53 DNS proxy, upstream switching, and query ring buffer. |
| **`src/device_manager.h/cpp`** | Connected station table, NetBIOS name query (UDP 137), OUI lookups, and block list. |
| **`src/scheduler.h/cpp`** | NTP real-time clock synchronization, bedtime curfews, and bandwidth quotas. |
| **`src/wifi_manager.h/cpp`** | AP/STA Wi-Fi state machine and captive portal. |
| **`src/ota_manager.h/cpp`** | Dual-bank OTA partition management with auto-rollback. |
| **`data/`** | Target folder for LittleFS filesystem image (HTML, CSS, JS bundles). |
| **`frontend/src/views/`** | Svelte 5 page components (`DashboardView`, `DnsView`, `DevicesView`, etc.). |
| **`frontend/src/components/`** | Reusable UI widgets (`Card`, `Modal`, `StatCard`, `Gauge`, `ProgressBar`, `SignalBars`). |
| **`frontend/src/services/`** | API client (`api.service.js`), WebSocket (`websocket.service.js`), Mock (`mock.service.js`). |
| **`frontend/src/config/constants.js`** | API endpoint URLs and app settings. |

---

## 🚀 3. Step-by-Step: How to Add a New Feature

Let's walk through an example: **Adding a new feature (e.g. "Bandwidth Speed Test" or "Custom Blocklist")**.

### Step A: Create the Firmware Subsystem (C++)
1. Create header `src/your_feature.h`:
   ```cpp
   #pragma once
   #include <Arduino.h>
   #include <ArduinoJson.h>

   class YourFeature {
   public:
       static YourFeature& instance();
       void begin();
       void loop();
       bool isEnabled() const;
       void setEnabled(bool enabled);
   private:
       YourFeature();
       bool _enabled{false};
   };
   ```
2. Implement logic in `src/your_feature.cpp`.
3. In `src/main.cpp`:
   - `#include "your_feature.h"`
   - Call `YourFeature::instance().begin();` in `setup()`.
   - Call `YourFeature::instance().loop();` in `loop()` (if non-blocking periodic work is needed).

---

### Step B: Expose REST & WebSocket Endpoints (C++)
In `src/web_server.cpp`:
1. Register your GET / POST endpoints in `registerApiRoutes()`:
   ```cpp
   // GET /api/yourfeature/status
   _server.on("/api/yourfeature/status", HTTP_GET, [](AsyncWebServerRequest* request) {
       JsonDocument doc;
       doc["enabled"] = YourFeature::instance().isEnabled();
       String response;
       serializeJson(doc, response);
       request->send(200, "application/json", response);
   });

   // POST /api/yourfeature/set
   _server.on("/api/yourfeature/set", HTTP_POST, [](AsyncWebServerRequest* request) {
       // Handled in onBody handler for JSON payloads
   });
   ```
2. If real-time telemetry is needed, add the metric to the WebSocket broadcast in `broadcastStats()`:
   ```cpp
   doc["yourMetric"] = YourFeature::instance().getMetric();
   ```

---

### Step C: Update Frontend API & Mock Layers (JavaScript)
To ensure offline development on your Mac works seamlessly:
1. In `frontend/src/config/constants.js`, add your endpoint:
   ```javascript
   export const API_ENDPOINTS = {
     // ...
     YOUR_FEATURE: '/api/yourfeature/status',
     YOUR_FEATURE_SET: '/api/yourfeature/set',
   }
   ```
2. In `frontend/src/services/mock.service.js`, add mock data and handlers:
   ```javascript
   let mockFeatureState = { enabled: true }
   export function getMockFeature() { return { ...mockFeatureState } }
   export function setMockFeature(val) { mockFeatureState.enabled = val; return { status: 'ok' } }
   ```
3. In `frontend/src/services/api.service.js`, export the API call with the simulation fallback:
   ```javascript
   export async function apiGetFeature() {
     if (isSimulationMode) return getMockFeature()
     return await fetchWithTimeout(API_ENDPOINTS.YOUR_FEATURE)
   }
   ```

---

### Step D: Build the Svelte 5 View (UI)
1. Create `frontend/src/views/YourFeatureView.svelte`:
   ```svelte
   <script>
     import { onMount } from 'svelte'
     import Card from '../components/ui/Card.svelte'
     import { apiGetFeature } from '../services/api.service.js'

     let state = $state({ enabled: false })

     onMount(async () => {
       state = await apiGetFeature()
     })
   </script>

   <div class="flex flex-col gap-6">
     <h1 class="text-2xl font-bold text-white">Your New Feature</h1>
     <Card>
       <span class="text-slate-300">Status: {state.enabled ? 'Active' : 'Disabled'}</span>
     </Card>
   </div>
   ```

---

### Step E: Hook up Navigation & Routing
1. In `frontend/src/App.svelte`:
   - Import `YourFeatureView from './views/YourFeatureView.svelte'`
   - Add to `routes`: `'/yourfeature': YourFeatureView`
2. In `frontend/src/components/layout/Sidebar.svelte`:
   - Add to `navItems`: `{ href: '#/yourfeature', label: 'Feature Name', icon: YourIcon }`

---

## 🔨 4. How to Build, Test, and Deploy

### Local Development (Frontend Only, with Simulator)
Runs with hot-module reload on your Mac without an ESP32:
```bash
cd frontend
npm run dev
# Open http://localhost:5173
```

---

### Production Build & ESP32 Deployment

Follow these 3 simple commands:

#### 1. Compile the Frontend
Compiles Svelte into minimal HTML/CSS/JS and deposits them directly into the ESP32's `data/` folder:
```bash
cd frontend
npm run build
```

#### 2. Compile & Upload the LittleFS Filesystem (Web Files)
Packages the `data/` folder into a LittleFS binary image and flashes it:
```bash
# From the repository root:
pio run -e serial -t uploadfs
```

#### 3. Compile & Upload Firmware (C++ Code)
Compiles C++ code and flashes the ESP32-S3 over USB:
```bash
pio run -e serial -t upload
```

---

### Automated Tests

From `frontend/`, run the complete suite:
```bash
npm run test:all
```

This runs:
- Vitest unit tests for simulator state, telemetry, and OUI recognition.
- Vitest integration tests for frontend-to-firmware API contracts and route coverage.
- Playwright Chromium tests for navigation and WiFi scan rendering.
- PlatformIO native C++ unit tests for backend scheduler policy boundaries.
- PlatformIO builds for both `serial` and `ota` firmware environments.

Run individual layers when iterating:
```bash
npm run test:unit
npm run test:integration
npm run test:e2e:install
npm run test:e2e
npm run test:backend
npm run test:firmware
```

The E2E suite starts its own Vite server and uses mocked API responses, so it does
not require an ESP32. Hardware-in-the-loop validation still requires a flashed
board and a reachable router.

---

### Wireless Updates (Over-The-Air / OTA)

Once the device is installed near your router, you never need a USB cable again:

- **Method 1: ElegantOTA Web Browser Upload**
  1. Open `http://microrouter.local/update`
  2. Username: `admin`, Password: `microrouter`
  3. Choose **Firmware** (`.pio/build/serial/firmware.bin`) or **Filesystem** (`.pio/build/serial/littlefs.bin`) and click Upload.
- **Method 2: Command Line ArduinoOTA**
  ```bash
  pio run -e ota -t upload
  ```

---

## 🛡️ 5. Golden Rules for Embedded Development on ESP32-S3

1. **Zero Heap Allocation in Fast Paths**:
   - In time-critical network code (like DNS UDP port 53), avoid `String`, `malloc`, or `new`. Use static BSS buffers (`static uint8_t buf[512]`).
2. **Never Block the Web Server Thread**:
   - `ESPAsyncWebServer` runs asynchronously. Never call `delay()`, `while(!ready)`, or synchronous HTTP network calls inside route handlers.
3. **Dual-Bank OTA Safety**:
   - Always call `OtaManager::instance().confirmRunningPartition()` after boot so the bootloader validates the new firmware. If the app crashes, the ESP32 automatically reboots into the previous partition.
4. **Thread Safety with Mutexes**:
   - When sharing state between FreeRTOS tasks (e.g. DNS task on Core 0 and Web server on Core 1), always guard data structures using `xSemaphoreCreateMutex()`. Never hold a mutex while doing network I/O (`sendto`/`recvfrom`).
5. **Keep LittleFS Bundle Tiny**:
   - Keep total web assets under 500 KB (currently ~214 KB uncompressed, ~59 KB gzipped) to leave room in the 2MB LittleFS partition.
