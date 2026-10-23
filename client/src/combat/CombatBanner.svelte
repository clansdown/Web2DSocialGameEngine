<script lang="ts">
  /**
   * CombatBanner — a procedural heraldic banner rendered from a cosmetic
   * appearance descriptor (rts_design.md §15.2). The `banner` slot shows in the
   * lobby, deploy, and results screens and above the knight. Appearance only:
   * no gameplay key is read or rendered here.
   */
  import type { combat_appearance_dto } from './protocol';

  interface Props {
    banner?: combat_appearance_dto | null;
    size?: number;
    title?: string;
  }

  let { banner = null, size = 40, title = '' }: Props = $props();

  let palette = $derived(banner?.palette ?? ['#1b1b2b', '#4da3ff']);
  let primary = $derived(palette[0] ?? '#1b1b2b');
  let accent = $derived(palette[1] ?? '#4da3ff');
  let trim = $derived(banner?.trim ?? '#c9a227');
  let pattern = $derived(banner?.pattern ?? 'plain');
  let emblem = $derived(banner?.emblem ?? 'none');
</script>

<svg
  width={size}
  height={size * 1.4}
  viewBox="0 0 40 56"
  role="img"
  aria-label={title}
  class="combat-banner"
>
  {#if title}<title>{title}</title>{/if}
  <rect x="1" y="1" width="38" height="54" rx="3" fill={primary} stroke={trim} stroke-width="2" />
  {#if pattern === 'barry'}
    {#each [8, 20, 32] as y}
      <rect x="3" y={y} width="34" height="5" fill={accent} opacity="0.75" />
    {/each}
  {:else if pattern === 'quarterly'}
    <rect x="3" y="3" width="17" height="25" fill={accent} opacity="0.8" />
    <rect x="20" y="28" width="17" height="25" fill={accent} opacity="0.8" />
  {:else if pattern === 'chevron'}
    <path d="M3 34 L20 16 L37 34 L37 53 L3 53 Z" fill={accent} opacity="0.75" />
  {:else if pattern === 'sable'}
    <rect x="3" y="3" width="34" height="50" fill={accent} opacity="0.2" />
  {/if}
  {#if emblem === 'raven'}
    <path d="M20 22 l6 6 l-4 0 l0 8 l-4 0 l0 -8 l-4 0 z" fill={trim} />
  {:else if emblem === 'flame'}
    <path d="M20 18 q5 8 5 12 a5 5 0 0 1 -10 0 q0 -4 5 -12 z" fill={trim} />
  {:else if emblem === 'leaf'}
    <ellipse cx="20" cy="30" rx="6" ry="10" fill={trim} opacity="0.85" />
  {/if}
</svg>

<style>
  .combat-banner {
    display: inline-block;
    vertical-align: middle;
  }
</style>
