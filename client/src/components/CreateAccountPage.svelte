<script lang="ts">
  import { onMount } from 'svelte';
  import { createEventDispatcher } from 'svelte';
  import { createAccountRequest } from '../lib/api';
  import { handleError } from '../lib/errors';
  import * as auth from '../lib/auth';
  import { checkDigitalCredentialsSupport, requestAgeVerification } from '../lib/digitalCredentials';
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
  let ageVerificationPending = $state(false);
  let ageVerified = $state(false);

  function updateSafeDisplayPreview(): void {
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
  });

  function handleWord1Change(event: Event): void {
    const target = event.target as HTMLSelectElement;
    word1 = target.value;
    updateSafeDisplayPreview();
  }

  function handleWord2Change(event: Event): void {
    const target = event.target as HTMLSelectElement;
    word2 = target.value;
    updateSafeDisplayPreview();
  }

  async function handleAdultChange(event: Event): Promise<void> {
    const target = event.target as HTMLInputElement;
    const checked = target.checked;

    if (!checked) {
      adult = false;
      ageVerified = false;
      return;
    }

    if (!dcApiSupported) {
      handleError(
        'Digital Credentials API not supported in this browser. Adult verification unavailable.',
        new Error('DC API not available'),
        { category: 'general', context: 'handleAdultChange' }
      );
      target.checked = false;
      return;
    }

    ageVerificationPending = true;
    authLoading.set(true);

    try {
      const credential = await requestAgeVerification('create-account');
      if (!credential) {
        handleError(
          'Age verification cancelled or failed. Adult flag will not be set.',
          new Error('Verification cancelled'),
          { category: 'validation', context: 'handleAdultChange' }
        );
        target.checked = false;
        adult = false;
        ageVerified = false;
      } else {
        adult = true;
        ageVerified = true;
      }
    } catch (err) {
      handleError('Age verification failed. Adult flag will not be set.', err, { category: 'validation', context: 'handleAdultChange' });
      target.checked = false;
      adult = false;
      ageVerified = false;
    } finally {
      ageVerificationPending = false;
      authLoading.set(false);
    }
  }

  async function handleSubmit() {
    authLoading.set(true);

    if (formPassword !== formConfirmPassword) {
      handleError('Passwords do not match', new Error('Password mismatch'), { category: 'validation', context: 'handleSubmit' });
      authLoading.set(false);
      return;
    }

    if (formPassword.length < 8) {
      handleError('Password must be at least 8 characters', new Error('Password too short'), { category: 'validation', context: 'handleSubmit' });
      authLoading.set(false);
      return;
    }

    if (!word1 || !word2) {
      handleError('Please select both words for your character name', new Error('Missing words'), { category: 'validation', context: 'handleSubmit' });
      authLoading.set(false);
      return;
    }

    try {
      const response = await createAccountRequest(
        formUsername,
        formPassword,
        adult,
        word1,
        word2,
        displayName || undefined
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
      handleError('Account creation failed', err, { category: 'auth', context: 'handleSubmit' });
    } finally {
      authLoading.set(false);
    }
  }

  function handleSwitchToLogin() {
    dispatch('switchToLogin');
  }
</script>

<div class="container d-flex justify-content-center align-items-center min-vh-100">
  <div class="card p-4" style="max-width: 500px; width: 100%;">
    <h2 class="mb-4 text-center">Create Account</h2>

    <form onsubmit={(e) => { e.preventDefault(); handleSubmit(); }}>
      <div class="mb-3">
        <label for="username" class="form-label">Username</label>
        <input
          type="text"
          class="form-control"
          id="username"
          bind:value={formUsername}
          disabled={$authLoading}
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
          disabled={$authLoading}
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
          disabled={$authLoading}
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
          disabled={$authLoading}
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
          disabled={$authLoading}
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
          onchange={handleAdultChange}
          disabled={$authLoading || ageVerificationPending}
        />
        <label class="form-check-label" for="adult">
          I am 18 or older (enable custom display name)
        </label>
        {#if !dcApiSupported}
          <div class="form-text text-warning">
            Digital Credentials API not available in this browser
          </div>
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
            disabled={$authLoading}
            placeholder="Enter custom display name"
          />
          <div class="form-text">
            Only available for verified adults. Leave blank to use safe name.
          </div>
        </div>
      {/if}

      <button
        type="submit"
        class="btn btn-primary w-100"
        disabled={$authLoading || ageVerificationPending}
      >
        {#if $authLoading}
          <span class="spinner-border spinner-border-sm me-2" role="status" aria-hidden="true"></span>
          Creating account...
        {:else}
          Create Account
        {/if}
      </button>
    </form>

    <div class="mt-3 text-center">
      <button class="btn btn-link" onclick={handleSwitchToLogin}>
        Back to Login
      </button>
    </div>
  </div>
</div>
