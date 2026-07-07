<script lang="ts">
  /**
   * Displays the starting path selection screen for new characters.
   * Shows two path cards (Wolf Warden / Assarter of the Wildlands)
   * with path images and names fetched from the server text system.
   *
   * Fetches the necessary text IDs on mount in the current language.
   */

  import { language } from '../lib/stores';
  import { getTextsRequest } from '../lib/api';
  import StoryText from './StoryText.svelte';

  interface Props {
    onConfirm: (archetype: string) => void;
  }

  let { onConfirm }: Props = $props();

  let selectedPath = $state<string | null>(null);
  let texts = $state<Record<string, string>>({});
  let textsLoaded = $state(false);

  const TEXT_IDS: string[] = [
    'ui_path_select_title',
    'ui_path_select_intro',
    'ui_path_wolf_warden',
    'ui_path_assarter',
    'ui_confirm',
  ];

  /**
   * Loads translated text from the server for path selection UI.
   * Uses the current language from the language store.
   */
  async function loadTexts(): Promise<void> {
    textsLoaded = false;
    try {
      texts = await getTextsRequest($language, TEXT_IDS);
    } catch {
      // Fall back to English IDs as display — empty strings handled in template
      texts = {};
    } finally {
      textsLoaded = true;
    }
  }

  /**
   * Handles card click: selects the path for visual feedback.
   *
   * @param archetype - 'wolf_warden' or 'assarter'
   */
  function handleCardClick(archetype: string): void {
    selectedPath = archetype;
  }

  /**
   * Handles confirm button click: emits the selected archetype.
   */
  function handleConfirm(): void {
    if (selectedPath) {
      onConfirm(selectedPath);
    }
  }

  $effect(() => {
    loadTexts();
  });
</script>

<div class="container py-5">
  <h2 class="text-center mb-4">{texts['ui_path_select_title'] || 'Choose Your Path'}</h2>

  {#if !textsLoaded}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Loading...</span>
      </div>
    </div>
  {:else}
    {#if texts['ui_path_select_intro']}
      <div class="mb-5 mx-auto" style="max-width: 900px;">
        <StoryText text={texts['ui_path_select_intro']} />
      </div>
    {/if}
    <div class="row g-4 justify-content-center">
      <!-- Wolf Warden Card -->
      <div class="col-12 col-md-6 col-lg-5">
        <div
          class="card text-center h-100 {selectedPath === 'wolf_warden' ? 'border-primary border-2' : ''}"
          style="cursor: pointer;"
          role="button"
          tabindex="0"
          onclick={() => handleCardClick('wolf_warden')}
          onkeydown={(e) => { if (e.key === 'Enter') handleCardClick('wolf_warden'); }}
        >
          <img src="/images/ui/path_wolf_warden.png" alt="Wolf Warden" class="img-fluid rounded mx-auto mt-3 d-block" style="width: 200px; height: 200px; object-fit: cover;" />
          <div class="card-body">
            <h4 class="card-title">{texts['ui_path_wolf_warden'] || 'Wolf Warden'}</h4>
          </div>
        </div>
      </div>

      <!-- Assarter Card -->
      <div class="col-12 col-md-6 col-lg-5">
        <div
          class="card text-center h-100 {selectedPath === 'assarter' ? 'border-primary border-2' : ''}"
          style="cursor: pointer;"
          role="button"
          tabindex="0"
          onclick={() => handleCardClick('assarter')}
          onkeydown={(e) => { if (e.key === 'Enter') handleCardClick('assarter'); }}
        >
          <!-- Image placeholder: path_assarter.png — settler with axe clearing land -->
          <div
            class="d-flex align-items-center justify-content-center bg-secondary-subtle mx-auto mt-3 rounded"
            style="width: 200px; height: 200px;"
          >
            <span class="text-muted">Assarter</span>
          </div>
          <div class="card-body">
            <h4 class="card-title">{texts['ui_path_assarter'] || 'Assarter of the Wildlands'}</h4>
          </div>
        </div>
      </div>
    </div>

    <div class="text-center mt-4">
      <button
        class="btn btn-primary btn-lg px-5"
        disabled={!selectedPath}
        onclick={handleConfirm}
      >
        {texts['ui_confirm'] || 'Confirm'}
      </button>
    </div>
  {/if}
</div>
