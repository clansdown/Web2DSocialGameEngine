<script lang="ts">
  /**
   * PlayerAvatar renders a character's own portrait (the layered safe/custom
   * PFP) in a portrait-shaped box. It is deliberately separate from
   * RetinuePortrait, which renders retinue headshots inside decorative frames
   * (square, framed): player avatars carry no retinue frame and are portrait
   * shaped.
   *
   * The image is scaled with `object-fit: contain` so the whole portrait is
   * visible, preserving its aspect ratio, inside the 1:1.5 (w:h) area. Any
   * letterbox space is filled by the mat background.
   *
   * @param portrait_url - Server-resolved avatar URL (may be "")
   * @param width - Box width in pixels; the height is 1.5x this
   */
  import { with_avatar_auth } from '../lib/avatars';

  interface Props {
    portrait_url?: string;
    width?: number;
  }

  let { portrait_url = '', width = 160 }: Props = $props();

  let ok = $state(true);
  const resolved_url = $derived(with_avatar_auth(portrait_url));
  const height = $derived(Math.round(width * 1.5));

  // When a reused instance is handed a new source, allow the image to retry.
  $effect(() => {
    void resolved_url;
    ok = true;
  });
</script>

<div class="player-avatar" style="width: {width}px; height: {height}px;">
  {#if resolved_url && ok}
    <img
      class="player-avatar__img"
      src={resolved_url}
      alt=""
      onerror={() => { ok = false; }}
    />
  {/if}
</div>

<style>
  .player-avatar {
    position: relative;
    display: block;
    overflow: hidden;
    border-radius: 8px;
    background: rgba(0, 0, 0, 0.35);
    flex: 0 0 auto;
  }

  /* Contain: the whole portrait fits inside the 1:1.5 area, aspect preserved. */
  .player-avatar__img {
    width: 100%;
    height: 100%;
    object-fit: contain;
    display: block;
  }
</style>
