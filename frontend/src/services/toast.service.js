// ═══════════════════════════════════════════════════════════════════
// Toast Notification Service
// ═══════════════════════════════════════════════════════════════════

import { writable } from 'svelte/store'
import { TIMING } from '../config/constants.js'

export const toasts = writable([])

let nextId = 1

/**
 * Trigger a toast notification
 * @param {string} message
 * @param {'info'|'success'|'warning'|'error'} type
 * @param {number} [duration]
 */
export function showToast(message, type = 'info', duration = TIMING.TOAST_DEFAULT_DURATION_MS) {
  const id = nextId++
  toasts.update((current) => [...current, { id, message, type }])

  if (duration > 0) {
    setTimeout(() => {
      dismissToast(id)
    }, duration)
  }
}

/**
 * Dismiss a toast by id
 * @param {number} id
 */
export function dismissToast(id) {
  toasts.update((current) => current.filter((t) => t.id !== id))
}
