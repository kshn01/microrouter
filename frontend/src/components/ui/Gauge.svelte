<script>
  let {
    percent = 0,
    label = '',
    sublabel = '',
    size = 140,
    color = 'indigo', // 'indigo' | 'cyan' | 'emerald'
  } = $props()

  const strokeWidth = 10
  let radius = $derived((size - strokeWidth) / 2)
  let circumference = $derived(2 * Math.PI * radius)

  let strokeOffset = $derived(
    circumference - (Math.min(100, Math.max(0, percent)) / 100) * circumference
  )

  const strokeColors = {
    indigo: 'url(#gauge-grad-indigo)',
    cyan: 'url(#gauge-grad-cyan)',
    emerald: 'url(#gauge-grad-emerald)',
  }
</script>

<div class="flex flex-col items-center justify-center p-3">
  <div class="relative flex items-center justify-center" style="width: {size}px; height: {size}px;">
    <svg width={size} height={size} class="transform -rotate-90">
      <defs>
        <linearGradient id="gauge-grad-indigo" x1="0%" y1="0%" x2="100%" y2="100%">
          <stop offset="0%" stop-color="#6366f1" />
          <stop offset="100%" stop-color="#a855f7" />
        </linearGradient>
        <linearGradient id="gauge-grad-cyan" x1="0%" y1="0%" x2="100%" y2="100%">
          <stop offset="0%" stop-color="#06b6d4" />
          <stop offset="100%" stop-color="#3b82f6" />
        </linearGradient>
        <linearGradient id="gauge-grad-emerald" x1="0%" y1="0%" x2="100%" y2="100%">
          <stop offset="0%" stop-color="#10b981" />
          <stop offset="100%" stop-color="#06b6d4" />
        </linearGradient>
      </defs>

      <!-- Background track -->
      <circle
        cx={size / 2}
        cy={size / 2}
        r={radius}
        fill="transparent"
        stroke="rgba(255, 255, 255, 0.07)"
        stroke-width={strokeWidth}
      />

      <!-- Animated progress track -->
      <circle
        cx={size / 2}
        cy={size / 2}
        r={radius}
        fill="transparent"
        stroke={strokeColors[color] || strokeColors.indigo}
        stroke-width={strokeWidth}
        stroke-linecap="round"
        stroke-dasharray={circumference}
        stroke-dashoffset={strokeOffset}
        style="transition: stroke-dashoffset 0.8s cubic-bezier(0.16, 1, 0.3, 1);"
      />
    </svg>

    <!-- Center Label -->
    <div class="absolute inset-0 flex flex-col items-center justify-center text-center">
      <span class="text-xl font-bold tracking-tight text-white font-mono">{label || `${Math.round(percent)}%`}</span>
      {#if sublabel}
        <span class="text-[10px] uppercase font-semibold text-slate-400 tracking-wider mt-0.5">{sublabel}</span>
      {/if}
    </div>
  </div>
</div>
