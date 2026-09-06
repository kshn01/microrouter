// ═══════════════════════════════════════════════════════════════════
// Live Telemetry Reactive Store
// ═══════════════════════════════════════════════════════════════════

import { writable, derived } from 'svelte/store'
import { rssiToPercent } from '../types/models.js'

export const telemetry = writable({
  uptimeMs: 0,
  freeHeap: 0,
  totalHeap: 327680,
  minFreeHeap: 0,
  heapPercent: 0,
  wifiConnected: false,
  wifiSsid: '—',
  wifiIp: '—',
  wifiRssi: -90,
  wifiChannel: 0,
  wifiMac: '—',
  wsClients: 0,
  bootCount: 0,
  fwVersion: '1.0.0',
  partition: 'ota_0',
  fwValidated: true,
  dnsTotal: 0,
  dnsBlocked: 0,
  dnsProfile: 'cloudflare',
  curfewActive: false,
  connectedDevices: 0,
})

export const uptime = derived(telemetry, ($t) => $t.uptimeMs || 0)
export const freeHeap = derived(telemetry, ($t) => $t.freeHeap || 0)
export const totalHeap = derived(telemetry, ($t) => $t.totalHeap || 327680)
export const heapPercent = derived(telemetry, ($t) => $t.heapPercent || 0)
export const wifiRssi = derived(telemetry, ($t) => $t.wifiRssi || -90)
export const wifiSignalPercent = derived(wifiRssi, ($r) => rssiToPercent($r))
export const wifiSsid = derived(telemetry, ($t) => $t.wifiSsid || '—')
export const wifiIp = derived(telemetry, ($t) => $t.wifiIp || '—')
export const wifiConnected = derived(telemetry, ($t) => $t.wifiConnected || false)
export const wsClients = derived(telemetry, ($t) => $t.wsClients || 0)
export const activePartition = derived(telemetry, ($t) => $t.partition || 'ota_0')
export const fwVersion = derived(telemetry, ($t) => $t.fwVersion || '1.0.0')
export const fwValidated = derived(telemetry, ($t) => $t.fwValidated || false)
export const bootCount = derived(telemetry, ($t) => $t.bootCount || 0)
export const dnsTotal = derived(telemetry, ($t) => $t.dnsTotal || 0)
export const dnsBlocked = derived(telemetry, ($t) => $t.dnsBlocked || 0)
export const dnsProfile = derived(telemetry, ($t) => $t.dnsProfile || 'cloudflare')
export const curfewActive = derived(telemetry, ($t) => $t.curfewActive || false)
export const connectedDevicesCount = derived(telemetry, ($t) => $t.connectedDevices || 0)

export function updateTelemetry(data) {
  if (data) {
    const payload = data.data ? { ...data.data } : data
    telemetry.set(payload)
  }
}
