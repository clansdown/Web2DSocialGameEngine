<script lang="ts">
  /**
   * Shown in the hub when a freeholder is eligible to become a Mesne Lord
   * (phase freehold_qualified). Lets the player name their Honor and apply; on
   * success the character founds the honor (a barony under its pre-elevation
   * name) and enters the shared sandbox state.
   */
  import { currentCharacter } from '../lib/stores';
  import { applyForMesneLordRequest, getSafeContentRequest, type safe_content_dto } from '../lib/api';
  import { loadTexts } from '../lib/text';
  import * as auth from '../lib/auth';
  import { handleError } from '../lib/errors';
  import GameText from './GameText.svelte';

  interface Props {
    onApplied: () => void;
    onBack: () => void;
  }

  let { onApplied, onBack }: Props = $props();

  let texts = $state<Record<string, string>>({});
  let honorName = $state('');
  let safeNameId = $state('');
  let safeContent = $state<safe_content_dto | null>(null);
  let error = $state<string | null>(null);
  let submitting = $state(false);
  let isAdult = $derived($currentCharacter?.is_adult === true);

  const TEXT_IDS: string[] = [
    'ui_honor_title', 'ui_honor_body', 'ui_honor_name_label', 'ui_honor_name_placeholder',
    'ui_honor_apply', 'ui_honor_duplicate', 'ui_back_button', 'ui_barony_create_safe_name'
  ];

  $effect(() => {
    loadTexts(TEXT_IDS).then(t => { texts = t; }).catch(() => {});
    const creds = auth.getInMemoryCredentials();
    const token = auth.getSessionToken();
    if (creds && token) {
      getSafeContentRequest({ username: creds.username, token })
        .then((safe) => {
          safeContent = safe;
          safeNameId = safe.barony_names[0]?.id ?? '';
        })
        .catch(() => {});
    }
  });

  /**
   * Applies to become a Mesne Lord with the entered Honor name. Duplicate-name
   * errors are shown inline; other failures surface via handleError.
   */
  async function apply(): Promise<void> {
    const trimmed = honorName.trim();
    if (!$currentCharacter || submitting || !safeNameId || (isAdult && !trimmed)) return;
    submitting = true;
    error = null;
    try {
      const creds = auth.getInMemoryCredentials();
      const token = auth.getSessionToken();
      if (!creds || !token) {
        handleError('Not authenticated', new Error('No session'));
        return;
      }
      await applyForMesneLordRequest($currentCharacter.id, safeNameId, isAdult ? trimmed : '', { username: creds.username, token });
      onApplied();
    } catch (e) {
      const message = e instanceof Error ? e.message : 'Failed to apply';
      if (message.includes('already exists')) {
        error = texts['ui_honor_duplicate'] || 'A barony or honor with that name already exists. Choose another.';
      } else {
        handleError('Failed to apply for mesne lord', e);
      }
    } finally {
      submitting = false;
    }
  }
</script>

<div class="container py-4">
  <button class="btn btn-outline-secondary mb-4" onclick={onBack}>
    &larr; {texts['ui_back_button'] || 'Back'}
  </button>

  <div class="card border-primary">
    <div class="card-body p-4">
      <h2 class="text-center mb-4" style="font-family: serif;">
        {texts['ui_honor_title'] || 'Become a Mesne Lord'}
      </h2>

      <GameText text={texts['ui_honor_body'] || ''} />

      <div class="mb-3 mt-3">
        <label for="honor-safe-name" class="form-label">
          {texts['ui_barony_create_safe_name'] || 'Safe name'}
        </label>
        <select id="honor-safe-name" class="form-select" bind:value={safeNameId}>
          {#each safeContent?.barony_names ?? [] as entry (entry.id)}
            <option value={entry.id}>{entry.name ?? entry.id}</option>
          {/each}
        </select>
      </div>

      {#if isAdult}
        <div class="mb-3">
          <label for="honor-name" class="form-label">
            {texts['ui_honor_name_label'] || 'Name of your Honor'}
          </label>
          <input
            id="honor-name"
            type="text"
            class="form-control"
            bind:value={honorName}
            placeholder={texts['ui_honor_name_placeholder'] || 'e.g. Stormhold'}
            maxlength={64}
          />
        </div>
      {/if}

      {#if error}
        <div class="alert alert-danger py-2">{error}</div>
      {/if}

      <div class="d-flex justify-content-end">
        <button
          class="btn btn-primary"
          onclick={apply}
          disabled={submitting || !safeNameId || (isAdult && !honorName.trim())}
        >
          {#if submitting}
            <span class="spinner-border spinner-border-sm me-1" role="status"></span>
          {/if}
          {texts['ui_honor_apply'] || 'Found Your Honor'}
        </button>
      </div>
    </div>
  </div>
</div>
