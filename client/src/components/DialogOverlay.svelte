<script lang="ts">
  /**
   * Displays a modal overlay with a dark backdrop and centered content panel.
   * Wraps its children with the overlay frame (title, dismiss button).
   * When no children are provided, falls back to the legacy body-paragraph display.
   *
   * Props:
   *   title - The overlay title text (empty string to hide heading)
   *   body - (legacy) Body text, paragraphs separated by \n\n
   *   onDismiss - Callback when user clicks Continue or backdrop
   *   children - Snippet for custom content inside the overlay panel
   *   size - Panel width: 'md' (600px), 'lg' (800px), 'xlg' (1000px)
   *   noPadding - When true, removes inner padding (for components with their own padding)
   */

  import type { Snippet } from 'svelte';

  interface Props {
    title: string;
    body?: string;
    onDismiss: () => void;
    children?: Snippet;
    size?: 'md' | 'lg' | 'xlg';
    noPadding?: boolean;
  }

  let { title, body = '', onDismiss, children, size = 'md', noPadding = false }: Props = $props();

  const panelWidth = $derived(
    size === 'xlg' ? '1000px' : size === 'lg' ? '800px' : '600px'
  );

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
  <!-- Content panel -->
  <div
    class="rounded shadow-lg"
    class:p-5={!noPadding}
    style="
      max-width: {panelWidth};
      width: 90%;
      max-height: 80vh;
      overflow-y: auto;
      background: linear-gradient(180deg, #f5e6c8, #ede0c8, #f5e6c8);
      border: 2px solid #8b7355;
      color: #3a2a1a;
    "
    onclick={(e) => e.stopPropagation()}
  >
    {#if title}
      <h2 class="text-center mb-4" style="font-family: serif;">{title}</h2>
    {/if}

    {#if children}
      {@render children()}
    {:else}
      {#each getParagraphs(body) as paragraph}
        <p class="mb-3" style="font-size: 1.1rem; line-height: 1.7; font-family: serif;">
          {paragraph}
        </p>
      {/each}
    {/if}

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
