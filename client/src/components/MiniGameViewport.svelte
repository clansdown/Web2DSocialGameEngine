<script lang="ts">
  /**
   * Full-screen mini-game shell.
   *
   * Owns the responsive viewport, the optional forced-landscape rotation, and
   * the rotate-to-landscape button. The shell element and rotation state are
   * exposed to the active mini-game through Svelte context so the game can fit
   * its canvas and install the rotated input bridge.
   */
  import { setContext } from 'svelte';
  import { writable } from 'svelte/store';
  import type { Snippet } from 'svelte';
  import RotateGameButton from './RotateGameButton.svelte';
  import { observe_viewport_changes, is_touch_device } from '../lib/canvas_fit';
  import {
    MINI_GAME_VIEWPORT_CONTEXT,
    type mini_game_viewport_context
  } from '../lib/mini_game_viewport';

  interface Props {
    children?: Snippet;
  }

  let { children }: Props = $props();

  let shell_el = $state<HTMLDivElement | null>(null);
  let viewport_width = $state(typeof window !== 'undefined' ? window.innerWidth : 0);
  let viewport_height = $state(typeof window !== 'undefined' ? window.innerHeight : 0);
  let physical_portrait = $state(false);
  let forced_landscape = $state(false);

  const touch_device = is_touch_device();

  let render_rotation = $derived(forced_landscape && physical_portrait);
  let show_rotate_button = $derived(
    !forced_landscape && physical_portrait && touch_device
  );
  let shell_style = $derived(
    render_rotation
      ? `width:${viewport_height}px;height:${viewport_width}px;`
      : `width:${viewport_width}px;height:${viewport_height}px;`
  );

  const shell_store = writable<HTMLElement | null>(null);
  const rotated_store = writable(false);
  const forced_store = writable(false);

  const context: mini_game_viewport_context = {
    shell: shell_store,
    rotated: rotated_store,
    forced: forced_store
  };
  setContext(MINI_GAME_VIEWPORT_CONTEXT, context);

  /**
   * Updates the cached viewport dimensions and whether the physical viewport
   * is portrait. Uses visualViewport when available so mobile browser chrome
   * does not leave stale dimensions.
   */
  function update_viewport_metrics(): void {
    const visual_viewport = window.visualViewport;
    const width = visual_viewport?.width ?? window.innerWidth;
    const height = visual_viewport?.height ?? window.innerHeight;

    viewport_width = width;
    viewport_height = height;
    physical_portrait = height > width;
  }

  $effect(() => {
    shell_store.set(shell_el);
    rotated_store.set(render_rotation);
    forced_store.set(forced_landscape);
  });

  $effect(() => {
    if (!shell_el) return;
    update_viewport_metrics();
    return observe_viewport_changes(shell_el, update_viewport_metrics);
  });

  function rotate_to_landscape(): void {
    forced_landscape = true;
  }
</script>

<div
  bind:this={shell_el}
  class="mini-game-shell"
  class:is-rotated={render_rotation}
  style={shell_style}
>
  {@render children?.()}
  <RotateGameButton
    active={show_rotate_button}
    on_rotate={rotate_to_landscape}
  />
</div>

<style>
  .mini-game-shell {
    position: fixed;
    inset: 0;
    width: 100%;
    height: 100vh;
    height: 100dvh;
    overflow: hidden;
    background: #000;
    overscroll-behavior: none;
  }

  .mini-game-shell.is-rotated {
    inset: auto;
    left: 50%;
    top: 50%;
    transform: translate(-50%, -50%) rotate(90deg);
    transform-origin: center center;
  }
</style>
