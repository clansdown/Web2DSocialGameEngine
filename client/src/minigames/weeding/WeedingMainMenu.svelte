<script lang="ts">
  /**
   * Campaign wrapper for Assarting (weeding). Renders the campaign map for the
   * player's current track (wildlands marche or great wildlands marche). Only
   * shown during campaign phases — ongoing mode uses WeedingOngoing instead.
   */

  import { playerGameState } from '../../lib/stores';
  import { getMiniGameConfigs } from '../../lib/game_state';
  import type { MiniGameConfig } from '../../lib/api';
  import WeedingCampaign from './WeedingCampaign.svelte';

  interface Props {
    onStartLevel: (levelId: number) => void;
    onBack: () => void;
  }

  let { onStartLevel, onBack }: Props = $props();

  let config = $state<MiniGameConfig | null>(null);
  let loading = $state(true);

  let isBaronTrack = $derived($playerGameState?.game_phase === 'baron_track');
  let gridSize = $derived(isBaronTrack ? (config?.baron_grid_size ?? 4) : (config?.grid_size ?? 3));
  let minLevelId = $derived(isBaronTrack ? 10 : 1);

  async function loadConfig(): Promise<void> {
    loading = true;
    try {
      const configs = await getMiniGameConfigs('weeding');
      config = configs['weeding'] ?? null;
    } catch {
      config = null;
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    loadConfig();
  });
</script>

<div class="container-fluid p-0">
  {#if loading}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Loading...</span>
      </div>
    </div>
  {:else}
    <WeedingCampaign
      {gridSize}
      {minLevelId}
      {onStartLevel}
      {onBack}
    />
  {/if}
</div>
