<script lang="ts">
  import { getActiveErrors, removeError, clearAllErrors, type AppError } from '../lib/errors';

  const activeErrors = $derived(getActiveErrors());
  
  /**
   * Maps error severity levels to Bootstrap alert CSS classes.
   * Converts internal severity to appropriate alert styling.
   * 
   * @param severity - ErrorSeverity level (error, warning, info, success)
   * @returns string - Bootstrap alert class name
   * 
   * Usage: Used in template to determine alert styling
   */
  function getAlertClass(severity: AppError['severity']): string {
    switch (severity) {
      case 'error': return 'alert-danger';
      case 'warning': return 'alert-warning';
      case 'info': return 'alert-info';
      case 'success': return 'alert-success';
      default: return 'alert-danger';
    }
  }
  
  /**
   * Dismisses a single error by removing it from the error store.
   * 
   * @param errorId - Unique identifier of the error to dismiss
   * @returns void
   * 
   * Usage: Attached to individual error close button
   */
  function dismiss(errorId: string): void {
    removeError(errorId);
  }
  
  /**
   * Dismisses all active errors by clearing the error store.
   * 
   * @param none
   * @returns void
   * 
   * Usage: Attached to "Dismiss all" button when multiple errors exist
   */
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
