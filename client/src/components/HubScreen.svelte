<script lang="ts">
  import { language, playerGameState, currentCharacter } from '../lib/stores';
  import { getTextsRequest } from '../lib/api';
  import { getConfig, setConfig } from '../lib/storage';
  import MiniGameSelect from '../minigames/MiniGameSelect.svelte';
  import ManorMenu from './ManorMenu.svelte';
  import Chat from './Chat.svelte';
  import RoyalTournament from './RoyalTournament.svelte';
  import Adventure from './Adventure.svelte';

  interface Props {
    activities?: string[];
    onStartLevel: (gameId: string, levelId: number) => void;
  }

  let { activities = [], onStartLevel }: Props = $props();

  let selectedActivity = $state<string | null>(null);
  let displayNames = $state<Record<string, string>>({});
  let resuming = $state(true);

  /**
   * Loads display names for all available activities from the text system.
   */
  async function loadActivityNames() {
    const keys = activities.map(id => `activity_${id}`);
    try {
      const texts = await getTextsRequest($language, keys, $currentCharacter?.sex || undefined);
      const names: Record<string, string> = {};
      for (const id of activities) {
        names[id] = texts[`activity_${id}`] || id.charAt(0).toUpperCase() + id.slice(1);
      }
      displayNames = names;
    } catch {
      // Fallback to capitalized IDs
      const names: Record<string, string> = {};
      for (const id of activities) {
        names[id] = id.charAt(0).toUpperCase() + id.slice(1);
      }
      displayNames = names;
    }
  }

  /**
   * Restores the last activity from OPFS on mount.
   */
  async function restoreLastActivity() {
    try {
      const last = await getConfig<string>('last_activity');
      if (last && activities.includes(last)) {
        selectedActivity = last;
      }
    } catch {
      // Ignore
    } finally {
      resuming = false;
    }
  }

  function selectActivity(id: string) {
    selectedActivity = id;
    setConfig('last_activity', id);
  }

  function goBackToHub() {
    selectedActivity = null;
    setConfig('last_activity', null);
  }

  $effect(() => {
    if (activities.length > 0) {
      loadActivityNames();
      restoreLastActivity();
    } else {
      resuming = false;
    }
  });
</script>

<div class="container py-4">
  {#if resuming}
    <div class="d-flex justify-content-center py-5">
      <div class="spinner-border" role="status">
        <span class="visually-hidden">Loading...</span>
      </div>
    </div>
  {:else if selectedActivity === null}
    <!-- Activity grid -->
    <div class="text-center mb-5">
      <h1>Ravenest</h1>
      {#if $currentCharacter}
        <p class="text-muted">
          Playing as: {$currentCharacter.display_name}
          (Level {$currentCharacter.level})
        </p>
      {/if}
    </div>

    <div class="row g-4 justify-content-center">
      {#each activities as id}
        <div class="col-md-5 col-lg-4">
          <div
            class="card h-100 border-primary cursor-pointer"
            style="cursor: pointer;"
            role="button"
            tabindex="0"
            onclick={() => selectActivity(id)}
            onkeydown={(e) => { if (e.key === 'Enter') selectActivity(id); }}
          >
            <div class="card-body text-center p-5">
              <h3 class="card-title mb-3">{displayNames[id] || id}</h3>
              <span class="text-muted">{id === 'tasks' ? 'Campaigns and mini-games' : 'Coming Soon'}</span>
            </div>
          </div>
        </div>
      {/each}
    </div>
  {:else if selectedActivity === 'tasks'}
    <MiniGameSelect
      {onStartLevel}
      onBack={goBackToHub}
    />
  {:else if selectedActivity === 'manor'}
    <ManorMenu onBack={goBackToHub} />
  {:else if selectedActivity === 'chat'}
    <Chat onBack={goBackToHub} />
  {:else if selectedActivity === 'tournament'}
    <RoyalTournament onBack={goBackToHub} />
  {:else if selectedActivity === 'adventure'}
    <Adventure onBack={goBackToHub} />
  {:else}
    <div class="container py-5">
      <button class="btn btn-outline-secondary mb-4" onclick={goBackToHub}>
        &larr; Back
      </button>
      <div class="alert alert-warning">Unknown activity: {selectedActivity}</div>
    </div>
  {/if}
</div>
