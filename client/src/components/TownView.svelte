<script lang="ts">
  import { currentCharacter, playerGameState } from '../lib/stores';

  interface Props {
    onPlayMiniGame: (miniGame: string) => void;
  }

  let { onPlayMiniGame }: Props = $props();

  /**
   * Handles starting a mini-game replay from the town view.
   * 
   * @param miniGame - The mini-game name to start
   */
  function handlePlay(miniGame: string) {
    onPlayMiniGame(miniGame);
  }
</script>

<div class="container py-5">
  <div class="d-flex justify-content-between align-items-center mb-4">
    <h1>Ravenest</h1>
    <div>
      <span class="me-3">
        Playing as: <strong>{$currentCharacter?.display_name}</strong>
        (Level {$currentCharacter?.level})
      </span>
    </div>
  </div>

  <div class="alert alert-success">
    <h4 class="alert-heading">Base Unlocked!</h4>
    <p class="mb-0">
      You have completed the initial campaign and earned the right to build your base.
      The base building interface is coming soon.
    </p>
  </div>

  {#if $playerGameState}
    <div class="row g-4 mt-3">
      {#each $playerGameState.progress.filter(p => p.completed).reduce((acc: string[], p) => {
        if (!acc.includes(p.mini_game)) acc.push(p.mini_game);
        return acc;
      }, []) as completedMiniGame}
        <div class="col-md-6">
          <div class="card h-100">
            <div class="card-body text-center">
              <h5 class="card-title">{completedMiniGame === 'tower_defense' ? 'Tower Defense' : 'Weeding'}</h5>
              <p class="card-text">Campaign completed! Replay for additional rewards.</p>
              <button
                class="btn btn-outline-primary"
                onclick={() => handlePlay(completedMiniGame)}
              >
                Play Again
              </button>
            </div>
          </div>
        </div>
      {/each}
    </div>
  {/if}
</div>
