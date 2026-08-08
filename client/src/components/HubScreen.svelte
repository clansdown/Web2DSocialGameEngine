<script lang="ts">
  import { playerGameState, currentCharacter } from '../lib/stores';
  import { loadTexts } from '../lib/text';
  import { route_store, navigate, go_back } from '../lib/router';
  import MiniGameSelect from '../minigames/MiniGameSelect.svelte';
  import ManorMenu from './ManorMenu.svelte';
  import Chat from './Chat.svelte';
  import RoyalTournament from './RoyalTournament.svelte';
  import Adventure from './Adventure.svelte';
  import LandPatentPanel from './LandPatentPanel.svelte';

  interface Props {
    activities?: string[];
    onStartLevel: (gameId: string, levelId: number) => void;
    onJoinBarony?: () => void;
    onBaronTrackStarted?: () => void;
  }

  let { activities = [], onStartLevel, onJoinBarony, onBaronTrackStarted }: Props = $props();

  let displayNames = $state<Record<string, string>>({});

  let gridActivities = $derived(activities.filter(id => id !== 'land_patent'));

  // The open activity comes from the hash route (#/activity/<id>). A route
  // whose id is not available in the current phase falls back to the hub grid.
  let activity = $derived(
    $route_store.type === 'activity' && gridActivities.includes($route_store.id)
      ? $route_store.id
      : null
  );

  // Nested route segments after the activity id (e.g. ['thread','42'] for
  // #/activity/chat/thread/42) — passed to panels for sub-navigation.
  let activityRest = $derived($route_store.type === 'activity' ? $route_store.rest : []);

  /**
   * Loads display names for all available activities from the text system.
   */
  async function loadActivityNames() {
    const keys = gridActivities.map(id => `activity_${id}`);
    try {
      const texts = await loadTexts(keys);
      const names: Record<string, string> = {};
      for (const id of gridActivities) {
        names[id] = texts[`activity_${id}`] || id.charAt(0).toUpperCase() + id.slice(1);
      }
      displayNames = names;
    } catch {
      // Fallback to capitalized IDs
      const names: Record<string, string> = {};
      for (const id of gridActivities) {
        names[id] = id.charAt(0).toUpperCase() + id.slice(1);
      }
      displayNames = names;
    }
  }

  function selectActivity(id: string) {
    navigate({ type: 'activity', id, rest: [] });
  }

  function goBackToHub() {
    go_back();
  }

  $effect(() => {
    if (gridActivities.length > 0) {
      loadActivityNames();
    }
  });
</script>

{#if activity === null}
  <div class="container py-4">
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
      {#if activities.includes('land_patent') && onJoinBarony && onBaronTrackStarted}
        <div class="col-12">
          <LandPatentPanel
            onJoinBarony={onJoinBarony}
            onBaronTrackStarted={onBaronTrackStarted}
          />
        </div>
      {/if}
      {#each gridActivities as id}
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
  </div>
{:else if activity === 'tasks'}
  <div class="container py-4">
    <MiniGameSelect
      {onStartLevel}
      onBack={goBackToHub}
    />
  </div>
{:else if activity === 'manor'}
  <!-- Full-bleed (no container) so the manor canvas fills the viewport -->
  <ManorMenu onBack={goBackToHub} />
{:else if activity === 'chat'}
  <div class="container py-4">
    <Chat onBack={goBackToHub} rest={activityRest} />
  </div>
{:else if activity === 'tournament'}
  <div class="container py-4">
    <RoyalTournament onBack={goBackToHub} />
  </div>
{:else if activity === 'adventure'}
  <div class="container py-4">
    <Adventure onBack={goBackToHub} />
  </div>
{:else}
  <div class="container py-4">
    <button class="btn btn-outline-secondary mb-4" onclick={goBackToHub}>
      &larr; Back
    </button>
    <div class="alert alert-warning">Unknown activity: {activity}</div>
  </div>
{/if}
