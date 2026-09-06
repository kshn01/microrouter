import { beforeEach, describe, expect, it } from 'vitest'
import {
  blockMockDevice,
  generateMockTelemetry,
  getMockDevices,
  getMockDnsConfig,
  getMockGuestLimits,
  getMockWifiNetworks,
  setMockDnsConfig,
  setMockGuestLimits,
} from '../../src/services/mock.service.js'
import { lookupOUI } from '../../src/utils/oui.js'

describe('mock service', () => {
  beforeEach(() => {
    setMockDnsConfig({ profile: 'cloudflare', primary: '1.1.1.1', secondary: '1.0.0.1' })
    setMockGuestLimits({ curfewEnabled: true, dailyQuotaMB: 2048, hourlyQuotaMB: 500 })
    blockMockDevice('00:fc:8b:2e:55:18', false)
  })

  it('returns only valid 2.4 GHz mock scan channels', () => {
    const networks = getMockWifiNetworks()
    expect(networks.length).toBeGreaterThan(0)
    expect(networks.every((network) => network.channel >= 1 && network.channel <= 14)).toBe(true)
  })

  it('persists DNS and parental settings in simulation state', () => {
    setMockDnsConfig({ profile: 'adguard', primary: '94.140.14.14' })
    setMockGuestLimits({ curfewEnabled: false, dailyQuotaMB: 1024 })

    expect(getMockDnsConfig()).toMatchObject({ profile: 'adguard', primary: '94.140.14.14' })
    expect(getMockGuestLimits()).toMatchObject({ curfewEnabled: false, dailyQuotaMB: 1024 })
  })

  it('updates device access state case-insensitively', () => {
    blockMockDevice('00:FC:8B:2E:55:18', true)
    const fireTv = getMockDevices().devices.find((device) => device.mac === '00:fc:8b:2e:55:18')
    expect(fireTv.isBlocked).toBe(true)
  })

  it('generates telemetry with monotonic uptime and valid network metrics', () => {
    const first = generateMockTelemetry()
    const second = generateMockTelemetry()
    expect(second.uptimeMs).toBeGreaterThan(first.uptimeMs)
    expect(second.wifiChannel).toBeGreaterThanOrEqual(1)
    expect(second.wifiChannel).toBeLessThanOrEqual(14)
  })

  it('recognizes known hardware vendors and randomized MACs', () => {
    expect(lookupOUI('3c:15:c2:44:9a:12').vendor).toBe('Apple Inc.')
    expect(lookupOUI('02:00:00:00:00:01').isRandomized).toBe(true)
  })
})