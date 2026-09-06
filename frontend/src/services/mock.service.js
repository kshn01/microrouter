// ═══════════════════════════════════════════════════════════════════
// MicroRouter Mock Simulation Service
// Enables full offline development on Mac/PC with realistic live telemetry,
// DNS queries, connected devices with OUI vendors, and parental controls.
// ═══════════════════════════════════════════════════════════════════

import { TIMING } from '../config/constants.js'
import { lookupOUI } from '../utils/oui.js'

let simulatedUptimeMs = 3600 * 1000 * 4 + 1000 * 60 * 22 // 4h 22m
let telemetryInterval = null

// DNS Simulation State
let mockDnsConfig = {
  profile: 'cloudflare',
  primary: '1.1.1.1',
  secondary: '1.0.0.1',
  dohCanary: true,
  blockMeta: true,
  blockTiktok: true,
  totalQueries: 1428,
  blockedQueries: 114
}

const SAMPLE_DOMAINS = [
  { domain: 'google.com', client: '192.168.1.102', blocked: false, reason: 'PASS' },
  { domain: 'use-application-dns.net', client: '192.168.1.105', blocked: true, reason: 'DOH_CANARY' },
  { domain: 'api.facebook.com', client: '192.168.1.103', blocked: true, reason: 'META_BLOCK' },
  { domain: 'github.com', client: '192.168.1.101', blocked: false, reason: 'PASS' },
  { domain: 'byteoversea.com', client: '192.168.1.104', blocked: true, reason: 'TIKTOK_BLOCK' },
  { domain: 'portal.home', client: '192.168.1.102', blocked: false, reason: 'LOCAL_PORTAL' },
  { domain: 'netflix.com', client: '192.168.1.106', blocked: false, reason: 'PASS' },
  { domain: 'instagram.com', client: '192.168.1.105', blocked: true, reason: 'META_BLOCK' },
  { domain: 'apple.com', client: '192.168.1.102', blocked: false, reason: 'PASS' },
  { domain: 'graph.instagram.com', client: '192.168.1.103', blocked: true, reason: 'META_BLOCK' },
  { domain: 'cloudflare.com', client: '192.168.1.101', blocked: false, reason: 'PASS' },
  { domain: 'whatsapp.net', client: '192.168.1.105', blocked: true, reason: 'META_BLOCK' },
]

let mockQueryLog = []
// Pre-populate query log
for (let i = 0; i < 24; i++) {
  const sample = SAMPLE_DOMAINS[i % SAMPLE_DOMAINS.length]
  mockQueryLog.unshift({
    domain: sample.domain,
    clientIp: sample.client,
    blocked: sample.blocked,
    reason: sample.reason,
    timestamp: Date.now() - (24 - i) * 8500
  })
}

// Devices Simulation State
let mockDevices = [
  {
    mac: '3c:15:c2:44:9a:12',
    ip: '192.168.1.101',
    hostname: 'MacBook-Pro',
    netbios: 'MBP-WORK',
    rssi: -48,
    isBlocked: false,
    waiverSecRemaining: 0,
    rxBytes: 41258900,
    txBytes: 12480300,
    lastSeen: '10s ago'
  },
  {
    mac: 'bc:d1:d3:88:21:40',
    ip: '192.168.1.102',
    hostname: 'Galaxy-S23',
    netbios: 'SAM-S23',
    rssi: -62,
    isBlocked: false,
    waiverSecRemaining: 0,
    rxBytes: 8521000,
    txBytes: 1940000,
    lastSeen: '4s ago'
  },
  {
    mac: '98:b6:e9:11:ff:02',
    ip: '192.168.1.103',
    hostname: 'Nintendo-Switch',
    netbios: '',
    rssi: -71,
    isBlocked: false,
    waiverSecRemaining: 1420, // 23 min remaining on waiver
    rxBytes: 154000000,
    txBytes: 8900000,
    lastSeen: '1s ago'
  },
  {
    mac: '00:fc:8b:2e:55:18',
    ip: '192.168.1.104',
    hostname: 'FireTV-Stick-4K',
    netbios: '',
    rssi: -55,
    isBlocked: false,
    waiverSecRemaining: 0,
    rxBytes: 310500000,
    txBytes: 4200000,
    lastSeen: '12s ago'
  },
  {
    mac: '18:69:d8:aa:bb:cc',
    ip: '192.168.1.105',
    hostname: 'Tuya-Smart-Plug',
    netbios: '',
    rssi: -79,
    isBlocked: false,
    waiverSecRemaining: 0,
    rxBytes: 420000,
    txBytes: 280000,
    lastSeen: '25s ago'
  },
  {
    mac: '70:f3:53:1a:2b:3c',
    ip: '192.168.1.106',
    hostname: 'Desktop-Gaming-PC',
    netbios: 'DESKTOP-RYZEN',
    rssi: -52,
    isBlocked: true, // Blocked by admin
    waiverSecRemaining: 0,
    rxBytes: 21900000,
    txBytes: 5200000,
    lastSeen: '2m ago'
  }
]

