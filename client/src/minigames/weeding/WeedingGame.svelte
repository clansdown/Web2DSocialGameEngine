<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import GameBoard from '../../lib/GameBoard.svelte';
  import { createWeedingEngine, renderWeedingFrame, updateWeedingEngine } from './weeding_engine';
  import type { StartMiniGameResponse } from '../../lib/api';
  import type { WeedingLevelConfig } from './weeding_types';

  interface Props {
    levelConfig: StartMiniGameResponse;
    onGameOver: (won: boolean, score: number) => void;
  }

  let { levelConfig, onGameOver }: Props = $props();

  let config: WeedingLevelConfig = $state(
    (levelConfig.level_config ?? levelConfig) as WeedingLevelConfig
  );

  let canvas: HTMLCanvasElement;
  let context: CanvasRenderingContext2D;
  let engine = $state(createWeedingEngine(config, 800, 500));
  let animFrameId: number | null = null;
  let lastTime = 0;

  function gameLoop(timestamp: number) {
    if (engine.finished) return;

    const deltaMs = lastTime === 0 ? 0 : timestamp - lastTime;
    lastTime = timestamp;

    engine = updateWeedingEngine(engine, deltaMs);

    if (canvas && context) {
      canvas.width = canvas.clientWidth;
      canvas.height = canvas.clientHeight;
      engine = { ...engine, width: canvas.width, height: canvas.height };
      renderWeedingFrame(context, engine);
    }

    animFrameId = requestAnimationFrame(gameLoop);
  }

  function handleWin() {
    engine = { ...engine, finished: true, won: true, score: 100 };
    onGameOver(true, 100);
  }

  function handleLose() {
    engine = { ...engine, finished: true, won: false, score: 0 };
    onGameOver(false, 0);
  }

  onMount(() => {
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
