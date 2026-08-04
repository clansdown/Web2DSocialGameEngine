<script lang="ts">
  import { onMount } from 'svelte';
  import AuthPage from './components/AuthPage.svelte';
  import CharacterSelect from './components/CharacterSelect.svelte';
  import HubScreen from './components/HubScreen.svelte';
  import ErrorDisplay from './components/ErrorDisplay.svelte';
  import LanguageSelect from './components/LanguageSelect.svelte';
  import PathSelect from './components/PathSelect.svelte';
  import SexSelect from './components/SexSelect.svelte';
  import BaronyJoin from './components/BaronyJoin.svelte';
  import BaronyCreate from './components/BaronyCreate.svelte';
  import DialogOverlay from './components/DialogOverlay.svelte';
  import StoryText from './components/StoryText.svelte';
  // MiniGameSelect is used internally by HubScreen
  import MiniGameContainer from './minigames/MiniGameContainer.svelte';
  import * as auth from './lib/auth';
  import { user, characters, currentCharacter, authLoading, playerGameState, language, isAuthenticated } from './lib/stores';
  import { loginRequest, refreshToken, setCharacterArchetypeRequest, setCharacterSexRequest, acknowledgeLandPatentRequest } from './lib/api';
  import { loadTexts, loadText } from './lib/text';
  import { fetchPlayerState } from './lib/game_state';
  import type { EndMiniGameResponse } from './lib/api';
  import { handleError } from './lib/errors';

  let needsAuth = $state(false);
  let initialized = $state(false);
  let activeMiniGame = $state<string | null>(null);
  let selectedLevelId = $state<number>(0);

  // Language selection state
  let languageLoading = $state(true);
  let languageSet = $state(false);

  // Path selection state
  let showIntroOverlay = $state(false);
  let introTitle = $state('');
  let introBody = $state('');

  // Barony flow state
  let showBaronyList = $state(false);
  let showBaronyCreate = $state(false);
  let showBaronyGrid = $state(false);

  // Land patent notification state
  let showLandPatent = $state(false);
  let landPatentText = $state('');
  let landPatentHandled = $state(false);

  // Barony patent notification state (shown when the baron track is completed)
  let showBaronyPatent = $state(false);
  let baronyPatentText = $state('');

  /**
   * Loads stored language preference on startup.
   */
  async function initLanguage(): Promise<void> {
    languageLoading = true;
    try {
      const stored = await auth.loadStoredLanguage();
      if (stored) {
        language.set(stored);
        languageSet = true;
      }
    } catch {
      // Ignore — user will see LanguageSelect
    } finally {
      languageLoading = false;
    }
  }

  function handleLanguageChosen(): void {
    languageSet = true;
  }

  /**
   * Attempts to automatically log in using stored credentials.
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
   */
  async function loadGameState() {
    if (!$currentCharacter) return;

    activeMiniGame = null;

    try {
      await fetchPlayerState($currentCharacter.id);
    } catch (e) {
      handleError('Failed to load game state', e);
    }
  }

  /**
   * Handles confirmation of starting path archetype.
   */
  function handleSetSex(): void {
    // SexSelect updates currentCharacter internally — just re-check routing
  }

  async function handleConfirmPath(archetype: string): Promise<void> {
    if (!$currentCharacter) return;

    try {
      const token = auth.getSessionToken();
      const username = auth.getInMemoryCredentials()?.username;
      if (!token || !username) {
        handleError('Authentication required', new Error('No session'));
        return;
      }

      const updatedChar = await setCharacterArchetypeRequest(
        $currentCharacter.id,
        archetype,
        { username, token }
      );

      currentCharacter.set(updatedChar);

      const textIds = archetype === 'wolf_warden'
        ? ['path_wolf_warden_intro_title', 'path_wolf_warden_intro_body']
        : ['path_assarter_intro_title', 'path_assarter_intro_body'];

      const introTexts = await loadTexts(textIds);

      introTitle = introTexts[textIds[0]] || '';
      introBody = introTexts[textIds[1]] || '';
      showIntroOverlay = true;

    } catch (e) {
      handleError('Failed to set character path', e);
    }
  }

  function handleIntroDismiss(): void {
    showIntroOverlay = false;
  }

  function handleStartLevel(gameId: string, levelId: number) {
    activeMiniGame = gameId;
    selectedLevelId = levelId;
  }

  function handleGameComplete(_results: EndMiniGameResponse) {
    activeMiniGame = null;
  }

  function handleGameError(error: string) {
    handleError('Mini-game error', new Error(error));
  }

  // ── Barony flow handlers ──────────────────────────────────────

  function handleGoToJoinBarony(): void {
    showBaronyList = true;
  }

  function handleBaronyJoined(): void {
    showBaronyList = false;
    // Fetch game state to update phase to sandbox
    if ($currentCharacter) {
      fetchPlayerState($currentCharacter.id);
    }
  }

  function handleBackFromBaronyList(): void {
    showBaronyList = false;
  }

  function handleBaronTrackStarted(): void {
    // Game phase is now baron_track — the routing will show MiniGameSelect
    // Fetch game state to refresh
    if ($currentCharacter) {
      fetchPlayerState($currentCharacter.id);
    }
  }

  function handleGameCompleteFromGrid(results: EndMiniGameResponse) {
    activeMiniGame = null;
    if ($currentCharacter) {
      fetchPlayerState($currentCharacter.id);
    }
    // If we just completed the baron track, show the barony patent first, then
    // the create barony screen once it is dismissed.
    if (results.baron_right_earned) {
      loadText('barony_patent_earned').then((raw) => {
        let text = raw;
        text = text.replace(/\<seal\>/g, `\n\n<img src="/images/ui/kings_seal.png" style="width: 10em; height: auto;" alt="King's Seal" />`);
        baronyPatentText = text;
        showBaronyPatent = true;
      });
    }
  }

  /**
   * Dismisses the land patent notification and acknowledges it server-side.
   */
  async function dismissLandPatent(): Promise<void> {
    const creds = auth.getInMemoryCredentials();
    const token = auth.getSessionToken();
    if (creds && token && $currentCharacter) {
      try {
        await acknowledgeLandPatentRequest($currentCharacter.id, { username: creds.username, token });
      } catch { /* best effort */ }
    }
    showLandPatent = false;
    landPatentHandled = true;
  }

  /**
   * Dismisses the barony patent notification and proceeds to create the barony
   * (the player's honor name is already prefilled).
   */
  function dismissBaronyPatent(): void {
    showBaronyPatent = false;
    showBaronyCreate = true;
  }

  /**
   * Shows the land patent overlay when the server flags an unacknowledged
   * patent transition in the player state response.
   */
  $effect(() => {
    const state = $playerGameState;
    if (!state || landPatentHandled) return;
    if ((state as any).land_patent_earned && !showLandPatent) {
      loadText('land_patent_earned').then((raw) => {
        let text = raw;
        text = text.replace(/\<seal\>/g, `\n\n<img src="/images/ui/kings_seal.png" style="width: 10em; height: auto;" alt="King's Seal" />`);
        landPatentText = text;
        showLandPatent = true;
      });
    }
  });

  function handleBaronyCreated(): void {
    showBaronyCreate = false;
    if ($currentCharacter) {
      fetchPlayerState($currentCharacter.id);
    }
  }

  function handleBackFromBaronyCreate(): void {
    showBaronyCreate = false;
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
    await initLanguage();
    if (!auth.hasSession()) {
      await attemptAutoLogin();
    }
    initialized = true;
  });
