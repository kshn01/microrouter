// ═══════════════════════════════════════════════════════════════════
// Data Models & Transformation Helpers
// ═══════════════════════════════════════════════════════════════════

/**
 * Format milliseconds into human-readable uptime (e.g. "2d 4h 12m 30s")
 * @param {number} ms
 * @returns {string}
 */
export function formatUptime(ms) {
  if (!ms || ms <= 0) return '0s'
  const totalSeconds = Math.floor(ms / 1000)
  const days = Math.floor(totalSeconds / 86400)
  const hours = Math.floor((totalSeconds % 86400) / 3600)
  const minutes = Math.floor((totalSeconds % 3600) / 60)
  const seconds = totalSeconds % 60

  if (days > 0) return `${days}d ${hours}h ${minutes}m`
  if (hours > 0) return `${hours}h ${minutes}m ${seconds}s`
  if (minutes > 0) return `${minutes}m ${seconds}s`
  return `${seconds}s`
}

/**
 * Format bytes into KB/MB/GB
 * @param {number} bytes
 * @returns {string}
 */
export function formatBytes(bytes) {
  if (!bytes || bytes === 0) return '0 B'
  const units = ['B', 'KB', 'MB', 'GB']
  const i = Math.floor(Math.log(bytes) / Math.log(1024))
  return `${parseFloat((bytes / Math.pow(1024, i)).toFixed(1))} ${units[i]}`
}

/**
 * Convert dBm RSSI to a 0 - 100% signal quality score
 * @param {number} rssi
 * @returns {number}
 */
export function rssiToPercent(rssi) {
  if (!rssi || rssi === 0) return 0
  return Math.min(100, Math.max(0, Math.round(((rssi + 90) / 60) * 100)))
}
