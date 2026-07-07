/**
 * Displays the character sex selection screen for new or legacy characters.
 * Shows two cards (Male / Female) with icons, loads text from the server,
 * and calls the API to persist the choice.
 */
<script lang="ts">
  import { language, currentCharacter } from '../lib/stores';
  import { getTextsRequest, setCharacterSexRequest } from '../lib/api';
  import * as auth from '../lib/auth';

  interface Props {
    onConfirm: () => void;
  }

  let { onConfirm }: Props = $props();

  let selectedSex = $state<string | null>(null);
  let texts = $state<Record<string, string>>({});
  let textsLoaded = $state(false);
  let submitting = $state(false);

  const TEXT_IDS: string[] = [
    'ui_sex_select_title',
    'ui_sex_male',
    'ui_sex_female',
    'ui_confirm',
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

  function handleCardClick(sex: string): void {
    selectedSex = sex;
  }

  async function handleConfirm(): Promise<void> {
    if (!selectedSex || !$currentCharacter) return;

    submitting = true;
    const token = auth.getSessionToken();
    const username = auth.getInMemoryCredentials()?.username;
    if (!token || !username) {
      submitting = false;
      return;
    }

    try {
      const updated = await setCharacterSexRequest($currentCharacter.id, selectedSex, { username, token });
      currentCharacter.set(updated);
      onConfirm();
    } catch {
      submitting = false;
    }
  }

  $effect(() => {
    loadTexts();
  });
</script>

<div class="container py-5">
  <h2 class="text-center mb-4">{texts['ui_sex_select_title'] || 'Choose Your Character\'s Sex'}</h2>

  {#if !textsLoaded}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Loading...</span>
      </div>
    </div>
  {:else}
    <div class="row g-4 justify-content-center">
      <div class="col-12 col-sm-8 col-md-6 col-lg-4">
        <div
          class="card text-center h-100 {selectedSex === 'male' ? 'border-primary border-2' : ''}"
          style="cursor: pointer;"
          role="button"
          tabindex="0"
          onclick={() => handleCardClick('male')}
          onkeydown={(e) => { if (e.key === 'Enter') handleCardClick('male'); }}
        >
          <div class="card-body py-5">
            <div class="display-1 mb-3">♂</div>
            <h4 class="card-title">{texts['ui_sex_male'] || 'Male'}</h4>
          </div>
        </div>
      </div>

      <div class="col-12 col-sm-8 col-md-6 col-lg-4">
        <div
          class="card text-center h-100 {selectedSex === 'female' ? 'border-primary border-2' : ''}"
          style="cursor: pointer;"
          role="button"
          tabindex="0"
          onclick={() => handleCardClick('female')}
          onkeydown={(e) => { if (e.key === 'Enter') handleCardClick('female'); }}
        >
          <div class="card-body py-5">
            <div class="display-1 mb-3">♀</div>
            <h4 class="card-title">{texts['ui_sex_female'] || 'Female'}</h4>
          </div>
        </div>
      </div>
    </div>

    <div class="text-center mt-4">
      <button
        class="btn btn-primary btn-lg px-5"
        disabled={!selectedSex || submitting}
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
