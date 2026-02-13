<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import { loginRequest } from '../lib/api';
  import * as auth from '../lib/auth';
  import { user, characters, currentCharacter, authLoading, authError } from '../lib/stores';

  const dispatch = createEventDispatcher<{
    switchToCreate: void;
  }>();

  let formUsername = $state('');
  let formPassword = $state('');
  let rememberMe = $state(false);

  async function handleLogin() {
    authLoading.set(true);
    authError.set(null);

    try {
      const response = await loginRequest(formUsername, formPassword);

      if (!response.token) {
        throw new Error('No token received from server');
      }

      auth.setSessionToken(response.token);
      auth.setInMemoryCredentials(formUsername, formPassword);

      if (rememberMe) {
        await auth.saveCredentialsLocally(formUsername, formPassword);
      }

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
      const message = err instanceof Error ? err.message : 'Login failed';
      authError.set(message);
    } finally {
      authLoading.set(false);
    }
  }
</script>

<div class="container d-flex justify-content-center align-items-center min-vh-100">
  <div class="card p-4" style="max-width: 400px; width: 100%;">
    <h2 class="mb-4 text-center">Login to Ravenest</h2>

    {#if $authError}
      <div class="alert alert-danger">{$authError}</div>
    {/if}

    <form onsubmit={(e) => { e.preventDefault(); handleLogin(); }}>
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
        />
      </div>

      <div class="mb-3 form-check">
        <input
          type="checkbox"
          class="form-check-input"
          id="rememberMe"
          bind:checked={rememberMe}
          disabled={$authLoading}
        />
        <label class="form-check-label" for="rememberMe">
          Store username/password locally (insecure)
        </label>
      </div>

      <button
        type="submit"
        class="btn btn-primary w-100"
        disabled={$authLoading}
      >
        {#if $authLoading}
          <span class="spinner-border spinner-border-sm me-2" role="status" aria-hidden="true"></span>
          Logging in...
        {:else}
          Login
        {/if}
      </button>
    </form>

    <div class="mt-3 text-center">
      <button class="btn btn-link" onclick={() => dispatch('switchToCreate')}>
        Create an account
      </button>
    </div>
  </div>
</div>
