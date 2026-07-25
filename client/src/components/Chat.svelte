<script lang="ts">
  interface Props {
    onBack: () => void;
  }

  let { onBack }: Props = $props();
</script>

<div class="container py-5">
  <button class="btn btn-outline-secondary mb-4" onclick={onBack}>
    &larr; Back
  </button>
  <div class="card">
    <div class="card-body text-center p-5">
      <h3 class="card-title">Chat</h3>
      <p class="text-muted mt-3">Coming Soon</p>
    </div>
  </div>
</div>
