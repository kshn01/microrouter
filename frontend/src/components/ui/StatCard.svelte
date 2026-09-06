<script>
  let {
    title = '',
    value = '',
    unit = '',
    subtitle = '',
    color = 'indigo', // 'indigo' | 'cyan' | 'emerald' | 'amber' | 'purple'
    icon: IconComponent = null,
  } = $props()

  const colorStyles = {
    indigo: {
      bg: 'bg-indigo-500/10 text-indigo-400 border-indigo-500/20',
      glow: 'from-indigo-500/10',
    },
    cyan: {
      bg: 'bg-cyan-500/10 text-cyan-400 border-cyan-500/20',
      glow: 'from-cyan-500/10',
    },
    emerald: {
      bg: 'bg-emerald-500/10 text-emerald-400 border-emerald-500/20',
      glow: 'from-emerald-500/10',
    },
    amber: {
      bg: 'bg-amber-500/10 text-amber-400 border-amber-500/20',
      glow: 'from-amber-500/10',
    },
    purple: {
      bg: 'bg-purple-500/10 text-purple-400 border-purple-500/20',
      glow: 'from-purple-500/10',
    },
  }

  let style = $derived(colorStyles[color] || colorStyles.indigo)
</script>

<div class="relative overflow-hidden glass-card glass-panel-hover rounded-2xl p-5 border border-white/10 flex flex-col justify-between group">
  <!-- Subtle gradient highlight line at top -->
  <div class="absolute top-0 inset-x-0 h-0.5 bg-gradient-to-r {style.glow} via-white/20 to-transparent"></div>

  <div class="flex items-center justify-between mb-3">
    <span class="text-xs font-semibold uppercase tracking-wider text-slate-400">{title}</span>
    {#if IconComponent}
      <div class="w-9 h-9 rounded-xl flex items-center justify-center border {style.bg} transition-transform group-hover:scale-110 duration-200">
        <IconComponent class="w-4 h-4" />
      </div>
    {/if}
  </div>

  <div class="flex items-baseline gap-1.5 my-1">
    <span class="text-2xl lg:text-3xl font-bold tracking-tight text-white">{value}</span>
    {#if unit}
      <span class="text-xs font-medium text-slate-400">{unit}</span>
    {/if}
  </div>

  {#if subtitle}
    <div class="text-xs text-slate-400 mt-2 flex items-center gap-1.5">
      {subtitle}
    </div>
  {/if}
</div>
