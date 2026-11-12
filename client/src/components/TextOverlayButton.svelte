<script lang="ts">
  /**
   * Dismiss button for textured story/game text overlays.
   *
   * The label comes from the text system (`ui_continue`) unless `label` is
   * supplied. An optional image can be used as the button plaque; when
   * `image_slice_px` is set, the image is applied as a CSS border-image
   * 9-slice so longer translated labels do not distort it.
   *
   * Props:
   *   on_click - Dismiss handler
   *   label - Optional pre-resolved label override
   *   button - Optional image/style config
   */
  import { language, currentCharacter } from '../lib/stores';
  import { loadText } from '../lib/text';
  import { asset_url } from '../lib/public_urls';
  import type { UITextureButton } from '../lib/api';

  interface Props {
    on_click: () => void;
    label?: string;
    button?: UITextureButton;
  }

  let { on_click, label, button }: Props = $props();

  let loaded_label = $state('');

  /**
   * Resolves a configured button image. Bare filenames live under images/ui;
   * absolute paths/URLs are passed through asset_url unchanged.
   *
   * @param image - Configured image filename or URL
   * @returns Resolved public URL
   */
  function resolve_button_image(image: string): string {
    if (image.startsWith('/') || /^(?:[a-z][a-z0-9+.-]*:|\/\/)/i.test(image)) {
      return asset_url(image);
    }
    return asset_url(`/images/ui/${image}`);
  }

  $effect(() => {
    if (label) {
      loaded_label = label;
      return;
    }
    $language;
    $currentCharacter;
    let cancelled = false;
    loadText('ui_continue').then((text) => {
      if (!cancelled) loaded_label = text;
    });
    return () => {
      cancelled = true;
    };
  });

  let button_style = $derived.by(() => {
    const styles: string[] = [
      'font-family: Georgia, "Palatino", serif',
      'font-size: 1rem',
      'cursor: pointer'
    ];

    if (button?.min_width_px) styles.push(`min-width: ${button.min_width_px}px`);
    if (button?.min_height_px) styles.push(`min-height: ${button.min_height_px}px`);
    styles.push(`padding: ${button?.padding ?? '0.55rem 1.75rem'}`);
    styles.push(`color: ${button?.text_color ?? '#f5e6c8'}`);

    if (button?.image) {
      const image_url = resolve_button_image(button.image);
      if (button.image_slice_px && button.image_slice_px > 0) {
        const slice = button.image_slice_px;
        styles.push(`border: ${slice}px solid transparent`);
        styles.push(`border-image: url("${image_url}") ${slice} fill stretch`);
        styles.push('background: transparent');
      } else {
        styles.push(`background-image: url("${image_url}")`);
        styles.push('background-size: 100% 100%');
        styles.push('background-repeat: no-repeat');
        styles.push('background-color: transparent');
        styles.push('border: 0');
      }
    } else {
      styles.push('background: #8b7355');
      styles.push('border: 1px solid #6b5335');
      styles.push('border-radius: 0.5rem');
      styles.push('box-shadow: 0 2px 6px rgba(0, 0, 0, 0.35)');
    }

    return styles.join('; ');
  });
</script>

{#if loaded_label}
  <button
    type="button"
    class="text-overlay-button"
    style={button_style}
    onclick={on_click}
  >
    {loaded_label}
  </button>
{/if}
