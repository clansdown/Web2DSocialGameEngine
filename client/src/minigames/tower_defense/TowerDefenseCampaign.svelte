<script lang="ts">
  import { playerGameState, language } from '../../lib/stores';
  import { getTextsRequest } from '../../lib/api';
  import Book from '../../components/Book.svelte';
  import GameText from '../../components/GameText.svelte';

  interface Props {
    campaignId: string;
    gridSize: number;
    minLevelId: number;
    onStartLevel: (levelId: number) => void;
    onBack: () => void;
  }

  let { campaignId, gridSize, minLevelId, onStartLevel, onBack }: Props = $props();

  let infoText = $state('');
  let displayName = $state('');

  const bookPages: { image: string; textId: string }[] = [
    { image: '/images/tower_defense/mobs/dire_rat.png', textId: 'td_book_enemy_dire_rat' },
    { image: '/images/tower_defense/mobs/wolf.png', textId: 'td_book_enemy_wolf' },
    { image: '/images/tower_defense/mobs/great_wolf.png', textId: 'td_book_enemy_great_wolf' },
    { image: '/images/tower_defense/mobs/wild_boar.png', textId: 'td_book_enemy_boar' },
    { image: '/images/tower_defense/mobs/matted_wolf.png', textId: 'td_book_enemy_matted_wolf' },
    { image: '/images/tower_defense/mobs/tri_boar.png', textId: 'td_book_enemy_triboar' },
    { image: '/images/tower_defense/mobs/dire_wolf.png', textId: 'td_book_enemy_dire_wolf' },
    { image: '/images/tower_defense/units/shortbow_archer.png', textId: 'td_book_unit_shortbow_archer' },
    { image: '/images/tower_defense/units/longbow_archer.png', textId: 'td_book_unit_longbow_archer' },
    { image: '/images/tower_defense/units/swordsman.png', textId: 'td_book_unit_swordsman' },
    { image: '/images/tower_defense/units/alchemist.png', textId: 'td_book_unit_alchemist' },
    { image: '/images/tower_defense/towers/single_archer_tower.png', textId: 'td_book_tower_single_archer_tower' },
    { image: '/images/tower_defense/towers/three_archer_tower.png', textId: 'td_book_tower_three_archer_tower' },
    { image: '/images/tower_defense/towers/ballista.png', textId: 'td_book_tower_ballista' },
  ];

  const mapImageUrl = '/images/tower_defense/wolf_warden_map1.jpg';

  let infoTextId = $derived(
    campaignId === 'great_wolf_marche'
      ? 'ui_campaign_intro_great_wolf_warden'
      : 'ui_campaign_intro_wolf_warden'
  );

  let titleKey = $derived(
    campaignId === 'great_wolf_marche'
      ? 'ui_campaign_great_wolf_marche'
      : 'ui_campaign_wolf_marche'
  );

  function isLevelAvailable(levelId: number): boolean {
    if (!$playerGameState) return false;
    if (levelId === minLevelId) return true;
    if (levelId < minLevelId) return false;
    const prevLevel = $playerGameState.progress.find(
      p => p.mini_game === 'tower_defense' && p.level_id === levelId - 1
    );
    return prevLevel?.completed ?? false;
  }

  function isLevelCompleted(levelId: number): boolean {
    if (!$playerGameState) return false;
    const level = $playerGameState.progress.find(
      p => p.mini_game === 'tower_defense' && p.level_id === levelId
    );
    return level?.completed ?? false;
  }

  function handleLevelClick(levelId: number) {
    if (isLevelAvailable(levelId)) {
      onStartLevel(levelId);
    }
  }

  $effect(() => {
    getTextsRequest($language, [titleKey, infoTextId]).then(texts => {
      displayName = texts[titleKey] ?? 'Wolf Marche';
      infoText = texts[infoTextId] ?? '';
    }).catch(() => {
      displayName = 'Wolf Marche';
      infoText = '';
    });
  });
</script>

<div class="container py-5">
  <div class="d-flex align-items-center mb-4">
    <button class="btn btn-outline-secondary me-3" onclick={onBack}>
      &larr; Back
    </button>
    <div>
      <h2 class="mb-0">{displayName}</h2>
      <small class="text-muted">Campaign Progress</small>
    </div>
    <div class="ms-auto">
      <Book pages={bookPages} toolText="Bestiary" width={110} />
    </div>
  </div>

  <div style="position: relative; max-width: 800px; margin: 0 auto;">
    <img src={mapImageUrl} alt="Campaign Map" style="width: 100%; height: auto; display: block; border-radius: 8px;" />
    <div
      style="position: absolute; inset: 0; display: grid; grid-template-columns: repeat({gridSize}, 1fr); grid-template-rows: repeat({gridSize}, 1fr);"
    >
      {#each Array(gridSize * gridSize) as _, i}
        {@const levelId = i + minLevelId}
        {@const conquered = isLevelCompleted(levelId)}
        {@const available = isLevelAvailable(levelId)}
        <div
          role="button"
          tabindex={available ? 0 : -1}
          style="cursor: {available ? 'pointer' : 'not-allowed'}; position: relative; transition: background 0.2s; border-radius: 4px; margin: 2px; {!available ? 'background: rgba(0, 0, 0, 0.55);' : conquered ? '' : 'background: rgba(0, 0, 0, 0.35);'}"
          onmouseenter={(e) => { if (available) (e.currentTarget as HTMLElement).style.background = conquered ? 'rgba(255, 255, 255, 0.1)' : 'rgba(0, 0, 0, 0.2)'; }}
          onmouseleave={(e) => { if (available) (e.currentTarget as HTMLElement).style.background = conquered ? '' : 'rgba(0, 0, 0, 0.35)'; }}
          onclick={() => handleLevelClick(levelId)}
          onkeydown={(e) => { if (e.key === 'Enter') handleLevelClick(levelId); }}
        >
          {#if conquered}
            <div style="position: absolute; top: 4px; right: 4px; color: #22c55e; font-size: 18px; text-shadow: 0 1px 3px rgba(0,0,0,0.5);">&#10003;</div>
          {/if}
        </div>
      {/each}
    </div>
  </div>

  {#if infoText}
    <div class="mx-auto mt-4" style="max-width: 800px;">
      <GameText text={infoText} />
    </div>
  {/if}
</div>
