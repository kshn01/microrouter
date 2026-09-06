<script>
  import { onMount, onDestroy } from 'svelte'
  import {
    Shield,
    ShieldAlert,
    ShieldCheck,
    Globe,
    RefreshCw,
    Trash2,
    Search,
    CheckCircle2,
    XCircle,
    Sliders,
    Zap,
    Lock,
    Server,
    ExternalLink,
    Layers,
    Check,
    AlertTriangle
  } from '@lucide/svelte'
  import Card from '../components/ui/Card.svelte'
  import StatCard from '../components/ui/StatCard.svelte'
  import {
    apiGetDnsConfig,
    apiSetDnsConfig,
    apiGetDnsQueries,
    apiClearDnsQueries,
    apiGetRouterDns,
    apiSetRouterDns,
  } from '../services/api.service.js'
  import { showToast } from '../services/toast.service.js'

  // Component reactive states using Svelte 5 runes
  let loading = $state(true)
  let saving = $state(false)
  let clearing = $state(false)
  let syncingRouter = $state(false)
  let searchQuery = $state('')

  let config = $state({
    profile: 'cloudflare',
    primary: '1.1.1.1',
    secondary: '1.0.0.1',
    dohCanary: true,
    blockMeta: false,
    blockTiktok: false,
    totalQueries: 0,
    blockedQueries: 0,
  })

  let routerDns = $state({
    routerSynced: false,
    dhcpPrimary: '192.168.1.7',
    dhcpSecondary: '1.1.1.1',
    haMode: true,
    localDomain: 'portal.home',
  })

  let queries = $state([])
  let pollInterval = null

  const profiles = [
    {
      id: 'cloudflare',
      name: 'Cloudflare',
      desc: 'High speed Anycast & zero logging',
      primary: '1.1.1.1',
      secondary: '1.0.0.1',
      tag: 'Fastest',
      color: 'indigo',
    },
    {
      id: 'cloudflare_family',
      name: 'Cloudflare Family',
      desc: 'Automatic malware & adult site blocking',
      primary: '1.1.1.3',
      secondary: '1.0.0.3',
      tag: 'Family Safe',
      color: 'emerald',
    },
    {
      id: 'adguard',
      name: 'AdGuard DNS',
      desc: 'Network-wide ads and tracker blocking',
      primary: '94.140.14.14',
      secondary: '94.140.15.15',
      tag: 'AdBlock',
      color: 'purple',
    },
    {
      id: 'google',
      name: 'Google DNS',
      desc: 'Reliable global Anycast resolution',
      primary: '8.8.8.8',
      secondary: '8.8.4.4',
      tag: 'Global',
      color: 'cyan',
    },
    {
      id: 'custom',
      name: 'Custom Upstream',
      desc: 'Specify your own primary & secondary DNS',
      primary: '',
      secondary: '',
      tag: 'Manual',
      color: 'amber',
    },
  ]

  let filteredQueries = $derived(
    queries.filter((q) => {
      if (!searchQuery) return true
      const s = searchQuery.toLowerCase()
      return (
        (q.domain && q.domain.toLowerCase().includes(s)) ||
        (q.clientIp && q.clientIp.toLowerCase().includes(s)) ||
        (q.reason && q.reason.toLowerCase().includes(s))
      )
    })
  )

  let blockRatePercent = $derived(
    config.totalQueries > 0
      ? Math.round((config.blockedQueries / config.totalQueries) * 100)
      : 0
  )

  async function loadData() {
    try {
      const [cfgRes, qRes, rDnsRes] = await Promise.all([
        apiGetDnsConfig(),
        apiGetDnsQueries(),
        apiGetRouterDns(),
      ])
      if (cfgRes) {
        config = {
          ...config,
          ...cfgRes,
          primary: cfgRes.primary || cfgRes.primaryIp || config.primary,
          secondary: cfgRes.secondary || cfgRes.secondaryIp || config.secondary,
          totalQueries: cfgRes.totalQueries ?? cfgRes.total ?? config.totalQueries,
          blockedQueries: cfgRes.blockedQueries ?? cfgRes.blocked ?? config.blockedQueries,
        }
      }
      if (rDnsRes) {
        routerDns = {
          ...routerDns,
          ...rDnsRes,
          haMode: rDnsRes.haMode ?? rDnsRes.hybridDns ?? true,
        }
      }
      if (qRes) {
        if (qRes.queries && Array.isArray(qRes.queries)) {
          queries = qRes.queries.map(q => ({
            ...q,
            blocked: q.blocked !== undefined ? q.blocked : (q.status === 'BLOCKED' || q.status === 'CANARY'),
            reason: q.reason || q.status || 'RESOLVED'
          }))
        }
        const stats = qRes.stats || qRes
        if (stats.total !== undefined) config.totalQueries = stats.total
        if (stats.totalQueries !== undefined) config.totalQueries = stats.totalQueries
        if (stats.blocked !== undefined) config.blockedQueries = stats.blocked
        if (stats.blockedQueries !== undefined) config.blockedQueries = stats.blockedQueries
      }
    } catch (err) {
      console.error('Failed to load DNS data', err)
    } finally {
      loading = false
    }
  }

  async function saveDnsConfig() {
    saving = true
    try {
      const res = await apiSetDnsConfig({
        profile: config.profile,
        primary: config.primary,
        secondary: config.secondary,
        dohCanary: config.dohCanary,
        blockMeta: config.blockMeta,
        blockTiktok: config.blockTiktok,
        haMode: routerDns.haMode,
      })
      if (res && res.routerSynced !== undefined) {
        routerDns.routerSynced = res.routerSynced
      }
      showToast('DNS Shield & Router DHCP synchronized', 'success')
    } catch (err) {
      showToast('Failed to save DNS settings: ' + err.message, 'error')
    } finally {
      saving = false
    }
  }

  async function syncRouterDns() {
    syncingRouter = true
    try {
      const res = await apiSetRouterDns({
        profile: config.profile,
        primary: config.primary,
        secondary: config.secondary,
        haMode: routerDns.haMode,
      })
      if (res && res.routerSynced) {
        routerDns.routerSynced = true
        showToast('ZTE Gateway DHCP Option 6 & Local Domain synchronized!', 'success')
      } else {
        showToast('Sync request sent to ZTE Gateway', 'info')
      }
    } catch (err) {
      showToast('Router sync failed: ' + err.message, 'error')
    } finally {
      syncingRouter = false
    }
  }

  function selectProfile(p) {
    config.profile = p.id
    if (p.id !== 'custom') {
      config.primary = p.primary
      config.secondary = p.secondary
    }
  }

  async function handleClearLog() {
    clearing = true
    try {
      await apiClearDnsQueries()
      queries = []
      showToast('Network Spyglass query log cleared', 'info')
    } catch (err) {
      showToast('Error clearing log: ' + err.message, 'error')
    } finally {
      clearing = false
    }
  }

  function formatTime(ts) {
    if (!ts) return 'just now'
    const diff = Math.floor((Date.now() - ts) / 1000)
    if (diff < 5) return 'just now'
    if (diff < 60) return `${diff}s ago`
    if (diff < 3600) return `${Math.floor(diff / 60)}m ago`
    return new Date(ts).toLocaleTimeString()
  }

  onMount(() => {
    loadData()
    // Poll queries every 3 seconds for real-time traffic view
    pollInterval = setInterval(async () => {
      try {
        const qRes = await apiGetDnsQueries()
        if (qRes) {
          if (qRes.queries && Array.isArray(qRes.queries)) {
            queries = qRes.queries.map(q => ({
              ...q,
              blocked: q.blocked !== undefined ? q.blocked : (q.status === 'BLOCKED' || q.status === 'CANARY'),
              reason: q.reason || q.status || 'RESOLVED'
            }))
          }
          const stats = qRes.stats || qRes
          if (stats.total !== undefined) config.totalQueries = stats.total
          if (stats.totalQueries !== undefined) config.totalQueries = stats.totalQueries
          if (stats.blocked !== undefined) config.blockedQueries = stats.blocked
          if (stats.blockedQueries !== undefined) config.blockedQueries = stats.blockedQueries
        }
      } catch (e) {
        // silent poll error
      }
    }, 3000)
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
        <Shield class="w-8 h-8 text-indigo-400" />
        DNS Shield & Network Spyglass
      </h1>
      <p class="text-sm text-slate-400 mt-1">
        High-performance local UDP port 53 resolver, upstream switching, DoH sinkhole & live traffic inspector
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
      <button
        onclick={saveDnsConfig}
        disabled={saving}
        class="inline-flex items-center gap-2 px-4 py-2 rounded-xl text-xs font-semibold bg-indigo-600 hover:bg-indigo-500 text-white shadow-lg shadow-indigo-600/30 transition-colors disabled:opacity-50"
      >
        {#if saving}
          <RefreshCw class="w-3.5 h-3.5 animate-spin" />
          Applying...
        {:else}
          <CheckCircle2 class="w-3.5 h-3.5" />
          Apply Settings
        {/if}
      </button>
    </div>
  </div>

  <!-- Metric Counters -->
  <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
    <StatCard
      title="Total DNS Queries"
      value={String(config.totalQueries)}
      subtitle="Processed on UDP Port 53"
      color="indigo"
      icon={Globe}
    />
    <StatCard
      title="Threats & Ads Blocked"
      value={String(config.blockedQueries)}
      subtitle="{blockRatePercent}% of all queries filtered"
      color="emerald"
      icon={ShieldCheck}
    />
    <StatCard
      title="Active Upstream"
      value={config.profile.toUpperCase()}
      subtitle="{config.primary} · {config.secondary}"
      color="purple"
      icon={Zap}
    />
    <StatCard
      title="DoH Canary Status"
      value={config.dohCanary ? 'Active' : 'Disabled'}
      subtitle="Forces gateway DNS on mobile/browsers"
      color="cyan"
      icon={Lock}
    />
  </div>

  <!-- High-Availability Hybrid Router Integration Card -->
  <div class="relative overflow-hidden rounded-2xl border border-indigo-500/30 bg-gradient-to-br from-indigo-950/40 via-slate-900/60 to-slate-950/80 p-5 sm:p-6 backdrop-blur-xl shadow-xl shadow-indigo-950/20">
    <div class="flex flex-col sm:flex-row sm:items-center justify-between gap-4 pb-4 border-b border-white/10">
      <div class="flex items-center gap-3">
        <div class="p-2.5 rounded-xl bg-indigo-500/10 border border-indigo-500/20 text-indigo-400">
          <Server class="w-6 h-6" />
        </div>
        <div>
          <div class="flex items-center gap-2 flex-wrap">
            <h2 class="text-base font-bold text-white tracking-tight">High-Availability Hybrid Gateway DNS</h2>
            <span class="inline-flex items-center gap-1.5 px-2.5 py-0.5 rounded-full text-[11px] font-semibold {routerDns.routerSynced ? 'bg-emerald-500/15 text-emerald-300 border border-emerald-500/30' : 'bg-amber-500/15 text-amber-300 border border-amber-500/30'}">
              <span class="w-1.5 h-1.5 rounded-full {routerDns.routerSynced ? 'bg-emerald-400 animate-pulse' : 'bg-amber-400'}"></span>
              {routerDns.routerSynced ? 'Synced with ZTE Gateway' : 'Pending Gateway Push'}
            </span>
          </div>
          <p class="text-xs text-slate-400 mt-0.5">
            Automated DHCP Option 6 broadcast to all home devices with zero-downtime failover protection
          </p>
        </div>
      </div>

      <div class="flex items-center gap-2">
        <button
          onclick={syncRouterDns}
          disabled={syncingRouter}
          class="inline-flex items-center gap-2 px-3.5 py-2 rounded-xl text-xs font-semibold bg-indigo-500/20 hover:bg-indigo-500/30 text-indigo-200 border border-indigo-500/40 transition-all disabled:opacity-50 shadow-sm"
        >
          <RefreshCw class="w-3.5 h-3.5 {syncingRouter ? 'animate-spin' : ''}" />
          {syncingRouter ? 'Pushing to Router...' : 'Sync to Router DHCP'}
        </button>
      </div>
    </div>

    <!-- Active DNS Distribution Flow -->
    <div class="grid grid-cols-1 md:grid-cols-3 gap-4 my-4">
      <div class="p-4 rounded-xl bg-white/[0.02] border border-white/10 flex flex-col gap-1.5">
        <div class="flex items-center justify-between">
          <span class="text-xs font-semibold text-slate-400 uppercase tracking-wider">DHCP Option 6 (Primary)</span>
          <span class="px-2 py-0.5 rounded-md text-[10px] font-bold bg-indigo-500/20 text-indigo-300 border border-indigo-500/30">MicroRouter ESP32</span>
        </div>
        <div class="text-lg font-mono font-bold text-white">{routerDns.dhcpPrimary || '192.168.1.7'}</div>
        <p class="text-[11px] text-slate-400 leading-snug">All phones & PCs on Wi-Fi query this IP automatically for ad & threat filtering.</p>
      </div>

      <div class="p-4 rounded-xl bg-white/[0.02] border border-white/10 flex flex-col gap-1.5">
        <div class="flex items-center justify-between">
          <span class="text-xs font-semibold text-slate-400 uppercase tracking-wider">DHCP Option 6 (Secondary)</span>
          <span class="px-2 py-0.5 rounded-md text-[10px] font-bold {routerDns.haMode ? 'bg-emerald-500/20 text-emerald-300 border border-emerald-500/30' : 'bg-slate-700/50 text-slate-400 border border-slate-600'}">
            {routerDns.haMode ? 'Zero-Downtime Fallback' : 'Strict (None)'}
          </span>
        </div>
        <div class="text-lg font-mono font-bold {routerDns.haMode ? 'text-emerald-400' : 'text-slate-500'}">
          {routerDns.haMode ? (routerDns.dhcpSecondary || '1.1.1.1') : '0.0.0.0'}
        </div>
        <p class="text-[11px] text-slate-400 leading-snug">
          {routerDns.haMode ? 'Seamless failover: family never loses internet if ESP32 reboots or is unplugged.' : '100% of DNS queries must pass through ESP32 without upstream bypass.'}
        </p>
      </div>

      <div class="p-4 rounded-xl bg-white/[0.02] border border-white/10 flex flex-col gap-1.5">
        <div class="flex items-center justify-between">
          <span class="text-xs font-semibold text-slate-400 uppercase tracking-wider">Local Host Domain</span>
          <span class="px-2 py-0.5 rounded-md text-[10px] font-bold bg-purple-500/20 text-purple-300 border border-purple-500/30">Router DNS Table</span>
        </div>
        <div class="flex items-center gap-2">
          <a
            href="http://portal.home"
            target="_blank"
            rel="noreferrer"
            class="text-lg font-mono font-bold text-purple-300 hover:text-purple-200 transition-colors flex items-center gap-1.5"
          >
            portal.home
            <ExternalLink class="w-3.5 h-3.5 opacity-70" />
          </a>
        </div>
        <p class="text-[11px] text-slate-400 leading-snug">Accessible from any browser on your home network without typing an IP address.</p>
      </div>
    </div>

    <!-- Mode Selector Banner -->
    <div class="p-3.5 rounded-xl bg-white/[0.03] border border-white/10 flex flex-col sm:flex-row sm:items-center justify-between gap-3">
      <div class="flex items-center gap-2.5">
        <Layers class="w-4 h-4 text-indigo-400 flex-shrink-0" />
        <span class="text-xs font-semibold text-slate-300">Deployment Operational Mode:</span>
      </div>

      <div class="flex items-center gap-2">
        <button
          type="button"
          onclick={() => { routerDns.haMode = true; }}
          class="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold transition-all {routerDns.haMode ? 'bg-indigo-600 text-white shadow-md shadow-indigo-600/30' : 'bg-white/5 text-slate-400 hover:bg-white/10'}"
        >
          {#if routerDns.haMode}<Check class="w-3.5 h-3.5" />{/if}
          High Availability (Recommended)
        </button>

        <button
          type="button"
          onclick={() => { routerDns.haMode = false; }}
          class="flex items-center gap-1.5 px-3 py-1.5 rounded-lg text-xs font-semibold transition-all {!routerDns.haMode ? 'bg-purple-600 text-white shadow-md shadow-purple-600/30' : 'bg-white/5 text-slate-400 hover:bg-white/10'}"
        >
          {#if !routerDns.haMode}<Check class="w-3.5 h-3.5" />{/if}
          Strict Shield (Zero Leak)
        </button>
      </div>
    </div>
  </div>

  <!-- Upstream DNS Profiles & Filters Grid -->
  <div class="grid grid-cols-1 lg:grid-cols-3 gap-6">
    <!-- Upstream Selector (2 Cols) -->
    <div class="lg:col-span-2 flex flex-col gap-6">
      <Card>
        <div class="flex items-center justify-between pb-4 mb-4 border-b border-white/10">
          <div class="flex items-center gap-2">
            <Globe class="w-5 h-5 text-indigo-400" />
            <h2 class="text-base font-bold text-white tracking-tight">Upstream DNS Resolver</h2>
          </div>
          <span class="text-xs text-slate-400 font-medium">Select provider or set custom IP</span>
        </div>

        <div class="grid grid-cols-1 sm:grid-cols-2 gap-3 mb-4">
          {#each profiles as p}
            {@const isSelected = config.profile === p.id}
            <button
              type="button"
              onclick={() => selectProfile(p)}
              class="text-left p-4 rounded-xl border transition-all duration-200 relative {isSelected ? 'bg-indigo-600/15 border-indigo-500/60 ring-1 ring-indigo-500/50' : 'bg-white/[0.02] border-white/10 hover:bg-white/[0.05]'}"
            >
              <div class="flex items-center justify-between mb-1.5">
                <span class="font-bold text-sm text-white">{p.name}</span>
                <span class="text-[10px] font-semibold uppercase px-2 py-0.5 rounded-full {isSelected ? 'bg-indigo-500 text-white' : 'bg-white/10 text-slate-300'}">
                  {p.tag}
                </span>
              </div>
              <p class="text-xs text-slate-400 mb-2 leading-relaxed">{p.desc}</p>
              {#if p.id !== 'custom'}
                <div class="flex items-center gap-2 text-[11px] font-mono text-slate-400">
                  <span class="text-indigo-400">{p.primary}</span>
                  <span>·</span>
                  <span>{p.secondary}</span>
                </div>
              {/if}
            </button>
          {/each}
        </div>

        {#if config.profile === 'custom'}
          <div class="p-4 rounded-xl bg-white/[0.02] border border-white/10 grid grid-cols-1 sm:grid-cols-2 gap-4 mt-2">
            <div>
              <label class="block text-xs font-semibold text-slate-300 mb-1.5" for="primary-dns">
                Primary DNS IPv4
              </label>
              <input
                id="primary-dns"
                type="text"
                bind:value={config.primary}
                placeholder="1.1.1.1"
                class="w-full bg-slate-900 border border-white/10 rounded-lg px-3 py-2 text-xs font-mono text-white placeholder-slate-500 focus:outline-none focus:border-indigo-500"
              />
            </div>
            <div>
              <label class="block text-xs font-semibold text-slate-300 mb-1.5" for="secondary-dns">
                Secondary DNS IPv4
              </label>
              <input
                id="secondary-dns"
                type="text"
                bind:value={config.secondary}
                placeholder="1.0.0.1"
                class="w-full bg-slate-900 border border-white/10 rounded-lg px-3 py-2 text-xs font-mono text-white placeholder-slate-500 focus:outline-none focus:border-indigo-500"
              />
            </div>
          </div>
        {/if}
      </Card>
    </div>

    <!-- Security & Content Shields (1 Col) -->
    <div class="flex flex-col gap-6">
      <Card>
        <div class="flex items-center justify-between pb-4 mb-4 border-b border-white/10">
          <div class="flex items-center gap-2">
            <Sliders class="w-5 h-5 text-indigo-400" />
            <h2 class="text-base font-bold text-white tracking-tight">Content Shields</h2>
          </div>
        </div>

        <div class="flex flex-col gap-4">
          <!-- DoH Canary -->
          <div class="p-3.5 rounded-xl bg-white/[0.02] border border-white/10 flex items-center justify-between gap-3">
            <div>
              <span class="text-xs font-bold text-white block">DoH Canary Sinkhole</span>
              <span class="text-[11px] text-slate-400 block mt-0.5 leading-tight">
                Blocks <code class="text-indigo-400 font-mono">use-application-dns.net</code> to prevent browsers bypassing MicroRouter DNS
              </span>
            </div>
            <input
              type="checkbox"
              bind:checked={config.dohCanary}
              class="w-4 h-4 accent-indigo-500 rounded cursor-pointer"
            />
          </div>

          <!-- Meta Shield -->
          <div class="p-3.5 rounded-xl bg-white/[0.02] border border-white/10 flex items-center justify-between gap-3">
            <div>
              <span class="text-xs font-bold text-white block">Meta & Social Shield</span>
              <span class="text-[11px] text-slate-400 block mt-0.5 leading-tight">
                Sinkholes Facebook, Instagram, WhatsApp trackers and domain endpoints
              </span>
            </div>
            <input
              type="checkbox"
              bind:checked={config.blockMeta}
              class="w-4 h-4 accent-indigo-500 rounded cursor-pointer"
            />
          </div>

          <!-- TikTok Shield -->
          <div class="p-3.5 rounded-xl bg-white/[0.02] border border-white/10 flex items-center justify-between gap-3">
            <div>
              <span class="text-xs font-bold text-white block">TikTok & ByteDance Shield</span>
              <span class="text-[11px] text-slate-400 block mt-0.5 leading-tight">
                Filters TikTok and ByteDance algorithmic telemetry & video CDNs
              </span>
            </div>
            <input
              type="checkbox"
              bind:checked={config.blockTiktok}
              class="w-4 h-4 accent-indigo-500 rounded cursor-pointer"
            />
          </div>

          <div class="mt-2 p-3 rounded-xl bg-indigo-500/10 border border-indigo-500/20 text-[11px] text-indigo-300">
            <strong>Local Hostnames:</strong> Queries for <span class="font-mono text-white">microrouter.local</span> and <span class="font-mono text-white">portal.home</span> are resolved internally on zero-heap memory.
          </div>
        </div>
      </Card>
    </div>
  </div>

  <!-- Network Spyglass Query Log -->
  <Card>
    <div class="flex flex-col sm:flex-row sm:items-center justify-between gap-4 pb-4 mb-4 border-b border-white/10">
      <div class="flex items-center gap-3">
        <div class="w-8 h-8 rounded-lg bg-indigo-500/20 text-indigo-400 flex items-center justify-center">
          <Search class="w-4 h-4" />
        </div>
        <div>
          <h2 class="text-base font-bold text-white tracking-tight">Network Spyglass Query Log</h2>
          <span class="text-xs text-slate-400">Live FreeRTOS circular ring buffer inspector</span>
        </div>
      </div>

      <div class="flex items-center gap-3">
        <!-- Search Input -->
        <div class="relative">
          <Search class="w-3.5 h-3.5 absolute left-3 top-1/2 -translate-y-1/2 text-slate-500" />
          <input
            type="text"
            bind:value={searchQuery}
            placeholder="Filter domains or clients..."
            class="bg-slate-900 border border-white/10 rounded-xl pl-9 pr-3 py-1.5 text-xs text-white placeholder-slate-500 focus:outline-none focus:border-indigo-500 w-48 sm:w-64"
          />
        </div>

        <button
          onclick={handleClearLog}
          disabled={clearing || queries.length === 0}
          class="inline-flex items-center gap-1.5 px-3 py-1.5 rounded-xl text-xs font-semibold bg-rose-500/10 hover:bg-rose-500/20 text-rose-300 border border-rose-500/20 transition-colors disabled:opacity-40"
        >
          <Trash2 class="w-3.5 h-3.5" />
          Clear
        </button>
      </div>
    </div>

    <!-- Query Table -->
    <div class="overflow-x-auto">
      <table class="w-full text-left text-xs">
        <thead class="bg-white/[0.02] border-b border-white/5 text-slate-400 uppercase tracking-wider text-[10px]">
          <tr>
            <th class="py-2.5 px-4 font-semibold">Status</th>
            <th class="py-2.5 px-4 font-semibold">Requested Domain</th>
            <th class="py-2.5 px-4 font-semibold">Client IPv4</th>
            <th class="py-2.5 px-4 font-semibold">Policy / Action</th>
            <th class="py-2.5 px-4 font-semibold text-right">Time</th>
          </tr>
        </thead>
        <tbody class="divide-y divide-white/5 font-mono">
          {#if filteredQueries.length === 0}
            <tr>
              <td colspan="5" class="py-8 text-center text-slate-500 font-sans">
                {searchQuery ? 'No queries match your search query.' : 'Waiting for network DNS queries...'}
              </td>
            </tr>
          {:else}
            {#each filteredQueries as q}
              <tr class="hover:bg-white/[0.02] transition-colors">
                <td class="py-2.5 px-4">
                  {#if q.blocked}
                    <span class="inline-flex items-center gap-1 text-[11px] font-sans font-semibold text-rose-400 bg-rose-500/10 px-2 py-0.5 rounded-full border border-rose-500/20">
                      <XCircle class="w-3 h-3" />
                      Blocked
                    </span>
                  {:else}
                    <span class="inline-flex items-center gap-1 text-[11px] font-sans font-semibold text-emerald-400 bg-emerald-500/10 px-2 py-0.5 rounded-full border border-emerald-500/20">
                      <CheckCircle2 class="w-3 h-3" />
                      Allowed
                    </span>
                  {/if}
                </td>
                <td class="py-2.5 px-4 text-white font-medium">
                  {q.domain}
                </td>
                <td class="py-2.5 px-4 text-slate-400">
                  {q.clientIp}
                </td>
                <td class="py-2.5 px-4 font-sans text-xs">
                  <span class="px-2 py-0.5 rounded bg-white/5 text-slate-300 border border-white/10 text-[11px]">
                    {q.reason || (q.blocked ? 'FILTER_MATCH' : 'FORWARDED')}
                  </span>
                </td>
                <td class="py-2.5 px-4 font-sans text-right text-slate-400 text-[11px]">
                  {formatTime(q.timestamp)}
                </td>
              </tr>
            {/each}
          {/if}
        </tbody>
      </table>
    </div>
  </Card>
</div>
