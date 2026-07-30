<script lang="ts">
  import { playerGameState, currentCharacter, language } from '../../lib/stores';
  import { tdRound } from '../../lib/game_state';
  import { setConfig } from '../../lib/storage';
  import { getTextsRequest } from '../../lib/api';
  import GameText from '../../components/GameText.svelte';
  import TowerDefenseCampaign from './TowerDefenseCampaign.svelte';

  interface Props {
    onStartLevel: (levelId: number) => void;
    onBack: () => void;
  }

  let { onStartLevel, onBack }: Props = $props();

  let selectedRounds = $state(8);
  let selectedDifficulty = $state(1);
  let customInfo = $state('');
  let customRewards = $state('');

  let isCampaign = $derived(
    $playerGameState?.game_phase === 'initial_mission' || $playerGameState?.game_phase === 'baron_track'
  );

  let isBaronTrack = $derived($playerGameState?.game_phase === 'baron_track');
  let campaignSize = $derived(isBaronTrack ? 4 : 3);
  let minLevelId = $derived(isBaronTrack ? 10 : 1);
  let campaignId = $derived(isBaronTrack ? 'great_wolf_marche' : 'wolf_marche');

  async function startCustomGame() {
    const result = await tdRound($currentCharacter!.id, {
      mini_game: 'tower_defense',
      level_id: 0,
      rounds: selectedRounds,
      difficulty: selectedDifficulty
    });
    await setConfig('pending_custom_game', result);
    onStartLevel(0);
  }

  $effect(() => {
    if (!isCampaign) {
      getTextsRequest($language, ['td_custom_game_info', 'td_custom_game_rewards']).then(texts => {
        customInfo = texts['td_custom_game_info'] ?? '';
        customRewards = texts['td_custom_game_rewards'] ?? '';
      }).catch(() => {
        customInfo = '';
        customRewards = '';
      });
    }
  });
</script>

{#if isCampaign}
  <TowerDefenseCampaign
    campaignId={campaignId}
    gridSize={campaignSize}
    minLevelId={minLevelId}
    {onStartLevel}
    {onBack}
  />
{:else}
  <div class="container py-5">
    <button class="btn btn-outline-secondary mb-4" onclick={onBack}>
      &larr; Back
    </button>

    <h2>Tower Defense</h2>

    {#if customInfo}
      <div class="mb-4" style="max-width: 600px;">
        <GameText text={customInfo} />
      </div>
    {/if}

    <div class="mb-4">
      <label class="form-label fw-bold">Rounds</label>
      <div class="d-flex flex-wrap gap-2">
        {#each [3, 5, 8, 10, 12, 15, 20, 30] as r}
          <button
            class="btn btn-lg {selectedRounds === r ? 'btn-primary' : 'btn-outline-primary'}"
            onclick={() => { selectedRounds = r; }}
          >
            {r}
          </button>
        {/each}
      </div>
    </div>

    <div class="mb-4">
      <label class="form-label fw-bold">Difficulty</label>
      <div class="d-flex flex-wrap gap-2">
        {#each [1, 2, 3, 4, 5, 6, 7, 8, 9, 10] as d}
          <button
            class="btn {selectedDifficulty === d ? 'btn-danger' : 'btn-outline-danger'}"
            onclick={() => { selectedDifficulty = d; }}
          >
            {d}
          </button>
        {/each}
      </div>
    </div>

    {#if customRewards}
      <div class="mb-4" style="max-width: 600px;">
        <GameText text={customRewards} />
      </div>
    {/if}

    <button class="btn btn-success btn-lg px-5" onclick={startCustomGame}>
      Start Game
    </button>
  </div>
{/if}
