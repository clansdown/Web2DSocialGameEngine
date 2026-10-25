<script lang="ts">
  /**
   * Creates a new barony after completing the baron track.
   * Shows a form for name and optional description.
   */

  import { currentCharacter, playerGameState } from '../lib/stores';
  import { createBaronyRequest, getSafeContentRequest, type safe_content_dto } from '../lib/api';
  import * as auth from '../lib/auth';
  import { handleError } from '../lib/errors';
  import { loadTexts as fetchTexts } from '../lib/text';

  interface Props {
    onCreated: () => void;
    onBack: () => void;
  }

  let { onCreated, onBack }: Props = $props();

  // The safe name is mandatory; adults may additionally choose a custom name.
  // The honor-name prefill is matched to the safe catalog when possible.
  let name = $state($playerGameState?.honor_name ?? '');
  let safeNameId = $state('');
  let safeTagIds = $state<string[]>([]);
  let description = $state('');
  let acceptsMinors = $state(!($currentCharacter?.is_adult ?? false));
  let safeContent = $state<safe_content_dto | null>(null);
  let loading = $state(false);
  let error = $state<string | null>(null);
  let texts = $state<Record<string, string>>({});

  let isAdult = $derived($currentCharacter?.is_adult === true);

  const TEXT_IDS: string[] = [
    'ui_barony_create_title',
    'ui_barony_create_name_label',
    'ui_barony_create_desc_label',
    'ui_barony_create_safe_name',
    'ui_barony_create_tags',
    'ui_barony_create_custom_name',
    'ui_barony_create_accepts_minors',
    'ui_confirm',
  ];

  async function loadTexts(): Promise<void> {
    try {
      texts = await fetchTexts(TEXT_IDS);
    } catch {
      texts = {};
    }
  }

  async function loadSafeContent(): Promise<void> {
    const token = auth.getSessionToken();
    const username = auth.getInMemoryCredentials()?.username;
    if (!token || !username) return;
    try {
      safeContent = await getSafeContentRequest({ username, token });
      const honorName = $playerGameState?.honor_name?.trim().toLowerCase();
      const match = safeContent.barony_names.find(
        (entry) => (entry.name ?? '').trim().toLowerCase() === honorName
      );
      safeNameId = match?.id ?? safeContent.barony_names[0]?.id ?? '';
      if (!name.trim() && match?.name) name = match.name;
    } catch {
      safeContent = null;
    }
  }

  $effect(() => {
    // Backstop: if the honor name arrives after mount, fill it when the field
    // is still empty (never overwrite the player's own edits).
    const honorName = $playerGameState?.honor_name;
    if (honorName && !name.trim()) {
      name = honorName;
    }
  });

  async function handleCreate(): Promise<void> {
    if (!$currentCharacter || loading || !safeNameId) return;
    loading = true;
    error = null;
    try {
      const token = auth.getSessionToken();
      const username = auth.getInMemoryCredentials()?.username;
      if (!token || !username) return;
      await createBaronyRequest(
        $currentCharacter.id,
        safeNameId,
        safeTagIds,
        isAdult ? name.trim() : '',
        isAdult ? description.trim() : '',
        isAdult ? acceptsMinors : true,
        { username, token }
      );
      onCreated();
    } catch (e) {
      error = e instanceof Error ? e.message : 'Failed to create barony';
    } finally {
      loading = false;
    }
  }

  $effect(() => {
    loadTexts();
    loadSafeContent();
  });
</script>

<div class="container py-5">
  <div class="d-flex align-items-center mb-4">
    <button class="btn btn-outline-secondary me-3" onclick={onBack}>
      &larr; Back
    </button>
    <h2 class="mb-0">{texts['ui_barony_create_title'] || 'Name Your Barony'}</h2>
  </div>

  <div class="row justify-content-center">
    <div class="col-12 col-md-6">
      {#if error}
        <div class="alert alert-danger">{error}</div>
      {/if}

      <div class="mb-3">
        <label for="barony-safe-name" class="form-label">{texts['ui_barony_create_safe_name'] || 'Safe name'}</label>
        <select id="barony-safe-name" class="form-select" bind:value={safeNameId}>
          {#each safeContent?.barony_names ?? [] as entry (entry.id)}
            <option value={entry.id}>{entry.name ?? entry.id}</option>
          {/each}
        </select>
      </div>

      <div class="mb-3">
        <div class="form-label">{texts['ui_barony_create_tags'] || 'Tags'}</div>
        {#each safeContent?.barony_tags ?? [] as entry (entry.id)}
          <div class="form-check form-check-inline">
            <input
              class="form-check-input"
              type="checkbox"
              id={`barony-tag-${entry.id}`}
              value={entry.id}
              bind:group={safeTagIds}
            />
            <label class="form-check-label" for={`barony-tag-${entry.id}`}>{entry.name ?? entry.id}</label>
          </div>
        {/each}
      </div>

      {#if isAdult}
        <div class="mb-3">
          <label for="barony-name" class="form-label">{texts['ui_barony_create_custom_name'] || 'Custom name (optional)'}</label>
          <input
            id="barony-name"
            type="text"
            class="form-control"
            bind:value={name}
            placeholder="Stormhold"
            maxlength={64}
          />
        </div>

        <div class="mb-3">
          <label for="barony-desc" class="form-label">{texts['ui_barony_create_desc_label'] || 'Description (optional)'}</label>
          <textarea
            id="barony-desc"
            class="form-control"
            bind:value={description}
            rows={3}
            maxlength={256}
          ></textarea>
        </div>

        <div class="form-check mb-3">
          <input class="form-check-input" type="checkbox" id="barony-accepts-minors" bind:checked={acceptsMinors} />
          <label class="form-check-label" for="barony-accepts-minors">
            {texts['ui_barony_create_accepts_minors'] || 'Accept minor members'}
          </label>
        </div>
      {/if}

      <button
        class="btn btn-primary"
        onclick={handleCreate}
        disabled={loading || !safeNameId}
      >
        {#if loading}
          <span class="spinner-border spinner-border-sm me-1" role="status"></span>
        {/if}
        {texts['ui_confirm'] || 'Create'}
      </button>
    </div>
  </div>
</div>
