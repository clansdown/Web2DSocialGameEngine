<script lang="ts">
  /**
   * Book component that displays a clickable book icon which opens a dialog
   * showing an opened book with two-page spread. Left page shows an image,
   * right page shows translated markdown text. Supports page flipping via
   * drag and chevron overlays.
   *
   * Props:
   *   pages - Array of pages with image URL and text system ID
   *   toolText - Tooltip text for the closed book icon
   *   width - Fixed width (px). Omit to fill container with aspect ratio
   *   height - Fixed height (px). Omit to fill container with aspect ratio
   */
  import { marked } from 'marked';
  import { language } from '../lib/stores';
  import { getTextsRequest } from '../lib/api';

  interface BookPage {
    image: string;
    textId: string;
  }

  interface Props {
    pages: BookPage[];
    toolText: string;
    width?: number;
    height?: number;
  }

  let { pages, toolText, width, height }: Props = $props();

  let isOpen = $state(false);
  let currentPage = $state(0);
  let pageTexts = $state<Record<string, string>>({});
  let textsLoaded = $state(false);
  let dragStartX = $state(0);
  let isDragging = $state(false);
  let contentKey = $state(0);
  let fading = $state(false);

  let triggerStyle = $derived(
    width && height
      ? `width: ${width}px; height: ${height}px;`
      : 'width: 100%; aspect-ratio: 757 / 914;'
  );

  let hasPrev = $derived(currentPage >= 2);
  let hasNext = $derived(currentPage + 2 < pages.length);

  function open() {
    if (pages.length === 0) return;
    isOpen = true;
    currentPage = 0;
    contentKey = 0;
    loadTexts();
  }

  function close() {
    isOpen = false;
    isDragging = false;
  }

  async function loadTexts() {
    textsLoaded = false;
    const ids = pages.map(p => p.textId);
    try {
      const texts = await getTextsRequest($language, ids);
      pageTexts = texts;
    } catch {
      pageTexts = {};
    }
    textsLoaded = true;
  }

  function goNext() {
    if (!hasNext || fading) return;
    fading = true;
    setTimeout(() => {
      currentPage += 2;
      contentKey++;
      requestAnimationFrame(() => fading = false);
    }, 100);
  }

  function goPrev() {
    if (!hasPrev || fading) return;
    fading = true;
    setTimeout(() => {
      currentPage -= 2;
      contentKey++;
      requestAnimationFrame(() => fading = false);
    }, 200);
  }

  function handlePointerDown(e: PointerEvent) {
    dragStartX = e.clientX;
    isDragging = true;
  }

  function handlePointerUp(e: PointerEvent) {
    if (!isDragging) return;
    isDragging = false;
    const delta = e.clientX - dragStartX;
    if (delta > 50) goPrev();
    else if (delta < -50) goNext();
  }

  function handleKeydown(e: KeyboardEvent) {
    if (e.key === 'Escape') close();
    else if (e.key === 'ArrowLeft') goPrev();
    else if (e.key === 'ArrowRight') goNext();
  }

  function handleOverlayClick(e: MouseEvent) {
    const target = e.target as HTMLElement;
    if (!target.closest('.book-panel')) close();
  }
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div
  class="book-trigger"
  style={triggerStyle}
  title={toolText}
  onclick={open}
  role="button"
  tabindex="0"
  onkeydown={(e) => { if (e.key === 'Enter') open(); }}
>
  <img
    src="/images/ui/book.png"
    alt="Open book"
    style="width: 100%; height: 100%; object-fit: contain; pointer-events: none; display: block;"
  />
</div>

