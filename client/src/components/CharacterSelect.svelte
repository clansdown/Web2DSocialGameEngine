<script lang="ts">
  import { characters, currentCharacter, user } from '../lib/stores';
  import * as auth from '../lib/auth';
  import type { Character } from '../lib/api';

  /**
   * Selects a character as the active character for the session.
   * Updates currentCharacter store and persists selection to OPFS.
   * 
   * @param character - Character object to select
   * @returns Promise<void>
   * 
   * Usage: Attached to character card onclick/keydown events
   */
  async function selectCharacter(character: Character) {
    currentCharacter.set(character);
    await auth.saveCurrentCharacterId(character.id);
  }

  /**
   * Logs out the current user by clearing all session data.
   * Clears session token, in-memory credentials, and stored credentials.
   * Resets all auth stores to initial state.
   * 
   * @param none
   * @returns Promise<void>
   * 
   * Usage: Attached to Logout button onclick
   */
  async function handleLogout() {
    await auth.logout();
    user.set(null);
    characters.set([]);
    currentCharacter.set(null);
  }
</script>

<div class="container py-5">
  <div class="d-flex justify-content-between align-items-center mb-4">
    <h2>Select a Character</h2>
    <div>
      <span class="me-3">Logged in as: <strong>{$user?.username}</strong></span>
      <button class="btn btn-outline-secondary btn-sm" onclick={handleLogout}>
        Logout
      </button>
    </div>
  </div>

  {#if $characters.length === 0}
    <div class="alert alert-warning">No characters found</div>
  {:else}
    <div class="row">
      {#each $characters as char}
        <div class="col-md-4 mb-3">
          <div
            class="card h-100 {$currentCharacter?.id === char.id ? 'border-primary' : ''}"
            style="cursor: pointer;"
            onclick={() => selectCharacter(char)}
            role="button"
            tabindex="0"
            onkeydown={(e) => { if (e.key === 'Enter') selectCharacter(char); }}
          >
            <div class="card-body">
              <h5 class="card-title">{char.display_name}</h5>
              <p class="card-text">Level {char.level}</p>
              {#if $currentCharacter?.id === char.id}
                <span class="badge bg-primary">Active</span>
              {/if}
            </div>
          </div>
        </div>
      {/each}
    </div>
  {/if}
</div>
