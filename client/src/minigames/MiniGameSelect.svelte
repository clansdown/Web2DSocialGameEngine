<script lang="ts">
  /**
   * Routes the Tasks page based on the player's game phase.
   *
   * During campaign phases (initial_mission, baron_track) the player is only
   * shown the mini-game on their track, rendered as its campaign map.
   * In ongoing phases (land_patent, baron_right, sandbox) the player is shown
   * cards for every available mini-game; selecting one opens that game's
   * ongoing-mode setup component (difficulty + size + silver reward preview).
   *
   * All user-facing text comes from the text system via loadTexts().
   */

  import { playerGameState, currentCharacter } from '../lib/stores';
  import { getMiniGameConfigs } from '../lib/game_state';
  import { loadTexts } from '../lib/text';
  import type { MiniGameConfig } from '../lib/api';
  import TowerDefenseMainMenu from './tower_defense/TowerDefenseMainMenu.svelte';
  import WeedingMainMenu from './weeding/WeedingMainMenu.svelte';
  import TowerDefenseOngoing from './tower_defense/TowerDefenseOngoing.svelte';
  import WeedingOngoing from './weeding/WeedingOngoing.svelte';

  const ARCHETYPE_TO_GAME: Record<string, string> = {
    wolf_warden: 'tower_defense',
    assarter: 'weeding',
  };

  const CAMPAIGN_PHASES: string[] = ['initial_mission', 'baron_track'];

  interface Props {
    onStartLevel: (gameId: string, levelId: number) => void;
    onBack: () => void;
  }

  let { onStartLevel, onBack }: Props = $props();

  let allConfigs = $state<Record<string, MiniGameConfig> | null>(null);
  let loading = $state(true);
  let loadError = $state<string | null>(null);
  let selectedGame = $state<string | null>(null);

  const PAGE_TEXT_IDS: string[] = [
    'ui_minigame_select_title',
    'ui_minigame_tower_defense_title',
    'ui_minigame_tower_defense_desc',
    'ui_minigame_weeding_title',
    'ui_minigame_weeding_desc',
    'ui_minigame_no_track',
    'ui_minigame_load_error',
    'ui_back_button',
    'ui_play_button',
    'ui_loading',
  ];
  let texts = $state<Record<string, string>>({});

  let phase = $derived($playerGameState?.game_phase ?? '');
  let inCampaign = $derived(CAMPAIGN_PHASES.includes(phase));
  let archetypeGame = $derived(
    $currentCharacter?.archetype ? (ARCHETYPE_TO_GAME[$currentCharacter.archetype] ?? null) : null
  );

  async function loadConfigs(): Promise<void> {
    loading = true;
    loadError = null;
    try {
      allConfigs = await getMiniGameConfigs();
    } catch (e) {
      loadError = e instanceof Error ? e.message : 'Failed to load configs';
    } finally {
      loading = false;
    }
  }

  function goBack(): void {
    if (selectedGame) {
      selectedGame = null;
    } else {
      onBack();
    }
  }

  $effect(() => {
    loadTexts(PAGE_TEXT_IDS).then(t => { texts = t; }).catch(() => { texts = {}; });
  });

  $effect(() => {
    if (!inCampaign && !allConfigs) {
      loadConfigs();
    }
  });
</script>

{#if inCampaign && archetypeGame === 'tower_defense'}
  <TowerDefenseMainMenu
    onStartLevel={(levelId: number) => onStartLevel('tower_defense', levelId)}
    onBack={goBack}
  />
{:else if inCampaign && archetypeGame === 'weeding'}
  <WeedingMainMenu
    onStartLevel={(levelId: number) => onStartLevel('weeding', levelId)}
    onBack={goBack}
  />
{:else if inCampaign}
  <div class="container py-5">
    <button class="btn btn-outline-secondary mb-4" onclick={goBack}>
      &larr; {texts['ui_back_button'] || 'Back'}
    </button>
    <div class="alert alert-warning">{texts['ui_minigame_no_track'] || ''}</div>
  </div>
{:else if selectedGame === 'tower_defense'}
  <TowerDefenseOngoing
    {onStartLevel}
    onBack={goBack}
  />
{:else if selectedGame === 'weeding'}
  <WeedingOngoing
    {onStartLevel}
    onBack={goBack}
  />
{:else if loading}
  <div class="d-flex justify-content-center py-5">
    <div class="spinner-border" role="status">
      <span class="visually-hidden">{texts['ui_loading'] || 'Loading...'}</span>
    </div>
  </div>
{:else if loadError}
  <div class="container py-5">
    <div class="alert alert-danger">{texts['ui_minigame_load_error'] || loadError}</div>
  </div>
{:else if allConfigs}
  <div class="container py-4">
    <div class="d-flex align-items-center mb-4">
      <button class="btn btn-outline-secondary me-3" onclick={goBack}>
        &larr; {texts['ui_back_button'] || 'Back'}
      </button>
      <h2 class="mb-0">{texts['ui_minigame_select_title'] || ''}</h2>
    </div>
    <div class="row g-4 justify-content-center">
      {#each Object.entries(allConfigs) as [id, config]}
        {@const titleKey = `ui_minigame_${id}_title`}
        {@const descKey = `ui_minigame_${id}_desc`}
        <div class="col-md-5">
          <div
            class="card h-100 border-primary cursor-pointer"
            style="cursor: pointer;"
            role="button"
            tabindex="0"
            onclick={() => { selectedGame = id; }}
            onkeydown={(e) => { if (e.key === 'Enter') selectedGame = id; }}
          >
            <div class="card-body text-center pt-3 px-3 pb-2">
              <h3 class="card-title mb-0">{texts[titleKey] || config.display_name}</h3>
            </div>
            {#if config.image}
              <img
                src={config.image}
                alt={texts[titleKey] || config.display_name}
                style="width: 100%; aspect-ratio: 1 / 1; object-fit: cover;"
              />
            {/if}
            <div class="card-body text-center p-4">
              <p class="card-text text-muted mb-4">{texts[descKey] || config.description}</p>
              <button class="btn btn-primary btn-lg px-5">
                {texts['ui_play_button'] || 'Play'}
              </button>
            </div>
          </div>
        </div>
      {/each}
    </div>
  </div>
{/if}
