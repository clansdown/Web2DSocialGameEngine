<script lang="ts">
  /**
   * Campaign path selection screen shown when the player begins the game.
   * Shows cards for every available mini-game campaign (Wolf Warden / Assarter
   * of the Wildlands). All user-facing text comes from the text system.
   */

  import { getMiniGameConfigs } from '../lib/game_state';
  import { loadTexts } from '../lib/text';
  import type { MiniGameConfig } from '../lib/api';

  interface Props {
    onSelect: (miniGame: string) => void;
  }

  let { onSelect }: Props = $props();

  let miniGames: Record<string, MiniGameConfig> | null = $state(null);
  let loading = $state(true);
  let loadError = $state<string | null>(null);
  let texts = $state<Record<string, string>>({});

  const PAGE_TEXT_IDS: string[] = [
    'ui_mission_select_title',
    'ui_mission_select_intro',
    'ui_minigame_tower_defense_title',
    'ui_minigame_tower_defense_desc',
    'ui_minigame_weeding_title',
    'ui_minigame_weeding_desc',
    'ui_levels',
    'ui_grid',
    'ui_begin_campaign',
    'ui_loading',
  ];

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
    loadTexts(PAGE_TEXT_IDS).then(t => { texts = t; }).catch(() => { texts = {}; });
  });

  $effect(() => {
    loadMiniGameConfigs();
  });
</script>

<div class="container py-5">
  <div class="text-center mb-5">
    <h2>{texts['ui_mission_select_title'] || ''}</h2>
    <p class="text-muted">
      {texts['ui_mission_select_intro'] || ''}
    </p>
  </div>

  {#if loading}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">{texts['ui_loading'] || 'Loading...'}</span>
      </div>
    </div>
  {:else if loadError}
    <div class="alert alert-danger">{loadError}</div>
  {:else if miniGames}
    <div class="row g-4 justify-content-center">
      {#each Object.entries(miniGames) as [key, config]}
        {@const titleKey = `ui_minigame_${key}_title`}
        {@const descKey = `ui_minigame_${key}_desc`}
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
              <h3 class="card-title mb-3">{texts[titleKey] || config.display_name}</h3>
              <p class="card-text text-muted mb-4">{texts[descKey] || config.description}</p>
              <div class="d-flex justify-content-center gap-4 mb-3">
                <div class="text-center">
                  <div class="fs-2 fw-bold">{config.grid_size * config.grid_size}</div>
                  <small class="text-muted">{texts['ui_levels'] || 'Levels'}</small>
                </div>
                <div class="text-center">
                  <div class="fs-2 fw-bold">{config.grid_size}&times;{config.grid_size}</div>
                  <small class="text-muted">{texts['ui_grid'] || 'Grid'}</small>
                </div>
              </div>
              <button class="btn btn-primary btn-lg px-5">
                {texts['ui_begin_campaign'] || 'Begin Campaign'}
              </button>
            </div>
          </div>
        </div>
      {/each}
    </div>
  {/if}
</div>
