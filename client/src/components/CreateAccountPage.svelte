<script lang="ts">
  import { onMount } from 'svelte';
  import { createEventDispatcher } from 'svelte';
  import { createAccountRequest, getRegistrationConfigRequest, type registration_mode } from '../lib/api';
  import { loadTexts } from '../lib/text';
  import { handleError } from '../lib/errors';
  import * as auth from '../lib/auth';
  import { user, characters, currentCharacter, language } from '../lib/stores';
  import { get } from 'svelte/store';

  const dispatch = createEventDispatcher<{
    switchToLogin: void;
  }>();

  let formUsername = $state('');
  let formPassword = $state('');
  let formConfirmPassword = $state('');
  let word1 = $state('');
  let word2 = $state('');
  let safeWords1: string[] = $state([]);
  let safeWords2: string[] = $state([]);

  // Account creation always starts non-adult. The removed creation-time 18+
  // Digital Credentials flow is preserved for a future self-service Profile
  // action in docs/adult_verification.md.
  let sex = $state('');
  let submitting = $state(false);
  let reg_mode = $state<registration_mode>('off');
  let invitation_required = $state(false);
  let invitation_code = $state('');
  let registration_texts = $state<Record<string, string>>({});

  function registration_text(id: string): string {
    return registration_texts[id] ?? id;
  }

  /**
   * Selects a random word from a safe-word list so new accounts do not all
   * start with the same suggested character name.
   *
   * @param words - Safe words to choose from
   * @returns A random word, or an empty string when the list is empty
   */
  function pick_random_safe_word(words: string[]): string {
    if (words.length === 0) return '';
    return words[Math.floor(Math.random() * words.length)];
  }

  /**
   * Updates the safe display name preview when word selections change.
   * Currently a placeholder for future preview functionality.
   * 
   * @param none - Uses word1 and word2 state values
   * @returns void
   * 
   * Usage: Called when word1 or word2 selection changes
   */
  function refreshSafeDisplayNamePreview(): void {
    if (word1 && word2) {
    }
  }

  onMount(async () => {
    try {
      const response = await fetch('/safe_words.json');
      const data = await response.json();
      safeWords1 = data.safe_words_1;
      safeWords2 = data.safe_words_2;
      word1 = pick_random_safe_word(safeWords1);
      word2 = pick_random_safe_word(safeWords2);
    } catch (err) {
      console.error('Failed to load safe words:', err);
      handleError('Failed to load safe words data', err, { category: 'general', context: 'loadSafeWords' });
    }

    try {
      const registration = await getRegistrationConfigRequest();
      reg_mode = registration.mode;
      invitation_required = registration.invite_required;
    } catch (err) {
      console.error('Failed to load registration config:', err);
      handleError('Failed to load registration configuration', err, {
        category: 'general',
        context: 'loadRegistrationConfig'
      });
    }

    try {
      registration_texts = await loadTexts([
        'ui_registration_invitation_code',
        'ui_registration_invitation_required',
        'ui_registration_invitation_help'
      ]);
    } catch (err) {
      console.error('Failed to load registration texts:', err);
    }
  });

  /**
   * Handles the first word selection change for character name.
   * Updates word1 state and refreshes the safe display preview.
   * 
   * @param event - ChangeEvent from the select element
   * @returns void
   * 
   * Usage: Attached to word1 select onchange event
   */
  function handleWord1Change(event: Event): void {
    const target = event.target as HTMLSelectElement;
    word1 = target.value;
    refreshSafeDisplayNamePreview();
  }

  /**
   * Handles the for character name.
 second word selection change   * Updates word2 state and refreshes the safe display preview.
   * 
   * @param event - ChangeEvent from the select element
   * @returns void
   * 
   * Usage: Attached to word2 select onchange event
   */
  function handleWord2Change(event: Event): void {
    const target = event.target as HTMLSelectElement;
    word2 = target.value;
    refreshSafeDisplayNamePreview();
  }

  /**
   * Handles the account creation form submission.
   * Validates passwords match and meet length requirements.
   * Calls API to create account and sets up session on success.
   * Updates auth stores and navigates to character selection.
   * 
   * @param none - Uses form state values (username, password, words, etc.)
   * @returns Promise<void>
   * 
   * Usage: Called when form is submitted
   */
  async function handleCreateAccount() {
    submitting = true;

    if (formPassword !== formConfirmPassword) {
      handleError('Passwords do not match', new Error('Password mismatch'), { category: 'validation', context: 'handleCreateAccount' });
      submitting = false;
      return;
    }

    if (formPassword.length < 8) {
      handleError('Password must be at least 8 characters', new Error('Password too short'), { category: 'validation', context: 'handleCreateAccount' });
      submitting = false;
      return;
    }

    if (!word1 || !word2) {
      handleError('Please select both words for your character name', new Error('Missing words'), { category: 'validation', context: 'handleCreateAccount' });
      submitting = false;
      return;
    }

    if (invitation_required && !invitation_code.trim()) {
      handleError(
        registration_text('ui_registration_invitation_required'),
        new Error('Invitation code required'),
        { category: 'validation', context: 'handleCreateAccount' }
      );
      submitting = false;
      return;
    }

    try {
      const response = await createAccountRequest(
        formUsername,
        formPassword,
        word1,
        word2,
        sex || undefined,
        get(language),
        invitation_code.trim() || undefined
      );

      if (!response.token) {
        throw new Error('No token received from server');
      }

      auth.setSessionToken(response.token);
      auth.setInMemoryCredentials(formUsername, formPassword);

      user.set({
        id: response.user_id,
        username: response.username,
        adult: response.adult
      });
      characters.set(response.characters);

      // Persist the chosen interface / display language locally too.
      if (response.language) {
        language.set(response.language);
        await auth.saveStoredLanguage(response.language);
      }

      const lastCharId = await auth.loadCurrentCharacterId();
      const lastChar = response.characters.find(c => c.id === lastCharId) || response.characters[0];
      currentCharacter.set(lastChar || null);

    } catch (err: unknown) {
      alert(`Account creation failed: ${err instanceof Error ? err.message : String(err)}`);
    } finally {
      submitting = false;
    }
  }

  /**
   * Switches the view back to the login page.
   * Dispatches switchToLogin event to parent AuthPage component.
   * 
   * @param none
   * @returns void
   * 
   * Usage: Attached to "Back to Login" button onclick
   */
  function handleSwitchToLogin() {
    dispatch('switchToLogin');
  }
