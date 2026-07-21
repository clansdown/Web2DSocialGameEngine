      <script lang="ts">
  /**
   * Container component that orchestrates the mini-game lifecycle.
   * For tower defense, uses the new SimpleGame component with tdRound API.
   * For weeding, uses the existing startMiniGame/endMiniGame flow.
   */
  import TowerDefenseGame from './tower_defense/TowerDefenseGame.svelte';
  import SimpleGame from './tower_defense/SimpleGame.svelte';
  import WeedingGame from './weeding/WeedingGame.svelte';
  import DialogOverlay from '../components/DialogOverlay.svelte';
  import StoryText from '../components/StoryText.svelte';
  import { startMiniGame, endMiniGame } from '../lib/game_state';
  import { currentCharacter, playerGameState, language } from '../lib/stores';
  import { getTextsRequest } from '../lib/api';
  import type { StartMiniGameResponse, EndMiniGameResponse, UnlockItem } from '../lib/api';
  import { marked } from 'marked';

  interface QueueItem {
    id: string;
    textKey: string;
    imageUrl: string;
  }

  interface Props {
    miniGame: string;
    levelId?: number;
    onComplete: (results: EndMiniGameResponse) => void;
    onError: (error: string) => void;
  }

  let { miniGame, levelId = 0, onComplete, onError }: Props = $props();

  let loading = $state(true);
  let startError = $state<string | null>(null);
  let levelConfig: StartMiniGameResponse | null = $state(null);
  let gameStarted = $state(false);
  let gameFinished = $state(false);
  let gameResults: EndMiniGameResponse | null = $state(null);
  let showResults = $state(false);

  // For tower defense with new SimpleGame
  let tdStarted = $state(false);

  // King's message overlay state
  let showKingsMessage = $state(false);
  let unlockQueue: QueueItem[] = $state([]);
  let unlockIndex = $state(0);
  let kingsMsgTemplate = $state('');
  let itemTexts: Record<string, string> = $state({});

  /**
   * Starts the mini-game session for non-TD games (weeding).
   */
  async function initializeGame() {
    if (miniGame === 'tower_defense') {
      tdStarted = true;
      loading = false;
      return;
    }

    loading = true;
    startError = null;
    try {
      if (!$currentCharacter) {
        throw new Error('No character selected');
      }

      const result = await startMiniGame($currentCharacter.id, miniGame);
      levelConfig = result;
      gameStarted = true;
    } catch (e) {
      const msg = e instanceof Error ? e.message : 'Failed to start mini-game';
      startError = msg;
      onError(msg);
    } finally {
      loading = false;
    }
  }

  /**
   * Called by the mini-game component when the game finishes (weeding path).
   */
  async function handleGameOver(won: boolean, score: number) {
    if (gameFinished || !$currentCharacter || !levelConfig) return;

    gameFinished = true;

    try {
      const levelId = levelConfig.level_id ?? levelConfig.level_config?.id ?? 0;

      const results = await endMiniGame(
        $currentCharacter.id,
        miniGame,
        levelId,
        won,
        score
      );

      gameResults = results;
      showResults = true;
    } catch (e) {
      const msg = e instanceof Error ? e.message : 'Failed to end mini-game';
      onError(msg);
    }
  }

  /**
   * Called by SimpleGame when TD is complete.
   * Checks for new unlocks and shows the king's message overlay before results.
   */
  async function handleTDComplete(results: EndMiniGameResponse, forfeited = false) {
    if (gameFinished) return;
    gameFinished = true;
    gameResults = results;

    if (forfeited) {
      onComplete(results);
      return;
    }

    const unlocks = results.new_unlocks;
    if (unlocks && (unlocks.new_units.length > 0 || unlocks.new_towers.length > 0)) {
      // Build the unlock queue
      const queue: QueueItem[] = [];
      const textKeysToFetch: string[] = ['kings_message_unlock'];

      for (const u of unlocks.new_units) {
        queue.push({
          id: u.id,
          textKey: u.text_key,
          imageUrl: `/images/tower_defense/units/${u.id}.png`
        });
        textKeysToFetch.push(u.text_key);
      }

      for (const t of unlocks.new_towers) {
        queue.push({
          id: t.id,
          textKey: t.text_key,
          imageUrl: `/images/tower_defense/towers/${t.id}.png`
        });
        textKeysToFetch.push(t.text_key);
      }

      // Fetch all text content
      const sex = $currentCharacter?.sex || 'male';
      const texts = await getTextsRequest($language, textKeysToFetch, sex);

      kingsMsgTemplate = texts['kings_message_unlock'] || '';

      // Store item texts keyed by text_key
      const itemMap: Record<string, string> = {};
      for (const qi of queue) {
        itemMap[qi.id] = texts[qi.textKey] || '';
      }
      itemTexts = itemMap;

      unlockQueue = queue;
      unlockIndex = 0;
      showKingsMessage = true;
    } else {
      showResults = true;
    }
  }

  /**
   * Builds the full markdown for the current unlock item.
   */
  function currentFullText(): string {
    const item = unlockQueue[unlockIndex];
    if (!item) return '';
    const itemText = itemTexts[item.id] || '';

    const unitName = itemText.split('\n')[0].split('**')[1] || itemText.split('\n')[0];

    let msg = kingsMsgTemplate
        .replace(/\{unit_name\}/g, unitName)
        .replace(/\<unit\>/g, `\n\n![](${item.imageUrl})\n\n${itemText}`)
        .replace(/\<seal\>/g, `\n\n<img src="/images/ui/kings_seal.png" style="width: 10em; height: auto;" alt="King's Seal" />`);

    if ($currentCharacter) {
      msg = msg.replace(/\{character_name\}/g, $currentCharacter.display_name);
    }

    return msg;
  }

  /**
   * Advances through the unlock queue or shows results.
   */
  function dismissKingsMessage() {
    if (unlockIndex < unlockQueue.length - 1) {
      unlockIndex++;
    } else {
      showKingsMessage = false;
      showResults = true;
    }
  }

  $effect(() => {
    if (!gameStarted && !gameFinished && !tdStarted) {
      initializeGame();
    }
  });
