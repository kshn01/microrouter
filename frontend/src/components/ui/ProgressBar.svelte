<script>
  let {
    label = '',
    valueText = '',
    percent = 0,
    color = 'indigo', // 'indigo' | 'emerald' | 'cyan'
  } = $props()

  const barColors = {
    indigo: 'bg-gradient-to-r from-indigo-500 to-purple-500 shadow-[0_0_12px_rgba(99,102,241,0.4)]',
    emerald: 'bg-gradient-to-r from-emerald-500 to-teal-400 shadow-[0_0_12px_rgba(16,185,129,0.4)]',
    cyan: 'bg-gradient-to-r from-cyan-500 to-blue-500 shadow-[0_0_12px_rgba(6,182,212,0.4)]',
  }
</script>

<div class="flex flex-col gap-2 my-2">
  <div class="flex items-center justify-between text-xs">
    <span class="font-medium text-slate-300">{label}</span>
    <span class="font-mono text-slate-400">{valueText || `${Math.round(percent)}%`}</span>
  </div>

  <div class="w-full h-2 rounded-full bg-slate-800/80 overflow-hidden border border-white/5 relative">
    <div
      class="h-full rounded-full transition-all duration-700 ease-out {barColors[color] || barColors.indigo}"
      style="width: {Math.min(100, Math.max(0, percent))}%;"
    ></div>
  </div>
</div>
