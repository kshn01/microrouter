// ═══════════════════════════════════════════════════════════════════
// MicroRouter Mock Simulation Service
// Enables full offline development on Mac/PC with realistic live telemetry
// ═══════════════════════════════════════════════════════════════════

import { TIMING } from '../config/constants.js'

let simulatedUptimeMs = 3600 * 1000 * 4 + 1000 * 60 * 22 // 4h 22m
let telemetryInterval = null

/**
 * Generate a realistic telemetry snapshot
 */
export function generateMockTelemetry() {
  simulatedUptimeMs += TIMING.MOCK_TELEMETRY_INTERVAL_MS

  // Jitter heap memory slightly to simulate real ESP32 RTOS activity
  const heapJitter = Math.floor(Math.sin(Date.now() / 3000) * 12000)
  const freeHeap = 195000 + heapJitter
  const totalHeap = 327680
  const heapPercent = Math.round((freeHeap / totalHeap) * 100)

  // Jitter RSSI slightly
  const rssiJitter = Math.floor(Math.cos(Date.now() / 5000) * 4)
  const wifiRssi = -58 + rssiJitter

  return {
    uptimeMs: simulatedUptimeMs,
    freeHeap,
    totalHeap,
    minFreeHeap: 182400,
    heapPercent,
    wifiConnected: true,
    wifiSsid: 'NetGear-Mesh-5G',
    wifiIp: '192.168.1.142',
    wifiRssi,
    wifiChannel: 6,
    wifiMac: '7C:DF:A1:04:88:EC',
    wsClients: 2,
    bootCount: 14,
    fwVersion: '1.0.0',
    partition: 'ota_0',
    fwValidated: true,
  }
}

/**
 * Mock system info hardware response
 */
export function getMockSystemInfo() {
  return {
    chipModel: 'ESP32-S3 (Dual-Core)',
    chipRevision: '0.1',
    cpuCores: 2,
    cpuFreqMHz: 240,
    flashSizeMB: 8,
    fsUsedBytes: 94208,
    fsTotalBytes: 2031616,
    freeHeap: 198240,
    totalHeap: 327680,
    minFreeHeap: 182400,
    activePartition: 'ota_0',
    firmwareVersion: '1.0.0',
    macAddress: '7C:DF:A1:04:88:EC',
  }
}

/**
 * Mock WiFi scan list
 */
export function getMockWifiNetworks() {
  return [
    { ssid: 'NetGear-Mesh-5G', rssi: -54, secure: true, channel: 6 },
    { ssid: 'Starlink-Router', rssi: -62, secure: true, channel: 1 },
    { ssid: 'Office-HighSpeed', rssi: -71, secure: true, channel: 11 },
    { ssid: 'SmartHome-IoT', rssi: -78, secure: true, channel: 6 },
    { ssid: 'Guest-Open-WiFi', rssi: -84, secure: false, channel: 3 },
  ]
}

/**
 * Start simulated telemetry streaming to a listener callback
 * @param {Function} onTelemetry
 * @returns {Function} cleanup function
 */
export function startMockTelemetryStream(onTelemetry) {
  if (telemetryInterval) clearInterval(telemetryInterval)

  // Send initial frame immediately
  onTelemetry(generateMockTelemetry())

  telemetryInterval = setInterval(() => {
    onTelemetry(generateMockTelemetry())
  }, TIMING.MOCK_TELEMETRY_INTERVAL_MS)

  return () => {
    if (telemetryInterval) {
      clearInterval(telemetryInterval)
      telemetryInterval = null
    }
  }
}
