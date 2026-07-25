<script lang="ts">
  import { playerGameState, currentCharacter } from '../lib/stores';
  import { getMiniGameConfigs } from '../lib/game_state';
  import type { MiniGameConfig } from '../lib/api';
  import TowerDefenseMainMenu from './tower_defense/TowerDefenseMainMenu.svelte';
  import WeedingMainMenu from './weeding/WeedingMainMenu.svelte';

  const ARCHETYPE_TO_GAME: Record<string, string> = {
    wolf_warden: 'tower_defense',
    assarter: 'weeding',
  };

  interface Props {
    onStartLevel: (gameId: string, levelId: number) => void;
    onBack: () => void;
  }

  let { onStartLevel, onBack }: Props = $props();

  let allConfigs = $state<Record<string, MiniGameConfig> | null>(null);
  let loading = $state(true);
  let loadError = $state<string | null>(null);
  let selectedGame = $state<string | null>(null);

  let activeGame = $derived(
    selectedGame ?? (
      $currentCharacter?.archetype
        ? (ARCHETYPE_TO_GAME[$currentCharacter.archetype] ?? null)
        : null
    )
  );

  async function loadConfigs() {
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

  function goBack() {
    if (selectedGame) {
      selectedGame = null;
    } else {
      onBack();
    }
  }

  function handleMainMenuLevel(levelId: number) {
    if (activeGame) {
      onStartLevel(activeGame, levelId);
    }
  }

  $effect(() => {
    if (!activeGame) {
      loadConfigs();
    }
  });
</script>

{#if activeGame}
  {#if activeGame === 'tower_defense'}
    <TowerDefenseMainMenu onStartLevel={handleMainMenuLevel} onBack={goBack} />
  {:else if activeGame === 'weeding'}
    <WeedingMainMenu onStartLevel={handleMainMenuLevel} onBack={goBack} />
  {:else}
    <div class="container py-5">
      <button class="btn btn-outline-secondary mb-4" onclick={goBack}>
        &larr; Back
      </button>
      <div class="alert alert-warning">Unknown mini-game: {activeGame}</div>
    </div>
  {/if}
{:else if loading}
  <div class="d-flex justify-content-center py-5">
    <div class="spinner-border" role="status">
      <span class="visually-hidden">Loading...</span>
    </div>
  </div>
{:else if loadError}
  <div class="container py-5">
    <div class="alert alert-danger">{loadError}</div>
  </div>
{:else if allConfigs}
  <div class="container py-4">
    <div class="d-flex align-items-center mb-4">
      <button class="btn btn-outline-secondary me-3" onclick={onBack}>
        &larr; Back
      </button>
      <h2 class="mb-0">Select a Game</h2>
    </div>
    <div class="row g-4 justify-content-center">
      {#each Object.entries(allConfigs) as [id, config]}
        <div class="col-md-5">
          <div
            class="card h-100 border-primary cursor-pointer"
            style="cursor: pointer;"
            role="button"
            tabindex="0"
            onclick={() => { selectedGame = id; }}
            onkeydown={(e) => { if (e.key === 'Enter') selectedGame = id; }}
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
                Play
              </button>
            </div>
          </div>
        </div>
      {/each}
    </div>
  </div>
{/if}