</script>

<div class="container d-flex justify-content-center align-items-center min-vh-100">
  <div class="card p-4" style="max-width: 500px; width: 100%;">
    <h2 class="mb-4 text-center">Create Account</h2>

    <div>
      <fieldset class="mb-3">
        <legend class="form-label">Character Sex</legend>
        <div class="d-flex gap-3">
          <div class="form-check">
            <input
              type="radio"
              class="form-check-input"
              id="sexMale"
              name="sex"
              value="male"
              bind:group={sex}
              disabled={submitting}
            />
            <label class="form-check-label" for="sexMale">♂ Male</label>
          </div>
          <div class="form-check">
            <input
              type="radio"
              class="form-check-input"
              id="sexFemale"
              name="sex"
              value="female"
              bind:group={sex}
              disabled={submitting}
            />
            <label class="form-check-label" for="sexFemale">♀ Female</label>
          </div>
        </div>
      </fieldset>

      <div class="mb-3">
        <label for="username" class="form-label">Username</label>
        <input
          type="text"
          class="form-control"
          id="username"
          bind:value={formUsername}
          disabled={submitting}
          required
        />
      </div>

      <div class="mb-3">
        <label for="password" class="form-label">Password</label>
        <input
          type="password"
          class="form-control"
          id="password"
          bind:value={formPassword}
          disabled={submitting}
          required
          minlength="8"
        />
      </div>

      <div class="mb-3">
        <label for="confirmPassword" class="form-label">Confirm Password</label>
        <input
          type="password"
          class="form-control"
          id="confirmPassword"
          bind:value={formConfirmPassword}
          disabled={submitting}
          required
        />
      </div>

      <div class="mb-3">
        <label for="word1" class="form-label">Character Name - Word 1</label>
        <select
          class="form-select"
          id="word1"
          bind:value={word1}
          onchange={handleWord1Change}
          disabled={submitting}
          required
        >
          {#each safeWords1 as word}
            <option value={word}>{word}</option>
          {/each}
        </select>
      </div>

      <div class="mb-3">
        <label for="word2" class="form-label">Character Name - Word 2</label>
        <select
          class="form-select"
          id="word2"
          bind:value={word2}
          onchange={handleWord2Change}
          disabled={submitting}
          required
        >
          {#each safeWords2 as word}
            <option value={word}>{word}</option>
          {/each}
        </select>
      </div>

      <div class="mb-3">
        <div class="form-text">
          Your character will be named: <strong>{word1}{word2}</strong>
        </div>
      </div>

      {#if reg_mode !== 'off'}
        <div class="mb-3">
          <label for="invitationCode" class="form-label">
            {registration_text('ui_registration_invitation_code')}
          </label>
          <input
            type="text"
            class="form-control text-uppercase"
            id="invitationCode"
            bind:value={invitation_code}
            disabled={submitting}
            required={invitation_required}
            autocomplete="off"
            spellcheck="false"
          />
          <div class="form-text">{registration_text('ui_registration_invitation_help')}</div>
        </div>
      {/if}

      <button
        type="button"
        class="btn btn-primary w-100"
        onclick={handleCreateAccount}
        disabled={submitting}
      >
        {#if submitting}
          <span class="spinner-border spinner-border-sm me-2" role="status" aria-hidden="true"></span>
          Creating account...
        {:else}
          Create Account
        {/if}
      </button>
    </div>

    <div class="mt-3 text-center">
      <button type="button" class="btn btn-link" onclick={handleSwitchToLogin}>
        Back to Login
      </button>
    </div>
  </div>
</div>