// Guest Curfew & Bandwidth Limits Simulation State
let mockGuestLimits = {
  curfewEnabled: true,
  startHour: 22,
  startMin: 0,
  endHour: 6,
  endMin: 30,
  dailyQuotaMB: 2048,
  hourlyQuotaMB: 500,
  curfewActive: false,
  currentTime: '21:20',
  guestTxBytes: 84120000,
  guestRxBytes: 495000000,
  activeWaiversCount: 1
}

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

  // Randomly add DNS query every few ticks to show live activity
  if (Math.random() < 0.35) {
    const sample = SAMPLE_DOMAINS[Math.floor(Math.random() * SAMPLE_DOMAINS.length)]
    mockQueryLog.unshift({
      domain: sample.domain,
      clientIp: sample.client,
      blocked: sample.blocked,
      reason: sample.reason,
      timestamp: Date.now()
    })
    if (mockQueryLog.length > 40) mockQueryLog.pop()
    mockDnsConfig.totalQueries++
    if (sample.blocked) mockDnsConfig.blockedQueries++
  }

  // Decrement waiver timers
  mockDevices.forEach(d => {
    if (d.waiverSecRemaining > 0) {
      d.waiverSecRemaining = Math.max(0, d.waiverSecRemaining - 1)
    }
  })

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
    dnsTotal: mockDnsConfig.totalQueries,
    dnsBlocked: mockDnsConfig.blockedQueries,
    dnsProfile: mockDnsConfig.profile,
    curfewActive: mockGuestLimits.curfewActive,
    connectedDevices: mockDevices.length
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

// ═══════════════════════════════════════════════════════════════════
// Mock DNS Shield Handlers
// ═══════════════════════════════════════════════════════════════════

export function getMockDnsConfig() {
  return { ...mockDnsConfig }
}

export function setMockDnsConfig(newConfig) {
  mockDnsConfig = { ...mockDnsConfig, ...newConfig }
  return { status: 'ok', ...mockDnsConfig }
}

export function getMockDnsQueries() {
  return {
    total: mockDnsConfig.totalQueries,
    blocked: mockDnsConfig.blockedQueries,
    queries: [...mockQueryLog]
  }
}

export function clearMockDnsQueries() {
  mockQueryLog = []
  return { status: 'ok' }
}

// ═══════════════════════════════════════════════════════════════════
// Mock Device Manager Handlers
// ═══════════════════════════════════════════════════════════════════

export function getMockDevices() {
  return {
    count: mockDevices.length,
    devices: mockDevices.map(d => {
      const vendorInfo = lookupOUI(d.mac)
      return {
        ...d,
        vendor: vendorInfo.vendor,
        deviceType: vendorInfo.type,
        icon: vendorInfo.icon,
        isRandomized: vendorInfo.isRandomized
      }
    })
  }
}

export function blockMockDevice(mac, blocked) {
  const dev = mockDevices.find(d => d.mac.toLowerCase() === mac.toLowerCase())
  if (dev) {
    dev.isBlocked = blocked
  }
  return { status: 'ok', mac, isBlocked: blocked }
}

