<script>
  import { Clock, Wifi, HardDrive, ShieldCheck, Radio, Server, CheckCircle2, Zap } from '@lucide/svelte'
  import StatCard from '../components/ui/StatCard.svelte'
  import Gauge from '../components/ui/Gauge.svelte'
  import Card from '../components/ui/Card.svelte'
  import {
    telemetry,
    uptime,
    freeHeap,
    totalHeap,
    heapPercent,
    wifiRssi,
    wifiSignalPercent,
    wifiSsid,
    wifiIp,
    wifiConnected,
    wsClients,
    activePartition,
    fwVersion,
    fwValidated,
    bootCount,
  } from '../stores/telemetry.store.js'
  import { formatUptime, formatBytes } from '../types/models.js'
</script>

<div class="flex flex-col gap-8 animate-in fade-in duration-300">
  <!-- Page Title Header -->
  <div class="flex flex-col gap-1">
    <h1 class="text-2xl lg:text-3xl font-bold tracking-tight text-white">Network Overview</h1>
    <p class="text-sm text-slate-400">Real-time gateway telemetry and wireless router metrics</p>
  </div>

  <!-- Primary Stats Grid -->
  <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-4">
    <StatCard
      title="Uptime"
      value={formatUptime($uptime)}
      subtitle="Continuous system operation"
      color="cyan"
      icon={Clock}
    />
    <StatCard
      title="WiFi Signal"
      value="{$wifiRssi}"
      unit="dBm"
      subtitle="{$wifiSsid} · {$wifiSignalPercent}% quality"
      color="emerald"
      icon={Wifi}
    />
    <StatCard
      title="Free Memory"
      value={formatBytes($freeHeap)}
      subtitle="of {formatBytes($totalHeap)} total heap"
      color="purple"
      icon={HardDrive}
    />
    <StatCard
      title="Firmware"
      value="v{$fwVersion}"
      subtitle="Partition: {$activePartition} ({$fwValidated ? 'Validated' : 'Pending'})"
      color="indigo"
      icon={ShieldCheck}
    />
  </div>

  <!-- Dynamic Gauges Section -->
  <div class="grid grid-cols-1 md:grid-cols-3 gap-5">
    <Card hover={true} class="flex flex-col items-center justify-center text-center">
      <Gauge percent={$heapPercent} label="{$heapPercent}%" sublabel="Available" color="indigo" />
      <span class="text-sm font-semibold text-slate-200 mt-1">Free RAM Buffer</span>
      <span class="text-xs text-slate-400 mt-0.5">{formatBytes($freeHeap)} free of {formatBytes($totalHeap)}</span>
    </Card>

    <Card hover={true} class="flex flex-col items-center justify-center text-center">
      <Gauge percent={$wifiSignalPercent} label="{$wifiSignalPercent}%" sublabel="Quality" color="emerald" />
      <span class="text-sm font-semibold text-slate-200 mt-1">Wireless Link</span>
      <span class="text-xs text-slate-400 mt-0.5">{$wifiRssi} dBm · {$wifiConnected ? 'Connected' : 'Standalone'}</span>
    </Card>

    <Card hover={true} class="flex flex-col items-center justify-center text-center">
      <Gauge percent={Math.min(100, $wsClients * 25)} label={String($wsClients)} sublabel="Clients" color="cyan" />
      <span class="text-sm font-semibold text-slate-200 mt-1">WebSocket Sessions</span>
      <span class="text-xs text-slate-400 mt-0.5">Real-time bidirectional streams</span>
    </Card>
  </div>

  <!-- Gateway Properties Card -->
  <Card>
    <div class="flex items-center justify-between pb-4 mb-4 border-b border-white/10">
      <div class="flex items-center gap-2">
        <Server class="w-5 h-5 text-indigo-400" />
        <h2 class="text-base font-bold text-white tracking-tight">Active Gateway Parameters</h2>
      </div>
      <span class="text-xs font-mono text-emerald-400 bg-emerald-500/10 px-2.5 py-1 rounded-full border border-emerald-500/20">Online</span>
    </div>

    <div class="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-4 gap-6">
      <div class="flex flex-col gap-1">
        <span class="text-xs text-slate-400 font-medium">Gateway IP</span>
        <span class="font-mono text-sm font-semibold text-white">{$wifiIp}</span>
      </div>
      <div class="flex flex-col gap-1">
        <span class="text-xs text-slate-400 font-medium">Station MAC</span>
        <span class="font-mono text-sm font-semibold text-white">{$telemetry.wifiMac || '7C:DF:A1:04:88:EC'}</span>
      </div>
      <div class="flex flex-col gap-1">
        <span class="text-xs text-slate-400 font-medium">Channel</span>
        <span class="font-mono text-sm font-semibold text-white">CH {$telemetry.wifiChannel || 6} (2.4 GHz)</span>
      </div>
      <div class="flex flex-col gap-1">
        <span class="text-xs text-slate-400 font-medium">Boot Lifecycle</span>
        <span class="font-mono text-sm font-semibold text-white">Boot #{$bootCount}</span>
      </div>
    </div>
  </Card>
</div>
