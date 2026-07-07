<script lang="ts">
  /**
   * Lists all available dukedoms for the player to join.
   * Shows name, description, owner, and member count for each.
   * On join, calls the API and transitions to sandbox (TownView).
   */

  import { language, currentCharacter } from '../lib/stores';
  import { getDukedomsRequest, joinDukedomRequest } from '../lib/api';
  import * as auth from '../lib/auth';
  import { handleError } from '../lib/errors';

  interface Props {
    onJoined: () => void;
    onBack: () => void;
  }

  let { onJoined, onBack }: Props = $props();

  let dukedoms = $state<Awaited<ReturnType<typeof getDukedomsRequest>>>([]);
  let loading = $state(true);
  let loadError = $state<string | null>(null);
  let joiningId = $state<number | null>(null);

  async function loadDukedoms(): Promise<void> {
    loading = true;
    loadError = null;
    try {
      const token = auth.getSessionToken();
      const username = auth.getInMemoryCredentials()?.username;
      if (!token || !username) return;
      dukedoms = await getDukedomsRequest({ username, token });
    } catch (e) {
      loadError = e instanceof Error ? e.message : 'Failed to load dukedoms';
    } finally {
      loading = false;
    }
  }

  async function handleJoin(dukedomId: number): Promise<void> {
    if (!$currentCharacter || joiningId !== null) return;
    joiningId = dukedomId;
    try {
      const token = auth.getSessionToken();
      const username = auth.getInMemoryCredentials()?.username;
      if (!token || !username) return;
      await joinDukedomRequest($currentCharacter.id, dukedomId, { username, token });
      onJoined();
    } catch (e) {
      handleError('Failed to join dukedom', e);
    } finally {
      joiningId = null;
    }
  }

  $effect(() => {
    loadDukedoms();
  });
</script>

<div class="container py-5">
  <div class="d-flex align-items-center mb-4">
    <button class="btn btn-outline-secondary me-3" onclick={onBack}>
      &larr; Back
    </button>
    <h2 class="mb-0">Choose a Dukedom</h2>
  </div>

  {#if loading}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Loading...</span>
      </div>
    </div>
  {:else if loadError}
    <div class="alert alert-danger">{loadError}</div>
  {:else if dukedoms.length === 0}
    <div class="alert alert-info">
      No dukedoms available yet. Consider starting your own!
    </div>
  {:else}
    <div class="row g-3">
      {#each dukedoms as dukedom}
        <div class="col-12 col-md-6">
          <div class="card h-100">
            <div class="card-body">
              <h5 class="card-title">{dukedom.name}</h5>
              {#if dukedom.description}
                <p class="card-text text-muted">{dukedom.description}</p>
              {/if}
              <p class="card-text">
                <small class="text-muted">
                  Founded by {dukedom.owner_name} &middot; {dukedom.member_count} member{dukedom.member_count !== 1 ? 's' : ''}
                </small>
              </p>
              <button
                class="btn btn-primary"
                onclick={() => handleJoin(dukedom.id)}
                disabled={joiningId === dukedom.id}
              >
                {#if joiningId === dukedom.id}
                  <span class="spinner-border spinner-border-sm me-1" role="status"></span>
                {/if}
                Join
              </button>
            </div>
          </div>
        </div>
      {/each}
    </div>
  {/if}
</div>
