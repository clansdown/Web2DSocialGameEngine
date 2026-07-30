<script lang="ts">
  /**
   * Lists all available baronies for the player to join.
   * Shows name, description, owner, and member count for each.
   * On join, calls the API and transitions to sandbox (TownView).
   */

  import { language, currentCharacter } from '../lib/stores';
  import { getBaroniesRequest, joinBaronyRequest } from '../lib/api';
  import * as auth from '../lib/auth';
  import { handleError } from '../lib/errors';

  interface Props {
    onJoined: () => void;
    onBack: () => void;
  }

  let { onJoined, onBack }: Props = $props();

  let baronies = $state<Awaited<ReturnType<typeof getBaroniesRequest>>>([]);
  let loading = $state(true);
  let loadError = $state<string | null>(null);
  let joiningBaronyId = $state<number | null>(null);

  async function loadBaronys(): Promise<void> {
    loading = true;
    loadError = null;
    try {
      const token = auth.getSessionToken();
      const username = auth.getInMemoryCredentials()?.username;
      if (!token || !username) return;
      baronies = await getBaroniesRequest({ username, token });
    } catch (e) {
      loadError = e instanceof Error ? e.message : 'Failed to load baronies';
    } finally {
      loading = false;
    }
  }

  async function handleJoin(baronyId: number): Promise<void> {
    if (!$currentCharacter || joiningBaronyId !== null) return;
    joiningBaronyId = baronyId;
    try {
      const token = auth.getSessionToken();
      const username = auth.getInMemoryCredentials()?.username;
      if (!token || !username) return;
      await joinBaronyRequest($currentCharacter.id, baronyId, { username, token });
      onJoined();
    } catch (e) {
      handleError('Failed to join barony', e);
    } finally {
      joiningBaronyId = null;
    }
  }

  $effect(() => {
    loadBaronys();
  });
</script>

<div class="container py-5">
  <div class="d-flex align-items-center mb-4">
    <button class="btn btn-outline-secondary me-3" onclick={onBack}>
      &larr; Back
    </button>
    <h2 class="mb-0">Choose a Barony</h2>
  </div>

  {#if loading}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Loading...</span>
      </div>
    </div>
  {:else if loadError}
    <div class="alert alert-danger">{loadError}</div>
  {:else if baronies.length === 0}
    <div class="alert alert-info">
      No baronies available yet. Consider starting your own!
    </div>
  {:else}
    <div class="row g-3">
      {#each baronies as barony}
        <div class="col-12 col-md-6">
          <div class="card h-100">
            <div class="card-body">
              <h5 class="card-title">{barony.name}</h5>
              {#if barony.description}
                <p class="card-text text-muted">{barony.description}</p>
              {/if}
              <p class="card-text">
                <small class="text-muted">
                  Founded by {barony.owner_name} &middot; {barony.member_count} member{barony.member_count !== 1 ? 's' : ''}
                </small>
              </p>
              <button
                class="btn btn-primary"
                onclick={() => handleJoin(barony.id)}
                disabled={joiningBaronyId === barony.id}
              >
                {#if joiningBaronyId === barony.id}
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
