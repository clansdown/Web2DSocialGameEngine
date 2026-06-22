<script lang="ts">
  import { onMount, onDestroy } from 'svelte';
  import GameBoard from '../../lib/GameBoard.svelte';
  import { createEngine, renderFrame, updateEngine } from './tower_defense_engine';
  import type { StartMiniGameResponse } from '../../lib/api';
  import type { TowerDefenseLevelConfig } from './tower_defense_types';

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
  let engine = $state(createEngine(config, 800, 500));
  let animFrameId: number | null = null;
  let lastTime = 0;
  let startTime = 0;

  /**
   * The main game loop. Calculates delta time, updates engine state,
   * and renders the current frame.
   * 
   * @param timestamp - Current time from requestAnimationFrame
   */
  function gameLoop(timestamp: number) {
    if (engine.finished) return;

    const deltaMs = lastTime === 0 ? 0 : timestamp - lastTime;
    lastTime = timestamp;

    engine = updateEngine(engine, deltaMs);

    if (canvas && context) {
      canvas.width = canvas.clientWidth;
      canvas.height = canvas.clientHeight;
      engine = { ...engine, width: canvas.width, height: canvas.height };
      renderFrame(context, engine);
    }

    animFrameId = requestAnimationFrame(gameLoop);
  }

  /**
   * Handles the Win button click. Stops the game loop and reports victory.
   */
  function handleWin() {
    engine = { ...engine, finished: true, won: true, score: 100 };
    onGameOver(true, 100);
  }

  /**
   * Handles the Lose button click. Stops the game loop and reports defeat.
   */
  function handleLose() {
    engine = { ...engine, finished: true, won: false, score: 0 };
    onGameOver(false, 0);
  }

  onMount(() => {
    startTime = performance.now();
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
