<script lang="ts">
  /**
   * Displays a language selection grid as the very first screen on app start.
   * Shows all available languages in their native script.
   * Saves the choice to OPFS and updates the language store.
   */

  import { language } from '../lib/stores';
  import * as auth from '../lib/auth';

  interface LanguageOption {
    code: string;
    native_name: string;
  }

  let { onLanguageChosen }: { onLanguageChosen: () => void } = $props();

  const languages: LanguageOption[] = [
    { code: 'en', native_name: 'English' },
    { code: 'es', native_name: 'Español' },
    { code: 'de', native_name: 'Deutsch' },
    { code: 'fr', native_name: 'Français' },
    { code: 'it', native_name: 'Italiano' },
    { code: 'ar', native_name: 'العربية' },
    { code: 'zh-CN', native_name: '简体中文' },
    { code: 'ko', native_name: '한국어' },
    { code: 'ja', native_name: '日本語' },
  ];

  /**
   * Handles language selection by saving to OPFS, updating the store,
   * and triggering the callback to proceed.
   *
   * @param code - Language code to select
   */
  async function selectLanguage(code: string): Promise<void> {
    try {
      language.set(code);
      await auth.saveStoredLanguage(code);
      onLanguageChosen();
    } catch (error) {
      alert(`Language selection failed: ${error instanceof Error ? error.message : String(error)}`);
    }
  }
</script>

<div class="container d-flex flex-column justify-content-center align-items-center min-vh-100">
  <div class="text-center mb-5">
    <h1 class="display-5 fw-bold">Ravenest</h1>
    <p class="text-muted">Build and Battle</p>
  </div>

  <div class="row g-3 justify-content-center" style="max-width: 500px;">
    {#each languages as lang}
      <div class="col-4">
        <button
          class="btn btn-outline-light w-100 py-3 fs-5"
          onclick={() => selectLanguage(lang.code)}
        >
          {lang.native_name}
        </button>
      </div>
    {/each}
  </div>
</div>
