<script lang="ts">
  import TowerDefenseGame from './tower_defense/TowerDefenseGame.svelte';
  import WeedingGame from './weeding/WeedingGame.svelte';
  import { startMiniGame, endMiniGame } from '../lib/game_state';
  import { currentCharacter, playerGameState } from '../lib/stores';
  import type { StartMiniGameResponse, EndMiniGameResponse } from '../lib/api';

  interface Props {
    miniGame: string;
    onComplete: (results: EndMiniGameResponse) => void;
    onError: (error: string) => void;
  }

  let { miniGame, onComplete, onError }: Props = $props();

  let loading = $state(true);
  let startError = $state<string | null>(null);
  let levelConfig: StartMiniGameResponse | null = $state(null);
  let gameStarted = $state(false);
  let gameFinished = $state(false);
  let gameResults: EndMiniGameResponse | null = $state(null);
  let showResults = $state(false);

  /**
   * Starts the mini-game session by calling the server API.
   * Loads the level config that the client game component will render.
   */
  async function initializeGame() {
    loading = true;
    startError = null;
    try {
      if (!$currentCharacter) {
        throw new Error('No character selected');
      }

      const result = await startMiniGame($currentCharacter.id, miniGame);
      levelConfig = result;
      gameStarted = true;
    } catch (e) {
      const msg = e instanceof Error ? e.message : 'Failed to start mini-game';
      startError = msg;
      onError(msg);
    } finally {
      loading = false;
    }
  }

  /**
   * Called by the mini-game component when the game finishes.
   * Reports results to the server and transitions to results display.
   * 
   * @param won - Whether the player won
   * @param score - The player's score
   */
  async function handleGameOver(won: boolean, score: number) {
    if (gameFinished || !$currentCharacter || !levelConfig) return;

    gameFinished = true;

    try {
      const levelId = levelConfig.level_id ?? levelConfig.level_config?.id ?? 0;

      const results = await endMiniGame(
        $currentCharacter.id,
        miniGame,
        levelId,
        won,
        score
      );

      gameResults = results;
      showResults = true;
    } catch (e) {
      const msg = e instanceof Error ? e.message : 'Failed to end mini-game';
      onError(msg);
    }
  }

  $effect(() => {
    if (!gameStarted && !gameFinished) {
      initializeGame();
    }
  });
</script>

<div class="container-fluid p-0">
  {#if loading}
    <div class="d-flex justify-content-center align-items-center py-5">
      <div class="text-center">
        <div class="spinner-border mb-3" role="status">
          <span class="visually-hidden">Loading...</span>
        </div>
        <p class="text-muted">Starting {miniGame === 'tower_defense' ? 'Tower Defense' : 'Weeding'}...</p>
      </div>
    </div>
  {:else if startError}
    <div class="container py-5">
      <div class="alert alert-danger">{startError}</div>
    </div>
  {:else if showResults && gameResults}
    <div class="container py-5">
      <div class="card">
        <div class="card-body text-center p-5">
          <h3 class="mb-3">
            {gameResults.completed ? 'Victory!' : 'Defeat'}
          </h3>
          <p class="lead mb-2">Score: {gameResults.score}</p>
          {#if gameResults.new_best_score === gameResults.score && gameResults.score > 0}
            <p class="text-success">New Best Score!</p>
          {/if}
          {#if gameResults.completed}
            <div class="mb-3">
              <h5>Rewards</h5>
              {#each Object.entries(gameResults.rewards) as [resource, amount]}
                <span class="badge bg-success me-2">{resource}: +{amount}</span>
              {/each}
            </div>
          {/if}
          {#if gameResults.base_unlocked}
            <div class="alert alert-success">
              <h4 class="alert-heading">Base Unlocked!</h4>
              <p class="mb-0">You have proven yourself worthy. The right to build is yours.</p>
            </div>
          {/if}
          {#if gameResults.land_patent_earned}
            <div class="alert alert-info">
              <h4 class="alert-heading">Land Patent Earned!</h4>
              <p class="mb-0">You have completed all initial missions. You may now join a dukedom or earn the right to start your own.</p>
            </div>
          {/if}
          {#if gameResults.duke_right_earned}
            <div class="alert alert-warning">
              <h4 class="alert-heading">Dukedom Right Earned!</h4>
              <p class="mb-0">You have cleared the duke track. You may now found your own dukedom.</p>
            </div>
          {/if}
          {#if gameResults.new_unlocks && (gameResults.new_unlocks.new_units.length > 0 || gameResults.new_unlocks.new_towers.length > 0)}
            <div class="alert alert-info">
              <h4 class="alert-heading">New Unlocks!</h4>
              <p class="mb-0">
                {#each gameResults.new_unlocks.new_units as unit}
                  <span class="badge bg-info me-2 mb-1">Unit: {unit}</span>
                {/each}
                {#each gameResults.new_unlocks.new_towers as tower}
                  <span class="badge bg-warning text-dark me-2 mb-1">Tower: {tower}</span>
                {/each}
              </p>
            </div>
          {/if}
          <button class="btn btn-primary btn-lg mt-4" onclick={() => onComplete(gameResults)}>
            Continue
          </button>
        </div>
      </div>
    </div>
  {:else if levelConfig && gameStarted}
    {#if miniGame === 'tower_defense'}
      <TowerDefenseGame
        {levelConfig}
        onGameOver={handleGameOver}
      />
    {:else if miniGame === 'weeding'}
      <WeedingGame
        {levelConfig}
        onGameOver={handleGameOver}
      />
    {:else}
      <div class="container py-5">
        <div class="alert alert-warning">Unknown mini-game: {miniGame}</div>
      </div>
    {/if}
  {/if}
</div>
