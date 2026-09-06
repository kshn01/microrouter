// ═══════════════════════════════════════════════════════════════════
// MicroRouter Application Constants & Configuration
// ═══════════════════════════════════════════════════════════════════

export const APP_CONFIG = {
  NAME: 'MicroRouter',
  VERSION: '1.0.0',
  PLATFORM: 'ESP32-S3',
  DEFAULT_HOSTNAME: 'microrouter.local',
}

export const API_ENDPOINTS = {
  SYSTEM_INFO: '/api/system/info',
  SYSTEM_RESTART: '/api/system/restart',
  WIFI_SCAN: '/api/wifi/scan',
  WIFI_CONNECT: '/api/wifi/connect',
  WIFI_DISCONNECT: '/api/wifi/disconnect',
  OTA_PORTAL: '/update',
  WEBSOCKET: '/ws',

  // DNS Shield & Spyglass
  DNS_GET: '/api/dns/get',
  DNS_SET: '/api/dns/set',
  DNS_QUERIES: '/api/dns/queries',
  DNS_CLEAR_QUERIES: '/api/dns/queries/clear',

  // Connected Devices & Access Control
  DEVICES: '/api/devices',
  DEVICE_BLOCK: '/api/device/block',
  DEVICE_ALLOW: '/api/device/allow',
  DEVICE_WAIVER: '/api/device/waiver',

  // Curfew & Parental Controls
  GUEST_LIMIT_GET: '/api/guest/limit/get',
  GUEST_LIMIT_SET: '/api/guest/limit/set',
  GUEST_QUOTA_SET: '/api/guest/quota/set',
}

export const TIMING = {
  WS_RECONNECT_INTERVAL_MS: 3000,
  MOCK_TELEMETRY_INTERVAL_MS: 1000,
  TOAST_DEFAULT_DURATION_MS: 4000,
  REQUEST_TIMEOUT_MS: 8000,
}

export const STORAGE_KEYS = {
  SIMULATION_ENABLED: 'microrouter_simulation_mode',
}
