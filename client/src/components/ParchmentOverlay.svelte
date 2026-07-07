<script lang="ts">
  /**
   * Displays an overlay with vertically-tiled parchment background (CSS gradient stub),
   * showing an intro narrative title and body paragraphs.
   * Used after path selection to present the character's introductory story.
   *
   * When images/ui/parchment_bg.png exists, swap the CSS gradient for:
   *   background: url(/images/ui/parchment_bg.png) repeat-y;
   *
   * Props:
   *   title - The overlay title text
   *   body - The overlay body text (paragraphs separated by \n\n)
   *   onDismiss - Callback when user clicks Continue
   */

  interface Props {
    title: string;
    body: string;
    onDismiss: () => void;
  }

  let { title, body, onDismiss }: Props = $props();

  /**
   * Splits body text into paragraphs for individual rendering.
   * Paragraphs are separated by double newlines.
   *
   * @param text - The body text to split
   * @returns string[] - Array of paragraph strings
   */
  function getParagraphs(text: string): string[] {
    return text.split('\n\n').filter(p => p.trim().length > 0);
  }
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div
  class="position-fixed top-0 start-0 w-100 h-100 d-flex align-items-center justify-content-center"
  style="z-index: 1050; background: rgba(0, 0, 0, 0.6);"
  onclick={onDismiss}
>
  <!-- Parchment panel: CSS gradient stub, replace with tiled image when available -->
  <div
    class="p-5 rounded shadow-lg"
    style="
      max-width: 600px;
      width: 90%;
      max-height: 80vh;
      overflow-y: auto;
      background: linear-gradient(180deg, #f5e6c8, #ede0c8, #f5e6c8);
      border: 2px solid #8b7355;
      color: #3a2a1a;
    "
    onclick={(e) => e.stopPropagation()}
  >
    <h2 class="text-center mb-4" style="font-family: serif;">{title}</h2>

    {#each getParagraphs(body) as paragraph}
      <p class="mb-3" style="font-size: 1.1rem; line-height: 1.7; font-family: serif;">
        {paragraph}
      </p>
    {/each}

    <div class="text-center mt-4">
      <button
        class="btn px-4 py-2"
        style="background: #8b7355; color: #f5e6c8; border: 1px solid #6b5335;"
        onclick={onDismiss}
      >
        Continue
      </button>
    </div>
  </div>
</div>
