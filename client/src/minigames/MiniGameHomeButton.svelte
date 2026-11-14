<script lang="ts">
  /**
   * Shared mini-game navigation button that always returns to the hub grid.
   *
   * The label comes from the text system. A small house icon keeps the control
   * recognizable across languages.
   */
  import { loadText } from '../lib/text';
  import { language, currentCharacter } from '../lib/stores';

  interface Props {
    onHome: () => void;
  }

  let { onHome }: Props = $props();

  let label = $state('');

  $effect(() => {
    $language;
    $currentCharacter;
    let cancelled = false;
    loadText('ui_home_button')
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
</script>

<button
  type="button"
  class="btn btn-outline-secondary d-inline-flex align-items-center gap-1"
  title={label}
  aria-label={label}
  onclick={onHome}
>
  <svg
    xmlns="http://www.w3.org/2000/svg"
    width="16"
    height="16"
    fill="currentColor"
    viewBox="0 0 16 16"
    aria-hidden="true"
    focusable="false"
  >
    <path
      d="M8.707 1.5a1 1 0 0 0-1.414 0L.646 8.146a.5.5 0 0 0 .708.708L2 8.207V13.5A1.5 1.5 0 0 0 3.5 15h9a1.5 1.5 0 0 0 1.5-1.5V8.207l.646.647a.5.5 0 0 0 .708-.708L13 5.793V2.5a.5.5 0 0 0-.5-.5h-1a.5.5 0 0 0-.5.5v1.293L8.707 1.5ZM13 7.207V13.5a.5.5 0 0 1-.5.5h-9a.5.5 0 0 1-.5-.5V7.207l5-5 5 5Z"
    />
  </svg>
  <span>{label}</span>
</button>
