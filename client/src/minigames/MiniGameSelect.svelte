<script lang="ts">
  import { playerGameState, language } from '../lib/stores';
  import { getMiniGameConfigs } from '../lib/game_state';
  import { getTextsRequest } from '../lib/api';
  import type { MiniGameConfig } from '../lib/api';
  import GameText from '../components/GameText.svelte';

  interface Props {
    miniGame: string;
    gridSize?: number;
    mapImage?: string;
    title?: string;
    infoTextId?: string;
    onStartLevel: (levelId: number) => void;
    onBack: () => void;
  }

  let { miniGame, gridSize: propGridSize, mapImage, title, infoTextId, onStartLevel, onBack }: Props = $props();

  let config = $state<MiniGameConfig | null>(null);
  let loading = $state(true);
  let loadError = $state<string | null>(null);
  let infoText = $state('');

  let displayName = $derived(title ?? config?.display_name ?? miniGame);
  let configGridSize = $derived(config?.grid_size ?? 3);
  let gridSize = $derived(propGridSize ?? configGridSize);

  /**
   * Loads the mini-game configuration to display the grid.
   */
  async function loadConfig() {
    loading = true;
    loadError = null;
    try {
      const configs = await getMiniGameConfigs(miniGame);
      config = configs[miniGame] ?? null;
    } catch (e) {
      loadError = e instanceof Error ? e.message : 'Failed to load config';
    } finally {
      loading = false;
    }
  }

  /**
   * Checks whether a given level is available for the player to attempt.
   * Levels are sequential: level N is available if level N-1 is completed (or N=1).
   * 
   * @param levelId - The level ID to check
   * @returns boolean - true if the level can be played
   */
  function isLevelAvailable(levelId: number): boolean {
    if (!$playerGameState) return false;

    if (levelId === 1) return true;

    const prevLevel = $playerGameState.progress.find(
      p => p.mini_game === miniGame && p.level_id === levelId - 1
    );
    return prevLevel?.completed ?? false;
  }

  /**
   * Checks whether a given level has been completed.
   * 
   * @param levelId - The level ID to check
   * @returns boolean - true if the level is completed
   */
  function isLevelCompleted(levelId: number): boolean {
    if (!$playerGameState) return false;

    const level = $playerGameState.progress.find(
      p => p.mini_game === miniGame && p.level_id === levelId
    );
    return level?.completed ?? false;
  }

  function getLevelProgress(levelId: number): string {
    if (!$playerGameState) return 'Locked';
    const level = $playerGameState.progress.find(
      p => p.mini_game === miniGame && p.level_id === levelId
    );
    if (!level) return isLevelAvailable(levelId) ? 'Available' : 'Locked';
    if (level.completed) return `Completed (Best: ${level.best_score})`;
    return level.times_played > 0 ? 'Failed' : 'Available';
  }

  /**
   * Handles clicking a level tile in the grid.
   * Only starts if the level is available.
   * 
   * @param levelId - The level ID clicked
   */
  function handleLevelClick(levelId: number) {
    if (isLevelAvailable(levelId)) {
      onStartLevel(levelId);
    }
  }

  $effect(() => {
    loadConfig();
  });

  $effect(() => {
    if (infoTextId) {
      getTextsRequest($language, [infoTextId]).then((texts) => {
        infoText = texts[infoTextId] ?? '';
      }).catch(() => {
        infoText = '';
      });
    }
  });
</script>

<div class="container py-5">
  <div class="d-flex align-items-center mb-4">
    <button class="btn btn-outline-secondary me-3" onclick={onBack}>
      &larr; Back
    </button>
    <div>
      <h2 class="mb-0">{displayName}</h2>
      <small class="text-muted">Campaign Progress</small>
    </div>
  </div>

  {#if loading}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Loading...</span>
      </div>
    </div>
  {:else if loadError}
    <div class="alert alert-danger">{loadError}</div>
  {:else if mapImage}
    <div style="position: relative; max-width: 800px; margin: 0 auto;">
      <img src={mapImage} alt="Campaign Map" style="width: 100%; height: auto; display: block; border-radius: 8px;" />
      <div
        style="position: absolute; inset: 0; display: grid; grid-template-columns: repeat({gridSize}, 1fr); grid-template-rows: repeat({gridSize}, 1fr);"
      >
        {#each Array(gridSize * gridSize) as _, i}
          {@const levelId = i + 1}
          {@const conquered = isLevelCompleted(levelId)}
          <div
            role="button"
            tabindex="0"
            style="cursor: pointer; position: relative; transition: background 0.2s; border-radius: 4px; margin: 2px; {conquered ? '' : 'background: rgba(0, 0, 0, 0.35);'}"
            onmouseenter={(e) => { (e.currentTarget as HTMLElement).style.background = conquered ? 'rgba(255, 255, 255, 0.1)' : 'rgba(0, 0, 0, 0.2)'; }}
            onmouseleave={(e) => { (e.currentTarget as HTMLElement).style.background = conquered ? '' : 'rgba(0, 0, 0, 0.35)'; }}
            onclick={() => onStartLevel(levelId)}
            onkeydown={(e) => { if (e.key === 'Enter') onStartLevel(levelId); }}
          >
            {#if conquered}
              <div style="position: absolute; top: 4px; right: 4px; color: #22c55e; font-size: 18px; text-shadow: 0 1px 3px rgba(0,0,0,0.5);">&#10003;</div>
            {/if}
          </div>
        {/each}
      </div>
    </div>
    {#if infoText}
      <div class="mx-auto mt-4" style="max-width: 800px;">
        <GameText text={infoText} />
      </div>
    {/if}
  {:else}
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
                    <div class="mt-1">
                      <small class="text-muted">{getLevelProgress(levelId)}</small>
                    </div>
                  </div>
                </div>
              </div>
            {/each}
          </div>
        </div>
      {/each}
    </div>
  {/if}
</div>
