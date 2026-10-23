<script lang="ts">
  /**
   * Lists the character's received letters (newest first). Selecting a letter
   * renders its full body and marks it read. Reachable from the hub's Letters
   * button, which appears once at least one letter has arrived.
   */
  import { currentCharacter } from '../lib/stores';
  import { getLettersRequest, markLetterReadRequest, type Letter } from '../lib/api';
  import { loadTexts } from '../lib/text';
  import * as auth from '../lib/auth';
  import { handleError } from '../lib/errors';
  import StoryText from './StoryText.svelte';

  interface Props {
    onBack: () => void;
  }

  let { onBack }: Props = $props();

  let texts = $state<Record<string, string>>({});
  let letters = $state<Letter[]>([]);
  let loading = $state(true);
  let selected = $state<Letter | null>(null);

  // The <seal> token becomes the king's-seal image (matches other royal letters).
  const SEAL_TOKEN = `<img src="/images/ui/kings_seal.png" style="width: 10em; height: auto;" alt="King's Seal" />`;

  const TEXT_IDS: string[] = [
    'ui_letters_title', 'ui_letters_empty', 'ui_letters_unread', 'ui_letters_read', 'ui_back_button'
  ];

  /**
   * Loads the panel texts and the character's letters.
   */
  async function load(): Promise<void> {
    if (!$currentCharacter) return;
    loading = true;
    try {
      texts = await loadTexts(TEXT_IDS);
      const creds = auth.getInMemoryCredentials();
      const token = auth.getSessionToken();
      if (!creds || !token) return;
      letters = await getLettersRequest($currentCharacter.id, { username: creds.username, token });
    } catch (e) {
      handleError('Failed to load letters', e);
    } finally {
      loading = false;
    }
  }

  /**
   * Opens a letter and marks it read (updating the local list immediately).
   *
   * @param letter - The letter to open
   */
  async function selectLetter(letter: Letter): Promise<void> {
    selected = letter;
    if (letter.read_at !== 0 || !$currentCharacter) return;
    const creds = auth.getInMemoryCredentials();
    const token = auth.getSessionToken();
    if (!creds || !token) return;
    try {
      await markLetterReadRequest($currentCharacter.id, letter.id, { username: creds.username, token });
      const now = Math.floor(Date.now() / 1000);
      letters = letters.map(l => (l.id === letter.id ? { ...l, read_at: now } : l));
      selected = { ...letter, read_at: now };
    } catch {
      // best effort — the letter still displays
    }
  }

  /**
   * Formats a unix timestamp as a short local date.
   *
   * @param unix - Seconds since the epoch
   * @returns string - Localized date string
   */
  function formatDate(unix: number): string {
    return new Date(unix * 1000).toLocaleDateString();
  }

  $effect(() => {
    load();
  });
</script>

<div class="container py-4">
  <button class="btn btn-outline-secondary mb-4" onclick={onBack}>
    &larr; {texts['ui_back_button'] || 'Back'}
  </button>

  <h2 class="mb-4" style="font-family: serif;">{texts['ui_letters_title'] || 'Letters'}</h2>

  {#if loading}
    <div class="d-flex justify-content-center py-4">
      <div class="spinner-border" role="status"><span class="visually-hidden">Loading...</span></div>
    </div>
  {:else if selected}
    <div class="mb-3">
      <button class="btn btn-outline-secondary btn-sm" onclick={() => (selected = null)}>
        &larr; {texts['ui_letters_title'] || 'Letters'}
      </button>
    </div>
    <StoryText id={selected.letter_key} tokens={{ seal: SEAL_TOKEN }} />
  {:else if letters.length === 0}
    <div class="alert alert-secondary">{texts['ui_letters_empty'] || 'You have received no letters.'}</div>
  {:else}
    <div class="list-group">
      {#each letters as letter}
        <button
          class="list-group-item list-group-item-action d-flex justify-content-between align-items-center"
          onclick={() => selectLetter(letter)}
        >
          <span>
            {formatDate(letter.created_at)}
            {#if letter.read_at === 0}
              <span class="badge bg-primary ms-2">{texts['ui_letters_unread'] || 'New'}</span>
            {/if}
          </span>
          <span class="text-muted small">{texts['ui_letters_read'] || 'Read'} &rarr;</span>
        </button>
      {/each}
    </div>
  {/if}
</div>
