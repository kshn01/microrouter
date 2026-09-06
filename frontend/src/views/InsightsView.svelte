<script>
  import { onMount } from 'svelte'
  import {
    BarChart3,
    Activity,
    Users,
    Clock,
    ShieldAlert,
    Trash2,
    RefreshCw,
    Search,
    Wifi,
    Laptop,
    Smartphone,
    Tv,
    Gamepad,
    Calendar,
    ArrowDownRight,
    ArrowUpRight,
    Database,
    CheckCircle2
  } from '@lucide/svelte'
  import { apiFetchGuestAnalytics, apiDeleteGuestAnalytics, apiClearGuestUsage } from '../services/api.service.js'
  import { showToast } from '../services/toast.service.js'
  import { lookupOUI } from '../utils/oui.js'

  let loading = $state(true)
  let refreshing = $state(false)
  let searchQuery = $state('')
  let records = $state([])
  let confirmResetOpen = $state(false)
  let deletingMac = $state(null)

  function formatBytes(bytes) {
    if (!bytes || bytes === 0) return '0 B'
    const k = 1024
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB']
    const i = Math.floor(Math.log(bytes) / Math.log(k))
    return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + ' ' + sizes[i]
  }

  function formatDuration(seconds) {
    if (!seconds || seconds <= 0) return '0m'
    const hrs = Math.floor(seconds / 3600)
    const mins = Math.floor((seconds % 3600) / 60)
    if (hrs === 0) return `${mins}m`
    return `${hrs}h ${mins}m`
  }

  function getDeviceIcon(name = '', vendor = '') {
    const s = (name + ' ' + vendor).toLowerCase()
    if (s.includes('phone') || s.includes('iphone') || s.includes('galaxy') || s.includes('pixel') || s.includes('android')) return Smartphone
    if (s.includes('tv') || s.includes('firetv') || s.includes('roku') || s.includes('chromecast') || s.includes('apple tv')) return Tv
    if (s.includes('switch') || s.includes('playstation') || s.includes('xbox') || s.includes('nintendo')) return Gamepad
    return Laptop
  }

  async function loadAnalytics() {
    try {
      refreshing = true
      const res = await apiFetchGuestAnalytics()
      records = res?.records || []
      // Sort highest 7-day usage first
      records.sort((a, b) => {
        const aTotal = (a.todayUsageBytes || 0) + (a.history || []).reduce((acc, h) => acc + (h.bytesUsed || 0), 0)
        const bTotal = (b.todayUsageBytes || 0) + (b.history || []).reduce((acc, h) => acc + (h.bytesUsed || 0), 0)
        return bTotal - aTotal
      })
    } catch (err) {
      showToast('Failed to load guest analytics', 'error')
    } finally {
      loading = false
      refreshing = false
    }
  }

  async function handleDeleteRecord(mac) {
    try {
      await apiDeleteGuestAnalytics(mac)
      records = records.filter(r => r.mac.toLowerCase() !== mac.toLowerCase())
      showToast('Record deleted from 7-day history', 'success')
      deletingMac = null
    } catch (err) {
      showToast('Failed to delete history record', 'error')
    }
  }

  async function handleClearAllUsage() {
    try {
      await apiClearGuestUsage()
      showToast('Today\'s usage data reset', 'success')
      confirmResetOpen = false
      await loadAnalytics()
    } catch (err) {
      showToast('Failed to reset usage data', 'error')
    }
  }

  // Derived KPIs
  let totalDataBytes = $derived(
    records.reduce((sum, r) => {
      const hist = (r.history || []).reduce((hSum, h) => hSum + (h.bytesUsed || 0), 0)
      return sum + (r.todayUsageBytes || 0) + hist
    }, 0)
  )

  let onlineCount = $derived(records.filter(r => r.currentlyOnline).length)

  let totalActiveSeconds = $derived(
    records.reduce((sum, r) => {
      const hist = (r.history || []).reduce((hSum, h) => hSum + (h.activeSecs || 0), 0)
      return sum + (r.todayActiveSecs || 0) + hist
    }, 0)
  )

  let totalBlockCount = $derived(
    records.reduce((sum, r) => {
      const hist = (r.history || []).reduce((hSum, h) => hSum + (h.quotaBlockCount || 0), 0)
      return sum + hist
    }, 0)
  )

  // 7-day Traffic Trend Bar Chart
  let trendData = $derived.by(() => {
    // 0 = Today, 1 = Yesterday (D-1), ..., 7 = D-7
    const dayTotals = [0, 0, 0, 0, 0, 0, 0, 0]
    const dayLabels = ['Today', 'D-1', 'D-2', 'D-3', 'D-4', 'D-5', 'D-6', 'D-7']

    records.forEach(r => {
      dayTotals[0] += (r.todayUsageBytes || 0)
      const hist = r.history || []
      hist.forEach((h, idx) => {
        if (idx < 7) {
          dayTotals[idx + 1] += (h.bytesUsed || 0)
        }
      })
    })

    const maxVal = Math.max(...dayTotals, 1024 * 1024)
    // Reverse so chronologically left-to-right (D-7 -> Today)
    const points = []
    for (let i = 7; i >= 0; i--) {
      points.push({
        label: dayLabels[i],
        bytes: dayTotals[i],
        pct: Math.max(8, Math.round((dayTotals[i] / maxVal) * 100)),
        isToday: i === 0
      })
    }
    return points
  })

  // Filtered devices
  let filteredRecords = $derived(
    records.filter(r => {
      if (!searchQuery.trim()) return true
      const q = searchQuery.toLowerCase()
      return (
        (r.hostname || '').toLowerCase().includes(q) ||
        (r.mac || '').toLowerCase().includes(q) ||
        (r.ip || '').toLowerCase().includes(q)
      )
    })
  )

  onMount(() => {
    loadAnalytics()
  })
