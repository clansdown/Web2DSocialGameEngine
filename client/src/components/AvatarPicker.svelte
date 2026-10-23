<script lang="ts">
  /**
   * AvatarPicker is the character-creation PFP step. The player must choose a
   * safe-pool avatar before continuing (there is no random assignment). If the
   * safe pool is empty (art not authored yet) the step calls `onSkip` so
   * creation is never blocked; the Profile activity is where a portrait is
   * chosen later.
   *
   * The 18+ custom upload flow lives in ProfilePanel; this step is safe-pool
   * only.
   */
  import { onMount } from 'svelte';
  import { currentCharacter } from '../lib/stores';
  import { getAvatarPoolRequest, setCharacterAvatarRequest, type safe_avatar_dto } from '../lib/api';
  import { loadTexts } from '../lib/text';
  import SafeAvatarGrid from './SafeAvatarGrid.svelte';

  interface Props {
    onConfirm: () => void;
    onSkip: () => void;
  }

  let { onConfirm, onSkip }: Props = $props();

  const TEXT_IDS: string[] = [
    'ui_avatar_select_title',
    'ui_avatar_note',
    'ui_confirm',
    'ui_loading'
  ];

  let texts = $state<Record<string, string>>({});
  let pool = $state<safe_avatar_dto[]>([]);
  let selected = $state<string | null>(null);
  let loading = $state(true);
  let submitting = $state(false);
  let errorMsg = $state('');

  onMount(async () => {
    texts = await loadTexts(TEXT_IDS);
    try {
      // The safe pool is gender-split; request the character's own pool.
      const gender = $currentCharacter?.sex === 'female' ? 'female' : 'male';
      const data = await getAvatarPoolRequest(gender);
      pool = data.safe;
      // No art authored yet — nothing to choose, don't block character creation.
      if (pool.length === 0) {
        onSkip();
        return;
      }
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
    } finally {
      loading = false;
    }
  });

  async function handleConfirm(): Promise<void> {
    const character = $currentCharacter;
    if (!selected || !character) return;
    submitting = true;
    errorMsg = '';
    try {
      const update = await setCharacterAvatarRequest(character.id, selected);
      currentCharacter.set({ ...character, ...update });
      onConfirm();
    } catch (e) {
      errorMsg = e instanceof Error ? e.message : String(e);
    } finally {
      submitting = false;
    }
  }
</script>

<div class="container py-5">
  <h2 class="text-center mb-4">{texts['ui_avatar_select_title'] || 'Choose Your Portrait'}</h2>
  <p class="text-center text-muted small">{texts['ui_avatar_note'] || ''}</p>

  {#if errorMsg}
    <div class="alert alert-danger small py-1">{errorMsg}</div>
  {/if}

  {#if loading}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">{texts['ui_loading'] || 'Loading...'}</span>
      </div>
    </div>
  {:else}
    <SafeAvatarGrid avatars={pool} {selected} onSelect={(id) => { selected = id; }} />

    <div class="text-center mt-4">
      <button
        class="btn btn-primary btn-lg px-5"
        disabled={!selected || submitting}
        onclick={handleConfirm}
      >
        {#if submitting}
          <span class="spinner-border spinner-border-sm me-2" role="status" aria-hidden="true"></span>
        {/if}
        {texts['ui_confirm'] || 'Confirm'}
      </button>
    </div>
  {/if}
</div>
