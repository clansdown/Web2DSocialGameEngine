<script lang="ts">
  import { onMount } from 'svelte';
  import { createEventDispatcher } from 'svelte';
  import { createAccountRequest, verifyAgeOverrideRequest } from '../lib/api';
  import { handleError } from '../lib/errors';
  import * as auth from '../lib/auth';
  import { checkDigitalCredentialsSupport, requestAgeVerification, isBraveBrowser } from '../lib/digitalCredentials';
  import { user, characters, currentCharacter, authLoading } from '../lib/stores';

  const dispatch = createEventDispatcher<{
    switchToLogin: void;
  }>();

  let formUsername = $state('');
  let formPassword = $state('');
  let formConfirmPassword = $state('');
  let adult = $state(false);
  let displayName = $state('');
  let word1 = $state('');
  let word2 = $state('');
  let safeWords1: string[] = $state([]);
  let safeWords2: string[] = $state([]);
  let dcApiSupported = $state(false);
  let isBrave = $state(false);
  let ageVerificationPending = $state(false);
  let ageVerified = $state(false);
  let showOverrideInput = $state(false);
  let overrideCode = $state('');
  let overrideVerifying = $state(false);
  let verifiedOverrideCode = $state('');
  let sex = $state('');
  let submitting = $state(false);

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
      word1 = safeWords1[0];
      word2 = safeWords2[0];
    } catch (err) {
      console.error('Failed to load safe words:', err);
      handleError('Failed to load safe words data', err, { category: 'general', context: 'loadSafeWords' });
    }

    dcApiSupported = await checkDigitalCredentialsSupport();
    isBrave = await isBraveBrowser();
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
   * Handles the adult flag checkbox change with digital credentials verification.
   * If checking the box, requests age verification via Digital Credentials API.
   * If unchecking, simply clears adult and ageVerified flags.
   * 
   * @param event - ChangeEvent from the checkbox input
   * @returns Promise<void>
   * 
   * Usage: Attached to adult checkbox onchange event
   */
  async function handleAdultToggle(event: Event): Promise<void> {
    const target = event.target as HTMLInputElement;
    const checked = target.checked;

    if (!checked) {
      adult = false;
      ageVerified = false;
      verifiedOverrideCode = '';
      return;
    }

    if (!dcApiSupported) {
      const msg = isBrave
        ? 'WARNING: Brave ships with the Digital Credentials API disabled by default. Enable it for access to this feature.'
        : 'Digital Credentials API not supported in this browser. Adult verification unavailable.';
      handleError(msg, new Error('DC API not available'), { category: 'general', context: 'handleAdultToggle' });
      target.checked = false;
      return;
    }

    ageVerificationPending = true;

    try {
      const credential = await requestAgeVerification('create-account');
      if (!credential) {
        handleError(
          'Age verification cancelled or failed. Adult flag will not be set.',
          new Error('Verification cancelled'),
          { category: 'validation', context: 'handleAdultToggle' }
        );
        target.checked = false;
        adult = false;
        ageVerified = false;
      } else {
        adult = true;
        ageVerified = true;
        verifiedOverrideCode = '';
      }
    } catch (err) {
      handleError('Age verification failed. Adult flag will not be set.', err, { category: 'validation', context: 'handleAdultToggle' });
      target.checked = false;
      adult = false;
      ageVerified = false;
      verifiedOverrideCode = '';
    } finally {
      ageVerificationPending = false;
    }
  }

  /**
   * Handles age verification override code submission.
   * Validates the override code against the server.
   * Sets adult and ageVerified on success.
   * 
   * @param none - Uses overrideCode state
   * @returns Promise<void>
   * 
   * Usage: Called when user submits override code form
   */
  async function handleOverrideCodeSubmit(): Promise<void> {
    const code = overrideCode.trim();
    if (!code) return;

    overrideVerifying = true;

    const verified = await verifyAgeOverrideRequest(code);
    if (verified) {
      adult = true;
      ageVerified = true;
      verifiedOverrideCode = code;
      showOverrideInput = false;
      overrideCode = '';
    } else {
      alert('Invalid override code.');
    }

    overrideVerifying = false;
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

    try {
      const response = await createAccountRequest(
        formUsername,
        formPassword,
        adult,
        word1,
        word2,
        displayName || undefined,
        undefined,
        verifiedOverrideCode || undefined,
        sex || undefined
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
              disabled={submitting || overrideVerifying || ageVerificationPending}
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
              disabled={submitting || overrideVerifying || ageVerificationPending}
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
          disabled={submitting || overrideVerifying || ageVerificationPending}
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
          disabled={submitting || overrideVerifying || ageVerificationPending}
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
          disabled={submitting || overrideVerifying || ageVerificationPending}
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
          disabled={submitting || overrideVerifying || ageVerificationPending}
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
          disabled={submitting || overrideVerifying || ageVerificationPending}
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

      <div class="mb-3 form-check">
        <input
          type="checkbox"
          class="form-check-input"
          id="adult"
          bind:checked={adult}
          onchange={handleAdultToggle}
          disabled={$authLoading || ageVerificationPending}
        />
        <label class="form-check-label" for="adult">
          I am 18 or older (enable custom display name)
        </label>
        {#if !dcApiSupported}
          {#if isBrave}
            <div class="form-text text-warning">
              WARNING: Brave ships with the Digital Credentials API disabled by default.
              Enable it at <span class="fw-bold user-select-all">brave://flags/#web-digital-credentials</span>.
            </div>
          {:else}
            <div class="form-text text-warning">
              Digital Credentials API not available in this browser
            </div>
          {/if}
        {/if}
        {#if !ageVerified && !ageVerificationPending}
          <div class="mt-2">
            <button
              type="button"
              class="btn btn-sm btn-link p-0"
              onclick={() => { showOverrideInput = !showOverrideInput; }}
            >
              {showOverrideInput ? 'Cancel' : 'Have an age verification code?'}
            </button>
          </div>
          {#if showOverrideInput}
            <div class="mt-2 d-flex gap-2">
              <input
                type="text"
                class="form-control form-control-sm"
                placeholder="Enter override code"
                bind:value={overrideCode}
                disabled={overrideVerifying}
                onkeydown={(e) => { if (e.key === 'Enter') handleOverrideCodeSubmit(); }}
              />
              <button
                type="button"
                class="btn btn-sm btn-outline-secondary"
                onclick={handleOverrideCodeSubmit}
                disabled={!overrideCode.trim() || overrideVerifying}
              >
                {#if overrideVerifying}
                  <span class="spinner-border spinner-border-sm me-1" role="status" aria-hidden="true"></span>
                {/if}
                Verify
              </button>
            </div>
          {/if}
        {/if}
        {#if ageVerificationPending}
          <div class="form-text text-info">
            Verifying age with digital credential...
          </div>
        {/if}
        {#if ageVerified}
          <div class="form-text text-success">
            Age verified successfully
          </div>
        {/if}
      </div>

      {#if adult}
        <div class="mb-3">
          <label for="displayName" class="form-label">Custom Display Name</label>
          <input
            type="text"
            class="form-control"
            id="displayName"
            bind:value={displayName}
            disabled={submitting || overrideVerifying || ageVerificationPending}
            placeholder="Enter custom display name"
          />
          <div class="form-text">
            Only available for verified adults. Leave blank to use safe name.
          </div>
        </div>
      {/if}

      <button
        type="button"
        class="btn btn-primary w-100"
        onclick={handleCreateAccount}
        disabled={submitting || overrideVerifying || ageVerificationPending}
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
