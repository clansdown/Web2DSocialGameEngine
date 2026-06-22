<script lang="ts">
  import { onMount } from 'svelte';
  import AuthPage from './components/AuthPage.svelte';
  import CharacterSelect from './components/CharacterSelect.svelte';
  import MissionSelect from './components/MissionSelect.svelte';
  import TownView from './components/TownView.svelte';
  import ErrorDisplay from './components/ErrorDisplay.svelte';
  import MiniGameSelect from './minigames/MiniGameSelect.svelte';
  import MiniGameContainer from './minigames/MiniGameContainer.svelte';
  import * as auth from './lib/auth';
  import { user, characters, currentCharacter, authLoading, playerGameState } from './lib/stores';
  import { loginRequest, refreshToken } from './lib/api';
  import { fetchPlayerState } from './lib/game_state';
  import type { EndMiniGameResponse } from './lib/api';
  import { handleError } from './lib/errors';

  let needsAuth = $state(false);
  let initialized = $state(false);
  let selectedMiniGame = $state<string | null>(null);
  let activeMiniGame = $state<string | null>(null);

  /**
   * Attempts to automatically log in using stored credentials.
   * Checks OPFS for saved username/password, authenticates with server,
   * and restores user session state if credentials are valid.
   * Shows auth screen if no stored credentials or login fails.
   * 
   * @param none - Uses stored credentials from OPFS
   * @returns Promise<void> - Updates auth stores on success
   * 
   * Usage: Called from onMount when app initializes
   */
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

  /**
   * Handles re-authentication when session is lost or expired.
   * Attempts to refresh token using in-memory credentials.
   * Shows auth screen if no credentials available or refresh fails.
   * 
   * @param none - Uses in-memory credentials from auth module
   * @returns Promise<boolean> - true if re-auth successful, false otherwise
   * 
   * Usage: Called by $effect when session is lost
   */
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

  /**
   * Loads the player's game state from the server after a character is selected.
   * Resets routing state to ensure the correct view is shown.
   */
  async function loadGameState() {
    if (!$currentCharacter) return;

    selectedMiniGame = null;
    activeMiniGame = null;

    try {
      await fetchPlayerState($currentCharacter.id);
    } catch (e) {
      handleError('Failed to load game state', e);
    }
  }

  /**
   * Handles the start of a mini-game from the selection grid.
   * Sets the active mini-game which triggers MiniGameContainer to render.
   * 
   * @param _levelId - The level ID to start (passed through to the container)
   */
  function handleStartLevel(_levelId: number) {
    if (!selectedMiniGame) return;
    activeMiniGame = selectedMiniGame;
  }

  /**
   * Handles the completion of a mini-game session.
   * Clears the active mini-game and refreshes game state.
   * The routing auto-transitions to the correct view based on updated state.
   * 
   * @param _results - The results from the mini-game session
   */
  function handleGameComplete(_results: EndMiniGameResponse) {
    activeMiniGame = null;
  }

  /**
   * Handles errors from mini-game operations.
   * Delegates to the global error handler for display.
   * 
   * @param error - Error message string
   */
  function handleGameError(error: string) {
    handleError('Mini-game error', new Error(error));
  }

  /**
   * Handles selecting a mini-game path from MissionSelect.
   * 
   * @param miniGame - The selected mini-game name
   */
  function handleSelectMission(miniGame: string) {
    selectedMiniGame = miniGame;
  }

  /**
   * Handles going back from MiniGameSelect to MissionSelect.
   */
  function handleBackToMissions() {
    selectedMiniGame = null;
  }

  /**
   * Handles starting a replay mini-game from TownView (sandbox phase).
   * 
   * @param miniGame - The mini-game to replay
   */
  function handleReplayMiniGame(miniGame: string) {
    selectedMiniGame = miniGame;
    activeMiniGame = miniGame;
  }

  $effect(() => {
    if (initialized && !auth.hasSession()) {
      handleNeedsAuth();
    }
  });

  $effect(() => {
    if ($currentCharacter && initialized) {
      loadGameState();
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
{:else if activeMiniGame}
  <MiniGameContainer
    miniGame={activeMiniGame}
    onComplete={handleGameComplete}
    onError={handleGameError}
  />
{:else if !$playerGameState}
  <div class="container d-flex justify-content-center align-items-center min-vh-50 py-5">
    <div class="spinner-border" role="status">
      <span class="visually-hidden">Loading game state...</span>
    </div>
  </div>
{:else if $playerGameState.game_phase === 'initial_mission'}
  {#if selectedMiniGame === null}
    <MissionSelect onSelect={handleSelectMission} />
  {:else}
    <MiniGameSelect
      miniGame={selectedMiniGame}
      onStartLevel={handleStartLevel}
      onBack={handleBackToMissions}
    />
  {/if}
{:else if $playerGameState.game_phase === 'sandbox'}
  <TownView onPlayMiniGame={handleReplayMiniGame} />
{/if}