export function setMockDeviceWaiver(mac, minutes) {
  const dev = mockDevices.find(d => d.mac.toLowerCase() === mac.toLowerCase())
  if (dev) {
    dev.waiverSecRemaining = minutes * 60
    dev.isBlocked = false
  }
  return { status: 'ok', mac, waiverSecRemaining: minutes * 60 }
}

// ═══════════════════════════════════════════════════════════════════
// Mock Parental / Curfew Limits Handlers
// ═══════════════════════════════════════════════════════════════════

export function getMockGuestLimits() {
  return { ...mockGuestLimits }
}

export function setMockGuestLimits(limits) {
  mockGuestLimits = { ...mockGuestLimits, ...limits }
  return { status: 'ok', ...mockGuestLimits }
}

export function setMockGuestQuota(dailyMB, hourlyMB) {
  mockGuestLimits.dailyQuotaMB = dailyMB
  mockGuestLimits.hourlyQuotaMB = hourlyMB
  return { status: 'ok', dailyMB, hourlyMB }
}

// ═══════════════════════════════════════════════════════════════════
// Mock 7-Day Guest Analytics & Router Parity Handlers
// ═══════════════════════════════════════════════════════════════════

let mockGuestAnalyticsRecords = [
  {
    mac: '3c:15:c2:44:9a:12',
    hostname: 'MacBook-Pro',
    ip: '192.168.1.101',
    currentlyOnline: true,
    todayUsageBytes: 524288000, // ~500 MB
    todayActiveSecs: 14400,
    validDaysCount: 7,
    history: [
      { dayIndex: 1, epochDay: 20600, bytesUsed: 1048576000, activeSecs: 21600, quotaBlockCount: 0 },
      { dayIndex: 2, epochDay: 20599, bytesUsed: 838860800, activeSecs: 18000, quotaBlockCount: 0 },
      { dayIndex: 3, epochDay: 20598, bytesUsed: 1258291200, activeSecs: 25200, quotaBlockCount: 1 },
      { dayIndex: 4, epochDay: 20597, bytesUsed: 629145600, activeSecs: 14400, quotaBlockCount: 0 },
      { dayIndex: 5, epochDay: 20596, bytesUsed: 943718400, activeSecs: 19800, quotaBlockCount: 0 },
      { dayIndex: 6, epochDay: 20595, bytesUsed: 419430400, activeSecs: 10800, quotaBlockCount: 0 },
      { dayIndex: 7, epochDay: 20594, bytesUsed: 734003200, activeSecs: 16200, quotaBlockCount: 0 }
    ]
  },
  {
    mac: 'bc:d1:d3:88:21:40',
    hostname: 'Galaxy-S23',
    ip: '192.168.1.102',
    currentlyOnline: true,
    todayUsageBytes: 262144000, // ~250 MB
    todayActiveSecs: 10800,
    validDaysCount: 7,
    history: [
      { dayIndex: 1, epochDay: 20600, bytesUsed: 419430400, activeSecs: 12000, quotaBlockCount: 0 },
      { dayIndex: 2, epochDay: 20599, bytesUsed: 524288000, activeSecs: 14000, quotaBlockCount: 0 },
      { dayIndex: 3, epochDay: 20598, bytesUsed: 314572800, activeSecs: 9000, quotaBlockCount: 0 },
      { dayIndex: 4, epochDay: 20597, bytesUsed: 471859200, activeSecs: 13500, quotaBlockCount: 0 },
      { dayIndex: 5, epochDay: 20596, bytesUsed: 209715200, activeSecs: 7200, quotaBlockCount: 0 },
      { dayIndex: 6, epochDay: 20595, bytesUsed: 367001600, activeSecs: 11000, quotaBlockCount: 0 },
      { dayIndex: 7, epochDay: 20594, bytesUsed: 262144000, activeSecs: 8500, quotaBlockCount: 0 }
    ]
  },
  {
    mac: '98:b6:e9:11:ff:02',
    hostname: 'Nintendo-Switch',
    ip: '192.168.1.103',
    currentlyOnline: false,
    todayUsageBytes: 1572864000, // ~1.5 GB
    todayActiveSecs: 18000,
    validDaysCount: 7,
    history: [
      { dayIndex: 1, epochDay: 20600, bytesUsed: 2097152000, activeSecs: 21600, quotaBlockCount: 2 },
      { dayIndex: 2, epochDay: 20599, bytesUsed: 1887436800, activeSecs: 19800, quotaBlockCount: 1 },
      { dayIndex: 3, epochDay: 20598, bytesUsed: 2411724800, activeSecs: 25000, quotaBlockCount: 2 },
      { dayIndex: 4, epochDay: 20597, bytesUsed: 1258291200, activeSecs: 15000, quotaBlockCount: 0 },
      { dayIndex: 5, epochDay: 20596, bytesUsed: 1677721600, activeSecs: 18000, quotaBlockCount: 1 },
      { dayIndex: 6, epochDay: 20595, bytesUsed: 838860800, activeSecs: 10000, quotaBlockCount: 0 },
      { dayIndex: 7, epochDay: 20594, bytesUsed: 1468006400, activeSecs: 16000, quotaBlockCount: 1 }
    ]
  },
  {
    mac: '00:fc:8b:2e:55:18',
    hostname: 'FireTV-Stick-4K',
    ip: '192.168.1.104',
    currentlyOnline: true,
    todayUsageBytes: 3145728000, // ~3.0 GB
    todayActiveSecs: 21600,
    validDaysCount: 7,
    history: [
      { dayIndex: 1, epochDay: 20600, bytesUsed: 4194304000, activeSecs: 28800, quotaBlockCount: 0 },
      { dayIndex: 2, epochDay: 20599, bytesUsed: 3670016000, activeSecs: 25200, quotaBlockCount: 0 },
      { dayIndex: 3, epochDay: 20598, bytesUsed: 4718592000, activeSecs: 32400, quotaBlockCount: 0 },
      { dayIndex: 4, epochDay: 20597, bytesUsed: 3145728000, activeSecs: 21600, quotaBlockCount: 0 },
      { dayIndex: 5, epochDay: 20596, bytesUsed: 5242880000, activeSecs: 36000, quotaBlockCount: 0 },
      { dayIndex: 6, epochDay: 20595, bytesUsed: 2621440000, activeSecs: 18000, quotaBlockCount: 0 },
      { dayIndex: 7, epochDay: 20594, bytesUsed: 3984588800, activeSecs: 27000, quotaBlockCount: 0 }
    ]
  }
]

