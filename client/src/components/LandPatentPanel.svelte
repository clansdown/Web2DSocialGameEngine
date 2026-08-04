<script lang="ts">
  /**
   * Displays the Land Patent decision panel on the hub grid during the
   * land_patent game phase. Replaces the Manor card until the player has
   * founded a manor. Offers two paths: join an existing barony or start
   * the baron track to earn the right to found one's own barony.
   *
   * Choosing the own-barony path opens a dialog that asks the player to name
   * the Honor they aspire to create; the name is sent to startBaronTrack and
   * must be unique (case-insensitive) against existing baronies.
   *
   * The instructional body text is rendered via GameText (vellum texture);
   * the award-time letter from the king is shown separately with StoryText.
   *
   * Fetches translated text in the current language on mount.
   */

  import { currentCharacter } from '../lib/stores';
  import { startBaronTrackRequest } from '../lib/api';
  import { loadTexts as fetchTexts } from '../lib/text';
  import * as auth from '../lib/auth';
  import { handleError } from '../lib/errors';
  import GameText from './GameText.svelte';
  import DialogOverlay from './DialogOverlay.svelte';

  interface Props {
    onJoinBarony: () => void;
    onBaronTrackStarted: () => void;
  }

  let { onJoinBarony, onBaronTrackStarted }: Props = $props();

  let texts = $state<Record<string, string>>({});
  let textsLoaded = $state(false);
  let showHonorDialog = $state(false);
  let honorName = $state('');
  let honorError = $state<string | null>(null);
  let startingTrack = $state(false);

  const TEXT_IDS: string[] = [
    'ui_patent_title',
    'ui_patent_body',
    'ui_patent_join',
    'ui_patent_start_own',
    'ui_patent_honor_title',
    'ui_patent_honor_body',
    'ui_patent_honor_label',
    'ui_patent_honor_placeholder',
    'ui_patent_honor_duplicate',
    'ui_patent_honor_begin',
    'ui_cancel',
  ];

  /**
   * Loads the patent panel text keys in the current language.
   */
  async function loadTexts(): Promise<void> {
    textsLoaded = false;
    try {
      texts = await fetchTexts(TEXT_IDS);
    } catch {
      texts = {};
    } finally {
      textsLoaded = true;
    }
  }

  /**
   * Opens the honor-name dialog for the own-barony path, clearing any prior
   * input and error state.
   */
  function openHonorDialog(): void {
    honorName = '';
    honorError = null;
    showHonorDialog = true;
  }

  /**
   * Closes the honor-name dialog without starting the baron track.
   */
  function closeHonorDialog(): void {
    if (startingTrack) return;
    showHonorDialog = false;
  }

  /**
   * Starts the baron track with the chosen honor name: validates the name,
   * calls the startBaronTrack API, and notifies the parent on success so the
   * game phase can be refreshed (routes to baron_track). Duplicate-name
   * errors are shown inline with a localized message instead of closing.
   */
  async function confirmStartBaronTrack(): Promise<void> {
    const trimmed = honorName.trim();
    if (!$currentCharacter || startingTrack || !trimmed) return;
    startingTrack = true;
    honorError = null;
    try {
      const token = auth.getSessionToken();
      const username = auth.getInMemoryCredentials()?.username;
      if (!token || !username) {
        handleError('Not authenticated', new Error('No session'));
        return;
      }
      await startBaronTrackRequest($currentCharacter.id, trimmed, { username, token });
      showHonorDialog = false;
      onBaronTrackStarted();
    } catch (e) {
      const message = e instanceof Error ? e.message : 'Failed to start baron track';
      if (message.includes('already exists')) {
        honorError = texts['ui_patent_honor_duplicate'] || 'A barony with that name already exists. Choose another.';
      } else {
        handleError('Failed to start baron track', e);
      }
    } finally {
      startingTrack = false;
    }
  }

  $effect(() => {
    loadTexts();
  });
</script>

<div class="card h-100 border-primary">
  <div class="card-body p-4">
    {#if !textsLoaded}
      <div class="d-flex justify-content-center py-4">
        <div class="spinner-border" role="status">
          <span class="visually-hidden">Loading...</span>
        </div>
      </div>
    {:else}
      <h2 class="text-center mb-4" style="font-family: serif;">
        {texts['ui_patent_title'] || 'Land Patent'}
      </h2>

      <GameText text={texts['ui_patent_body'] || ''} />

      <div class="row g-3 mt-2">
        <div class="col-12 col-md-6">
          <button
            class="btn btn-primary w-100 py-3"
            onclick={onJoinBarony}
          >
            {texts['ui_patent_join'] || 'Join a Barony'}
          </button>
        </div>
        <div class="col-12 col-md-6">
          <button
            class="btn btn-outline-primary w-100 py-3"
            onclick={openHonorDialog}
          >
            {texts['ui_patent_start_own'] || 'Earn the Right to Start One'}
          </button>
        </div>
      </div>
    {/if}
  </div>
</div>

{#if showHonorDialog}
  <DialogOverlay
    title={texts['ui_patent_honor_title'] || 'Name Your Future Honor'}
    onDismiss={closeHonorDialog}
    hideContinue
  >
    <p style="font-size: 1.05rem; line-height: 1.6; font-family: serif;">
      {texts['ui_patent_honor_body'] || ''}
    </p>

    <div class="mb-3">
      <label for="honor-name" class="form-label">
        {texts['ui_patent_honor_label'] || 'Name of your Honor'}
      </label>
      <input
        id="honor-name"
        type="text"
        class="form-control"
        bind:value={honorName}
        placeholder={texts['ui_patent_honor_placeholder'] || 'e.g. Stormhold'}
        maxlength={64}
      />
    </div>

    {#if honorError}
      <div class="alert alert-danger py-2">{honorError}</div>
    {/if}

    <div class="d-flex justify-content-end gap-2">
      <button
        class="btn btn-outline-secondary"
        onclick={closeHonorDialog}
        disabled={startingTrack}
      >
        {texts['ui_cancel'] || 'Cancel'}
      </button>
      <button
        class="btn btn-primary"
        onclick={confirmStartBaronTrack}
        disabled={startingTrack || !honorName.trim()}
      >
        {#if startingTrack}
          <span class="spinner-border spinner-border-sm me-1" role="status"></span>
        {/if}
        {texts['ui_patent_honor_begin'] || 'Name It and Begin'}
      </button>
    </div>
  </DialogOverlay>
{/if}
