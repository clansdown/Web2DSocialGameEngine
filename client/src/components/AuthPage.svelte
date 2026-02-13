<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import LoginPage from './LoginPage.svelte';
  import CreateAccountPage from './CreateAccountPage.svelte';

  const dispatch = createEventDispatcher<{
    switchToCreate: void;
    switchToLogin: void;
  }>();

  let currentView: 'login' | 'create' = $state('login');

  /**
   * Switches the auth view to account creation mode.
   * Updates currentView state to show CreateAccountPage.
   * 
   * @param none
   * @returns void
   * 
   * Usage: Attached to "Create an account" button in LoginPage
   */
  function handleSwitchToCreate() {
    currentView = 'create';
  }

  /**
   * Switches the auth view back to login mode.
   * Updates currentView state to show LoginPage.
   * 
   * @param none
   * @returns void
   * 
   * Usage: Attached to "Back to Login" button in CreateAccountPage
   */
  function handleSwitchToLogin() {
    currentView = 'login';
  }
</script>

{#if currentView === 'login'}
  <LoginPage on:switchToCreate={handleSwitchToCreate} />
{:else}
  <CreateAccountPage on:switchToLogin={handleSwitchToLogin} />
{/if}
