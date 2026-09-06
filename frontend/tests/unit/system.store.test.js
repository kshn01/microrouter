import { get } from 'svelte/store'
import { beforeEach, describe, expect, it, vi } from 'vitest'

const mocks = vi.hoisted(() => ({
  apiFetchSystemInfo: vi.fn(),
  apiRestartSystem: vi.fn(),
  showToast: vi.fn(),
}))

vi.mock('../../src/services/api.service.js', () => ({
  apiFetchSystemInfo: mocks.apiFetchSystemInfo,
  apiRestartSystem: mocks.apiRestartSystem,
}))
vi.mock('../../src/services/toast.service.js', () => ({ showToast: mocks.showToast }))

const { isLoadingSystem, loadSystemInfo, rebootDevice, systemInfo } =
  await import('../../src/stores/system.store.js')

describe('system store', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    systemInfo.set(null)
    isLoadingSystem.set(false)
  })

  it('loads and stores hardware diagnostics', async () => {
    const info = { chipModel: 'ESP32-S3', freeHeap: 1000 }
    mocks.apiFetchSystemInfo.mockResolvedValue(info)

    await expect(loadSystemInfo()).resolves.toEqual(info)
    expect(get(systemInfo)).toEqual(info)
    expect(get(isLoadingSystem)).toBe(false)
  })

  it('clears loading state and reports diagnostic failures', async () => {
    mocks.apiFetchSystemInfo.mockRejectedValue(new Error('unreachable'))

    await expect(loadSystemInfo()).resolves.toBeNull()
    expect(get(isLoadingSystem)).toBe(false)
    expect(mocks.showToast).toHaveBeenCalledWith('System info retrieval failed: unreachable', 'error')
  })

  it('sends a reboot request and confirms it to the user', async () => {
    mocks.apiRestartSystem.mockResolvedValue({ status: 'restarting' })

    await rebootDevice()
    expect(mocks.apiRestartSystem).toHaveBeenCalledOnce()
    expect(mocks.showToast).toHaveBeenCalledWith('Reboot signal transmitted', 'warning')
  })
})
