<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import GameBoard from '../../lib/GameBoard.svelte';
  import { create_engine, render_frame, update_engine } from './tower_defense_engine';
  import type { StartMiniGameResponse } from '../../lib/api';
  import type { TowerDefenseLevelConfig, MapMetadata } from './tower_defense_types';

  interface Props {
    levelConfig: StartMiniGameResponse;
    onGameOver: (won: boolean, score: number) => void;
  }

  let { levelConfig, onGameOver }: Props = $props();

  let config: TowerDefenseLevelConfig = $state(
    (levelConfig.level_config ?? levelConfig) as TowerDefenseLevelConfig
  );

  let canvas: HTMLCanvasElement;
  let context: CanvasRenderingContext2D;
  let engine = $state(create_engine(config, 800, 500));
  let animFrameId: number | null = null;
  let lastTime = 0;

  function gameLoop(timestamp: number) {
    if (engine.finished) return;

    const deltaMs = lastTime === 0 ? 0 : timestamp - lastTime;
    lastTime = timestamp;

    engine = update_engine(engine, deltaMs);

    if (canvas && context) {
      canvas.width = canvas.clientWidth;
      canvas.height = canvas.clientHeight;
      engine = { ...engine, width: canvas.width, height: canvas.height };
      render_frame(context, engine);
    }

    animFrameId = requestAnimationFrame(gameLoop);
  }

  async function loadBackgroundImage() {
    const meta = config.map_metadata as MapMetadata | undefined;
    if (!meta?.image_filename) return;

    const img = new Image();
    img.onload = () => {
      engine = { ...engine, background_image: img };
    };
    img.onerror = () => {
      console.warn('Failed to load map image:', meta.image_filename);
    };
    img.src = `/images/tower_defense/maps/${meta.image_filename}`;
  }

  function handleWin() {
    engine = { ...engine, finished: true, won: true, score: 100 };
    onGameOver(true, 100);
  }

  function handleLose() {
    engine = { ...engine, finished: true, won: false, score: 0 };
    onGameOver(false, 0);
  }

  onMount(async () => {
    await loadBackgroundImage();
    animFrameId = requestAnimationFrame(gameLoop);
  });

  onDestroy(() => {
    if (animFrameId !== null) {
      cancelAnimationFrame(animFrameId);
    }
  });
</script>

<div class="d-flex flex-column align-items-center" style="min-height: 100vh;">
  <div class="container-fluid p-0 position-relative" style="flex: 1; width: 100%; max-width: 900px;">
    <div style="width: 100%; height: 500px;">
      <GameBoard
        gameWidth={800}
        gameHeight={500}
        bind:canvas={canvas}
        bind:context={context}
      />
    </div>
  </div>

  <div class="d-flex gap-3 my-3">
    <button class="btn btn-success btn-lg" onclick={handleWin}>
      Win (Test)
    </button>
    <button class="btn btn-danger btn-lg" onclick={handleLose}>
      Lose (Test)
    </button>
  </div>
</div>
