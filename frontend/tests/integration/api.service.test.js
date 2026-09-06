import { afterEach, describe, expect, it, vi } from 'vitest'
import fs from 'node:fs'
import path from 'node:path'
import {
  apiGetDnsConfig,
  apiScanWiFiNetworks,
  apiSetGuestLimits,
  apiSetGuestQuota,
} from '../../src/services/api.service.js'

function jsonResponse(body) {
  return { ok: true, json: async () => body }
}

afterEach(() => {
  vi.restoreAllMocks()
})

describe('API service integration contracts', () => {
  it('keeps frontend API endpoints registered in firmware', () => {
    const root = path.resolve(import.meta.dirname, '../../..')
    const constants = fs.readFileSync(path.join(root, 'frontend/src/config/constants.js'), 'utf8')
    const webServer = fs.readFileSync(path.join(root, 'src/web_server.cpp'), 'utf8')
    const endpoints = [...constants.matchAll(/\b[A-Z0-9_]+:\s*'([^']+)'/g)]
      .map((match) => match[1])
      .filter((endpoint) => endpoint.startsWith('/api/'))
    const routes = [...webServer.matchAll(/_server\.on\("([^\"]+)"/g)].map((match) => match[1])

    expect(endpoints.length).toBeGreaterThan(0)
    expect(endpoints.filter((endpoint) => !routes.includes(endpoint))).toEqual([])
  })

  it('reads DNS configuration from the firmware endpoint', async () => {
    const fetchMock = vi.spyOn(globalThis, 'fetch').mockResolvedValue(
      jsonResponse({ profile: 'adguard', primary: '94.140.14.14' })
    )

    await expect(apiGetDnsConfig()).resolves.toMatchObject({ profile: 'adguard' })
    expect(fetchMock).toHaveBeenCalledWith('/api/dns/get', expect.objectContaining({ signal: expect.any(AbortSignal) }))
  })

  it('sends both UI and firmware-compatible curfew fields', async () => {
    const fetchMock = vi.spyOn(globalThis, 'fetch').mockResolvedValue(jsonResponse({ status: 'ok' }))

    await apiSetGuestLimits({ curfewEnabled: true, startHour: 22, endHour: 6 })
    const request = fetchMock.mock.calls[0]
    const body = JSON.parse(request[1].body)

    expect(request[0]).toBe('/api/guest/limit/set')
    expect(body).toMatchObject({ curfewEnabled: true, enabled: true, startHour: 22, endHour: 6 })
  })

  it('sends enabled daily and hourly quota fields', async () => {
    const fetchMock = vi.spyOn(globalThis, 'fetch').mockResolvedValue(jsonResponse({ status: 'ok' }))

    await apiSetGuestQuota(1024, 128)
    const body = JSON.parse(fetchMock.mock.calls[0][1].body)

    expect(body).toMatchObject({
      dailyQuotaMB: 1024,
      dailyLimitMb: 1024,
      dailyEnabled: true,
      hourlyQuotaMB: 128,
      hourlyLimitMb: 128,
      hourlyEnabled: true,
    })
  })

  it('passes firmware scan responses through for store normalization', async () => {
    vi.spyOn(globalThis, 'fetch').mockResolvedValue(
      jsonResponse({ networks: [{ ssid: 'Home', channel: 6, rssi: -40 }] })
    )

    await expect(apiScanWiFiNetworks()).resolves.toEqual({
      networks: [{ ssid: 'Home', channel: 6, rssi: -40 }],
    })
  })
})