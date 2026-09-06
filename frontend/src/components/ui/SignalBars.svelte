<script>
  import { rssiToPercent } from '../../types/models.js'

  let { rssi = -90 } = $props()

  let percent = $derived(rssiToPercent(rssi))

  function getBarActive(barIndex) {
    // 4 bars: 25%, 50%, 75%, 90%
    const thresholds = [15, 40, 65, 85]
    return percent >= thresholds[barIndex]
  }
</script>

<div class="flex items-end gap-1 h-5 px-1" title="{rssi} dBm ({percent}%)">
  {#each [0, 1, 2, 3] as i}
    <div
      class="w-1.5 rounded-full transition-all duration-300 {getBarActive(i) ? 'bg-emerald-400 shadow-[0_0_8px_rgba(52,211,153,0.5)]' : 'bg-slate-700/60'}"
      style="height: {(i + 1) * 25}%;"
    ></div>
  {/each}
</div>
