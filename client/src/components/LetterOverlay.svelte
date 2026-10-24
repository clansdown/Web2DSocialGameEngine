<script lang="ts">
  /**
   * One-time overlay shown when the character has an unread letter. Renders the
   * letter body through StoryText (which substitutes {character_name}/gender
   * server-side) and marks it read on dismissal.
   */
  import { currentCharacter } from '../lib/stores';
  import { markLetterReadRequest, type UnreadLetter } from '../lib/api';
  import { asset_url } from '../lib/public_urls';
  import * as auth from '../lib/auth';
  import StoryText from './StoryText.svelte';
  import DialogOverlay from './DialogOverlay.svelte';

  interface Props {
    letter: UnreadLetter;
    onDismiss: () => void;
  }

  let { letter, onDismiss }: Props = $props();
  let dismissing = $state(false);

  // The <seal> token becomes the king's-seal image (matches other royal letters).
  const SEAL_TOKEN = `<img src="${asset_url('/images/ui/kings_seal.png')}" style="width: 10em; height: auto;" alt="King's Seal" />`;

  /**
   * Marks the letter read server-side (best effort) and closes the overlay.
   */
  async function dismiss(): Promise<void> {
    if (dismissing) return;
    dismissing = true;
    const creds = auth.getInMemoryCredentials();
    const token = auth.getSessionToken();
    if (creds && token && $currentCharacter) {
      try {
        await markLetterReadRequest($currentCharacter.id, letter.id, { username: creds.username, token });
      } catch {
        // best effort — the overlay still closes
      }
    }
    dismissing = false;
    onDismiss();
  }
</script>

<DialogOverlay title="" size="xlg" noPadding onDismiss={dismiss}>
  <StoryText id={letter.letter_key} tokens={{ seal: SEAL_TOKEN }} />
</DialogOverlay>
