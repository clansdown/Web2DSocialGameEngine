<script lang="ts">
  import { currentCharacter, playerGameState } from '../lib/stores';
  import { getDukedomsRequest } from '../lib/api';
  import * as auth from '../lib/auth';

  interface Props {
    onPlayMiniGame: (miniGame: string) => void;
  }

  let { onPlayMiniGame }: Props = $props();

  /**
   * Fetches the player's dukedom membership.
   */
  let dukedomName = $state<string | null>(null);
  let dukedomRole = $state<string | null>(null);

  async function fetchDukedomInfo(): Promise<void> {
    try {
      const token = auth.getSessionToken();
      const username = auth.getInMemoryCredentials()?.username;
      if (!token || !username) return;

      // Check which dukedom this character belongs to
      const allDukedoms = await getDukedomsRequest({ username, token });
      // For now, just show the first dukedom's info
      // TODO: add a getMyDukedom endpoint for the logged-in character
      if (allDukedoms.length > 0) {
        dukedomName = 'Member of a dukedom'; // Placeholder
      }
    } catch {
      // Ignore — dukedom info is secondary
    }
  }

  function handlePlay(miniGame: string) {
    onPlayMiniGame(miniGame);
  }

  $effect(() => {
    fetchDukedomInfo();
  });
</script>

<div class="container py-5">
  <div class="d-flex justify-content-between align-items-center mb-4">
    <div>
      <h1 class="mb-0">Ravenest</h1>
      {#if dukedomName}
        <small class="text-muted">{dukedomName}</small>
      {/if}
    </div>
    <div class="text-end">
      <div>
        Playing as: <strong>{$currentCharacter?.display_name}</strong>
        (Level {$currentCharacter?.level})
      </div>
      <small class="text-muted">Mesne Lord</small>
    </div>
  </div>

  <div class="alert alert-success">
    <h4 class="alert-heading">Manor Established</h4>
    <p class="mb-0">
      Your manor stands ready. The realm awaits your deeds.
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