</script>

<div class="space-y-6">
  <!-- Page Header -->
  <div class="flex flex-col sm:flex-row sm:items-center justify-between gap-4">
    <div>
      <h1 class="text-2xl font-bold tracking-tight text-white flex items-center gap-2.5">
        <BarChart3 class="w-6 h-6 text-indigo-400" />
        Guest Analytics & Insights
      </h1>
      <p class="text-sm text-slate-400 mt-1">
        7-day rolling traffic telemetry, quota violation auditing, and per-client usage matrices
      </p>
    </div>

    <div class="flex items-center gap-3">
      <button
        onclick={() => (confirmResetOpen = true)}
        class="glass-button text-xs font-semibold px-3 py-2 text-rose-400 border border-rose-500/20 hover:bg-rose-500/10 flex items-center gap-1.5 transition-colors"
      >
        <Trash2 class="w-3.5 h-3.5" />
        Reset Today's Usage
      </button>

      <button
        onclick={loadAnalytics}
        disabled={refreshing}
        class="glass-button text-xs font-semibold px-3.5 py-2 text-slate-300 border border-white/10 hover:bg-white/5 flex items-center gap-1.5"
      >
        <RefreshCw class="w-3.5 h-3.5 {refreshing ? 'animate-spin text-indigo-400' : ''}" />
        Refresh
      </button>
    </div>
  </div>

  <!-- KPI Summary Cards -->
  <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
    <!-- Total Data -->
    <div class="glass-card p-5 relative overflow-hidden group">
      <div class="flex items-center justify-between">
        <span class="text-xs font-semibold text-slate-400 uppercase tracking-wider">7-Day Total Data</span>
        <div class="w-9 h-9 rounded-xl bg-indigo-500/10 border border-indigo-500/20 flex items-center justify-center text-indigo-400">
          <Database class="w-4 h-4" />
        </div>
      </div>
      <div class="mt-3">
        <div class="text-2xl font-bold text-white font-mono tracking-tight">
          {formatBytes(totalDataBytes)}
        </div>
        <div class="text-xs text-indigo-400/80 font-medium mt-1 flex items-center gap-1">
          <Activity class="w-3 h-3" />
          Rolling 7-day cumulative
        </div>
      </div>
      <div class="absolute -right-6 -bottom-6 w-20 h-20 bg-indigo-500/5 rounded-full blur-xl group-hover:bg-indigo-500/10 transition-colors"></div>
    </div>

    <!-- Tracked Devices -->
    <div class="glass-card p-5 relative overflow-hidden group">
      <div class="flex items-center justify-between">
        <span class="text-xs font-semibold text-slate-400 uppercase tracking-wider">Tracked Clients</span>
        <div class="w-9 h-9 rounded-xl bg-emerald-500/10 border border-emerald-500/20 flex items-center justify-center text-emerald-400">
          <Users class="w-4 h-4" />
        </div>
      </div>
      <div class="mt-3">
        <div class="text-2xl font-bold text-white font-mono tracking-tight">
          {records.length}
        </div>
        <div class="text-xs text-emerald-400 font-medium mt-1 flex items-center gap-1.5">
          <span class="w-2 h-2 rounded-full bg-emerald-400 animate-pulse"></span>
          {onlineCount} Online now
        </div>
      </div>
      <div class="absolute -right-6 -bottom-6 w-20 h-20 bg-emerald-500/5 rounded-full blur-xl group-hover:bg-emerald-500/10 transition-colors"></div>
    </div>

    <!-- Active Time -->
    <div class="glass-card p-5 relative overflow-hidden group">
      <div class="flex items-center justify-between">
        <span class="text-xs font-semibold text-slate-400 uppercase tracking-wider">Connected Time</span>
        <div class="w-9 h-9 rounded-xl bg-cyan-500/10 border border-cyan-500/20 flex items-center justify-center text-cyan-400">
          <Clock class="w-4 h-4" />
        </div>
      </div>
      <div class="mt-3">
        <div class="text-2xl font-bold text-white font-mono tracking-tight">
          {formatDuration(totalActiveSeconds)}
        </div>
        <div class="text-xs text-cyan-400/80 font-medium mt-1">
          Active gateway duration
        </div>
      </div>
      <div class="absolute -right-6 -bottom-6 w-20 h-20 bg-cyan-500/5 rounded-full blur-xl group-hover:bg-cyan-500/10 transition-colors"></div>
    </div>

    <!-- Quota Blocks -->
    <div class="glass-card p-5 relative overflow-hidden group">
      <div class="flex items-center justify-between">
        <span class="text-xs font-semibold text-slate-400 uppercase tracking-wider">Curfew / Cap Hits</span>
        <div class="w-9 h-9 rounded-xl bg-amber-500/10 border border-amber-500/20 flex items-center justify-center text-amber-400">
          <ShieldAlert class="w-4 h-4" />
        </div>
      </div>
      <div class="mt-3">
        <div class="text-2xl font-bold text-white font-mono tracking-tight">
          {totalBlockCount}
        </div>
        <div class="text-xs text-amber-400/80 font-medium mt-1">
          Automated quota cap enforcements
        </div>
      </div>
      <div class="absolute -right-6 -bottom-6 w-20 h-20 bg-amber-500/5 rounded-full blur-xl group-hover:bg-amber-500/10 transition-colors"></div>
    </div>
  </div>

  <!-- Weekly Traffic Trend Chart -->
  <div class="glass-card p-6">
    <div class="flex items-center justify-between mb-6">
      <div>
        <h2 class="text-base font-bold text-white flex items-center gap-2">
          <Calendar class="w-4 h-4 text-indigo-400" />
          Weekly Bandwidth Consumption Trend
        </h2>
        <p class="text-xs text-slate-400 mt-0.5">Daily data transferred chronologically across all monitored clients</p>
      </div>
      <div class="text-xs font-mono text-slate-400">
        Today: <span class="font-bold text-indigo-400">{formatBytes(trendData[trendData.length - 1]?.bytes || 0)}</span>
      </div>
    </div>

    <!-- Bar Chart Grid -->
    <div class="h-48 flex items-end justify-between gap-2 sm:gap-4 pt-6 px-2 border-b border-white/5">
      {#each trendData as bar}
        <div class="flex-1 flex flex-col items-center gap-2 h-full justify-end group relative">
          <!-- Hover Tooltip -->
          <div class="absolute -top-10 opacity-0 group-hover:opacity-100 pointer-events-none transition-all duration-200 bg-slate-900 border border-white/10 px-2.5 py-1 rounded-lg text-[11px] font-mono text-white shadow-xl whitespace-nowrap z-20">
            {formatBytes(bar.bytes)}
          </div>

          <!-- Bar Visual -->
          <div
            class="w-full max-w-[48px] rounded-t-lg transition-all duration-500 {bar.isToday ? 'bg-gradient-to-t from-indigo-600 to-indigo-400 shadow-[0_0_15px_rgba(99,102,241,0.5)]' : 'bg-slate-700/60 hover:bg-slate-600/80'}"
            style="height: {bar.pct}%"
          ></div>

          <!-- Axis Label -->
          <span class="text-[11px] font-medium {bar.isToday ? 'text-indigo-400 font-bold' : 'text-slate-400'}">
            {bar.label}
          </span>
        </div>
      {/each}
    </div>
  </div>

  <!-- Client Consumption Breakdown Table -->
  <div class="glass-card overflow-hidden">
    <div class="p-5 border-b border-white/10 flex flex-col sm:flex-row sm:items-center justify-between gap-4">
      <div>
        <h2 class="text-base font-bold text-white flex items-center gap-2">
          <Users class="w-4 h-4 text-indigo-400" />
          Per-Device Cumulative Usage Matrix
        </h2>
        <p class="text-xs text-slate-400 mt-0.5">Ranked by total historical bandwidth volume</p>
      </div>

      <!-- Search Input -->
      <div class="relative w-full sm:w-64">
        <Search class="w-4 h-4 absolute left-3 top-1/2 -translate-y-1/2 text-slate-400" />
        <input
          type="text"
          placeholder="Filter by device or MAC..."
          bind:value={searchQuery}
          class="glass-input w-full pl-9 pr-4 py-1.5 text-xs text-slate-200 placeholder-slate-500"
        />
      </div>
    </div>

    <!-- Table Container -->
    <div class="overflow-x-auto">
      <table class="w-full text-left text-xs">
        <thead class="bg-white/[0.02] border-b border-white/5 text-slate-400 font-semibold uppercase tracking-wider">
          <tr>
            <th class="px-5 py-3.5">Device</th>
            <th class="px-5 py-3.5">Network Identity</th>
            <th class="px-5 py-3.5">Today's Data</th>
            <th class="px-5 py-3.5">7-Day Total</th>
            <th class="px-5 py-3.5">Daily Breakdown</th>
            <th class="px-5 py-3.5 text-right">Actions</th>
          </tr>
        </thead>
        <tbody class="divide-y divide-white/5">
          {#if loading}
            <tr>
              <td colspan="6" class="px-5 py-12 text-center text-slate-400">
                <RefreshCw class="w-5 h-5 animate-spin mx-auto text-indigo-400 mb-2" />
                Loading device analytics history...
              </td>
            </tr>
          {:else if filteredRecords.length === 0}
            <tr>
              <td colspan="6" class="px-5 py-12 text-center text-slate-400">
                No telemetry records found for your search query.
              </td>
            </tr>
          {:else}
            {#each filteredRecords as dev}
              {@const oui = lookupOUI(dev.mac)}
              {@const DevIcon = getDeviceIcon(dev.hostname, oui.vendor)}
              {@const totalDevBytes = (dev.todayUsageBytes || 0) + (dev.history || []).reduce((acc, h) => acc + (h.bytesUsed || 0), 0)}

              <tr class="hover:bg-white/[0.02] transition-colors group">
                <!-- Device Name & Avatar -->
                <td class="px-5 py-4">
                  <div class="flex items-center gap-3">
                    <div class="w-9 h-9 rounded-xl bg-slate-800 border border-white/10 flex items-center justify-center text-slate-300">
                      <DevIcon class="w-4 h-4 {dev.currentlyOnline ? 'text-indigo-400' : 'text-slate-500'}" />
                    </div>
                    <div>
                      <div class="font-semibold text-white flex items-center gap-1.5">
                        {dev.hostname || 'Guest Device'}
                        {#if dev.currentlyOnline}
                          <span class="w-1.5 h-1.5 rounded-full bg-emerald-400" title="Online now"></span>
                        {/if}
                      </div>
                      <div class="text-[10px] text-slate-400 font-mono">
                        {oui.vendor || 'Unknown Vendor'}
                      </div>
                    </div>
                  </div>
                </td>

                <!-- IP & MAC -->
                <td class="px-5 py-4 font-mono text-[11px]">
                  <div class="text-slate-200">{dev.ip || 'Offline'}</div>
                  <div class="text-slate-500 uppercase">{dev.mac}</div>
                </td>

                <!-- Today's Data -->
                <td class="px-5 py-4 font-mono font-medium text-slate-200">
                  <div>{formatBytes(dev.todayUsageBytes || 0)}</div>
                  <div class="text-[10px] text-slate-500 font-sans">
                    {formatDuration(dev.todayActiveSecs || 0)} active
                  </div>
                </td>

                <!-- 7-Day Total -->
                <td class="px-5 py-4 font-mono font-bold text-indigo-400">
                  {formatBytes(totalDevBytes)}
                </td>

                <!-- Mini 7-Day Pills -->
                <td class="px-5 py-4">
                  <div class="flex items-center gap-1">
                    {#each (dev.history || []).slice(0, 7) as slot, sIdx}
                      <div
                        class="w-5 h-7 rounded bg-slate-800/80 border border-white/5 flex flex-col justify-end p-0.5"
                        title="D-{sIdx + 1}: {formatBytes(slot.bytesUsed || 0)}"
                      >
                        <div
                          class="w-full rounded-sm bg-indigo-500/70"
                          style="height: {Math.min(100, Math.max(10, Math.round(((slot.bytesUsed || 0) / (totalDevBytes || 1)) * 100)))}%"
                        ></div>
                      </div>
                    {/each}
                  </div>
                </td>

                <!-- Actions -->
                <td class="px-5 py-4 text-right">
                  <button
                    onclick={() => handleDeleteRecord(dev.mac)}
                    class="p-1.5 rounded-lg text-slate-400 hover:text-rose-400 hover:bg-rose-500/10 transition-colors"
                    title="Delete historical analytics for this MAC"
                  >
                    <Trash2 class="w-4 h-4" />
                  </button>
                </td>
              </tr>
            {/each}
          {/if}
        </tbody>
      </table>
    </div>
  </div>
</div>

<!-- Reset Usage Confirmation Modal -->
{#if confirmResetOpen}
  <div class="fixed inset-0 z-50 flex items-center justify-center p-4 bg-black/60 backdrop-blur-sm animate-in fade-in duration-200">
    <div class="glass-card max-w-md w-full p-6 border border-rose-500/30 shadow-2xl">
      <div class="flex items-center gap-3 text-rose-400 mb-3">
        <ShieldAlert class="w-6 h-6" />
        <h3 class="text-lg font-bold text-white">Reset Today's Usage?</h3>
      </div>
      <p class="text-sm text-slate-300 leading-relaxed">
        This will clear today's recorded bandwidth counters and active connected timers for all devices.
        Historical 7-day records will remain untouched.
      </p>
      <div class="mt-6 flex items-center justify-end gap-3">
        <button
          onclick={() => (confirmResetOpen = false)}
          class="glass-button px-4 py-2 text-xs font-semibold text-slate-300 border border-white/10 hover:bg-white/5"
        >
          Cancel
        </button>
        <button
          onclick={handleClearAllUsage}
          class="px-4 py-2 text-xs font-semibold rounded-xl bg-rose-600 hover:bg-rose-500 text-white shadow-lg shadow-rose-600/30 transition-all"
        >
          Confirm Reset
        </button>
      </div>
    </div>
  </div>
{/if}
