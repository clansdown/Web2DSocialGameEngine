<script lang="ts">
  import { onMount } from 'svelte';

  interface Props {
    gameWidth?: number;
    gameHeight?: number;
    onInit?: (canvas: HTMLCanvasElement, context: CanvasRenderingContext2D) => void;
    canvas?: HTMLCanvasElement;
    context?: CanvasRenderingContext2D;
  }

  let { 
    gameWidth = 10000, 
    gameHeight = 10000, 
    onInit = () => {},
    canvas = $bindable<HTMLCanvasElement>(),
    context = $bindable<CanvasRenderingContext2D>()
  }: Props = $props();

  let canvasContext: CanvasRenderingContext2D | null = $state(null);

  function updateCanvasDimensions(): void {
    if (canvas) {
      canvas.width = gameWidth;
      canvas.height = gameHeight;
      canvasContext = canvas.getContext('2d');
      if (canvasContext) {
        context = canvasContext;
      }
    }
  }

  onMount(async () => {
    updateCanvasDimensions();
    if (canvas && canvasContext) {
      onInit(canvas, canvasContext);
    }
  });

  $effect(() => {
    updateCanvasDimensions();
  });
</script>

<div style="width: 100%; height: 100%; display: block;">
  <canvas
    bind:this={canvas}
    style="display: block; width: 100%; height: 100%;"
  />
</div>
