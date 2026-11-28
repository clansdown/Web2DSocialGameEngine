<script lang="ts">
/**
 * Displays game text on a 9-sliced vellum texture.
 * Uses text_vellum_* images, selecting the closest aspect ratio for the
 * rendered panel and preserving the deckled edge with CSS border-image
 * slicing. Supports markdown text via the `text` prop, or custom content
 * via the children snippet.
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
  import {
    compute_texture_layout,
    select_texture_for_content,
    texture_layouts_equal,
    texture_min_height,
    type TextureLayout
  } from '../lib/text_texture';
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
  let contentEl: HTMLDivElement;
  let textures: UITexture[] = $state([]);
  let texturesLoaded = $state(false);
  let basePaddingVertical = $state(60);
  let basePaddingHorizontal = $state(60);
  let layout: TextureLayout | null = $state(null);
  let currentTexture: UITexture | null = $state(null);
  let button_config = $state<UITextureButton>({});
  let last_texture: UITexture | null = null;
  let last_layout: TextureLayout | null = null;

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

  let minHeight = $state(0);

  $effect(() => {
    async function load(): Promise<void> {
      const response: UITexturesResponse | null = await getUITexturesRequest('game_text');
      if (response) {
        textures = response.textures;
        basePaddingVertical = response.padding_vertical_px;
        basePaddingHorizontal = response.padding_horizontal_px;
        minHeight = texture_min_height(textures);
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

    const active_textures = textures;
    const padding_vertical = basePaddingVertical;
    const padding_horizontal = basePaddingHorizontal;
    const minimum_height = minHeight;

    let frame_id: number;

    function apply(): void {
      const panel_width = el.offsetWidth;
      const content_height = Math.max(0, contentEl?.offsetHeight ?? 0);
      const selected_texture: UITexture | null = select_texture_for_content(
        active_textures,
        panel_width,
        content_height,
        padding_vertical,
        padding_horizontal,
        minimum_height,
        last_texture
      );
      if (!selected_texture) return;

      if (!last_texture || last_texture.url !== selected_texture.url) {
        last_texture = selected_texture;
        currentTexture = selected_texture;
      }

      const next_layout: TextureLayout = compute_texture_layout(
        selected_texture,
        panel_width,
        content_height,
        padding_vertical,
        padding_horizontal,
        minimum_height
      );
      if (!texture_layouts_equal(last_layout, next_layout)) {
        last_layout = next_layout;
        layout = next_layout;
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
    if (contentEl) observer.observe(contentEl);

    frame_id = requestAnimationFrame(measure);

    return () => {
      observer.disconnect();
      cancelAnimationFrame(frame_id);
    };
  });
</script>

{#snippet textured_panel()}
  <div
    class="game-text-container shadow-sm {className}"
    bind:this={containerEl}
    style="
      box-sizing: border-box;
      border-style: {layout ? 'solid' : 'none'};
      border-color: transparent;
      border-width: {layout ? layout.border_vertical_px : 0}px {layout ? layout.border_horizontal_px : 0}px;
      border-image-source: {currentTexture ? `url('${currentTexture.url}')` : 'none'};
      border-image-slice: {layout ? `${layout.slice_vertical_px} ${layout.slice_horizontal_px} fill` : '0'};
      border-image-width: 1;
      border-image-repeat: stretch;
      padding: {layout ? layout.padding_vertical_px : basePaddingVertical}px {layout ? layout.padding_horizontal_px : basePaddingHorizontal}px;
      display: flex;
      flex-direction: column;
      justify-content: center;
      min-height: {minHeight}px;
    "
  >
    <div class="textured-content" bind:this={contentEl}>
      <div
        class="game-text"
        style="
          color: #3a3025;
          font-family: 'Georgia', 'Palatino', serif;
          line-height: 1.65;
          mix-blend-mode: multiply;
          opacity: 0.8;
          text-align: justify;
        "
      >
        {#if text || id}
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
.textured-content {
  display: flex;
  flex-direction: column;
  width: 100%;
}

.game-text {
text-shadow: 
    0 0 0.4px rgba(26, 26, 26, 0.6),      /* Soft edge bleed */
    0.3px 0.3px 0.5px rgba(90, 70, 50, 0.3); /* Faint, warm micro-shadow for depth */
}

.text-overlay-actions {
  display: flex;
  justify-content: center;
  margin-top: 1.5rem;
}
</style>