</script>

<ErrorDisplay />

{#if showIntroOverlay && $currentCharacter?.archetype}
  <DialogOverlay
    title={introTitle}
    body={introBody}
    onDismiss={handleIntroDismiss}
  />
{/if}

{#if showLandPatent}
  <DialogOverlay
    title=""
    size="xlg"
    noPadding
    onDismiss={dismissLandPatent}
  >
    <StoryText text={landPatentText} />
  </DialogOverlay>
{/if}

{#if showBaronyPatent}
  <DialogOverlay
    title=""
    size="xlg"
    noPadding
    onDismiss={dismissBaronyPatent}
  >
    <StoryText text={baronyPatentText} />
  </DialogOverlay>
{/if}

{#if languageLoading}
  <div class="container d-flex justify-content-center align-items-center min-vh-100">
    <div class="spinner-border" role="status">
      <span class="visually-hidden">Loading...</span>
    </div>
  </div>
{:else if !languageSet}
  <LanguageSelect onLanguageChosen={handleLanguageChosen} />
{:else if $authLoading}
  <div class="container d-flex justify-content-center align-items-center min-vh-100">
    <div class="spinner-border" role="status">
      <span class="visually-hidden">Loading...</span>
    </div>
  </div>
{:else if !$isAuthenticated}
  <AuthPage />
{:else if !$currentCharacter}
  <CharacterSelect />
{:else if !$currentCharacter.sex}
  <SexSelect onConfirm={handleSetSex} />
{:else if $currentCharacter.archetype === null && !showIntroOverlay}
  <PathSelect onConfirm={handleConfirmPath} />
{:else if showBaronyList}
  <BaronyJoin
    onJoined={handleBaronyJoined}
    onBack={handleBackFromBaronyList}
  />
{:else if showBaronyCreate}
  <BaronyCreate
    onCreated={handleBaronyCreated}
    onBack={handleBackFromBaronyCreate}
  />
{:else if activeMiniGame}
  <MiniGameContainer
    miniGame={activeMiniGame}
    levelId={selectedLevelId}
    onComplete={handleGameCompleteFromGrid}
    onError={handleGameError}
  />
{:else if !$playerGameState}
  <div class="container d-flex justify-content-center align-items-center min-vh-50 py-5">
    <div class="spinner-border" role="status">
      <span class="visually-hidden">Loading game state...</span>
    </div>
  </div>
{:else if $playerGameState.game_phase === 'initial_mission'}
  <HubScreen
    activities={$playerGameState.available_activities}
    onStartLevel={handleStartLevel}
  />
{:else if $playerGameState.game_phase === 'land_patent' || $playerGameState.game_phase === 'baron_track'}
  <HubScreen
    activities={$playerGameState.available_activities}
    onStartLevel={handleStartLevel}
    onJoinBarony={handleGoToJoinBarony}
    onBaronTrackStarted={handleBaronTrackStarted}
  />
{:else if $playerGameState.game_phase === 'baron_right'}
  <BaronyCreate
    onCreated={handleBaronyCreated}
    onBack={handleBackFromBaronyCreate}
  />
{:else if $playerGameState.game_phase === 'sandbox'}
  <HubScreen
    activities={$playerGameState.available_activities}
    onStartLevel={handleStartLevel}
  />
{/if}
