<script lang="ts">
  /**
   * Full-screen backdrop for textured story/game text overlays.
   *
   * This frame deliberately paints no panel, border, or background of its own:
   * StoryText/GameText render their silk/vellum texture as the panel. The
   * backdrop scrolls as one unit, so a tall texture and its text stay in sync.
   *
   * Props:
   *   on_dismiss - Closes the overlay (backdrop click or Escape)
   *   max_width - Optional maximum panel width (default 1000px)
   *   children - The textured panel to center
   */
  import type { Snippet } from 'svelte';

  interface Props {
    on_dismiss: () => void;
    max_width?: string;
    children: Snippet;
  }

  let { on_dismiss, max_width = '1000px', children }: Props = $props();

  let backdrop_el: HTMLDivElement;

  $effect(() => {
    backdrop_el?.focus();
  });

  /**
   * Closes the overlay when the player presses Escape.
   *
   * @param event - Window keydown event
   */
  function handle_keydown(event: KeyboardEvent): void {
    if (event.key === 'Escape') {
      on_dismiss();
    }
  }
</script>

<svelte:window onkeydown={handle_keydown} />

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div
  class="text-overlay-backdrop"
  role="dialog"
  aria-modal="true"
  tabindex="-1"
  bind:this={backdrop_el}
  onclick={on_dismiss}
>
  <!-- svelte-ignore a11y_click_events_have_key_events -->
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <div
    class="text-overlay-panel"
    style="max-width: {max_width};"
    onclick={(event) => event.stopPropagation()}
  >
    {@render children()}
  </div>
</div>

<style>
  .text-overlay-backdrop {
    position: fixed;
    inset: 0;
    z-index: 1050;
    display: flex;
    overflow-y: auto;
    padding: 2rem 1rem;
    background: rgba(0, 0, 0, 0.6);
    overscroll-behavior: contain;
    outline: none;
  }

  .text-overlay-panel {
    flex: 0 0 auto;
    margin: auto;
    width: 90%;
  }
</style>
