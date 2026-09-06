import { get } from 'svelte/store'
import { beforeEach, describe, expect, it, vi } from 'vitest'

const mocks = vi.hoisted(() => ({
  apiScanWiFiNetworks: vi.fn(),
  apiConnectWiFi: vi.fn(),
  apiDisconnectWiFi: vi.fn(),
  showToast: vi.fn(),
}))

vi.mock('../../src/services/api.service.js', () => ({
  apiScanWiFiNetworks: mocks.apiScanWiFiNetworks,
  apiConnectWiFi: mocks.apiConnectWiFi,
  apiDisconnectWiFi: mocks.apiDisconnectWiFi,
}))
vi.mock('../../src/services/toast.service.js', () => ({ showToast: mocks.showToast }))

const { scannedNetworks, isConnecting, isScanning, scanNetworks, connectNetwork, forgetNetwork } =
  await import('../../src/stores/wifi.store.js')

describe('WiFi store', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    scannedNetworks.set([])
    isScanning.set(false)
    isConnecting.set(false)
  })

  it('deduplicates SSIDs, keeps the strongest result, and filters non-2.4 GHz entries', async () => {
    mocks.apiScanWiFiNetworks.mockResolvedValue({
      networks: [
        { ssid: 'Home', channel: 6, rssi: -60 },
        { ssid: 'Home', channel: 6, rssi: -40 },
        { ssid: 'Office-5G', channel: 36, rssi: -30 },
        { ssid: '', channel: 11, rssi: -20 },
      ],
    })

    await expect(scanNetworks()).resolves.toEqual([{ ssid: 'Home', channel: 6, rssi: -40 }])
    expect(get(scannedNetworks)).toHaveLength(1)
    expect(get(isScanning)).toBe(false)
  })

  it('resets scanning state and reports failures', async () => {
    mocks.apiScanWiFiNetworks.mockRejectedValue(new Error('offline'))

    await expect(scanNetworks()).resolves.toEqual([])
    expect(get(isScanning)).toBe(false)
    expect(mocks.showToast).toHaveBeenCalledWith('WiFi scan failed: offline', 'error')
  })

  it('connects and forgets networks through the API boundary', async () => {
    mocks.apiConnectWiFi.mockResolvedValue({ status: 'connecting' })
    mocks.apiDisconnectWiFi.mockResolvedValue({ status: 'disconnected' })

    await expect(connectNetwork('Home', 'secret')).resolves.toEqual({ status: 'connecting' })
    expect(mocks.apiConnectWiFi).toHaveBeenCalledWith('Home', 'secret')
    expect(get(isConnecting)).toBe(false)

    await forgetNetwork()
    expect(mocks.apiDisconnectWiFi).toHaveBeenCalledOnce()
  })
})
