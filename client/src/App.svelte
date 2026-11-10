<script lang="ts">
  import { onMount } from 'svelte';
  import AuthPage from './components/AuthPage.svelte';
  import CharacterSelect from './components/CharacterSelect.svelte';
  import HubScreen from './components/HubScreen.svelte';
  import ErrorDisplay from './components/ErrorDisplay.svelte';
  import LanguageSelect from './components/LanguageSelect.svelte';
  import PathSelect from './components/PathSelect.svelte';
  import SexSelect from './components/SexSelect.svelte';
  import AvatarPicker from './components/AvatarPicker.svelte';
  import BaronyJoin from './components/BaronyJoin.svelte';
  import BaronyCreate from './components/BaronyCreate.svelte';
  import DialogOverlay from './components/DialogOverlay.svelte';
  import StoryText from './components/StoryText.svelte';
  import LetterOverlay from './components/LetterOverlay.svelte';
  // MiniGameSelect is used internally by HubScreen
  import MiniGameContainer from './minigames/MiniGameContainer.svelte';
  import * as auth from './lib/auth';
  import { user, characters, currentCharacter, authLoading, playerGameState, language, isAuthenticated } from './lib/stores';
  import { RefreshUnavailableError, refreshAccessToken, setCharacterArchetypeRequest, setCharacterSexRequest, acknowledgeLandPatentRequest } from './lib/api';
  import { loadTexts, loadText } from './lib/text';
  import { asset_url } from './lib/public_urls';
  import { fetchPlayerState } from './lib/game_state';
  import { route_store, navigate, replace_route } from './lib/router';
  import type { EndMiniGameResponse } from './lib/api';
  import { handleError } from './lib/errors';

  let needsAuth = $state(false);
  let initialized = $state(false);
  let auth_unavailable = $state(false);
  let auth_unavailable_text = $state('');
  let auth_retry_text = $state('');

  // Mini-game launch is driven by the hash route (#/game/<game>/<level>)
  let gameRoute = $derived($route_store.type === 'game' ? $route_store : null);

  // Language selection state
  let languageLoading = $state(true);
  let languageSet = $state(false);

  // Path selection state
  let showIntroOverlay = $state(false);
  let introTitle = $state('');
  let introBody = $state('');
  let introMarkdown = $derived(
    introTitle ? `# ${introTitle}\n\n${introBody}` : introBody
  );

  // Character-creation avatar step: complete once a pick is confirmed OR the
  // safe pool is empty (skip). The flag matters for the skip path, where no
  // avatar_safe_id is set and the picker would otherwise re-render forever.
  let avatar_step_done = $state(false);

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

  // Unread-letter overlay: the server returns the oldest unread letter; once
  // dismissed (and marked read) we refetch so the next one can appear.
  let dismissedLetterId = $state<number | null>(null);
  let activeLetter = $derived(
    $playerGameState?.unread_letter && $playerGameState.unread_letter.id !== dismissedLetterId
      ? $playerGameState.unread_letter
      : null
  );

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
   * Shows a retry screen instead of the login form when the refresh request
   * failed because the server was temporarily unavailable.
   */
  async function show_auth_unavailable(): Promise<void> {
    try {
      const texts = await loadTexts(['ui_auth_server_unavailable', 'ui_auth_retry']);
      auth_unavailable_text = texts['ui_auth_server_unavailable'] || '';
      auth_retry_text = texts['ui_auth_retry'] || '';
    } catch {
      auth_unavailable_text = '';
      auth_retry_text = '';
    }
    auth_unavailable = true;
    needsAuth = false;
  }

  /**
   * Attempts to automatically log in using the refresh cookie.
   */
  async function attemptAutoLogin() {
    authLoading.set(true);
    auth_unavailable = false;
    try {
      const response = await refreshAccessToken();
      if (response?.token) {
        auth.setStoredUsername(response.username);

        user.set({
          id: response.user_id,
          username: response.username,
          adult: response.adult
        });
        characters.set(response.characters);

        // Adopt the account's interface / display language on this device.
        if (response.language) {
          language.set(response.language);
          await auth.saveStoredLanguage(response.language);
          languageSet = true;
        }

        const lastCharId = await auth.loadCurrentCharacterId();
        const lastChar = response.characters.find(c => c.id === lastCharId) || response.characters[0];
        currentCharacter.set(lastChar || null);

        needsAuth = false;
        return;
      }
    } catch (error) {
      if (error instanceof RefreshUnavailableError) {
        await show_auth_unavailable();
        return;
      }
      // Definitive auth failure; fall through to login.
    } finally {
      authLoading.set(false);
    }
    needsAuth = true;
  }

  /**
   * Retries the startup refresh after a transient server outage.
   */
  async function retryAutoLogin(): Promise<void> {
    auth_unavailable = false;
    await attemptAutoLogin();
  }

  /**
   * Handles re-authentication when the access token is lost or expired.
   */
  async function handleNeedsAuth() {
    try {
      const refreshed = await refreshAccessToken();
      if (refreshed?.token) return true;
    } catch (error) {
      if (error instanceof RefreshUnavailableError) {
        await show_auth_unavailable();
        return false;
      }
      // Definitive auth failure; fall through.
    }
    auth.clearInMemoryCredentials();
    needsAuth = true;
    return false;
  }

  /**
   * Loads the player's game state from the server after a character is selected.
   */
  async function loadGameState() {
    if (!$currentCharacter) return;

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

  /**
   * Handles confirmation of the character-creation portrait pick. The picker
   * has already updated currentCharacter; this advances the creation flow.
   */
  function handleSetAvatar(): void {
    avatar_step_done = true;
  }

  /**
   * Handles the empty-safe-pool skip. Nothing was chosen, so advance without an
   * avatar; the Profile activity is where a portrait is picked later.
   */
  function handleSkipAvatar(): void {
    avatar_step_done = true;
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
    navigate({ type: 'game', game_id: gameId, level_id: levelId, rest: [] });
  }

  function handleGameError(error: string) {
    handleError('Mini-game error', new Error(error));
    // Return to the tasks page — the game entry is replaced so browser Back
    // does not re-enter the failed game.
    replace_route({ type: 'activity', id: 'tasks', rest: [] });
  }

  // ── Barony flow handlers ──────────────────────────────────────

  function handleGoToJoinBarony(): void {
    showBaronyList = true;
  }

  async function handleBaronyJoined(): Promise<void> {
    showBaronyList = false;
    // Land on the hub grid. Fetch game state to update phase to sandbox.
    navigate({ type: 'hub', rest: [] });
    if ($currentCharacter) {
      fetchPlayerState($currentCharacter.id);
    }
  }

  function handleBackFromBaronyList(): void {
    showBaronyList = false;
  }

  function handleBaronTrackStarted(): void {
    // Game phase is now baron_track — go straight to the campaign page.
    // Fetch game state to refresh.
    navigate({ type: 'activity', id: 'tasks', rest: [] });
    if ($currentCharacter) {
      fetchPlayerState($currentCharacter.id);
    }
  }

  /**
   * Founds a freehold → hub grid; refetches state to pick up the new phase.
   */
  function handleFreeholdFounded(): void {
    navigate({ type: 'hub', rest: [] });
    if ($currentCharacter) {
      fetchPlayerState($currentCharacter.id);
    }
  }

  /**
   * Founds an honor (mesne lord) → hub grid; refetches state for the new phase.
   */
  function handleHonorFounded(): void {
    navigate({ type: 'hub', rest: [] });
    if ($currentCharacter) {
      fetchPlayerState($currentCharacter.id);
    }
  }

  /**
   * Leaves a barony → hub grid; refetches state for the freehold phase.
   */
  function handleBaronyLeft(): void {
    navigate({ type: 'hub', rest: [] });
    if ($currentCharacter) {
      fetchPlayerState($currentCharacter.id);
    }
  }

  /**
   * Dismisses the unread-letter overlay and refetches so the next unread letter
   * (if any) can appear.
   */
  function handleLetterDismissed(): void {
    if (activeLetter) dismissedLetterId = activeLetter.id;
    if ($currentCharacter) {
      fetchPlayerState($currentCharacter.id);
    }
  }

  function handleGameCompleteFromGrid(results: EndMiniGameResponse) {
    // Replace the game history entry with the tasks page so browser Back
    // from the tasks page does not re-enter the finished game.
    replace_route({ type: 'activity', id: 'tasks', rest: [] });
    if ($currentCharacter) {
      fetchPlayerState($currentCharacter.id);
    }
    // If we just completed the baron track, show the barony patent first, then
    // the create barony screen once it is dismissed.
    if (results.baron_right_earned) {
      loadText('barony_patent_earned').then((raw) => {
        let text = raw;
        text = text.replace(/\<seal\>/g, `\n\n<img src="${asset_url('/images/ui/kings_seal.png')}" style="width: 10em; height: auto;" alt="King's Seal" />`);
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
        text = text.replace(/\<seal\>/g, `\n\n<img src="${asset_url('/images/ui/kings_seal.png')}" style="width: 10em; height: auto;" alt="King's Seal" />`);
        landPatentText = text;
        showLandPatent = true;
      });
    }
  });

  function handleBaronyCreated(): void {
    showBaronyCreate = false;
    // Land on the hub grid.
    navigate({ type: 'hub', rest: [] });
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
    title=""
    size="xlg"
    noPadding
    onDismiss={handleIntroDismiss}
  >
    <StoryText text={introMarkdown} />
  </DialogOverlay>
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

{#if activeLetter}
  <LetterOverlay letter={activeLetter} onDismiss={handleLetterDismissed} />
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
{:else if auth_unavailable}
  <div class="container d-flex justify-content-center align-items-center min-vh-100">
    <div class="card p-4 text-center" style="max-width: 32rem;">
      {#if auth_unavailable_text}
        <p class="mb-4">{auth_unavailable_text}</p>
      {/if}
      <button class="btn btn-primary" onclick={retryAutoLogin}>
        {auth_retry_text || 'Retry'}
      </button>
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
{:else if $currentCharacter.avatar_safe_id == null && !avatar_step_done && !showIntroOverlay}
  <AvatarPicker onConfirm={handleSetAvatar} onSkip={handleSkipAvatar} />
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
  {:else if gameRoute}
  <MiniGameContainer
    miniGame={gameRoute.game_id}
    levelId={gameRoute.level_id}
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
  {:else if $playerGameState.game_phase === 'land_patent' || $playerGameState.game_phase === 'baron_track' || $playerGameState.game_phase === 'freehold' || $playerGameState.game_phase === 'freehold_qualified'}
  <HubScreen
    activities={$playerGameState.available_activities}
    onStartLevel={handleStartLevel}
    onJoinBarony={handleGoToJoinBarony}
    onBaronTrackStarted={handleBaronTrackStarted}
    onFreeholdFounded={handleFreeholdFounded}
    onBaronyLeft={handleBaronyLeft}
    onHonorFounded={handleHonorFounded}
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
    onBaronyLeft={handleBaronyLeft}
  />
{/if}
