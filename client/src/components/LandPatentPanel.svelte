<script lang="ts">
  /**
   * Displays the Land Patent decision panel on the hub grid during the
   * land_patent game phase. Replaces the Manor card until the player has
   * founded a manor. Offers two paths: join an existing dukedom or start
   * the duke track to earn the right to found one's own dukedom.
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

  interface Props {
    onJoinBarony: () => void;
    onBaronTrackStarted: () => void;
  }

  let { onJoinBarony, onBaronTrackStarted }: Props = $props();

  let texts = $state<Record<string, string>>({});
  let textsLoaded = $state(false);
  let loadingBaronTrack = $state(false);

  const TEXT_IDS: string[] = [
    'ui_patent_title',
    'ui_patent_body',
    'ui_patent_join',
    'ui_patent_start_own',
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
   * Starts the duke track: calls the startBaronTrack API, then notifies the
   * parent so the game phase can be refreshed (routes to baron_track).
   */
  async function handleStartBaronTrack(): Promise<void> {
    if (!$currentCharacter || loadingBaronTrack) return;
    loadingBaronTrack = true;
    try {
      const token = auth.getSessionToken();
      const username = auth.getInMemoryCredentials()?.username;
      if (!token || !username) {
        handleError('Not authenticated', new Error('No session'));
        return;
      }
      await startBaronTrackRequest($currentCharacter.id, { username, token });
      onBaronTrackStarted();
    } catch (e) {
      handleError('Failed to start baron track', e);
    } finally {
      loadingBaronTrack = false;
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
            onclick={handleStartBaronTrack}
            disabled={loadingBaronTrack}
          >
            {#if loadingBaronTrack}
              <span class="spinner-border spinner-border-sm me-1" role="status"></span>
            {/if}
            {texts['ui_patent_start_own'] || 'Earn the Right to Start One'}
          </button>
        </div>
      </div>
    {/if}
  </div>
</div>