</script>

<div class="container-fluid p-0">
  {#if loading}
    <div class="d-flex justify-content-center align-items-center py-5">
      <div class="text-center">
        <div class="spinner-border mb-3" role="status">
          <span class="visually-hidden">Loading...</span>
        </div>
        <p class="text-muted">Starting {miniGame === 'tower_defense' ? 'Tower Defense' : 'Weeding'}...</p>
      </div>
    </div>
  {:else if startError}
    <div class="container py-5">
      <div class="alert alert-danger">{startError}</div>
    </div>
  {:else if showKingsMessage && unlockQueue.length > 0}
    <DialogOverlay title="" size="xlg" noPadding onDismiss={dismissKingsMessage}>
      <StoryText text={currentFullText()} />
    </DialogOverlay>
  {:else if showResults && gameResults}
    <div class="container py-5">
      <div class="card">
        <div class="card-body text-center p-5">
          <h3 class="mb-3">
            {gameResults.completed ? 'Victory!' : 'Defeat'}
          </h3>
          <p class="lead mb-2">Score: {gameResults.score}</p>
          {#if gameResults.new_best_score === gameResults.score && gameResults.score > 0}
            <p class="text-success">New Best Score!</p>
          {/if}
          {#if gameResults.completed}
            <div class="mb-3">
              <h5>Rewards</h5>
              {#each Object.entries(gameResults.rewards) as [resource, amount]}
                <span class="badge bg-success me-2">{resource}: +{amount}</span>
              {/each}
            </div>
          {/if}
          {#if gameResults.base_unlocked}
            <div class="alert alert-success">
              <h4 class="alert-heading">Base Unlocked!</h4>
              <p class="mb-0">You have proven yourself worthy. The right to build is yours.</p>
            </div>
          {/if}
          {#if gameResults.land_patent_earned}
            <div class="alert alert-info">
              <h4 class="alert-heading">Land Patent Earned!</h4>
              <p class="mb-0">You have completed all initial missions. You may now join a dukedom or earn the right to start your own.</p>
            </div>
          {/if}
          {#if gameResults.duke_right_earned}
            <div class="alert alert-warning">
              <h4 class="alert-heading">Dukedom Right Earned!</h4>
              <p class="mb-0">You have cleared the duke track. You may now found your own dukedom.</p>
            </div>
          {/if}

          <button class="btn btn-primary btn-lg mt-4" onclick={() => { if (gameResults) onComplete(gameResults); }}>
            Continue
          </button>
        </div>
      </div>
    </div>
  {:else if tdStarted && !gameFinished && $currentCharacter}
    <SimpleGame
      characterId={$currentCharacter.id}
      {miniGame}
      {levelId}
      onComplete={handleTDComplete}
      onError={onError}
    />
  {:else if levelConfig && gameStarted}
    {#if miniGame === 'tower_defense'}
      <TowerDefenseGame
        {levelConfig}
        onGameOver={handleGameOver}
      />
    {:else if miniGame === 'weeding'}
      <WeedingGame
        {levelConfig}
        onGameOver={handleGameOver}
      />
    {:else}
      <div class="container py-5">
        <div class="alert alert-warning">Unknown mini-game: {miniGame}</div>
      </div>
    {/if}
  {/if}
</div>
