<script lang="ts">
/**
 * Displays game text on a vellum-texture background.
 * Uses text_vellum_*.png as a scalable background image, selecting the
 * appropriate size based on the container's rendered height via
 * ResizeObserver. Supports markdown text via the `text` prop, or
 * custom content via the children snippet.
 */
  import { marked } from 'marked';
  import type { Snippet } from 'svelte';
  import { getUITexturesRequest } from '../lib/api';
  import type { UITexture, UITexturesResponse } from '../lib/api';

  interface Props {
    text?: string;
    children?: Snippet;
    class?: string;
  }

  let { text, children, class: className = '' }: Props = $props();

  let htmlContent = $derived(text ? marked.parse(text) : '');
  let containerEl: HTMLDivElement;
  let textures: UITexture[] = $state([]);
  let texturesLoaded = $state(false);
  let basePaddingVertical = 60;
  let basePaddingHorizontal = 60;
  let paddingVertical = $state(60);
  let paddingHorizontal = $state(60);
  let bgImage = $state('');
  let currentTexture: UITexture | null = $state(null);

  const MAX_STRETCH = 1.3;
  const MIN_STRETCH = 0.7;

  function pickBg(height: number): { url: string; w: number; h: number } | null {
    if (textures.length === 0) return null;

    let selected: UITexture | null = currentTexture;

    if (selected) {
      const lower = selected.height * MIN_STRETCH;
      const upper = selected.height * MAX_STRETCH;
      if (height < lower || height > upper) {
        selected = null;
      }
    }

    if (!selected) {
      let best: UITexture | null = null;
      let bestDist = Infinity;
      for (const t of textures) {
        const dist = Math.abs(height / t.height - 1);
        if (dist < bestDist) {
          bestDist = dist;
          best = t;
        }
      }
      selected = best ?? textures[textures.length - 1];
      currentTexture = selected;
    }

    return { url: selected.url, w: selected.width, h: selected.height };
  }

  let minHeight = $state(0);

  $effect(() => {
    async function load(): Promise<void> {
      const response: UITexturesResponse | null = await getUITexturesRequest('game_text');
      if (response) {
        textures = response.textures;
        basePaddingVertical = response.padding_vertical_px;
        basePaddingHorizontal = response.padding_horizontal_px;
        paddingVertical = response.padding_vertical_px;
        paddingHorizontal = response.padding_horizontal_px;
        if (textures.length > 0) {
          minHeight = textures[0].height * MIN_STRETCH;
        }
      }
      texturesLoaded = true;
    }
    load();
  });

  $effect(() => {
    text;
    const el = containerEl;
    if (!el || !texturesLoaded) return;

    let frameId: number;

    function apply(): void {
      const h = el.offsetHeight;
      const w = el.offsetWidth;
      const sel = pickBg(h);
      if (sel) {
        bgImage = sel.url;
        paddingHorizontal = basePaddingHorizontal * (w / sel.w);
        const style = getComputedStyle(el);
        const padT = parseFloat(style.paddingTop) || 0;
        const padB = parseFloat(style.paddingBottom) || 0;
        const contentH = el.clientHeight - padT - padB;
        const dv = sel.h - 2 * basePaddingVertical;
        paddingVertical = dv > 0
          ? basePaddingVertical * contentH / dv
          : basePaddingVertical * (contentH / sel.h);
      }
    }

    function measure(): void {
      apply();
    }

    measure();

    const observer = new ResizeObserver(() => {
      apply();
    });

    observer.observe(el);

    frameId = requestAnimationFrame(measure);

    return () => {
      observer.disconnect();
      cancelAnimationFrame(frameId);
    };
  });
</script>

<div
  class="game-text-container rounded-3 shadow-sm {className}"
  bind:this={containerEl}
  style="
    background-image: {bgImage ? 'url(' + bgImage + ')' : 'none'};
    background-size: 100% 100%;
    padding: {paddingVertical}px {paddingHorizontal}px;
    display: flex;
    flex-direction: column;
    justify-content: center;
    min-height: {minHeight}px;
  "
>
  <div
    class="game-text"
    style="
      color: #3a3025;
      font-family: 'Georgia', 'Palatino', serif;
      line-height: 1.65;
      mix-blend-mode: multiply;
      opacity: 0.85;
      text-align: justify;
    "
  >
    {#if text}
      {@html htmlContent}
    {:else if children}
      {@render children()}
    {/if}
  </div>
</div>
