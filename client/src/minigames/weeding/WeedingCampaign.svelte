<script lang="ts">
  import { playerGameState, language } from '../../lib/stores';
  import { getTextsRequest } from '../../lib/api';
  import GameText from '../../components/GameText.svelte';
  import Book from '../../components/Book.svelte';

  interface Props {
    gridSize: number;
    minLevelId: number;
    onStartLevel: (levelId: number) => void;
    onBack: () => void;
  }

  let { gridSize, minLevelId, onStartLevel, onBack }: Props = $props();

  let infoText = $state('');

  const mapImageUrl = '/images/tower_defense/wolf_warden_map1.jpg';

  const bookPages: { image: string; textId: string }[] = [
    { image: '', textId: 'wd_plant_bindweed' },
    { image: '', textId: 'wd_plant_nettles' },
    { image: '', textId: 'wd_plant_thistles' },
    { image: '', textId: 'wd_plant_brambles' },
    { image: '', textId: 'wd_plant_gorse' },
    { image: '', textId: 'wd_plant_iron_gorse' },
    { image: '', textId: 'wd_plant_briar_root' },
    { image: '', textId: 'wd_tool_scythe' },
    { image: '', textId: 'wd_tool_sickle' },
    { image: '', textId: 'wd_tool_billhook' },
    { image: '', textId: 'wd_tool_mattock' },
    { image: '', textId: 'wd_tool_axe' },
    { image: '', textId: 'wd_smother_rye' },
    { image: '', textId: 'wd_smother_sunn_hemp' },
  ];

  function isLevelAvailable(levelId: number): boolean {
    if (!$playerGameState) return false;
    if (levelId === minLevelId) return true;
    if (levelId < minLevelId) return false;
    const prevLevel = $playerGameState.progress.find(
      p => p.mini_game === 'weeding' && p.level_id === levelId - 1
    );
    return prevLevel?.completed ?? false;
  }

  function isLevelCompleted(levelId: number): boolean {
    if (!$playerGameState) return false;
    const level = $playerGameState.progress.find(
      p => p.mini_game === 'weeding' && p.level_id === levelId
    );
    return level?.completed ?? false;
  }

  function handleLevelClick(levelId: number) {
    if (isLevelAvailable(levelId)) {
      onStartLevel(levelId);
    }
  }

  $effect(() => {
    getTextsRequest($language, ['wd_weeding_intro']).then(texts => {
      infoText = texts['wd_weeding_intro'] ?? '';
    }).catch(() => {
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
      <h2 class="mb-0">Assarter of the Wildlands</h2>
      <small class="text-muted">Campaign Progress</small>
    </div>
    <div class="ms-auto">
      <Book pages={bookPages} toolText="Weeds & Tools" width={110} />
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
