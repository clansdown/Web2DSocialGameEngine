<script lang="ts">
  /**
   * Ongoing-mode setup screen for Assarting (weeding).
   * The player picks a difficulty and a grid size from options served by the
   * server's ongoing config. A GameText describes the mode and shows the
   * expected silver reward for the current settings — queried from the server
   * (estimateOngoingRewards) whenever the selections change, since the reward
   * pool and diminishing returns are server-enforced.
   *
   * Starting stores the settings for WeedingGame to consume on session start.
   */

  import { currentCharacter, language } from '../../lib/stores';
  import { getMiniGameConfigs } from '../../lib/game_state';
  import { estimateOngoingRewards } from '../../lib/api';
  import type { OngoingGameConfig } from '../../lib/api';
  import * as auth from '../../lib/auth';
  import { setConfig } from '../../lib/storage';
  import { loadTexts as fetchTexts } from '../../lib/text';
  import GameText from '../../components/GameText.svelte';
  import MiniGameHomeButton from '../MiniGameHomeButton.svelte';

  interface Props {
    onStartLevel: (gameId: string, levelId: number) => void;
    onHome: () => void;
    onBackToGames: () => void;
  }

  let { onStartLevel, onHome, onBackToGames }: Props = $props();

  let config = $state<OngoingGameConfig | null>(null);
  let loading = $state(true);
  let selectedDifficulty = $state(1);
  let selectedSize = $state(4);
  let infoText = $state('');
  let rewardsText = $state('');
  let rewardEstimate = $state('');
  let message = $state('');
  let starting = $state(false);
  let ui = $state<Record<string, string>>({});

  const TEXT_IDS: string[] = [
    'wd_ongoing_info',
    'wd_ongoing_rewards',
    'ui_wd_ongoing_title',
    'ui_wd_grid_size_label',
    'ui_difficulty_label',
    'ui_start_game',
    'ui_back_to_games',
    'ui_loading',
    'ui_minigame_estimate_error',
    'ui_minigame_start_error'
  ];

  /**
   * Loads the weeding ongoing config from the server.
   */
  async function loadConfig(): Promise<void> {
    loading = true;
    try {
      const configs = await getMiniGameConfigs('weeding');
      const ongoing = configs['weeding']?.ongoing;
      if (ongoing) {
        config = ongoing;
        selectedDifficulty = ongoing.default_difficulty ?? 1;
        selectedSize = ongoing.default_size ?? ongoing.size_options[0]?.value ?? 0;
      }
    } catch {
      config = null;
    } finally {
      loading = false;
    }
  }

  /**
   * Loads the info and rewards GameText in the current language.
   */
  async function loadTexts(): Promise<void> {
    try {
      const texts = await fetchTexts(TEXT_IDS);
      ui = texts;
      infoText = texts['wd_ongoing_info'] ?? '';
      rewardsText = texts['wd_ongoing_rewards'] ?? '';
    } catch {
      infoText = '';
      rewardsText = '';
    }
  }

  /**
   * Queries the server for the expected silver reward at the current settings
   * (pool-adjusted) and stores the formatted amount for the {rewards} placeholder.
   */
  async function refreshEstimate(): Promise<void> {
    if (!config || !$currentCharacter) return;
    const creds = auth.getInMemoryCredentials();
    const token = auth.getSessionToken();
    if (!creds || !token) return;
    try {
      const estimate = await estimateOngoingRewards(
        $currentCharacter.id,
        'weeding',
        selectedDifficulty,
        selectedSize,
        { username: creds.username, token }
      );
      rewardEstimate = estimate.silver_formatted;
      message = '';
    } catch {
      rewardEstimate = '';
      message = ui['ui_minigame_estimate_error'] ?? '';
    }
  }

  /**
   * Stores the selected settings for WeedingGame and launches the ongoing game.
   */
  async function startGame(): Promise<void> {
    if (!$currentCharacter || starting) return;
    starting = true;
    message = '';
    try {
      await setConfig('pending_weeding_game', {
        difficulty: selectedDifficulty,
        grid_size: selectedSize
      });
      onStartLevel('weeding', 0);
    } catch (e) {
      message = ui['ui_minigame_start_error'] ?? '';
      console.error('[Weeding ongoing] start failed:', e);
    } finally {
      starting = false;
    }
  }

  $effect(() => {
    $language;
    $currentCharacter;
    loadConfig();
    loadTexts();
  });

  $effect(() => {
    if (config) {
      refreshEstimate();
    }
  });
</script>

<div class="container py-5">
  <div class="d-flex flex-wrap align-items-center gap-2 mb-4">
    <MiniGameHomeButton {onHome} />
    <button class="btn btn-sm btn-link text-secondary p-0" onclick={onBackToGames}>
      &larr; {ui['ui_back_to_games'] ?? ''}
    </button>
  </div>

  <h2>{ui['ui_wd_ongoing_title'] ?? ''}</h2>

  {#if loading}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">{ui['ui_loading'] ?? ''}</span>
      </div>
    </div>
  {:else if config}
    {#if infoText}
      <div class="mb-4" style="max-width: 600px;">
        <GameText text={infoText} />
      </div>
    {/if}

    <div class="mb-4">
      <div class="fw-bold mb-2">{ui['ui_wd_grid_size_label'] ?? ''}</div>
      <div class="d-flex flex-wrap gap-2">
        {#each config.size_options as opt}
          <button
            class="btn btn-lg {selectedSize === opt.value ? 'btn-primary' : 'btn-outline-primary'}"
            onclick={() => { selectedSize = opt.value; }}
          >
            {opt.value}&times;{opt.value}
          </button>
        {/each}
      </div>
    </div>

    <div class="mb-4">
      <div class="fw-bold mb-2">{ui['ui_difficulty_label'] ?? ''}</div>
      <div class="d-flex flex-wrap gap-2">
        {#each config.difficulty_options as d}
          <button
            class="btn btn-lg {selectedDifficulty === d ? 'btn-danger' : 'btn-outline-danger'}"
            onclick={() => { selectedDifficulty = d; }}
          >
            {d}
          </button>
        {/each}
      </div>
    </div>

    {#if rewardsText}
      <div class="mb-4" style="max-width: 600px;">
        <GameText text={rewardEstimate ? rewardsText.replace(/\{rewards\}/g, rewardEstimate) : rewardsText} />
      </div>
    {/if}

    {#if message}
      <div class="alert alert-warning">{message}</div>
    {/if}

    <button class="btn btn-success btn-lg px-5" onclick={startGame} disabled={starting}>
      {#if starting}
        <span class="spinner-border spinner-border-sm me-1" role="status"></span>
      {/if}
      {ui['ui_start_game'] ?? ''}
    </button>
  {/if}
</div>
