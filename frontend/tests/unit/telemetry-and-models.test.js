import { get } from 'svelte/store'
import { describe, expect, it } from 'vitest'
import {
  connectedDevicesCount,
  dnsBlocked,
  telemetry,
  updateTelemetry,
  wifiSignalPercent,
} from '../../src/stores/telemetry.store.js'
import { formatBytes, formatUptime, rssiToPercent } from '../../src/types/models.js'

describe('telemetry store and data models', () => {
  it('accepts both websocket envelopes and direct telemetry payloads', () => {
    updateTelemetry({ data: { wifiRssi: -60, dnsBlocked: 7, connectedDevices: 3 } })
    expect(get(dnsBlocked)).toBe(7)
    expect(get(connectedDevicesCount)).toBe(3)
    expect(get(wifiSignalPercent)).toBe(50)

    updateTelemetry({ wifiRssi: -30, dnsBlocked: 1, connectedDevices: 1 })
    expect(get(dnsBlocked)).toBe(1)
    expect(get(connectedDevicesCount)).toBe(1)
  })

  it('handles null telemetry without throwing', () => {
    updateTelemetry(null)
    expect(get(telemetry)).toBeDefined()
  })

  it('formats uptime and byte values for the UI', () => {
    expect(formatUptime(0)).toBe('0s')
    expect(formatUptime(3661000)).toBe('1h 1m 1s')
    expect(formatUptime(90061000)).toBe('1d 1h 1m')
    expect(formatBytes(0)).toBe('0 B')
    expect(formatBytes(1536)).toBe('1.5 KB')
    expect(formatBytes(2 * 1024 * 1024)).toBe('2 MB')
  })

  it('clamps RSSI conversion to the expected range', () => {
    expect(rssiToPercent(-120)).toBe(0)
    expect(rssiToPercent(-60)).toBe(50)
    expect(rssiToPercent(-20)).toBe(100)
    expect(rssiToPercent(0)).toBe(0)
  })
})
