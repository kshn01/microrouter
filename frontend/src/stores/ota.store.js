// ═══════════════════════════════════════════════════════════════════
// OTA Updates Reactive Store
// ═══════════════════════════════════════════════════════════════════

import { API_ENDPOINTS } from '../config/constants.js'

export function openOtaPortal() {
  window.open(API_ENDPOINTS.OTA_PORTAL, '_blank')
}
