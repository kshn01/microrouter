<script>
  import { onMount, onDestroy } from 'svelte'
  import {
    Clock,
    Shield,
    Sliders,
    Moon,
    Sun,
    CheckCircle2,
    XCircle,
    AlertCircle,
    RefreshCw,
    HardDrive,
    Ban,
    UserCheck
  } from '@lucide/svelte'
  import Card from '../components/ui/Card.svelte'
  import StatCard from '../components/ui/StatCard.svelte'
  import ProgressBar from '../components/ui/ProgressBar.svelte'
  import {
    apiGetGuestLimits,
    apiSetGuestLimits,
    apiSetGuestQuota,
    apiGetDevices,
    apiSetDeviceWaiver
  } from '../services/api.service.js'
  import { formatBytes } from '../types/models.js'
  import { showToast } from '../services/toast.service.js'

  let loading = $state(true)
  let savingCurfew = $state(false)
  let savingQuota = $state(false)
  let pollInterval = null

  let limits = $state({
    curfewEnabled: true,
    startHour: 22,
    startMin: 0,
    endHour: 6,
    endMin: 30,
    dailyQuotaMB: 2048,
    hourlyQuotaMB: 500,
    curfewActive: false,
    currentTime: '--:--',
    guestTxBytes: 0,
    guestRxBytes: 0,
  })

  let waivedDevices = $state([])

  let dailyUsageMB = $derived(
    Math.round(((limits.guestRxBytes + limits.guestTxBytes) / (1024 * 1024)) * 10) / 10
  )
  let dailyUsagePercent = $derived(
    limits.dailyQuotaMB > 0
      ? Math.min(100, Math.round((dailyUsageMB / limits.dailyQuotaMB) * 100))
      : 0
  )

  async function loadData() {
    try {
      const [limitRes, devRes] = await Promise.all([
        apiGetGuestLimits(),
        apiGetDevices()
      ])
      if (limitRes) {
        limits = { ...limits, ...limitRes }
      }
      if (devRes && devRes.devices) {
        waivedDevices = devRes.devices.filter(
          (d) => (d.waiverSecRemaining || 0) > 0
        )
      }
    } catch (err) {
      console.error('Failed to load parental limit data', err)
    } finally {
      loading = false
    }
  }

  async function handleSaveCurfew() {
    savingCurfew = true
    try {
      await apiSetGuestLimits({
        curfewEnabled: limits.curfewEnabled,
        startHour: parseInt(limits.startHour, 10),
        startMin: parseInt(limits.startMin, 10),
        endHour: parseInt(limits.endHour, 10),
        endMin: parseInt(limits.endMin, 10),
      })
      showToast('Curfew schedule successfully updated', 'success')
      await loadData()
    } catch (err) {
      showToast('Failed to save curfew: ' + err.message, 'error')
    } finally {
      savingCurfew = false
    }
  }

  async function handleSaveQuota() {
    savingQuota = true
    try {
      await apiSetGuestQuota(
        parseInt(limits.dailyQuotaMB, 10),
        parseInt(limits.hourlyQuotaMB, 10)
      )
      showToast('Bandwidth quotas successfully saved', 'success')
      await loadData()
    } catch (err) {
      showToast('Failed to save quotas: ' + err.message, 'error')
    } finally {
      savingQuota = false
    }
  }

  async function revokeWaiver(mac) {
    try {
      await apiSetDeviceWaiver(mac, 0)
      waivedDevices = waivedDevices.filter((d) => d.mac !== mac)
      showToast(`Waiver revoked for ${mac}`, 'info')
    } catch (err) {
      showToast('Error revoking waiver: ' + err.message, 'error')
    }
  }

  function formatWaiverCountdown(sec) {
    if (!sec || sec <= 0) return 'Expired'
    const m = Math.floor(sec / 60)
    const s = sec % 60
    return `${m}m ${s < 10 ? '0' : ''}${s}s remaining`
  }

  function formatTimeDigits(h, m) {
    const hh = String(h).padStart(2, '0')
    const mm = String(m).padStart(2, '0')
    return `${hh}:${mm}`
  }

  onMount(() => {
    loadData()
    pollInterval = setInterval(loadData, 3000)
  })

  onDestroy(() => {
    if (pollInterval) clearInterval(pollInterval)
  })
