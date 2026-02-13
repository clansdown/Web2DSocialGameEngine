<script lang="ts">
  import { createEventDispatcher } from 'svelte';
  import LoginPage from './LoginPage.svelte';
  import CreateAccountPage from './CreateAccountPage.svelte';

  const dispatch = createEventDispatcher<{
    switchToCreate: void;
    switchToLogin: void;
  }>();

  let currentView: 'login' | 'create' = $state('login');

  function handleSwitchToCreate() {
    currentView = 'create';
  }

  function handleSwitchToLogin() {
    currentView = 'login';
  }
</script>

{#if currentView === 'login'}
  <LoginPage on:switchToCreate={handleSwitchToCreate} />
{:else}
  <CreateAccountPage on:switchToLogin={handleSwitchToLogin} />
{/if}
