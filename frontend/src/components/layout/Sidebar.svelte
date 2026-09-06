<script>
  import {
    Activity,
    Wifi,
    Cpu,
    ArrowUpRight,
    Radio,
    Server,
    Shield,
    Laptop,
    Moon,
    BarChart3,
  } from '@lucide/svelte'
  import { activePartition, wifiIp } from '../../stores/telemetry.store.js'
  import { connectionState } from '../../stores/connection.store.js'

  let { currentHash = '#/', mobileOpen = false, onCloseMobile = () => {} } = $props()

  const navItems = [
    { href: '#/', label: 'Dashboard', icon: Activity },
    { href: '#/dns', label: 'DNS Shield', icon: Shield },
    { href: '#/devices', label: 'Devices', icon: Laptop },
    { href: '#/insights', label: 'Guest Analytics', icon: BarChart3 },
    { href: '#/parental', label: 'Parental Controls', icon: Moon },
    { href: '#/wifi', label: 'WiFi Network', icon: Wifi },
    { href: '#/system', label: 'Diagnostics', icon: Cpu },
    { href: '#/ota', label: 'OTA Firmware', icon: ArrowUpRight },
  ]

  function isItemActive(href) {
    if (href === '#/' && (currentHash === '#/' || currentHash === '')) return true
    return currentHash.startsWith(href)
  }
</script>

<aside
  class="fixed top-0 bottom-0 left-0 z-40 w-64 glass-panel border-r border-white/10 flex flex-col transition-transform duration-300 ease-in-out lg:translate-x-0 {mobileOpen ? 'translate-x-0' : '-translate-x-full'}"
>
  <!-- Brand Header -->
  <div class="p-6 flex items-center gap-3 border-b border-white/10">
    <div class="w-10 h-10 rounded-xl bg-gradient-to-tr from-indigo-500 to-purple-500 flex items-center justify-center text-white shadow-[0_0_20px_rgba(99,102,241,0.5)]">
      <Radio class="w-5 h-5" />
    </div>
    <div class="flex flex-col">
      <span class="font-bold text-base tracking-tight text-white flex items-center gap-2">
        MicroRouter
        <span class="text-[10px] font-bold px-1.5 py-0.5 rounded bg-indigo-500/20 text-indigo-300 border border-indigo-500/30">S3</span>
      </span>
      <span class="text-[11px] text-slate-400 font-medium">Network Gateway</span>
    </div>
  </div>

  <!-- Navigation Menu -->
  <nav class="flex-1 px-3 py-6 flex flex-col gap-1.5 overflow-y-auto">
    {#each navItems as item}
      {@const IconComponent = item.icon}
      {@const active = isItemActive(item.href)}
      <a
        href={item.href}
        onclick={onCloseMobile}
        class="flex items-center gap-3 px-3.5 py-2.5 rounded-xl text-sm font-medium transition-all duration-200 relative group {active ? 'bg-indigo-600/20 text-white font-semibold' : 'text-slate-400 hover:text-white hover:bg-white/5'}"
      >
        {#if active}
          <div class="absolute left-0 top-2 bottom-2 w-1 rounded-r-full bg-indigo-500 shadow-[0_0_10px_#6366f1]"></div>
        {/if}
        <IconComponent class="w-4 h-4 {active ? 'text-indigo-400' : 'text-slate-400 group-hover:text-slate-300'}" />
        <span>{item.label}</span>
      </a>
    {/each}
  </nav>

  <!-- Sidebar Footer Details -->
  <div class="p-4 border-t border-white/10 bg-slate-950/40">
    <div class="rounded-xl p-3 bg-white/[0.03] border border-white/5 flex flex-col gap-2">
      <div class="flex items-center justify-between text-xs">
        <span class="text-slate-400">Partition</span>
        <span class="font-mono text-indigo-400 font-semibold uppercase">{$activePartition}</span>
      </div>
      <div class="flex items-center justify-between text-xs">
        <span class="text-slate-400">IP Address</span>
        <span class="font-mono text-slate-300 text-[11px]">{$wifiIp}</span>
      </div>
      {#if $connectionState.isSimulated}
        <div class="mt-1 pt-2 border-t border-white/5 flex items-center justify-between text-[11px]">
          <span class="text-amber-400 font-medium flex items-center gap-1">
            <span class="w-1.5 h-1.5 rounded-full bg-amber-400"></span>
            Sim Engine
          </span>
          <span class="text-slate-400">Offline Dev</span>
        </div>
      {/if}
    </div>
  </div>
</aside>
