<script lang="ts">
  import { getActiveErrors, removeError, clearAllErrors, type AppError } from '../lib/errors';

  const activeErrors = $derived(getActiveErrors());
  
  function getAlertClass(severity: AppError['severity']): string {
    switch (severity) {
      case 'error': return 'alert-danger';
      case 'warning': return 'alert-warning';
      case 'info': return 'alert-info';
      case 'success': return 'alert-success';
      default: return 'alert-danger';
    }
  }
  
  function dismiss(errorId: string): void {
    removeError(errorId);
  }
  
  function dismissAll(): void {
    clearAllErrors();
  }
</script>

{#if activeErrors.length > 0}
  <div class="error-overlay fixed-top p-3 d-flex flex-column gap-2" style="z-index: 1060;">
    {#each activeErrors as error (error.id)}
      <div 
        class="alert {getAlertClass(error.severity)} alert-dismissible fade show"
        role="alert"
        class:alert-dismissible={error.dismissible}
        class:fading-out={error.auto_dismissed}
      >
        <strong>{error.category.toUpperCase()}:</strong> {error.user_message}
        {#if error.dismissible}
          <button 
            type="button" 
            class="btn-close" 
            data-bs-dismiss="alert"
            aria-label="Close"
            onclick={() => dismiss(error.id)}
          ></button>
        {/if}
        {#if error.context}
          <small class="d-block mt-1 text-muted">
            Context: {error.context}
          </small>
        {/if}
      </div>
    {/each}
    
    {#if activeErrors.length > 1}
      <button class="btn btn-sm btn-outline-light align-self-end" onclick={dismissAll}>
        Dismiss all ({activeErrors.length})
      </button>
    {/if}
  </div>
{/if}

<style>
  .fading-out {
    opacity: 0;
    transition: opacity 0.3s ease-out;
  }
</style>
