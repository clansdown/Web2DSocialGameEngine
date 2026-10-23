<script lang="ts">
  /**
   * RetinuePortrait renders one member's headshot inside an optional decorative
   * frame. The frame is an ordinary transparent PNG drawn ON TOP of the
   * portrait, so the overlay art can have non-square/ornamental interior edges;
   * the portrait is stretched (object-fit: fill) into the rectangle the frame
   * config maps it to, and the frame's opaque pixels hide whatever spills past
   * the opening.
   *
   * Geometry comes from `game/config/portrait_frames.json` (via the cached
   * lib/portrait_frames.ts loader): each frame declares its image, its pixel
   * size, and the portrait destination rect in those same pixels. The frame is
   * contained inside the `size` box (preserving its aspect) and the portrait is
   * positioned using the identical contain math, so non-square frames align
   * correctly.
   *
   * When no portrait URL exists the component falls back to the class icon
   * (combatants/<class>/idle/1.png), then to an empty framed square. When no
   * frame config (or image) is available the portrait simply fills the box.
   *
   * @param portrait_url - Server-resolved portrait URL (may be "")
   * @param fallback_class - Unit class id for the fallback icon
   * @param variant - Frame variant id (e.g. "recruit", "management")
   * @param size - Rendered square size in pixels
   */
  import { onMount } from 'svelte';
  import { unit_sprite_url } from '../combat/unit_sprites';
  import { with_avatar_auth } from '../lib/avatars';
  import { load_portrait_frames } from '../lib/portrait_frames';
  import type { portrait_frame_dto } from '../lib/api';

  interface Props {
    portrait_url?: string;
    fallback_class?: string;
    variant?: 'recruit' | 'management';
    size?: number;
  }

  let {
    portrait_url = '',
    fallback_class = '',
    variant = 'management',
    size = 96
  }: Props = $props();

  let frames = $state<Record<string, portrait_frame_dto>>({});
  let frame_ok = $state(true);
  let icon_ok = $state(true);
  let head_ok = $state(true);

  const resolved_url = $derived(with_avatar_auth(portrait_url));
  const frame = $derived(frames[variant] ?? null);

  // Computed layout: contain the frame image inside the `size` box and map the
  // configured portrait rect through the same scale/offset. With no frame we
  // just fill the box.
  const layout = $derived.by((): {
    has_frame: boolean;
    frame_image: string;
    left: number;
    top: number;
    width: number;
    height: number;
  } => {
    const f = frame;
    if (!f || !frame_ok || f.image_width <= 0 || f.image_height <= 0) {
      return { has_frame: false, frame_image: '', left: 0, top: 0, width: size, height: size };
    }
    const scale = Math.min(size / f.image_width, size / f.image_height);
    const offset_x = (size - f.image_width * scale) / 2;
    const offset_y = (size - f.image_height * scale) / 2;
    return {
      has_frame: true,
      frame_image: f.image,
      left: offset_x + f.portrait.x * scale,
      top: offset_y + f.portrait.y * scale,
      width: f.portrait.width * scale,
      height: f.portrait.height * scale
    };
  });

  onMount(() => {
    load_portrait_frames()
      .then((loaded) => { frames = loaded; })
      .catch(() => { /* no frames authored yet — portrait fills the box */ });
  });

  // When a reused instance is handed a new source, allow the images to retry.
  $effect(() => {
    void resolved_url;
    void fallback_class;
    void frame;
    head_ok = true;
    icon_ok = true;
    frame_ok = true;
  });
</script>

<div class="retinue-portrait" style="width: {size}px; height: {size}px;">
  <div
    class="retinue-portrait__inner"
    style="left: {layout.left}px; top: {layout.top}px; width: {layout.width}px; height: {layout.height}px;"
  >
    {#if resolved_url && head_ok}
      <img
        class="retinue-portrait__head"
        src={resolved_url}
        alt=""
        loading="lazy"
        onerror={() => { head_ok = false; }}
      />
    {:else if fallback_class && icon_ok}
      <img
        class="retinue-portrait__icon"
        src={unit_sprite_url(fallback_class)}
        alt=""
        onerror={() => { icon_ok = false; }}
      />
    {/if}
  </div>

  {#if layout.has_frame}
    <img
      class="retinue-portrait__frame"
      src={layout.frame_image}
      alt=""
      onerror={() => { frame_ok = false; }}
    />
  {/if}
</div>

<style>
  .retinue-portrait {
    position: relative;
    flex: 0 0 auto;
    display: block;
  }

  .retinue-portrait__inner {
    position: absolute;
    overflow: hidden;
    border-radius: 6px;
    background: rgba(0, 0, 0, 0.25);
  }

  /* Stretch to fill — deliberately NOT object-fit: contain. */
  .retinue-portrait__head {
    width: 100%;
    height: 100%;
    object-fit: fill;
    display: block;
  }

  .retinue-portrait__icon {
    width: 100%;
    height: 100%;
    object-fit: contain;
    opacity: 0.8;
    display: block;
  }

  .retinue-portrait__frame {
    position: absolute;
    inset: 0;
    width: 100%;
    height: 100%;
    object-fit: contain;
    pointer-events: none;
    display: block;
  }
</style>