export function getMockGuestAnalytics() {
  return { records: [...mockGuestAnalyticsRecords] }
}

export function deleteMockGuestAnalytics(mac) {
  mockGuestAnalyticsRecords = mockGuestAnalyticsRecords.filter(
    (r) => r.mac.toLowerCase() !== mac.toLowerCase()
  )
  return { ok: true }
}

export function clearMockGuestUsage() {
  mockGuestAnalyticsRecords.forEach((r) => {
    r.todayUsageBytes = 0
    r.todayActiveSecs = 0
  })
  return { ok: true }
}

export function getMockSession() {
  return {
    sid: 'SIM_SID_998822',
    token: 'SIM_TOKEN_ABCD',
    wifi: '1',
    guest: true,
    guestSsid: 'MicroRouter-Guest',
    ip: '192.168.1.150',
    loggedIn: true,
    gatewayType: 'ZTE GPON F670L (Simulated)'
  }
}

export function mockRouterReboot() {
  return { ok: true }
}

export function mockRouterWifiToggle(on) {
  return { ok: true, state: on ? '1' : '0' }
}

export function mockRouterSsidToggle(idx, enable) {
  return { ok: true }
}

export function getMockLastLog() {
  return '[ZTE] devmgr_statusmgr_lua.lua OK 0\n[System] Web Server active\n[WiFi] Radio state: 1'
}

/**
 * Start simulated telemetry streaming to a listener callback
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
