import { get } from 'svelte/store'
import { afterEach, describe, expect, it, vi } from 'vitest'
import { dismissToast, showToast, toasts } from '../../src/services/toast.service.js'

describe('toast service', () => {
  afterEach(() => {
    vi.useRealTimers()
    toasts.set([])
  })

  it('adds and dismisses typed notifications', () => {
    showToast('Saved', 'success', 0)
    const toast = get(toasts)[0]
    expect(toast).toMatchObject({ message: 'Saved', type: 'success' })

    dismissToast(toast.id)
    expect(get(toasts)).toEqual([])
  })

  it('automatically expires notifications after their duration', () => {
    vi.useFakeTimers()
    showToast('Temporary', 'info', 1000)
    expect(get(toasts)).toHaveLength(1)

    vi.advanceTimersByTime(1000)
    expect(get(toasts)).toEqual([])
  })
})
