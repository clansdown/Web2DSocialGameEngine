<script lang="ts">
  /**
   * Creates a new dukedom after completing the duke track.
   * Shows a form for name and optional description.
   */

  import { language, currentCharacter } from '../lib/stores';
  import { createDukedomRequest } from '../lib/api';
  import * as auth from '../lib/auth';
  import { handleError } from '../lib/errors';
  import { getTextsRequest } from '../lib/api';

  interface Props {
    onCreated: () => void;
    onBack: () => void;
  }

  let { onCreated, onBack }: Props = $props();

  let name = $state('');
  let description = $state('');
  let loading = $state(false);
  let error = $state<string | null>(null);
  let texts = $state<Record<string, string>>({});

  const TEXT_IDS: string[] = [
    'ui_dukedom_create_title',
    'ui_dukedom_create_desc_label',
    'ui_confirm',
  ];

  async function loadTexts(): Promise<void> {
    try {
      texts = await getTextsRequest($language, TEXT_IDS);
    } catch {
      texts = {};
    }
  }

  async function handleCreate(): Promise<void> {
    if (!$currentCharacter || loading || !name.trim()) return;
    loading = true;
    error = null;
    try {
      const token = auth.getSessionToken();
      const username = auth.getInMemoryCredentials()?.username;
      if (!token || !username) return;
      await createDukedomRequest($currentCharacter.id, name.trim(), description.trim(), { username, token });
      onCreated();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to create dukedom';
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    loadTexts();
  });
</script>

<div class="container py-5">
  <div class="d-flex align-items-center mb-4">
    <button class="btn btn-outline-secondary me-3" onclick={onBack}>
      &larr; Back
    </button>
    <h2 class="mb-0">{texts['ui_dukedom_create_title'] || 'Name Your Dukedom'}</h2>
  </div>

  <div class="row justify-content-center">
    <div class="col-12 col-md-6">
      {#if error}
        <div class="alert alert-danger">{error}</div>
      {/if}

      <div class="mb-3">
        <label for="dukedom-name" class="form-label">{texts['ui_dukedom_create_title'] || 'Name'}</label>
        <input
          id="dukedom-name"
          type="text"
          class="form-control"
          bind:value={name}
          placeholder="Stormhold"
          maxlength={64}
        />
      </div>

      <div class="mb-3">
        <label for="dukedom-desc" class="form-label">{texts['ui_dukedom_create_desc_label'] || 'Description (optional)'}</label>
        <textarea
          id="dukedom-desc"
          class="form-control"
          bind:value={description}
          rows={3}
          maxlength={256}
        ></textarea>
      </div>

      <button
        class="btn btn-primary"
        onclick={handleCreate}
        disabled={loading || !name.trim()}
      >
        {#if loading}
          <span class="spinner-border spinner-border-sm me-1" role="status"></span>
        {/if}
        {texts['ui_confirm'] || 'Create'}
      </button>
    </div>
  </div>
</div>