</script>

<div class="flex flex-col gap-8 animate-in fade-in duration-300">
  <!-- Header Title -->
  <div class="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
    <div>
      <h1 class="text-2xl lg:text-3xl font-bold tracking-tight text-white flex items-center gap-3">
        <Moon class="w-8 h-8 text-indigo-400" />
        Parental Controls & Curfew
      </h1>
      <p class="text-sm text-slate-400 mt-1">
        Automated NTP-synced Wi-Fi curfews, daily bandwidth quotas, and temporary emergency access waivers
      </p>
    </div>
    <div class="flex items-center gap-3">
      <button
        onclick={loadData}
        class="inline-flex items-center gap-2 px-3 py-2 rounded-xl text-xs font-semibold bg-white/5 hover:bg-white/10 text-slate-300 border border-white/10 transition-colors"
      >
        <RefreshCw class="w-3.5 h-3.5" />
        Refresh
      </button>
    </div>
  </div>

  <!-- Curfew Status Alert Banner -->
  {#if limits.curfewActive}
    <div class="p-4 rounded-2xl bg-amber-500/10 border border-amber-500/30 flex items-center justify-between gap-4 shadow-lg shadow-amber-500/5">
      <div class="flex items-center gap-3">
        <div class="w-10 h-10 rounded-xl bg-amber-500/20 text-amber-400 flex items-center justify-center">
          <Moon class="w-5 h-5" />
        </div>
        <div>
          <span class="font-bold text-sm text-white block">Curfew Restriction is Active</span>
          <span class="text-xs text-amber-200/80">
            Guest stations are blocked from WAN internet access until {formatTimeDigits(limits.endHour, limits.endMin)}.
          </span>
        </div>
      </div>
      <span class="text-xs font-mono font-bold px-3 py-1 rounded-full bg-amber-500 text-slate-950">
        NTP Active
      </span>
    </div>
  {:else}
    <div class="p-4 rounded-2xl bg-emerald-500/10 border border-emerald-500/30 flex items-center justify-between gap-4">
      <div class="flex items-center gap-3">
        <div class="w-10 h-10 rounded-xl bg-emerald-500/20 text-emerald-400 flex items-center justify-center">
          <Sun class="w-5 h-5" />
        </div>
        <div>
          <span class="font-bold text-sm text-white block">Daytime Wi-Fi Active</span>
          <span class="text-xs text-emerald-200/80">
            Curfew window begins tonight at {formatTimeDigits(limits.startHour, limits.startMin)}.
          </span>
        </div>
      </div>
      <span class="text-xs font-mono font-bold px-3 py-1 rounded-full bg-emerald-500/20 text-emerald-400 border border-emerald-500/30">
        Normal Hours
      </span>
    </div>
  {/if}

  <!-- Stats Grid -->
  <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
    <StatCard
      title="Curfew Status"
      value={limits.curfewEnabled ? 'Armed' : 'Off'}
      subtitle={limits.curfewActive ? 'Currently Enforcing' : 'Standby'}
      color="purple"
      icon={Moon}
    />
    <StatCard
      title="Daily Quota Cap"
      value="{limits.dailyQuotaMB} MB"
      subtitle="{dailyUsageMB} MB consumed today ({dailyUsagePercent}%)"
      color="indigo"
      icon={HardDrive}
    />
    <StatCard
      title="Hourly Quota Cap"
      value="{limits.hourlyQuotaMB} MB"
      subtitle="Per-station hourly throttle limit"
      color="cyan"
      icon={Sliders}
    />
    <StatCard
      title="Active Waivers"
      value={String(waivedDevices.length)}
      subtitle="Bypassing curfew restrictions"
      color="amber"
      icon={UserCheck}
    />
  </div>

  <!-- Settings Grid -->
  <div class="grid grid-cols-1 lg:grid-cols-2 gap-6">
    <!-- Curfew Window Schedule -->
    <Card>
      <div class="flex items-center justify-between pb-4 mb-4 border-b border-white/10">
        <div class="flex items-center gap-2">
          <Moon class="w-5 h-5 text-indigo-400" />
          <h2 class="text-base font-bold text-white tracking-tight">Curfew Schedule (Bedtime)</h2>
        </div>
        <div class="flex items-center gap-2">
          <label class="text-xs text-slate-400 cursor-pointer" for="curfew-toggle">
            {limits.curfewEnabled ? 'Enabled' : 'Disabled'}
          </label>
          <input
            id="curfew-toggle"
            type="checkbox"
            bind:checked={limits.curfewEnabled}
            class="w-4 h-4 accent-indigo-500 rounded cursor-pointer"
          />
        </div>
      </div>

      <div class="flex flex-col gap-5 text-xs">
        <p class="text-slate-400 leading-relaxed">
          Specify the nightly timeframe when guest stations should be disconnected from the WAN. The MicroRouter ESP32 evaluates this continuously against synced NTP real-time clocks.
        </p>

        <div class="grid grid-cols-2 gap-4">
          <!-- Start Time (Night) -->
          <div class="p-4 rounded-xl bg-white/[0.02] border border-white/10 flex flex-col gap-2">
            <div class="flex items-center gap-2 text-indigo-400 font-semibold">
              <Moon class="w-4 h-4" />
              <span>Bedtime Lock (Night)</span>
            </div>
            <div class="flex items-center gap-2">
              <input
                type="number"
                min="0"
                max="23"
                bind:value={limits.startHour}
                class="w-16 bg-slate-900 border border-white/10 rounded-lg p-2 font-mono text-center text-white focus:outline-none focus:border-indigo-500"
              />
              <span class="text-slate-400 font-bold">:</span>
              <input
                type="number"
                min="0"
                max="59"
                bind:value={limits.startMin}
                class="w-16 bg-slate-900 border border-white/10 rounded-lg p-2 font-mono text-center text-white focus:outline-none focus:border-indigo-500"
              />
              <span class="text-slate-400 text-[11px]">(HH:MM)</span>
            </div>
          </div>

          <!-- End Time (Morning) -->
          <div class="p-4 rounded-xl bg-white/[0.02] border border-white/10 flex flex-col gap-2">
            <div class="flex items-center gap-2 text-emerald-400 font-semibold">
              <Sun class="w-4 h-4" />
              <span>Wake-up Unlock (Morning)</span>
            </div>
            <div class="flex items-center gap-2">
              <input
                type="number"
                min="0"
                max="23"
                bind:value={limits.endHour}
                class="w-16 bg-slate-900 border border-white/10 rounded-lg p-2 font-mono text-center text-white focus:outline-none focus:border-indigo-500"
              />
              <span class="text-slate-400 font-bold">:</span>
              <input
                type="number"
                min="0"
                max="59"
                bind:value={limits.endMin}
                class="w-16 bg-slate-900 border border-white/10 rounded-lg p-2 font-mono text-center text-white focus:outline-none focus:border-indigo-500"
              />
              <span class="text-slate-400 text-[11px]">(HH:MM)</span>
            </div>
          </div>
        </div>

        <div class="flex justify-end pt-2">
          <button
            onclick={handleSaveCurfew}
            disabled={savingCurfew}
            class="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-xs font-semibold bg-indigo-600 hover:bg-indigo-500 text-white shadow-lg shadow-indigo-600/30 transition-colors disabled:opacity-50"
          >
            {#if savingCurfew}
              <RefreshCw class="w-3.5 h-3.5 animate-spin" />
              Saving...
            {:else}
              <CheckCircle2 class="w-3.5 h-3.5" />
              Save Curfew
            {/if}
          </button>
        </div>
      </div>
    </Card>

    <!-- Bandwidth Quota Configuration -->
    <Card>
      <div class="flex items-center justify-between pb-4 mb-4 border-b border-white/10">
        <div class="flex items-center gap-2">
          <HardDrive class="w-5 h-5 text-indigo-400" />
          <h2 class="text-base font-bold text-white tracking-tight">Bandwidth Quotas</h2>
        </div>
      </div>

      <div class="flex flex-col gap-4 text-xs">
        <!-- Daily Consumption Progress -->
        <div class="p-4 rounded-xl bg-white/[0.02] border border-white/10 flex flex-col gap-2">
          <div class="flex items-center justify-between text-[11px]">
            <span class="text-slate-400">Total Guest Data Consumed Today</span>
            <span class="font-mono text-white font-bold">{dailyUsageMB} MB / {limits.dailyQuotaMB} MB</span>
          </div>
          <ProgressBar
            percent={dailyUsagePercent}
            color={dailyUsagePercent > 80 ? 'rose' : 'indigo'}
          />
        </div>

        <div class="grid grid-cols-2 gap-4">
          <div>
            <label class="block font-semibold text-slate-300 mb-1.5" for="daily-quota">
              Daily Limit (MB)
            </label>
            <input
              id="daily-quota"
              type="number"
              bind:value={limits.dailyQuotaMB}
              class="w-full bg-slate-900 border border-white/10 rounded-lg p-2 font-mono text-white focus:outline-none focus:border-indigo-500"
            />
          </div>

          <div>
            <label class="block font-semibold text-slate-300 mb-1.5" for="hourly-quota">
              Hourly Limit (MB)
            </label>
            <input
              id="hourly-quota"
              type="number"
              bind:value={limits.hourlyQuotaMB}
              class="w-full bg-slate-900 border border-white/10 rounded-lg p-2 font-mono text-white focus:outline-none focus:border-indigo-500"
            />
          </div>
        </div>

        <div class="flex justify-end pt-2">
          <button
            onclick={handleSaveQuota}
            disabled={savingQuota}
            class="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-xs font-semibold bg-indigo-600 hover:bg-indigo-500 text-white shadow-lg shadow-indigo-600/30 transition-colors disabled:opacity-50"
          >
            {#if savingQuota}
              <RefreshCw class="w-3.5 h-3.5 animate-spin" />
              Saving...
            {:else}
              <CheckCircle2 class="w-3.5 h-3.5" />
              Save Quotas
            {/if}
          </button>
        </div>
      </div>
    </Card>
  </div>

  <!-- Active Temporary Waivers Table -->
  <Card>
    <div class="flex items-center justify-between pb-4 mb-4 border-b border-white/10">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-lg bg-amber-500/20 text-amber-400 flex items-center justify-center">
          <Clock class="w-4 h-4" />
        </div>
        <div>
          <h2 class="text-base font-bold text-white tracking-tight">Active Temporary Access Waivers</h2>
          <span class="text-xs text-slate-400">Stations with curfew overrides in effect</span>
        </div>
      </div>
    </div>

    {#if waivedDevices.length === 0}
      <div class="py-10 text-center text-slate-500 text-xs">
        No devices currently have active waivers. Waivers can be granted from the <a href="#/devices" class="text-indigo-400 hover:underline">Device Inventory</a>.
      </div>
    {:else}
      <div class="overflow-x-auto">
        <table class="w-full text-left text-xs">
          <thead class="bg-white/[0.02] border-b border-white/5 text-slate-400 uppercase tracking-wider text-[10px]">
            <tr>
              <th class="py-2.5 px-4 font-semibold">Station / Hostname</th>
              <th class="py-2.5 px-4 font-semibold">MAC Address</th>
              <th class="py-2.5 px-4 font-semibold">Time Remaining</th>
              <th class="py-2.5 px-4 font-semibold text-right">Action</th>
            </tr>
          </thead>
          <tbody class="divide-y divide-white/5">
            {#each waivedDevices as d}
              <tr class="hover:bg-white/[0.02] transition-colors">
                <td class="py-3 px-4 font-bold text-white">
                  {d.hostname || 'Station'}
                </td>
                <td class="py-3 px-4 font-mono text-[11px] text-slate-400 uppercase">
                  {d.mac}
                </td>
                <td class="py-3 px-4 font-mono text-amber-400 font-semibold">
                  {formatWaiverCountdown(d.waiverSecRemaining)}
                </td>
                <td class="py-3 px-4 text-right">
                  <button
                    onclick={() => revokeWaiver(d.mac)}
                    class="px-2.5 py-1 rounded-lg text-xs font-semibold bg-rose-500/10 hover:bg-rose-500/20 text-rose-300 border border-rose-500/20 transition-colors"
                  >
                    Revoke Waiver
                  </button>
                </td>
              </tr>
            {/each}
          </tbody>
        </table>
      </div>
    {/if}
  </Card>
</div>
