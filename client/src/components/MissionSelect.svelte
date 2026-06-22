<script lang="ts">
  import { getMiniGameConfigs } from '../lib/game_state';
  import type { MiniGameConfig } from '../lib/api';

  interface Props {
    onSelect: (miniGame: string) => void;
  }

  let { onSelect }: Props = $props();

  let miniGames: Record<string, MiniGameConfig> | null = $state(null);
  let loading = $state(true);
  let loadError = $state<string | null>(null);

  /**
   * Loads mini-game configurations on mount to display available options.
   */
  async function loadMiniGameConfigs() {
    loading = true;
    loadError = null;
    try {
      miniGames = await getMiniGameConfigs();
    } catch (e) {
      loadError = e instanceof Error ? e.message : 'Failed to load mini-game config';
    } finally {
      loading = false;
    }
  }

  /**
   * Handles selecting a mini-game path to begin the campaign.
   * 
   * @param miniGame - The mini-game name to start
   */
  function handleSelect(miniGame: string) {
    onSelect(miniGame);
  }

  $effect(() => {
    loadMiniGameConfigs();
  });
</script>

<div class="container py-5">
  <div class="text-center mb-5">
    <h2>Choose Your Path</h2>
    <p class="text-muted">
      Complete a campaign to earn the right to build your base.
      Each campaign has 9 levels arranged in a 3&times;3 grid.
    </p>
  </div>

  {#if loading}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Loading...</span>
      </div>
    </div>
  {:else if loadError}
    <div class="alert alert-danger">{loadError}</div>
  {:else if miniGames}
    <div class="row g-4 justify-content-center">
      {#each Object.entries(miniGames) as [key, config]}
        <div class="col-md-5">
          <div
            class="card h-100 border-primary cursor-pointer"
            style="cursor: pointer;"
            onclick={() => handleSelect(key)}
            role="button"
            tabindex="0"
            onkeydown={(e) => { if (e.key === 'Enter') handleSelect(key); }}
          >
            <div class="card-body text-center p-5">
              <h3 class="card-title mb-3">{config.display_name}</h3>
              <p class="card-text text-muted mb-4">{config.description}</p>
              <div class="d-flex justify-content-center gap-4 mb-3">
                <div class="text-center">
                  <div class="fs-2 fw-bold">{config.grid_size * config.grid_size}</div>
                  <small class="text-muted">Levels</small>
                </div>
                <div class="text-center">
                  <div class="fs-2 fw-bold">{config.grid_size}&times;{config.grid_size}</div>
                  <small class="text-muted">Grid</small>
                </div>
              </div>
              <button class="btn btn-primary btn-lg px-5">
                Begin Campaign
              </button>
            </div>
          </div>
        </div>
      {/each}
    </div>
  {/if}
</div>
