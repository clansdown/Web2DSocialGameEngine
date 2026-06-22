<script lang="ts">
  import { playerGameState } from '../lib/stores';
  import { getMiniGameConfigs } from '../lib/game_state';
  import type { MiniGameConfig } from '../lib/api';

  interface Props {
    miniGame: string;
    onStartLevel: (levelId: number) => void;
    onBack: () => void;
  }

  let { miniGame, onStartLevel, onBack }: Props = $props();

  let config: MiniGameConfig | null = $state(null);
  let loading = $state(true);
  let loadError = $state<string | null>(null);

  let displayName = $derived(config?.display_name ?? miniGame);
  let gridSize = $derived(config?.grid_size ?? 3);

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
