<script lang="ts">
  /**
   * Displays the Land Patent screen after completing all 9 initial missions.
   * Offers two paths: join an existing dukedom or start the duke track
   * to earn the right to create one's own dukedom.
   *
   * Fetches translated text in the current language on mount.
   */

  import { language, currentCharacter } from '../lib/stores';
  import { getTextsRequest, startDukeTrackRequest } from '../lib/api';
  import * as auth from '../lib/auth';
  import { handleError } from '../lib/errors';

  interface Props {
    onJoinDukedom: () => void;
    onStartDukeTrack: () => void;
  }

  let { onJoinDukedom, onStartDukeTrack }: Props = $props();

  let texts = $state<Record<string, string>>({});
  let textsLoaded = $state(false);
  let loadingDukeTrack = $state(false);

  const TEXT_IDS: string[] = [
    'ui_patent_title',
    'ui_patent_body',
    'ui_patent_join',
    'ui_patent_start_own',
  ];

  async function loadTexts(): Promise<void> {
    textsLoaded = false;
    try {
      texts = await getTextsRequest($language, TEXT_IDS);
    } catch {
      texts = {};
    } finally {
      textsLoaded = true;
    }
  }

  async function handleStartDukeTrack(): Promise<void> {
    if (!$currentCharacter || loadingDukeTrack) return;
    loadingDukeTrack = true;
    try {
      const token = auth.getSessionToken();
      const username = auth.getInMemoryCredentials()?.username;
      if (!token || !username) {
        handleError('Not authenticated', new Error('No session'));
        return;
      }
      await startDukeTrackRequest($currentCharacter.id, { username, token });
      onStartDukeTrack();
    } catch (e) {
      handleError('Failed to start duke track', e);
    } finally {
      loadingDukeTrack = false;
    }
  }

  function getParagraphs(text: string): string[] {
    return text.split('\n\n').filter(p => p.trim().length > 0);
  }

  $effect(() => {
    loadTexts();
  });
</script>

<div class="container py-5">
  {#if !textsLoaded}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Loading...</span>
      </div>
    </div>
  {:else}
    <div class="row justify-content-center">
      <div class="col-12 col-lg-8">
        <!-- Parchment document styling -->
        <div
          class="p-5 rounded shadow-lg mb-4"
          style="
            background: linear-gradient(180deg, #f5e6c8, #ede0c8, #f5e6c8);
            border: 2px solid #8b7355;
            color: #3a2a1a;
          "
        >
          <h2 class="text-center mb-4" style="font-family: serif;">
            {texts['ui_patent_title'] || 'Land Patent'}
          </h2>

          {#each getParagraphs(texts['ui_patent_body'] || '') as paragraph}
            <p class="mb-3" style="font-size: 1.1rem; line-height: 1.7; font-family: serif;">
              {paragraph}
            </p>
          {/each}
        </div>

        <!-- Actions -->
        <div class="row g-3">
          <div class="col-12 col-md-6">
            <div class="card h-100 text-center">
              <div class="card-body">
                <h5 class="card-title">{texts['ui_patent_join'] || 'Join a Dukedom'}</h5>
                <p class="card-text text-muted">Join an existing dukedom, receive a manor, and start building.</p>
                <button class="btn btn-primary mt-2" onclick={onJoinDukedom}>
                  {texts['ui_patent_join'] || 'Join a Dukedom'}
                </button>
              </div>
            </div>
          </div>

          <div class="col-12 col-md-6">
            <div class="card h-100 text-center">
              <div class="card-body">
                <h5 class="card-title">{texts['ui_patent_start_own'] || 'Earn the Right to Start One'}</h5>
                <p class="card-text text-muted">Clear a 4x4 grid of additional missions to earn the right to found your own dukedom.</p>
                <button
                  class="btn btn-outline-primary mt-2"
                  onclick={handleStartDukeTrack}
                  disabled={loadingDukeTrack}
                >
                  {#if loadingDukeTrack}
                    <span class="spinner-border spinner-border-sm me-1" role="status"></span>
                  {/if}
                  {texts['ui_patent_start_own'] || 'Earn the Right to Start One'}
                </button>
              </div>
            </div>
          </div>
        </div>
      </div>
    </div>
  {/if}
</div>
