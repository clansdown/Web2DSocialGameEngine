<script lang="ts">
  import { onMount } from 'svelte';
  import AuthPage from './components/AuthPage.svelte';
  import CharacterSelect from './components/CharacterSelect.svelte';
  import ErrorDisplay from './components/ErrorDisplay.svelte';
  import * as auth from './lib/auth';
  import { user, characters, currentCharacter, isLoggedIn, authLoading } from './lib/stores';
  import { loginRequest, refreshToken } from './lib/api';

  let needsAuth = $state(false);
  let initialized = $state(false);

  async function attemptAutoLogin() {
    const storedCreds = await auth.loadStoredCredentials();
    if (storedCreds) {
      authLoading.set(true);
      try {
        const response = await loginRequest(storedCreds.username, storedCreds.password);

        if (response.token) {
          auth.setSessionToken(response.token);
          auth.setInMemoryCredentials(storedCreds.username, storedCreds.password);

          user.set({
            id: response.user_id,
            username: response.username,
            adult: response.adult
          });
          characters.set(response.characters);

          const lastCharId = await auth.loadCurrentCharacterId();
          const lastChar = response.characters.find(c => c.id === lastCharId) || response.characters[0];
          currentCharacter.set(lastChar || null);

          needsAuth = false;
          return;
        }
      } catch {
        await auth.clearStoredCredentials();
      } finally {
        authLoading.set(false);
      }
    }
    needsAuth = true;
  }

  async function handleNeedsAuth() {
    const creds = auth.getInMemoryCredentials();
    if (creds) {
      try {
        const result = await refreshToken(creds.username, creds.password);
        auth.setSessionToken(result.token);
        return true;
      } catch {
        auth.clearInMemoryCredentials();
        needsAuth = true;
        return false;
      }
    }
    needsAuth = true;
    return false;
  }

  $effect(() => {
    if (initialized && !auth.hasSession()) {
      handleNeedsAuth();
    }
  });

  onMount(async () => {
    if (!auth.hasSession()) {
      await attemptAutoLogin();
    }
    initialized = true;
  });
</script>

<ErrorDisplay />

{#if $authLoading}
  <div class="container d-flex justify-content-center align-items-center min-vh-100">
    <div class="spinner-border" role="status">
      <span class="visually-hidden">Loading...</span>
    </div>
  </div>
{:else if !auth.hasSession()}
  <AuthPage />
{:else if !$currentCharacter}
  <CharacterSelect />
{:else}
  <div class="container py-5">
    <div class="d-flex justify-content-between align-items-center mb-4">
      <h1>Ravenest</h1>
      <div>
        <span class="me-3">Playing as: <strong>{$currentCharacter?.display_name}</strong> (Level {$currentCharacter?.level})</span>
      </div>
    </div>
    <div class="alert alert-info">
      Game UI coming soon...
    </div>
  </div>
{/if}
