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

  import { playerGameState, currentCharacter, language } from '../lib/stores';
  import { getMiniGameConfigs } from '../lib/game_state';
  import { loadTexts } from '../lib/text';
  import type { MiniGameConfig } from '../lib/api';
  import TowerDefenseMainMenu from './tower_defense/TowerDefenseMainMenu.svelte';
  import WeedingMainMenu from './weeding/WeedingMainMenu.svelte';
  import TowerDefenseOngoing from './tower_defense/TowerDefenseOngoing.svelte';
  import WeedingOngoing from './weeding/WeedingOngoing.svelte';
  import MiniGameHomeButton from './MiniGameHomeButton.svelte';

  const ARCHETYPE_TO_GAME: Record<string, string> = {
    wolf_warden: 'tower_defense',
    assarter: 'weeding',
  };

  const CAMPAIGN_PHASES: string[] = ['initial_mission', 'baron_track'];

  interface Props {
    onStartLevel: (gameId: string, levelId: number) => void;
    onHome: () => void;
  }

  let { onStartLevel, onHome }: Props = $props();

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
      loadError = 'load_failed';
      console.error('[MiniGameSelect] Failed to load configs:', e);
    } finally {
      loading = false;
    }
  }

  /**
   * Returns from an ongoing-mode setup screen to the mini-game card list.
   * The screen header has a separate Home button for the hub grid.
   */
  function goBackToGames(): void {
    selectedGame = null;
  }

  $effect(() => {
    $language;
    $currentCharacter;
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
    {onHome}
  />
{:else if inCampaign && archetypeGame === 'weeding'}
  <WeedingMainMenu
    onStartLevel={(levelId: number) => onStartLevel('weeding', levelId)}
    {onHome}
  />
{:else if inCampaign}
  <div class="container py-5">
    <div class="mb-4">
      <MiniGameHomeButton {onHome} />
    </div>
    <div class="alert alert-warning">{texts['ui_minigame_no_track'] || ''}</div>
  </div>
{:else if selectedGame === 'tower_defense'}
  <TowerDefenseOngoing
    {onStartLevel}
    {onHome}
    onBackToGames={goBackToGames}
  />
{:else if selectedGame === 'weeding'}
  <WeedingOngoing
    {onStartLevel}
    {onHome}
    onBackToGames={goBackToGames}
  />
{:else if loading}
  <div class="container py-4">
    <div class="mb-4">
      <MiniGameHomeButton {onHome} />
    </div>
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">{texts['ui_loading'] ?? ''}</span>
      </div>
    </div>
  </div>
{:else if loadError}
  <div class="container py-4">
    <div class="mb-4">
      <MiniGameHomeButton {onHome} />
    </div>
    <div class="alert alert-danger">{texts['ui_minigame_load_error'] ?? ''}</div>
  </div>
{:else if allConfigs}
  <div class="container py-4">
    <div class="d-flex align-items-center mb-4">
      <div class="me-3">
        <MiniGameHomeButton {onHome} />
      </div>
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
              <h3 class="card-title mb-0">{texts[titleKey] ?? ''}</h3>
            </div>
            {#if config.image}
              <img
                src={config.image}
                alt={texts[titleKey] ?? ''}
                style="width: 100%; aspect-ratio: 1 / 1; object-fit: cover;"
              />
            {/if}
            <div class="card-body text-center p-4">
              <p class="card-text text-muted mb-4">{texts[descKey] ?? ''}</p>
              <button class="btn btn-primary btn-lg px-5">
                {texts['ui_play_button'] ?? ''}
              </button>
            </div>
          </div>
        </div>
      {/each}
    </div>
  </div>
{/if}
