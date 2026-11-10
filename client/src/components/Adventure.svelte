<script lang="ts">
  import { onMount } from 'svelte';
  import { loadTexts } from '../lib/text';

  interface Props {
    onBack: () => void;
  }

  let { onBack }: Props = $props();

  let texts = $state<Record<string, string>>({});

  onMount(async () => {
    texts = await loadTexts(['ui_back_button', 'ui_adventure_title', 'ui_adventure_coming_soon']);
  });
</script>

<div class="container py-5">
  <button class="btn btn-outline-secondary mb-4" onclick={onBack}>
    &larr; {texts['ui_back_button'] ?? ''}
  </button>
  <div class="card">
    <div class="card-body text-center p-5">
      <h3 class="card-title">{texts['ui_adventure_title'] ?? ''}</h3>
      <p class="text-muted mt-3">{texts['ui_adventure_coming_soon'] ?? ''}</p>
    </div>
  </div>
</div>
