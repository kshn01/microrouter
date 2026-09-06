// ═══════════════════════════════════════════════════════════════════
// WiFi Management Reactive Store
// ═══════════════════════════════════════════════════════════════════

import { writable } from 'svelte/store'
import { apiScanWiFiNetworks, apiConnectWiFi, apiDisconnectWiFi } from '../services/api.service.js'
import { showToast } from '../services/toast.service.js'

export const scannedNetworks = writable([])
export const isScanning = writable(false)
export const isConnecting = writable(false)

/**
 * Scan for surrounding WiFi networks
 */
export async function scanNetworks() {
  isScanning.set(true)
  try {
    const data = await apiScanWiFiNetworks()
    const strongestBySsid = new Map()
    for (const network of data.networks || []) {
      if (!network.ssid || !network.channel || network.channel > 14) continue
      const current = strongestBySsid.get(network.ssid)
      if (!current || network.rssi > current.rssi) strongestBySsid.set(network.ssid, network)
    }
    const sorted = [...strongestBySsid.values()].sort((a, b) => b.rssi - a.rssi)
    scannedNetworks.set(sorted)
    showToast(`Discovered ${sorted.length} WiFi networks`, 'success')
    return sorted
  } catch (err) {
    showToast(`WiFi scan failed: ${err.message}`, 'error')
    return []
  } finally {
    isScanning.set(false)
  }
}

/**
 * Connect to an Access Point
 * @param {string} ssid
 * @param {string} password
 */
export async function connectNetwork(ssid, password) {
  isConnecting.set(true)
  try {
    const res = await apiConnectWiFi(ssid, password)
    showToast(`Connecting to ${ssid}...`, 'info')
    return res
  } catch (err) {
    showToast(`Connection failed: ${err.message}`, 'error')
    throw err
  } finally {
    isConnecting.set(false)
  }
}

/**
 * Reset WiFi credentials and revert to AP mode
 */
export async function forgetNetwork() {
  try {
    await apiDisconnectWiFi()
    showToast('Credentials erased. ESP32 restarting in AP mode.', 'warning')
  } catch (err) {
    showToast(`Reset failed: ${err.message}`, 'error')
  }
}
