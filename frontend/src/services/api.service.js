// ═══════════════════════════════════════════════════════════════════
// MicroRouter REST API Service Layer
// ═══════════════════════════════════════════════════════════════════

import { API_ENDPOINTS, TIMING } from '../config/constants.js'
import {
  getMockSystemInfo,
  getMockWifiNetworks,
  getMockDnsConfig,
  setMockDnsConfig,
  getMockDnsQueries,
  clearMockDnsQueries,
  getMockDevices,
  blockMockDevice,
  setMockDeviceWaiver,
  getMockGuestLimits,
  setMockGuestLimits,
  setMockGuestQuota,
  getMockGuestAnalytics,
  deleteMockGuestAnalytics,
  clearMockGuestUsage,
  getMockSession,
  mockRouterReboot,
  mockRouterWifiToggle,
  mockRouterSsidToggle,
  getMockLastLog,
} from './mock.service.js'
import { showToast } from './toast.service.js'

let isSimulationMode = false

export function setApiSimulationMode(enabled) {
  isSimulationMode = enabled
}

/**
 * Perform a fetch request with an automatic abort timeout
 * @param {string} url
 * @param {RequestInit} [options]
 * @returns {Promise<any>}
 */
async function fetchWithTimeout(url, options = {}) {
  const controller = new AbortController()
  const timeoutId = setTimeout(() => controller.abort(), TIMING.REQUEST_TIMEOUT_MS)

  try {
    const res = await fetch(url, { ...options, signal: controller.signal })
    clearTimeout(timeoutId)
    if (!res.ok) {
      throw new Error(`HTTP Error ${res.status}: ${res.statusText}`)
    }
    return await res.json()
  } catch (err) {
    clearTimeout(timeoutId)
    throw err
  }
}

// ───────────────────────────────────────────────────────────────────
// System & Wi-Fi Operations
// ───────────────────────────────────────────────────────────────────

/**
 * Fetch system diagnostic information
 */
export async function apiFetchSystemInfo() {
  if (isSimulationMode) {
    return getMockSystemInfo()
  }

  try {
    return await fetchWithTimeout(API_ENDPOINTS.SYSTEM_INFO)
  } catch (err) {
    console.warn('[API] System info request failed, returning mock data:', err.message)
    return getMockSystemInfo()
  }
}

/**
 * Trigger an asynchronous WiFi network scan
 */
export async function apiScanWiFiNetworks() {
  if (isSimulationMode) {
    await new Promise((r) => setTimeout(r, 1200))
    return { networks: getMockWifiNetworks() }
  }

  try {
    return await fetchWithTimeout(API_ENDPOINTS.WIFI_SCAN)
  } catch (err) {
    console.warn('[API] WiFi scan failed, returning mock networks:', err.message)
    return { networks: getMockWifiNetworks() }
  }
}

/**
 * Connect to a WiFi network
 * @param {string} ssid
 * @param {string} password
 */
export async function apiConnectWiFi(ssid, password) {
  if (isSimulationMode) {
    await new Promise((r) => setTimeout(r, 800))
    showToast(`Connected to "${ssid}" in Simulation Mode`, 'success')
    return { status: 'connecting', ssid }
  }

  return await fetchWithTimeout(API_ENDPOINTS.WIFI_CONNECT, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid, password }),
  })
}

/**
 * Clear stored WiFi credentials (reset to AP mode)
 */
export async function apiDisconnectWiFi() {
  if (isSimulationMode) {
    await new Promise((r) => setTimeout(r, 600))
    showToast('Reset to AP mode in Simulation Mode', 'warning')
    return { status: 'disconnected' }
  }

  return await fetchWithTimeout(API_ENDPOINTS.WIFI_DISCONNECT, { method: 'POST' })
}

/**
 * Restart the ESP32 micro-controller
 */
export async function apiRestartSystem() {
  if (isSimulationMode) {
    showToast('Restart signal simulated', 'info')
    return { status: 'ok' }
  }

  try {
    await fetch(API_ENDPOINTS.SYSTEM_RESTART, { method: 'POST' })
  } catch {
    // Restarting drops connection, so errors are expected
  }
}

// ───────────────────────────────────────────────────────────────────
// DNS Shield & Spyglass
// ───────────────────────────────────────────────────────────────────

export async function apiGetDnsConfig() {
  if (isSimulationMode) {
    return getMockDnsConfig()
  }
  try {
    return await fetchWithTimeout(API_ENDPOINTS.DNS_GET)
  } catch (err) {
    console.warn('[API] DNS get failed, fallback to mock:', err.message)
    return getMockDnsConfig()
  }
}

export async function apiSetDnsConfig(config) {
  if (isSimulationMode) {
    await new Promise((r) => setTimeout(r, 400))
    showToast('DNS Shield settings updated (Simulated)', 'success')
    return setMockDnsConfig(config)
  }
  return await fetchWithTimeout(API_ENDPOINTS.DNS_SET, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(config),
  })
}

export async function apiGetDnsQueries() {
  if (isSimulationMode) {
    return getMockDnsQueries()
  }
  try {
    return await fetchWithTimeout(API_ENDPOINTS.DNS_QUERIES)
  } catch (err) {
    return getMockDnsQueries()
  }
}

export async function apiClearDnsQueries() {
  if (isSimulationMode) {
    return clearMockDnsQueries()
  }
  return await fetchWithTimeout(API_ENDPOINTS.DNS_CLEAR_QUERIES, { method: 'POST' })
}

// ───────────────────────────────────────────────────────────────────
// Connected Devices & Access Control
// ───────────────────────────────────────────────────────────────────

