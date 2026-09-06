import { expect, test } from '@playwright/test'

async function mockApi(page) {
  await page.route('**/api/**', async (route) => {
    const url = new URL(route.request().url())
    const body = url.pathname === '/api/wifi/scan'
      ? { networks: [
          { ssid: 'Home', channel: 6, rssi: -42, secure: true },
          { ssid: 'Home', channel: 6, rssi: -57, secure: true },
          { ssid: 'Office-5G', channel: 36, rssi: -35, secure: true },
          { ssid: '', channel: 11, rssi: -30, secure: false },
        ] }
      : url.pathname === '/api/dns/get'
        ? { profile: 'ultra_fast', primary: '1.1.1.1', secondary: '1.0.0.1', totalQueries: 4, blockedQueries: 1 }
        : url.pathname === '/api/dns/queries'
          ? { queries: [], stats: { total: 4, blocked: 1 } }
          : url.pathname === '/api/router/dns/get'
            ? { routerSynced: true, haMode: true, dhcpPrimary: '192.168.1.7', dhcpSecondary: '1.1.1.1' }
            : url.pathname === '/api/guest/limit/get'
              ? { curfewEnabled: true, startHour: 22, startMin: 0, endHour: 6, endMin: 30, dailyQuotaMB: 2048, hourlyQuotaMB: 500, curfewActive: false }
              : url.pathname === '/api/devices'
                ? { devices: [] }
                : {}
    await route.fulfill({ status: 200, contentType: 'application/json', body: JSON.stringify(body) })
  })
}

test('navigates between primary dashboard views', async ({ page }) => {
  await mockApi(page)
  await page.goto('/')
  await expect(page.getByText('MicroRouter').first()).toBeVisible()

  await page.goto('/#/dns')
  await expect(page.getByText('DNS Shield & Network Spyglass')).toBeVisible()

  await page.goto('/#/parental')
  await expect(page.getByText('Parental Controls & Curfew')).toBeVisible()
})

test('renders normalized 2.4 GHz scan results', async ({ page }) => {
  await mockApi(page)
  await page.goto('/#/wifi')
  await page.getByRole('button', { name: 'Scan Networks' }).click()

  await expect(page.getByText('Home', { exact: true })).toHaveCount(1)
  await expect(page.getByText('Office-5G', { exact: true })).toHaveCount(0)
  await expect(page.getByText('2.4 GHz Networks')).toBeVisible()
})