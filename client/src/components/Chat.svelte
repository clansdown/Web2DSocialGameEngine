<script lang="ts">
  interface Props {
    onBack: () => void;
    /**
     * Nested route segments after the activity id — e.g. ['thread', '42'] for
     * #/activity/chat/thread/42. Reserved for future thread navigation; the
     * router passes it through, but chat is not implemented yet.
     */
    rest?: string[];
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
