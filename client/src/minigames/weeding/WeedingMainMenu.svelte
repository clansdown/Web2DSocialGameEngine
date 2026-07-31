<script lang="ts">
  import { playerGameState } from '../../lib/stores';
  import { getMiniGameConfigs } from '../../lib/game_state';
  import WeedingCampaign from './WeedingCampaign.svelte';

  interface Props {
    onStartLevel: (levelId: number) => void;
    onBack: () => void;
  }

  let { onStartLevel, onBack }: Props = $props();

  let config: any = $state(null);
  let loading = $state(true);

  let gridSize = $derived.by(() => {
    if (!config) return 3;
    if ($playerGameState?.game_phase === 'baron_track') {
      return config.baron_grid_size ?? 4;
    }
    return config.grid_size ?? 3;
  });

  let minLevelId = $derived(
    $playerGameState?.game_phase === 'baron_track' ? 10 : 1
  );

  let isCampaign = $derived(
    $playerGameState?.game_phase === 'initial_mission' ||
    $playerGameState?.game_phase === 'baron_track'
  );

  async function loadConfig() {
    loading = true;
    try {
      const configs = await getMiniGameConfigs('weeding');
      config = configs['weeding'] ?? null;
    } catch {
      // ignore
    } finally {
      loading = false;
    }
  }

  function isLevelAvailable(levelId: number): boolean {
    if (!$playerGameState) return false;
    if (levelId === 1) return true;
    const prevLevel = $playerGameState.progress.find(
      p => p.mini_game === 'weeding' && p.level_id === levelId - 1
    );
    return prevLevel?.completed ?? false;
  }

  function isLevelCompleted(levelId: number): boolean {
    if (!$playerGameState) return false;
    const level = $playerGameState.progress.find(
      p => p.mini_game === 'weeding' && p.level_id === levelId
    );
    return level?.completed ?? false;
  }

  let allLevelsDone = $derived.by(() => {
    if (!$playerGameState || !config) return false;
    const totalLevels = config.grid_size * config.grid_size;
    if ($playerGameState.game_phase === 'baron_track') {
      const gs = config.baron_grid_size ?? 4;
      return $playerGameState.progress.filter(
        p => p.mini_game === 'weeding' && p.completed
      ).length >= gs * gs;
    }
    return $playerGameState.progress.filter(
      p => p.mini_game === 'weeding' && p.completed
    ).length >= totalLevels;
  });

  function handleLevelClick(levelId: number) {
    if (isLevelAvailable(levelId)) {
      onStartLevel(levelId);
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
  {:else if isCampaign}
    <WeedingCampaign
      {gridSize}
      {minLevelId}
      {onStartLevel}
      {onBack}
    />
    {#if allLevelsDone && $playerGameState?.game_phase === 'initial_mission'}
      <div class="text-center py-4">
        <button class="btn btn-success btn-lg" onclick={() => onStartLevel(minLevelId)}>
          Continue
        </button>
      </div>
    {/if}
  {:else}
    <!-- Level grid -->
    <div class="container py-5">
      <button class="btn btn-outline-secondary mb-4" onclick={onBack}>
        &larr; Back
      </button>

      <div class="text-center mb-4">
        <h2>Assarting</h2>
        <p class="text-muted">Clear the land for the kingdom.</p>
      </div>

      <div class="row g-3 justify-content-center">
        {#each Array(gridSize) as _, row}
          <div class="col-12">
            <div class="row g-3 justify-content-center">
              {#each Array(gridSize) as _, col}
                {@const levelId = row * gridSize + col + 1}
                {@const available = isLevelAvailable(levelId)}
                {@const completed = isLevelCompleted(levelId)}

                <div class="col-4 col-md-3 col-lg-2">
                  <div
                    class="card text-center {completed
                      ? 'border-success bg-success-subtle'
                      : available
                        ? 'border-primary cursor-pointer'
                        : 'border-secondary opacity-50'}"
                    style="cursor: {available ? 'pointer' : 'not-allowed'};"
                    role="button"
                    tabindex={available ? 0 : -1}
                    onclick={() => handleLevelClick(levelId)}
                    onkeydown={(e) => { if (e.key === 'Enter' && available) handleLevelClick(levelId); }}
                  >
                    <div class="card-body p-3">
                      <div class="fs-3 fw-bold">{levelId}</div>
                      <small class="text-muted">({row + 1},{col + 1})</small>
                      <div class="mt-2">
                        {#if completed}
                          <span class="badge bg-success">Done</span>
                        {:else if available}
                          <span class="badge bg-primary">Play</span>
                        {:else}
                          <span class="badge bg-secondary">Locked</span>
                        {/if}
                      </div>
                    </div>
                  </div>
                </div>
              {/each}
            </div>
          </div>
        {/each}
      </div>
    </div>
  {/if}
</div>
