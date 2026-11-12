<script lang="ts">
  /**
   * Displays story/narrative text on a silk-texture background.
   * Uses text_silk_*.png as a scalable background image, selecting the
   * appropriate size based on the container's rendered height via
   * ResizeObserver. Supports markdown text via the `text` prop, or
   * custom content via the children snippet.
   *
   * Text may be provided directly (`text`), or fetched from the text system
   * by ID (`id`). When `id` is used, the text is fetched in the current
   * language with the current character's context (gender/name substitution),
   * and re-fetched whenever language or character changes.
   * Optional `tokens` replace `{key}`/`<key>` placeholders before markdown
   * rendering (e.g. {seal} -> an <img> tag).
   */
  import { marked } from 'marked';
  import type { Snippet } from 'svelte';
  import { language, currentCharacter } from '../lib/stores';
  import { getUITexturesRequest } from '../lib/api';
  import { loadText } from '../lib/text';
  import { rewrite_asset_urls } from '../lib/public_urls';
  import TextOverlayFrame from './TextOverlayFrame.svelte';
  import TextOverlayButton from './TextOverlayButton.svelte';
  import type { UITexture, UITexturesResponse, UITextureButton } from '../lib/api';

  interface Props {
    text?: string;
    id?: string;
    tokens?: Record<string, string>;
    children?: Snippet;
    class?: string;
    overlay?: boolean;
    on_dismiss?: () => void;
    continue_label?: string;
    button?: UITextureButton;
  }

  let {
    text,
    id,
    tokens,
    children,
    class: className = '',
    overlay = false,
    on_dismiss,
    continue_label,
    button
  }: Props = $props();

  let fetchedText = $state('');
  let htmlContent = $derived(
    rewrite_asset_urls(marked.parse(applyTokens(id ? fetchedText : (text ?? ''), tokens)) as string)
  );
  let containerEl: HTMLDivElement;
  let textures: UITexture[] = $state([]);
  let texturesLoaded = $state(false);
  let basePaddingVertical = 60;
  let basePaddingHorizontal = 60;
  let paddingVertical = $state(60);
  let paddingHorizontal = $state(60);
  let bgImage = $state('');
  let currentTexture: UITexture | null = $state(null);
  let button_config = $state<UITextureButton>({});

  let effective_button = $derived.by(() => {
    const merged: UITextureButton = { ...button_config };
    if (button) {
      if (button.image !== undefined) merged.image = button.image;
      if (button.image_slice_px !== undefined) merged.image_slice_px = button.image_slice_px;
      if (button.text_color !== undefined) merged.text_color = button.text_color;
      if (button.min_width_px !== undefined) merged.min_width_px = button.min_width_px;
      if (button.min_height_px !== undefined) merged.min_height_px = button.min_height_px;
      if (button.padding !== undefined) merged.padding = button.padding;
    }
    return merged;
  });

  const MAX_STRETCH = 1.3;
  const MIN_STRETCH = 0.7;

  /**
   * Replaces {key} and <key> placeholders in the text with the provided token
   * values. Leaves unknown placeholders untouched.
   *
   * @param content - Raw markdown text
   * @param tokenMap - Map of placeholder key to replacement string
   * @returns Content with placeholders substituted
   */
  function applyTokens(content: string, tokenMap?: Record<string, string>): string {
    if (!tokenMap || !content) return content;
    let result = content;
    for (const [key, value] of Object.entries(tokenMap)) {
      result = result.split(`{${key}}`).join(value);
      result = result.split(`<${key}>`).join(value);
    }
    return result;
  }

  /**
   * Fetches text by ID whenever it (or the language/character context) changes.
   * Guards against races with a cancelled flag on cleanup.
   */
  $effect(() => {
    if (!id) {
      fetchedText = '';
      return;
    }
    $language;
    $currentCharacter;
    let cancelled = false;
    loadText(id).then(t => { if (!cancelled) fetchedText = t; });
    return () => { cancelled = true; };
  });

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
      const response: UITexturesResponse | null = await getUITexturesRequest('story_text');
      if (response) {
        textures = response.textures;
        basePaddingVertical = response.padding_vertical_px;
        basePaddingHorizontal = response.padding_horizontal_px;
        paddingVertical = response.padding_vertical_px;
        paddingHorizontal = response.padding_horizontal_px;
        if (textures.length > 0) {
          minHeight = textures[0].height * MIN_STRETCH;
        }
        button_config = response.button ?? {};
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
        // Horizontal: use offsetWidth directly (stable with border-box)
        paddingHorizontal = basePaddingHorizontal * (w / sel.w);
        // Vertical: use analytic formula to avoid feedback loop
        // P = (B * C) / (T - 2B) where C = content height, T = texture height
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

{#snippet textured_panel()}
  <div
    class="story-text-container rounded-3 shadow-sm {className}"
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
      class="story-text"
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
    {#if overlay && on_dismiss}
      <div class="text-overlay-actions">
        <TextOverlayButton
          on_click={on_dismiss}
          label={continue_label}
          button={effective_button}
        />
      </div>
    {/if}
  </div>
{/snippet}

{#if overlay}
  <TextOverlayFrame on_dismiss={on_dismiss ?? (() => {})}>
    {@render textured_panel()}
  </TextOverlayFrame>
{:else}
  {@render textured_panel()}
{/if}

<style>
  .story-text :global(img) {
    max-width: 100%;
    height: auto;
    display: block;
    margin: 1rem auto;
  }

  .text-overlay-actions {
    display: flex;
    justify-content: center;
    margin-top: 1.5rem;
  }
</style>
