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
    UserCheck,
    Activity,
    Smartphone,
    Laptop,
    Wifi,
    Plus,
    ExternalLink
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
  let curfewDirty = $state(false)
  let quotaDirty = $state(false)
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

  let allDevices = $state([])
  let waivedDevices = $state([])

  let protectedDevices = $derived(
    allDevices.filter((d) => Boolean(d.parentalControl))
  )

  let devicesOverLimitCount = $derived(
    protectedDevices.filter((d) => {
      if (!limits.dailyQuotaMB || limits.dailyQuotaMB <= 0) return false
      const usageMb = (d.dailyUsage || 0) / (1024 * 1024)
      return usageMb >= limits.dailyQuotaMB
    }).length
  )

  let dailyUsageMB = $derived(
    Math.round(((limits.guestRxBytes + limits.guestTxBytes) / (1024 * 1024)) * 10) / 10
  )

  function format12Hour(hour, min) {
    const h = parseInt(hour, 10) || 0
    const m = parseInt(min, 10) || 0
    const period = h >= 12 ? 'PM' : 'AM'
    const h12 = h % 12 === 0 ? 12 : h % 12
    return `${h12}:${String(m).padStart(2, '0')} ${period}`
  }

  function getCurfewDurationStr(startH, startM, endH, endM) {
    const s = (parseInt(startH, 10) || 0) * 60 + (parseInt(startM, 10) || 0)
    const e = (parseInt(endH, 10) || 0) * 60 + (parseInt(endM, 10) || 0)
    let diff = e - s
    if (diff < 0) diff += 1440
    const hrs = Math.floor(diff / 60)
    const mins = diff % 60
    return mins > 0 ? `${hrs}h ${mins}m` : `${hrs} hrs`
  }

  function toTimeString(h, m) {
    return `${String(h || 0).padStart(2, '0')}:${String(m || 0).padStart(2, '0')}`
  }

  function handleStartTimeChange(e) {
    const val = e.target.value
    if (!val) return
    const [h, m] = val.split(':')
    limits.startHour = parseInt(h, 10) || 0
    limits.startMin = parseInt(m, 10) || 0
    curfewDirty = true
  }

  function handleEndTimeChange(e) {
    const val = e.target.value
    if (!val) return
    const [h, m] = val.split(':')
    limits.endHour = parseInt(h, 10) || 0
    limits.endMin = parseInt(m, 10) || 0
    curfewDirty = true
  }

  function applyPreset(sH, sM, eH, eM) {
    limits.startHour = sH
    limits.startMin = sM
    limits.endHour = eH
    limits.endMin = eM
    curfewDirty = true
  }

  const PRESETS = [
    { label: 'School Night', desc: '10:00 PM – 6:30 AM', sH: 22, sM: 0, eH: 6, eM: 30 },
    { label: 'Early Bedtime', desc: '9:00 PM – 6:00 AM', sH: 21, sM: 0, eH: 6, eM: 0 },
    { label: 'Teen Schedule', desc: '11:00 PM – 7:00 AM', sH: 23, sM: 0, eH: 7, eM: 0 },
    { label: 'Strict Exam', desc: '8:30 PM – 7:00 AM', sH: 20, sM: 30, eH: 7, eM: 0 },
  ]

  const DAILY_QUOTA_PRESETS = [
    { label: 'Unlimited', desc: 'No daily limit', value: 0 },
    { label: '1 GB', desc: '1,024 MB', value: 1024 },
    { label: '2 GB', desc: '2,048 MB', value: 2048 },
    { label: '5 GB', desc: '5,120 MB', value: 5120 },
  ]

  const HOURLY_QUOTA_PRESETS = [
    { label: 'Uncapped', desc: 'No hourly limit', value: 0 },
    { label: '250 MB', desc: 'Quarter GB', value: 250 },
    { label: '500 MB', desc: 'Half GB', value: 500 },
    { label: '1 GB', desc: '1,024 MB', value: 1024 },
  ]

  function formatMbHuman(mb) {
    const val = parseInt(mb, 10) || 0
    if (val <= 0) return 'Unlimited'
    if (val >= 1024) {
      const gb = Math.round((val / 1024) * 10) / 10
      return `${val.toLocaleString()} MB (~${gb} GB)`
    }
    return `${val.toLocaleString()} MB`
  }

  function applyDailyQuotaPreset(val) {
    limits.dailyQuotaMB = val
    quotaDirty = true
  }

  function applyHourlyQuotaPreset(val) {
    limits.hourlyQuotaMB = val
    quotaDirty = true
  }

  let currentTimeMins = $derived.by(() => {
    const raw = limits.timeOnly || limits.currentTime
    if (!raw || raw === '--:--' || raw === 'Not Synced') return null
    // Extract HH:MM if string contains date or full ISO string
    const match = raw.match(/(\d{1,2}):(\d{2})/)
    if (!match) return null
    const h = parseInt(match[1], 10) || 0
    const m = parseInt(match[2], 10) || 0
    const total = h * 60 + m
    return Math.min(99, Math.max(0.5, Math.round(((total / 1440) * 100) * 10) / 10))
  })

  let timelineBlocks = $derived.by(() => {
    const s = (parseInt(limits.startHour, 10) || 0) * 60 + (parseInt(limits.startMin, 10) || 0)
    const e = (parseInt(limits.endHour, 10) || 0) * 60 + (parseInt(limits.endMin, 10) || 0)

    if (s > e) {
      const morningCurfewPct = (e / 1440) * 100
      const daytimePct = ((s - e) / 1440) * 100
      const nightCurfewPct = ((1440 - s) / 1440) * 100
      return { isOvernight: true, morningCurfewPct, daytimePct, nightCurfewPct }
    } else {
      const prePct = (s / 1440) * 100
      const curfewPct = ((e - s) / 1440) * 100
      const postPct = ((1440 - e) / 1440) * 100
      return { isOvernight: false, prePct, curfewPct, postPct }
    }
  })

  async function loadData() {
    try {
      const [limitRes, devRes] = await Promise.all([
        apiGetGuestLimits(),
        apiGetDevices()
      ])
      if (limitRes) {
        limits = {
          ...limits,
          ...limitRes,
          ...(curfewDirty ? {
            curfewEnabled: limits.curfewEnabled,
            startHour: limits.startHour,
            startMin: limits.startMin,
            endHour: limits.endHour,
            endMin: limits.endMin,
          } : {}),
          ...(quotaDirty ? {
            dailyQuotaMB: limits.dailyQuotaMB,
            hourlyQuotaMB: limits.hourlyQuotaMB,
          } : {}),
        }
      }
      if (devRes && devRes.devices) {
        allDevices = devRes.devices
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
      const res = await apiSetGuestLimits({
        curfewEnabled: limits.curfewEnabled,
        startHour: parseInt(limits.startHour, 10),
        startMin: parseInt(limits.startMin, 10),
        endHour: parseInt(limits.endHour, 10),
        endMin: parseInt(limits.endMin, 10),
      })
      curfewDirty = false
      if (res?.routerSynced === false) {
        showToast('Curfew saved, but strict router DNS could not be enabled', 'warning')
      } else {
        showToast('Curfew schedule successfully updated', 'success')
      }
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
      quotaDirty = false
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
      await loadData()
    } catch (err) {
      showToast('Error revoking waiver: ' + err.message, 'error')
    }
  }

  async function grantQuickWaiver(mac, durationSecs = 1800) {
    try {
      await apiSetDeviceWaiver(mac, durationSecs)
      showToast(`Temporary waiver granted for ${mac}`, 'success')
      await loadData()
    } catch (err) {
      showToast('Failed to grant waiver: ' + err.message, 'error')
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
            Guest stations are blocked by MicroRouter DNS until {formatTimeDigits(limits.endHour, limits.endMin)}.
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
      title="Daily Quota / Device"
      value={limits.dailyQuotaMB > 0 ? formatMbHuman(limits.dailyQuotaMB) : 'Uncapped'}
      subtitle="{protectedDevices.length} protected clients enrolled"
      color="indigo"
      icon={HardDrive}
    />
    <StatCard
      title="Guest Fleet Usage"
      value="{dailyUsageMB} MB"
      subtitle={devicesOverLimitCount > 0 ? `${devicesOverLimitCount} client(s) over quota` : 'Total combined traffic today'}
      color={devicesOverLimitCount > 0 ? 'rose' : 'cyan'}
      icon={Activity}
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
  <div class="grid grid-cols-1 lg:grid-cols-2 gap-6 items-stretch">
    <!-- Curfew Window Schedule -->
    <Card class="h-full flex flex-col justify-between">
      <div class="flex-1 flex flex-col">
        <!-- Card Header with iOS-style Switch -->
        <div class="flex items-center justify-between pb-4 mb-4 border-b border-white/10">
          <div class="flex items-center gap-2.5">
            <div class="w-9 h-9 rounded-xl bg-indigo-500/10 border border-indigo-500/20 flex items-center justify-center text-indigo-400">
              <Moon class="w-5 h-5" />
            </div>
            <div>
              <h2 class="text-base font-bold text-white tracking-tight">Curfew Schedule (Bedtime)</h2>
              <p class="text-[11px] text-slate-400">Scheduled Wi-Fi and DNS blackout for protected stations</p>
            </div>
          </div>

          <!-- Router Clock Indicator & Modern iOS Toggle Switch -->
          <div class="flex items-center gap-2.5">
            <div class="hidden sm:flex items-center gap-1.5 px-2.5 py-1 rounded-full bg-slate-900 border border-white/10 text-[11px] font-mono">
              <span class="w-1.5 h-1.5 rounded-full {limits.currentTime && limits.currentTime !== '--:--' ? 'bg-emerald-400' : 'bg-amber-400'}"></span>
              <span class="text-slate-400">Clock:</span>
              <span class="text-white font-bold">{limits.currentTime || '--:--'}</span>
            </div>

            <button
              type="button"
              role="switch"
              aria-checked={limits.curfewEnabled}
              onclick={() => { limits.curfewEnabled = !limits.curfewEnabled; curfewDirty = true; }}
              class="relative inline-flex h-6 w-11 shrink-0 cursor-pointer rounded-full border-2 border-transparent transition-colors duration-200 ease-in-out focus:outline-none {limits.curfewEnabled ? 'bg-indigo-600 shadow-[0_0_12px_rgba(99,102,241,0.5)]' : 'bg-slate-700'}"
            >
              <span class="sr-only">Enable Curfew</span>
              <span
                class="pointer-events-none inline-block h-5 w-5 transform rounded-full bg-white shadow-sm ring-0 transition duration-200 ease-in-out {limits.curfewEnabled ? 'translate-x-5' : 'translate-x-0'}"
              ></span>
            </button>
          </div>
        </div>

        <div class="flex flex-col gap-4 text-xs">
          <!-- Real-Time Status Pill -->
          {#if !limits.curfewEnabled}
            <div class="p-3 rounded-xl bg-white/[0.03] border border-white/5 flex items-center gap-2.5 text-slate-400">
              <AlertCircle class="w-4 h-4 text-slate-500 shrink-0" />
              <span>Curfew is disabled. Protected devices have unrestricted 24/7 internet access.</span>
            </div>
          {:else if limits.curfewActive}
            <div class="p-3 rounded-xl bg-indigo-500/10 border border-indigo-500/30 flex items-center justify-between gap-2 text-indigo-200">
              <div class="flex items-center gap-2">
                <Moon class="w-4 h-4 text-indigo-400 shrink-0" />
                <span>
                  <strong>Curfew Active Now:</strong> Internet access blocked until {format12Hour(limits.endHour, limits.endMin)}.
                </span>
              </div>
              <span class="px-2 py-0.5 rounded-full bg-indigo-500/20 text-indigo-300 font-mono text-[10px] uppercase font-semibold shrink-0">
                Lock Active
              </span>
            </div>
          {:else}
            <div class="p-3 rounded-xl bg-emerald-500/10 border border-emerald-500/20 flex items-center justify-between gap-2 text-emerald-200">
              <div class="flex items-center gap-2">
                <Sun class="w-4 h-4 text-emerald-400 shrink-0" />
                <span>
                  <strong>Normal Access Active:</strong> Bedtime lockout starts tonight at {format12Hour(limits.startHour, limits.startMin)}.
                </span>
              </div>
              <span class="text-slate-400 text-[11px] font-mono shrink-0">
                Router: {limits.currentTime || '--:--'}
              </span>
            </div>
          {/if}

          <!-- 24-Hour Visual Day Timeline -->
          <div class="p-3.5 rounded-xl bg-white/[0.02] border border-white/5 space-y-2">
            <div class="flex items-center justify-between text-[11px] font-medium text-slate-400">
              <span class="flex items-center gap-1">
                <Clock class="w-3.5 h-3.5 text-indigo-400" />
                24-Hour Day Timeline
              </span>
              <span class="text-slate-300">
                Duration: <strong class="text-white">{getCurfewDurationStr(limits.startHour, limits.startMin, limits.endHour, limits.endMin)}</strong>
              </span>
            </div>

            <!-- Visual Bar -->
            <div class="relative w-full h-7 rounded-lg overflow-hidden bg-slate-800 flex border border-white/5">
              {#if timelineBlocks.isOvernight}
                <!-- Morning Curfew Block (00:00 -> End) -->
                <div
                  class="h-full bg-indigo-950/80 border-r border-indigo-500/30 flex items-center justify-center text-[10px] font-mono text-indigo-300 transition-all duration-300 shrink-0 min-w-0 overflow-hidden"
                  style="width: {timelineBlocks.morningCurfewPct}%"
                  title="Bedtime Lock (until {format12Hour(limits.endHour, limits.endMin)})"
                >
                  {#if timelineBlocks.morningCurfewPct > 12}
                    <Moon class="w-3 h-3 text-indigo-400 shrink-0" />
                  {/if}
                </div>

                <!-- Daytime Unlocked Access (End -> Start) -->
                <div
                  class="h-full bg-emerald-500/15 flex items-center justify-center text-[10px] font-medium text-emerald-300 transition-all duration-300 shrink-0 min-w-0 overflow-hidden"
                  style="width: {timelineBlocks.daytimePct}%"
                  title="Internet Open"
                >
                  <span class="flex items-center gap-1 truncate px-1">
                    <Sun class="w-3 h-3 text-emerald-400 shrink-0" />
                    {#if timelineBlocks.daytimePct > 35}
                      <span class="truncate">Open Access</span>
                    {/if}
                  </span>
                </div>

                <!-- Night Curfew Block (Start -> 24:00) -->
                <div
                  class="h-full bg-indigo-950/80 border-l border-indigo-500/30 flex items-center justify-center text-[10px] font-mono text-indigo-300 transition-all duration-300 shrink-0 min-w-0 overflow-hidden"
                  style="width: {timelineBlocks.nightCurfewPct}%"
                  title="Bedtime Lock (starts {format12Hour(limits.startHour, limits.startMin)})"
                >
                  {#if timelineBlocks.nightCurfewPct > 12}
                    <Moon class="w-3 h-3 text-indigo-400 shrink-0" />
                  {/if}
                </div>
              {:else}
                <!-- Same-Day Curfew Blocks -->
                <div class="h-full bg-emerald-500/15 shrink-0 min-w-0 overflow-hidden" style="width: {timelineBlocks.prePct}%"></div>
                <div class="h-full bg-indigo-950/80 border-x border-indigo-500/30 flex items-center justify-center text-indigo-300 shrink-0 min-w-0 overflow-hidden" style="width: {timelineBlocks.curfewPct}%">
                  <Moon class="w-3 h-3 text-indigo-400 shrink-0" />
                </div>
                <div class="h-full bg-emerald-500/15 shrink-0 min-w-0 overflow-hidden" style="width: {timelineBlocks.postPct}%"></div>
              {/if}

              <!-- Router Clock Pointer (if time is synced) -->
              {#if currentTimeMins !== null}
                <div
                  class="absolute top-0 bottom-0 w-0.5 bg-amber-400 shadow-[0_0_8px_#fbbf24] z-10 transition-all duration-500 pointer-events-none"
                  style="left: {Math.min(99, Math.max(1, currentTimeMins))}%"
                  title="Current Router Clock: {limits.currentTime}"
                >
                  <div class="absolute -top-1 -translate-x-1/2 w-2 h-2 rounded-full bg-amber-400"></div>
                </div>
              {/if}
            </div>

            <!-- Timeline Axis Labels -->
            <div class="flex justify-between text-[10px] font-mono text-slate-500 px-0.5">
              <span>12 AM</span>
              <span>6 AM</span>
              <span>12 PM</span>
              <span>6 PM</span>
              <span>12 AM</span>
            </div>
          </div>

          <!-- Time Pickers Row -->
          <div class="grid grid-cols-1 sm:grid-cols-2 gap-3">
            <!-- Bedtime (Start) Card -->
            <div class="p-3.5 rounded-xl bg-white/[0.02] border border-white/10 hover:border-indigo-500/30 transition-all space-y-2">
              <div class="flex items-center justify-between">
                <div class="flex items-center gap-1.5 text-indigo-400 font-semibold text-xs">
                  <Moon class="w-4 h-4" />
                  <span>Bedtime (Turn Off)</span>
                </div>
                <span class="text-[10px] uppercase tracking-wider text-slate-400 font-medium">Night</span>
              </div>

              <div class="flex items-center justify-between gap-2 pt-1">
                <div class="text-xl font-bold font-mono text-white tracking-tight">
                  {format12Hour(limits.startHour, limits.startMin)}
                </div>
                <input
                  type="time"
                  value={toTimeString(limits.startHour, limits.startMin)}
                  onchange={handleStartTimeChange}
                  class="bg-slate-900 text-white text-xs border border-white/10 rounded-lg px-2.5 py-1.5 focus:outline-none focus:border-indigo-500 cursor-pointer font-mono"
                />
              </div>
            </div>

            <!-- Wake-up (End) Card -->
            <div class="p-3.5 rounded-xl bg-white/[0.02] border border-white/10 hover:border-emerald-500/30 transition-all space-y-2">
              <div class="flex items-center justify-between">
                <div class="flex items-center gap-1.5 text-emerald-400 font-semibold text-xs">
                  <Sun class="w-4 h-4" />
                  <span>Wake-up (Restore)</span>
                </div>
                <span class="text-[10px] uppercase tracking-wider text-slate-400 font-medium">Morning</span>
              </div>

              <div class="flex items-center justify-between gap-2 pt-1">
                <div class="text-xl font-bold font-mono text-white tracking-tight">
                  {format12Hour(limits.endHour, limits.endMin)}
                </div>
                <input
                  type="time"
                  value={toTimeString(limits.endHour, limits.endMin)}
                  onchange={handleEndTimeChange}
                  class="bg-slate-900 text-white text-xs border border-white/10 rounded-lg px-2.5 py-1.5 focus:outline-none focus:border-emerald-500 cursor-pointer font-mono"
                />
              </div>
            </div>
          </div>

          <!-- Quick Preset Pills -->
          <div class="space-y-1.5 pt-1">
            <div class="text-[11px] font-semibold text-slate-400 uppercase tracking-wider">
              Quick Schedules:
            </div>
            <div class="grid grid-cols-2 gap-2">
              {#each PRESETS as p}
                <button
                  type="button"
                  onclick={() => applyPreset(p.sH, p.sM, p.eH, p.eM)}
                  class="p-2.5 rounded-xl bg-white/[0.03] hover:bg-white/[0.08] border border-white/5 hover:border-white/15 text-left transition-all group"
                >
                  <div class="font-medium text-slate-200 group-hover:text-white text-[11px]">{p.label}</div>
                  <div class="text-[10px] text-slate-400 font-mono mt-0.5">{p.desc}</div>
                </button>
              {/each}
            </div>
          </div>
        </div>
      </div>

      <!-- Card Action Footer -->
      <div class="flex items-center justify-between pt-4 mt-auto border-t border-white/10">
        <div class="text-[11px] text-slate-400">
          {#if curfewDirty}
            <span class="inline-flex items-center gap-1 text-amber-400 font-medium">
              <span class="w-1.5 h-1.5 rounded-full bg-amber-400 animate-ping"></span>
              Unsaved curfew changes
            </span>
          {:else}
            <span class="text-slate-500">Curfew schedule synced</span>
          {/if}
        </div>

        <button
          onclick={handleSaveCurfew}
          disabled={savingCurfew}
          class="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-xs font-semibold bg-indigo-600 hover:bg-indigo-500 text-white shadow-lg shadow-indigo-600/30 transition-all disabled:opacity-50"
        >
          {#if savingCurfew}
            <RefreshCw class="w-3.5 h-3.5 animate-spin" />
            <span>Saving...</span>
          {:else}
            <CheckCircle2 class="w-3.5 h-3.5" />
            <span>Save Curfew</span>
          {/if}
        </button>
      </div>
    </Card>

    <!-- Bandwidth Quota Configuration -->
    <Card class="h-full flex flex-col justify-between">
      <div class="flex-1 flex flex-col">
        <!-- Card Header with Badges -->
        <div class="flex items-center justify-between pb-4 mb-4 border-b border-white/10">
          <div class="flex items-center gap-3">
            <div class="w-9 h-9 rounded-xl bg-indigo-500/10 border border-indigo-500/20 flex items-center justify-center text-indigo-400">
              <HardDrive class="w-5 h-5" />
            </div>
            <div>
              <h2 class="text-base font-bold text-white tracking-tight">Per-Device Bandwidth Quotas</h2>
              <p class="text-[11px] text-slate-400">Independent daily data caps and hourly throttles per protected client</p>
            </div>
          </div>

          <div class="flex items-center gap-2">
            {#if limits.dailyQuotaMB > 0}
              <span class="px-2.5 py-1 rounded-full bg-indigo-500/10 border border-indigo-500/20 text-indigo-300 font-mono text-[11px] font-semibold">
                {formatMbHuman(limits.dailyQuotaMB)} / device
              </span>
            {:else}
              <span class="px-2.5 py-1 rounded-full bg-emerald-500/10 border border-emerald-500/20 text-emerald-300 font-mono text-[11px] font-semibold">
                Unlimited Daily
              </span>
            {/if}
          </div>
        </div>

        <div class="flex flex-col gap-4 text-xs">
          <!-- Fleet Consumption Context Banner -->
          <div class="p-3 rounded-xl bg-white/[0.02] border border-white/5 flex items-center justify-between text-xs">
            <div class="flex items-center gap-2">
              <div class="w-2 h-2 rounded-full bg-cyan-400 animate-pulse"></div>
              <span class="text-slate-300 font-medium">Guest Fleet Combined Traffic:</span>
              <span class="font-mono text-white font-bold">{dailyUsageMB} MB</span>
            </div>
            <span class="text-[11px] text-slate-500 font-mono hidden sm:inline">
              Resets at 00:00 midnight
            </span>
          </div>

          <!-- Live Protected Devices Allowance Monitor -->
          <div class="space-y-2">
            <div class="flex items-center justify-between">
              <div class="text-[11px] font-semibold uppercase tracking-wider text-slate-400 flex items-center gap-1.5">
                <Sliders class="w-3.5 h-3.5 text-indigo-400" />
                <span>Protected Clients Live Allowance</span>
                <span class="ml-1 px-1.5 py-0.2 rounded bg-white/10 text-slate-300 text-[10px] font-mono">
                  {protectedDevices.length}
                </span>
              </div>
              <a
                href="#/devices"
                class="text-[11px] text-indigo-400 hover:text-indigo-300 transition-colors inline-flex items-center gap-1"
              >
                <span>Manage Devices</span>
                <ExternalLink class="w-3 h-3" />
              </a>
            </div>

            {#if protectedDevices.length === 0}
              <div class="p-4 rounded-xl bg-white/[0.02] border border-white/5 text-center text-slate-400 text-xs py-5">
                <p>No devices are currently enrolled in Parental Controls or Quota Limits.</p>
                <p class="text-[11px] text-slate-500 mt-1">Enroll devices in the <a href="#/devices" class="text-indigo-400 underline">Device Inventory</a> to manage curfews and data allowances.</p>
              </div>
            {:else}
              <div class="space-y-2.5 max-h-52 overflow-y-auto pr-1">
                {#each protectedDevices as d (d.mac)}
                  {@const devUsageMb = Math.round(((d.dailyUsage || 0) / (1024 * 1024)) * 10) / 10}
                  {@const devPct = limits.dailyQuotaMB > 0 ? Math.min(100, Math.round((devUsageMb / limits.dailyQuotaMB) * 100)) : 0}
                  {@const isOver = limits.dailyQuotaMB > 0 && devUsageMb >= limits.dailyQuotaMB}
                  {@const isNear = limits.dailyQuotaMB > 0 && devPct >= 80 && !isOver}
                  {@const hasWaiver = (d.waiverSecRemaining || 0) > 0}

                  <div class="p-3 rounded-xl bg-white/[0.03] border {isOver && !hasWaiver ? 'border-rose-500/30 bg-rose-500/[0.03]' : 'border-white/5'} flex flex-col gap-2">
                    <div class="flex items-center justify-between">
                      <div class="flex items-center gap-2 min-w-0">
                        <div class="w-6 h-6 rounded-lg {isOver && !hasWaiver ? 'bg-rose-500/10 text-rose-400' : 'bg-white/5 text-slate-400'} flex items-center justify-center shrink-0">
                          {#if d.hostname && d.hostname.toLowerCase().includes('mac')}
                            <Laptop class="w-3.5 h-3.5" />
                          {:else}
                            <Smartphone class="w-3.5 h-3.5" />
                          {/if}
                        </div>
                        <div class="min-w-0">
                          <div class="font-semibold text-white truncate max-w-[140px] sm:max-w-[180px] text-xs leading-tight">
                            {d.hostname || 'Device ' + d.mac.slice(-5)}
                          </div>
                          <div class="text-[10px] text-slate-500 font-mono">
                            {d.ip}
                          </div>
                        </div>
                      </div>

                      <div class="flex items-center gap-2">
                        {#if hasWaiver}
                          <span class="px-2 py-0.5 rounded-full text-[10px] font-mono font-semibold bg-emerald-500/10 border border-emerald-500/20 text-emerald-400">
                            Waiver Active
                          </span>
                        {:else if isOver}
                          <span class="px-2 py-0.5 rounded-full text-[10px] font-semibold bg-rose-500/10 border border-rose-500/30 text-rose-400">
                            Quota Exceeded
                          </span>
                          <button
                            type="button"
                            onclick={() => grantQuickWaiver(d.mac, 1800)}
                            class="px-2 py-0.5 rounded-lg text-[10px] font-semibold bg-rose-500/20 hover:bg-rose-500/30 text-rose-300 border border-rose-500/30 transition-all"
                            title="Grant temporary 30-minute waiver"
                          >
                            +30m
                          </button>
                        {:else if isNear}
                          <span class="px-2 py-0.5 rounded-full text-[10px] font-semibold bg-amber-500/10 border border-amber-500/20 text-amber-400">
                            Near Limit
                          </span>
                        {:else if limits.curfewActive}
                          <span class="px-2 py-0.5 rounded-full text-[10px] font-medium text-indigo-300 bg-indigo-500/10 border border-indigo-500/20">
                            Curfew Active
                          </span>
                        {:else}
                          <span class="px-2 py-0.5 rounded-full text-[10px] font-mono text-slate-400 bg-white/5 border border-white/5">
                            {d.band || 'Guest'}
                          </span>
                        {/if}

                        <span class="font-mono text-xs font-semibold text-white shrink-0">
                          {devUsageMb} MB
                          {#if limits.dailyQuotaMB > 0}
                            <span class="text-slate-400 font-normal text-[11px]">/ {limits.dailyQuotaMB} MB</span>
                          {/if}
                        </span>
                      </div>
                    </div>

                    {#if limits.dailyQuotaMB > 0}
                      <ProgressBar
                        percent={devPct}
                        color={hasWaiver ? 'emerald' : (isOver ? 'rose' : (isNear ? 'amber' : 'indigo'))}
                      />
                    {/if}
                  </div>
                {/each}
              </div>
            {/if}
          </div>

          <!-- Quota Policy Limits Configuration Grid -->
          <div class="grid grid-cols-1 sm:grid-cols-2 gap-4 pt-1">
            <!-- Daily Limit Panel -->
            <div class="p-3.5 rounded-xl bg-white/[0.02] border border-white/5 space-y-3 flex flex-col justify-between">
              <div>
                <div class="flex items-center justify-between mb-2">
                  <label class="font-semibold text-slate-200 text-xs flex items-center gap-1.5" for="daily-quota">
                    <span>Daily Cap / Client</span>
                  </label>
                  <span class="text-[11px] font-mono text-indigo-300 font-medium px-2 py-0.5 rounded bg-indigo-500/10 border border-indigo-500/20">
                    {formatMbHuman(limits.dailyQuotaMB)}
                  </span>
                </div>

                <div class="relative flex items-center">
                  <input
                    id="daily-quota"
                    type="number"
                    min="0"
                    step="128"
                    bind:value={limits.dailyQuotaMB}
                    oninput={() => (quotaDirty = true)}
                    class="w-full bg-slate-900 border border-white/10 rounded-lg py-2 pl-3 pr-12 font-mono text-white text-sm focus:outline-none focus:border-indigo-500 transition-colors"
                    placeholder="0 for unlimited"
                  />
                  <span class="absolute right-3 text-xs text-slate-400 font-mono pointer-events-none">
                    MB
                  </span>
                </div>
              </div>

              <!-- Quick Presets -->
              <div class="space-y-1 pt-1">
                <div class="text-[10px] uppercase font-semibold tracking-wider text-slate-400">
                  Quick Presets:
                </div>
                <div class="grid grid-cols-2 gap-1.5">
                  {#each DAILY_QUOTA_PRESETS as p}
                    <button
                      type="button"
                      onclick={() => applyDailyQuotaPreset(p.value)}
                      class="py-1.5 px-2 rounded-lg text-center transition-all border {limits.dailyQuotaMB === p.value ? 'bg-indigo-600/30 border-indigo-500 text-white font-semibold' : 'bg-white/[0.03] hover:bg-white/[0.08] border-white/5 text-slate-300'}"
                    >
                      <div class="text-[11px] font-medium leading-tight">{p.label}</div>
                    </button>
                  {/each}
                </div>
              </div>
            </div>

            <!-- Hourly Limit Panel -->
            <div class="p-3.5 rounded-xl bg-white/[0.02] border border-white/5 space-y-3 flex flex-col justify-between">
              <div>
                <div class="flex items-center justify-between mb-2">
                  <label class="font-semibold text-slate-200 text-xs flex items-center gap-1.5" for="hourly-quota">
                    <span>Hourly Throttle</span>
                  </label>
                  <span class="text-[11px] font-mono text-indigo-300 font-medium px-2 py-0.5 rounded bg-indigo-500/10 border border-indigo-500/20">
                    {formatMbHuman(limits.hourlyQuotaMB)}
                  </span>
                </div>

                <div class="relative flex items-center">
                  <input
                    id="hourly-quota"
                    type="number"
                    min="0"
                    step="64"
                    bind:value={limits.hourlyQuotaMB}
                    oninput={() => (quotaDirty = true)}
                    class="w-full bg-slate-900 border border-white/10 rounded-lg py-2 pl-3 pr-12 font-mono text-white text-sm focus:outline-none focus:border-indigo-500 transition-colors"
                    placeholder="0 for uncapped"
                  />
                  <span class="absolute right-3 text-xs text-slate-400 font-mono pointer-events-none">
                    MB
                  </span>
                </div>
              </div>

              <!-- Quick Presets -->
              <div class="space-y-1 pt-1">
                <div class="text-[10px] uppercase font-semibold tracking-wider text-slate-400">
                  Quick Presets:
                </div>
                <div class="grid grid-cols-2 gap-1.5">
                  {#each HOURLY_QUOTA_PRESETS as p}
                    <button
                      type="button"
                      onclick={() => applyHourlyQuotaPreset(p.value)}
                      class="py-1.5 px-2 rounded-lg text-center transition-all border {limits.hourlyQuotaMB === p.value ? 'bg-indigo-600/30 border-indigo-500 text-white font-semibold' : 'bg-white/[0.03] hover:bg-white/[0.08] border-white/5 text-slate-300'}"
                    >
                      <div class="text-[11px] font-medium leading-tight">{p.label}</div>
                    </button>
                  {/each}
                </div>
              </div>
            </div>
          </div>
        </div>
      </div>

      <!-- Card Action Footer -->
      <div class="flex items-center justify-between pt-4 mt-auto border-t border-white/10">
        <div class="text-[11px] text-slate-400">
          {#if quotaDirty}
            <span class="inline-flex items-center gap-1.5 text-amber-400 font-medium">
              <span class="w-1.5 h-1.5 rounded-full bg-amber-400 animate-ping"></span>
              Unsaved quota changes
            </span>
          {:else}
            <span class="text-slate-500">Per-device bandwidth quotas synced</span>
          {/if}
        </div>

        <button
          onclick={handleSaveQuota}
          disabled={savingQuota}
          class="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-xs font-semibold bg-indigo-600 hover:bg-indigo-500 text-white shadow-lg shadow-indigo-600/30 transition-all disabled:opacity-50"
        >
          {#if savingQuota}
            <RefreshCw class="w-3.5 h-3.5 animate-spin" />
            <span>Saving...</span>
          {:else}
            <CheckCircle2 class="w-3.5 h-3.5" />
            <span>Save Quotas</span>
          {/if}
        </button>
      </div>
    </Card>
  </div>

  <!-- Functional Verification & Testing Helper Card -->
  <Card class="border border-indigo-500/20 bg-indigo-500/[0.03]">
    <div class="flex items-start gap-4">
      <div class="w-10 h-10 rounded-xl bg-indigo-500/10 border border-indigo-500/20 flex items-center justify-center text-indigo-400 shrink-0 shadow">
        <Shield class="w-5 h-5" />
      </div>
      <div class="flex-1 min-w-0 space-y-2.5">
        <div class="flex flex-col sm:flex-row sm:items-center justify-between gap-2">
          <h3 class="text-sm font-bold text-white tracking-tight flex items-center gap-2">
            <span>How to Test & Verify Curfew Functionality</span>
          </h3>
          <span class="px-2.5 py-1 rounded-full text-[11px] font-mono font-semibold bg-indigo-500/20 text-indigo-300 border border-indigo-500/30 w-fit">
            Router Clock: {limits.currentTime || '--:--'}
          </span>
        </div>
        <p class="text-xs text-slate-300 leading-relaxed">
          Curfew triggers automatically whenever the router's internal clock falls between Bedtime and Wake-up. 
          When active, stations marked as <strong>Enrolled (Protected)</strong> in the Device Inventory have their DNS lookups blocked by MicroRouter's DNS engine.
        </p>
        <div class="grid grid-cols-1 sm:grid-cols-3 gap-3 pt-1 text-xs">
          <div class="p-3 rounded-xl bg-white/[0.03] border border-white/5">
            <span class="text-[10px] font-bold text-indigo-400 uppercase tracking-wider block">1. Check Router Time</span>
            <span class="text-slate-400 text-[11px] mt-1 block">Current router time is <strong class="text-white font-mono">{limits.currentTime || '--:--'}</strong>. Set Bedtime earlier and Wake-up later.</span>
          </div>
          <div class="p-3 rounded-xl bg-white/[0.03] border border-white/5">
            <span class="text-[10px] font-bold text-indigo-400 uppercase tracking-wider block">2. Save & Observe</span>
            <span class="text-slate-400 text-[11px] mt-1 block">Click "Save Curfew". The top status pill turns into <strong class="text-purple-300 font-semibold">Lock Active</strong>.</span>
          </div>
          <div class="p-3 rounded-xl bg-white/[0.03] border border-white/5">
            <span class="text-[10px] font-bold text-indigo-400 uppercase tracking-wider block">3. Test Device & Waiver</span>
            <span class="text-slate-400 text-[11px] mt-1 block">Try opening a site on a protected phone. Grant a +15m waiver to verify immediate bypass.</span>
          </div>
        </div>
      </div>
    </div>
  </Card>

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
