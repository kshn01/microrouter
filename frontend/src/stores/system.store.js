// ═══════════════════════════════════════════════════════════════════
// System & Hardware Reactive Store
// ═══════════════════════════════════════════════════════════════════

import { writable } from 'svelte/store'
import { apiFetchSystemInfo, apiRestartSystem } from '../services/api.service.js'
import { showToast } from '../services/toast.service.js'

export const systemInfo = writable(null)
export const isLoadingSystem = writable(false)

/**
 * Fetch hardware specs and diagnostic information
 */
export async function loadSystemInfo() {
  isLoadingSystem.set(true)
  try {
    const data = await apiFetchSystemInfo()
    systemInfo.set(data)
    return data
  } catch (err) {
    showToast(`System info retrieval failed: ${err.message}`, 'error')
    return null
  } finally {
    isLoadingSystem.set(false)
  }
}

/**
 * Restart the ESP32
 */
export async function rebootDevice() {
  try {
    await apiRestartSystem()
    showToast('Reboot signal transmitted', 'warning')
  } catch {
    showToast('Reboot initiated', 'info')
  }
}