{#if isOpen}
  <!-- svelte-ignore a11y_click_events_have_key_events -->
  <!-- svelte-ignore a11y_no_static_element_interactions -->
  <!-- svelte-ignore a11y_no_noninteractive_tabindex -->
  <div
    class="book-overlay"
    style="position: fixed; top: 0; left: 0; width: 100vw; height: 100vh; z-index: 1050; background: rgba(0, 0, 0, 0.6); display: flex; align-items: center; justify-content: center;"
    onclick={handleOverlayClick}
    onpointerdown={handlePointerDown}
    onpointerup={handlePointerUp}
    onkeydown={handleKeydown}
    tabindex={-1}
  >
      <div class="book-panel" style="position: relative; width: min(90vw, calc(90vh * 1102 / 731)); max-width: 1102px; aspect-ratio: 1102 / 731; user-select: none;" onclick={(e) => e.stopPropagation()}>
        <img
          src="/images/ui/book_opened.png"
          alt=""
          style="width: 100%; height: 100%; display: block; pointer-events: none;"
        />

        {#if pages[currentPage]}
          <div
            class="book-left-page"
            style="position: absolute; left: 8.17%; top: 8.21%; width: 32.67%; height: 68.40%; container-type: size; overflow-y: auto; color: #3a2a1a; font-family: Georgia, Palatino, serif; font-size: clamp(10px, 1.6vw, 16px); line-height: 1.6;"
          >
            <div class="book-text" class:fading style="transition: opacity 0.1s;">
              {#key contentKey}
                {#if textsLoaded}
                  {@html marked.parse(pageTexts[pages[currentPage].textId] ?? '')}
                {:else}
                  <div style="text-align: center; padding-top: 20%; opacity: 0.5;">Loading...</div>
                {/if}
              {/key}
            </div>
            {#if hasPrev}
              <button class="book-turn-btn" style="position: absolute; bottom: 4px; left: 4px; opacity: 0.4; transform: scaleX(-1);" onclick={goPrev}>&#x293A;</button>
            {/if}
          </div>
        {/if}

        {#if pages[currentPage + 1]}
          <div
            class="book-right-page"
            style="position: absolute; left: 49.46%; top: 8.21%; width: 31.31%; height: 68.40%; container-type: size; overflow-y: auto; color: #3a2a1a; font-family: Georgia, Palatino, serif; font-size: clamp(10px, 1.6vw, 16px); line-height: 1.6;"
          >
            <div class="book-text" class:fading style="transition: opacity 0.1s;">
              {#key contentKey}
                {#if textsLoaded}
                  {@html marked.parse(pageTexts[pages[currentPage + 1].textId] ?? '')}
                {:else}
                  <div style="text-align: center; padding-top: 20%; opacity: 0.5;">Loading...</div>
                {/if}
              {/key}
            </div>
            {#if hasNext}
              <button class="book-turn-btn" style="position: absolute; bottom: 4px; right: 4px; opacity: 0.4;" onclick={goNext}>&#x293A;</button>
            {/if}
          </div>
        {/if}

    </div>
  </div>
{/if}

<style>
  .book-trigger {
    display: inline-block;
    cursor: pointer;
    border-radius: 4px;
    transition: background 0.2s;
  }

  .book-trigger:hover {
    background: rgba(255, 255, 255, 0.1);
  }

  .book-panel {
    border-radius: 8px;
  }

  .book-text :global(img) {
    display: block;
    margin: 0.5rem auto;
    max-width: 100%;
    max-height: 50cqh;
    width: auto;
    height: auto;
    object-fit: contain;
  }

  .book-text :global(h1) {
    font-size: clamp(16px, 2.4vw, 26px);
    margin-top: 0;
    margin-bottom: 0.5rem;
    color: #2a1a0a;
  }

  .book-text :global(p) {
    margin-bottom: 0.5rem;
  }

  .book-text.fading {
    opacity: 0;
  }

  .book-turn-btn {
    background: none;
    border: none;
    cursor: pointer;
    font-size: 2.25rem;
    color: #1a0a00;
    padding: 4px 6px;
    line-height: 1;
    transition: opacity 0.2s;
  }

  .book-turn-btn:hover { opacity: 0.8 !important; }

  .book-left-page::-webkit-scrollbar,
  .book-right-page::-webkit-scrollbar {
    width: 4px;
  }

  .book-left-page::-webkit-scrollbar-thumb,
  .book-right-page::-webkit-scrollbar-thumb {
    background: #8b7355;
    border-radius: 2px;
  }
</style>
