<script lang="ts">
  /**
   * Rotate-to-landscape button for touch devices held in portrait.
   *
   * The full pill is shown for the first 15 seconds, then collapses to a small
   * tappable icon so it remains available without becoming nagging.
   */
  import { loadText } from '../lib/text';
  import { language, currentCharacter } from '../lib/stores';

  interface Props {
    active: boolean;
    text_id?: string;
    on_rotate: () => void;
  }

  let {
    active,
    text_id = 'ui_rotate_landscape',
    on_rotate
  }: Props = $props();

  let label = $state('');
  let compact = $state(false);

  $effect(() => {
    $language;
    $currentCharacter;
    let cancelled = false;
    loadText(text_id)
      .then((text) => {
        if (!cancelled) label = text;
      })
      .catch(() => {
        if (!cancelled) label = '';
      });
    return () => {
      cancelled = true;
    };
  });

  $effect(() => {
    if (!active || compact) return;
    const timer = window.setTimeout(() => {
      compact = true;
    }, 15000);
    return () => window.clearTimeout(timer);
  });
</script>

{#if active}
  <button
    type="button"
    class="rotate-game-button"
    class:compact
    title={label}
    aria-label={label}
    onclick={on_rotate}
  >
    <svg
      class="rotate-game-icon"
      viewBox="0 0 24 24"
      width="18"
      height="18"
      aria-hidden="true"
      focusable="false"
    >
      <rect
        x="8"
        y="3"
        width="8"
        height="18"
        rx="2"
        fill="none"
        stroke="currentColor"
        stroke-width="2"
      />
      <path
        d="M4 8a8 8 0 0 0 2.2 9.8"
        fill="none"
        stroke="currentColor"
        stroke-width="2"
        stroke-linecap="round"
      />
      <path
        d="M3.2 4.6 4.3 9l4.4-.8"
        fill="none"
        stroke="currentColor"
        stroke-width="2"
        stroke-linecap="round"
        stroke-linejoin="round"
      />
    </svg>
    <span class="rotate-game-label" class:is-compact={compact}>{label}</span>
  </button>
{/if}

<style>
  .rotate-game-button {
    position: absolute;
    z-index: 20;
    left: 50%;
    bottom: calc(16px + env(safe-area-inset-bottom, 0px));
    transform: translateX(-50%);
    display: inline-flex;
    align-items: center;
    justify-content: center;
    gap: 0.5rem;
    padding: 0.5rem 0.85rem;
    border: 1px solid rgba(248, 249, 250, 0.35);
    border-radius: 999px;
    background: rgba(25, 25, 30, 0.78);
    color: #f8f9fa;
    font-size: 0.9rem;
    line-height: 1.2;
    cursor: pointer;
    pointer-events: auto;
    backdrop-filter: blur(2px);
    transition:
      background 0.2s ease,
      padding 0.2s ease,
      width 0.2s ease,
      height 0.2s ease,
      border-radius 0.2s ease;
  }

  .rotate-game-button:hover {
    background: rgba(45, 45, 55, 0.92);
  }

  .rotate-game-button.compact {
    width: 40px;
    height: 40px;
    padding: 0;
    gap: 0;
    border-radius: 50%;
  }

  .rotate-game-icon {
    flex: 0 0 auto;
  }

  .rotate-game-label {
    white-space: nowrap;
    opacity: 1;
    max-width: 16rem;
    overflow: hidden;
    transition:
      opacity 0.2s ease,
      max-width 0.2s ease;
  }

  .rotate-game-label.is-compact {
    opacity: 0;
    max-width: 0;
  }

  @media (max-width: 420px) {
    .rotate-game-button:not(.compact) {
      max-width: 90vw;
    }

    .rotate-game-label {
      white-space: normal;
      text-align: center;
    }
  }
</style>