export async function apiGetDevices() {
  if (isSimulationMode) {
    return getMockDevices()
  }
  try {
    return await fetchWithTimeout(API_ENDPOINTS.DEVICES)
  } catch (err) {
    return getMockDevices()
  }
}

export async function apiBlockDevice(mac, blocked) {
  if (isSimulationMode) {
    await new Promise((r) => setTimeout(r, 300))
    showToast(blocked ? `Device ${mac} blocked (Simulated)` : `Device ${mac} allowed (Simulated)`, 'info')
    return blockMockDevice(mac, blocked)
  }
  const endpoint = blocked ? API_ENDPOINTS.DEVICE_BLOCK : API_ENDPOINTS.DEVICE_ALLOW
  return await fetchWithTimeout(endpoint, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ mac }),
  })
}

export async function apiSetDeviceWaiver(mac, minutes) {
  if (isSimulationMode) {
    await new Promise((r) => setTimeout(r, 300))
    showToast(`Access waiver granted for ${minutes} min (Simulated)`, 'success')
    return setMockDeviceWaiver(mac, minutes)
  }
  return await fetchWithTimeout(API_ENDPOINTS.DEVICE_WAIVER, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ mac, minutes }),
  })
}

// ───────────────────────────────────────────────────────────────────
// Curfew & Parental Controls
// ───────────────────────────────────────────────────────────────────

export async function apiGetGuestLimits() {
  if (isSimulationMode) {
    return getMockGuestLimits()
  }
  try {
    return await fetchWithTimeout(API_ENDPOINTS.GUEST_LIMIT_GET)
  } catch (err) {
    return getMockGuestLimits()
  }
}

export async function apiSetGuestLimits(limits) {
  if (isSimulationMode) {
    await new Promise((r) => setTimeout(r, 400))
    showToast('Guest curfew schedule saved (Simulated)', 'success')
    return setMockGuestLimits(limits)
  }
  return await fetchWithTimeout(API_ENDPOINTS.GUEST_LIMIT_SET, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(limits),
  })
}

export async function apiSetGuestQuota(dailyQuotaMB, hourlyQuotaMB) {
  if (isSimulationMode) {
    await new Promise((r) => setTimeout(r, 400))
    showToast('Bandwidth quotas saved (Simulated)', 'success')
    return setMockGuestQuota(dailyQuotaMB, hourlyQuotaMB)
  }
  return await fetchWithTimeout(API_ENDPOINTS.GUEST_QUOTA_SET, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ dailyQuotaMB, hourlyQuotaMB }),
  })
}

// ───────────────────────────────────────────────────────────────────
// Guest Analytics & Router Gateway Parity Operations
// ───────────────────────────────────────────────────────────────────

export async function apiFetchGuestAnalytics() {
  if (isSimulationMode) {
    return getMockGuestAnalytics()
  }
  try {
    return await fetchWithTimeout(API_ENDPOINTS.GUEST_ANALYTICS)
  } catch (err) {
    console.warn('[API] Guest analytics failed, using fallback:', err.message)
    return getMockGuestAnalytics()
  }
}

export async function apiDeleteGuestAnalytics(mac) {
  if (isSimulationMode) {
    showToast(`Deleted history for ${mac}`, 'success')
    return deleteMockGuestAnalytics(mac)
  }
  return await fetchWithTimeout(API_ENDPOINTS.GUEST_ANALYTICS_DELETE, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ mac }),
  })
}

export async function apiClearGuestUsage() {
  if (isSimulationMode) {
    showToast('Reset today\'s usage counters (Simulated)', 'success')
    return clearMockGuestUsage()
  }
  return await fetchWithTimeout(API_ENDPOINTS.GUEST_QUOTA_CLEAR, {
    method: 'POST',
  })
}

export async function apiRebootRouter(isSystemOnly = false) {
  if (isSimulationMode) {
    showToast('Router reboot initiated (Simulated)', 'success')
    return mockRouterReboot()
  }
  const url = isSystemOnly ? `${API_ENDPOINTS.ROUTER_REBOOT}?system=1` : API_ENDPOINTS.ROUTER_REBOOT
  return await fetchWithTimeout(url, { method: 'POST' })
}

export async function apiToggleWifi(on) {
  if (isSimulationMode) {
    showToast(`WiFi Radio turned ${on ? 'ON' : 'OFF'} (Simulated)`, 'success')
    return mockRouterWifiToggle(on)
  }
  return await fetchWithTimeout(`${API_ENDPOINTS.ROUTER_WIFI_TOGGLE}?on=${on ? '1' : '0'}`, {
    method: 'POST',
  })
}

export async function apiToggleSsid(ssidIdx = 1, enable = true) {
  if (isSimulationMode) {
    showToast(`SSID #${ssidIdx} turned ${enable ? 'ON' : 'OFF'} (Simulated)`, 'success')
    return mockRouterSsidToggle(ssidIdx, enable)
  }
  return await fetchWithTimeout(`${API_ENDPOINTS.ROUTER_SSID_TOGGLE}?ssidIdx=${ssidIdx}&enable=${enable ? '1' : '0'}`, {
    method: 'POST',
  })
}

export async function apiFetchSession() {
  if (isSimulationMode) {
    return getMockSession()
  }
  try {
    return await fetchWithTimeout(API_ENDPOINTS.SESSION)
  } catch (err) {
    return getMockSession()
  }
}

export async function apiFetchLastLog() {
  if (isSimulationMode) {
    return getMockLastLog()
  }
  try {
    const res = await fetch(API_ENDPOINTS.LASTLOG)
    return await res.text()
  } catch (err) {
    return getMockLastLog()
  }
}
