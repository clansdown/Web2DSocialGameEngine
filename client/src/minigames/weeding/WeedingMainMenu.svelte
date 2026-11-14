<script lang="ts">
  /**
   * Campaign wrapper for Assarting (weeding). Renders the campaign map for the
   * player's current track (wildlands marche or great wildlands marche). Only
   * shown during campaign phases — ongoing mode uses WeedingOngoing instead.
   */

  import { playerGameState, language, currentCharacter } from '../../lib/stores';
  import { getMiniGameConfigs } from '../../lib/game_state';
  import { loadTexts } from '../../lib/text';
  import type { MiniGameConfig } from '../../lib/api';
  import WeedingCampaign from './WeedingCampaign.svelte';
  import MiniGameHomeButton from '../MiniGameHomeButton.svelte';

  interface Props {
    onStartLevel: (levelId: number) => void;
    onHome: () => void;
  }

  let { onStartLevel, onHome }: Props = $props();

  let config = $state<MiniGameConfig | null>(null);
  let loading = $state(true);
  let loadingLabel = $state('');

  let isBaronTrack = $derived($playerGameState?.game_phase === 'baron_track');
  let gridSize = $derived(isBaronTrack ? (config?.baron_grid_size ?? 4) : (config?.grid_size ?? 3));
  let minLevelId = $derived(isBaronTrack ? 10 : 1);
  let campaignId = $derived(isBaronTrack ? 'great_wildlands_marche' : 'wildlands_marche');

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

  $effect(() => {
    $language;
    $currentCharacter;
    let cancelled = false;
    loadTexts(['ui_loading'])
      .then((texts) => {
        if (!cancelled) loadingLabel = texts['ui_loading'] ?? '';
      })
      .catch(() => {
        if (!cancelled) loadingLabel = '';
      });
    return () => {
      cancelled = true;
    };
  });
</script>

<div class="container-fluid p-0">
  {#if loading}
    <div class="container py-4">
      <div class="mb-4">
        <MiniGameHomeButton {onHome} />
      </div>
      <div class="d-flex justify-content-center py-5">
        <div class="spinner-border" role="status">
          <span class="visually-hidden">{loadingLabel}</span>
        </div>
      </div>
    </div>
  {:else}
    <WeedingCampaign
      {campaignId}
      {gridSize}
      {minLevelId}
      {onStartLevel}
      {onHome}
    />
  {/if}
</div>
