<script lang="ts">
  /**
   * CombatScreen — top-level container for the realtime combat game.
   *
   * Owns the WebSocket connection (CombatNetClient), dispatches inbound
   * envelopes to the active views, and routes the phase machine:
   *   lobby (MatchLobby + ready) → battle (CombatGame + CombatHud + chat/voice)
   *   → results.
   *
   * This is NOT a mini-game: it mounts from App.svelte as its own activity.
   */
  import { onMount } from 'svelte';
  import { loadTexts } from '../lib/text';
  import { currentCharacter } from '../lib/stores';
  import { CombatNetClient, type combat_connection_status } from './CombatNetClient';
  import MatchLobby from './MatchLobby.svelte';
  import CombatGame from './CombatGame.svelte';
  import CombatHud from './CombatHud.svelte';
  import CombatChat from './CombatChat.svelte';
  import CombatVoice from './CombatVoice.svelte';
  import type {
    combat_welcome_payload,
    combat_state_payload,
    combat_chat_payload,
    combat_voice_payload,
    combat_ended_payload,
    combat_player_dto
  } from './protocol';

  interface Props {
    mode: 'pve' | 'pvp';
    on_close: () => void;
  }

  let { mode, on_close }: Props = $props();

  let net = $state<CombatNetClient | null>(null);
  let match_id = $state('');
  let phase = $state('lobby');
  let connection_status = $state<combat_connection_status>('closed');
  let welcome = $state<combat_welcome_payload | null>(null);
  let players = $state<combat_player_dto[]>([]);
  let my_player_id = $state(0);
  let my_team = $state(1);
  let ended = $state<combat_ended_payload | null>(null);
  let error = $state('');
  let battle_time = $state(0);
  let unit_counts = $state<Map<number, number>>(new Map());
  let selected_count = $state(0);
  let chat_open = $state(false);
  let voice_active = $state(false);
  let texts = $state<Record<string, string>>({});

  let state_handler: ((payload: combat_state_payload) => void) | null = null;
  let chat_handler: ((payload: combat_chat_payload) => void) | null = null;
  let voice_handler: ((payload: combat_voice_payload) => void) | null = null;

  let teammates = $derived(
    players.filter((p) => p.player_id !== my_player_id && p.team === my_team).map((p) => p.player_id)
  );

  $effect(() => {
    loadTexts([
      'combat_pvp_coming_soon',
      'combat_lobby_waiting',
      'combat_ready_btn',
      'combat_results_title',
      'combat_results_victory',
      'combat_results_defeat',
      'combat_results_draw',
      'combat_results_back_btn',
      'combat_connecting'
    ]).then((loaded) => {
      texts = loaded;
    });
  });

  /**
   * Establishes the WebSocket connection to an existing match.
   *
   * @param matchId - Match id returned by combatCreate/combatJoin
   * @returns void
   */
  function connect(matchId: string): void {
    match_id = matchId;
    if (!$currentCharacter) return;
    net = new CombatNetClient($currentCharacter.id, matchId, {
      on_status: (status) => {
        connection_status = status;
      },
      on_error: (message) => {
        error = message;
      },
      on_message: (envelope) => {
        dispatch(envelope.type, envelope.payload);
      }
    });
    net.connect();
  }

  /**
   * Dispatches an inbound envelope to the appropriate view.
   *
   * @param type - Message type from the server
   * @param payload - Raw payload
   * @returns void
   */
  function dispatch(type: string, payload: unknown): void {
    if (type === 'welcome') {
      welcome = payload as combat_welcome_payload;
      players = welcome.players;
      my_player_id = welcome.player_id;
      my_team = welcome.team;
      return;
    }
    if (type === 'match_state' || type === 'match_update') {
      const state = payload as combat_state_payload;
      phase = state.phase;
      battle_time = state.battle_time ?? 0;
      // Track unit counts for the HUD (selected count comes from CombatGame).
      const counts = new Map<number, number>();
      for (const unit of state.units) {
        if (unit.status !== 'alive') continue;
        const owner = players.find((p) => p.player_id === unit.owner);
        const team = owner ? owner.team : 0;
        counts.set(team, (counts.get(team) ?? 0) + 1);
      }
      unit_counts = counts;
      state_handler?.(state);
      return;
    }
    if (type === 'chat') {
      chat_handler?.(payload as combat_chat_payload);
      return;
    }
    if (type === 'voice') {
      voice_handler?.(payload as combat_voice_payload);
      return;
    }
    if (type === 'match_ended') {
      ended = payload as combat_ended_payload;
      phase = 'ended';
      return;
    }
    if (type === 'error') {
      const p = payload as { error?: string };
      error = p.error ?? 'combat error';
    }
  }

  /**
   * Signals readiness; the match countdown starts when all players are ready.
   *
   * @param none
   * @returns void
   */
  function mark_ready(): void {
    net?.ready();
  }

  /**
   * Leaves the match and returns to the hub.
   *
   * @param none
   * @returns void
   */
  function leave_match(): void {
    if (net) {
      net.leave();
      net = null;
    }
    on_close();
  }

  /**
   * Toggles the team chat panel.
   *
   * @param none
   * @returns void
   */
  function toggle_chat(): void {
    chat_open = !chat_open;
  }

  /**
   * Toggles voice chat (CombatVoice manages its own media).
   *
   * @param none
   * @returns void
   */
  function toggle_voice(): void {
    voice_active = !voice_active;
  }

  onMount(() => {
    return () => {
      if (net) {
        net.close();
        net = null;
      }
    };
  });

  let result_title = $derived(() => {
    if (!ended) return '';
    if (ended.winner_team === -1 || ended.winner_team === 0) {
      return texts.combat_results_draw;
    }
    return ended.winner_team === my_team
      ? texts.combat_results_victory
      : texts.combat_results_defeat;
  });
</script>

{#if mode === 'pvp'}
  <div class="container py-5 text-center">
    <div class="alert alert-info">{texts.combat_pvp_coming_soon}</div>
    <button class="btn btn-outline-secondary" onclick={on_close}>Back</button>
  </div>
{:else if !match_id}
  <MatchLobby
    on_joined={connect}
    on_back={on_close}
  />
{:else if !welcome}
  <div class="container py-5 text-center">
    <div class="spinner-border" role="status">
      <span class="visually-hidden">{texts.combat_connecting}</span>
    </div>
    <div class="mt-3 text-muted">{connection_status}</div>
    {#if error}
      <div class="alert alert-danger mt-3">{error}</div>
    {/if}
  </div>
{:else if phase === 'lobby' || phase === ''}
  <div class="container py-4">
    <h2 class="mb-3">{welcome.match_code}</h2>
    <p class="text-muted">{texts.combat_lobby_waiting}</p>
    <ul class="list-group mb-3">
      {#each players as player}
        <li class="list-group-item d-flex justify-content-between">
          <span>
            {player.display_name}
            {#if player.player_id === my_player_id}<span class="badge text-bg-primary ms-2">You</span>{/if}
          </span>
          <span class="badge {player.ready ? 'text-bg-success' : 'text-bg-secondary'}">
            {player.ready ? 'Ready' : 'Not ready'}
          </span>
        </li>
      {/each}
    </ul>
    <button class="btn btn-primary" onclick={mark_ready}>
      {texts.combat_ready_btn}
    </button>
    <button class="btn btn-outline-secondary ms-2" onclick={leave_match}>
      {texts.combat_results_back_btn}
    </button>
    {#if error}
      <div class="alert alert-danger mt-3">{error}</div>
    {/if}
  </div>
{:else if phase === 'ended'}
  <div class="container py-5 text-center">
    <h2>{texts.combat_results_title}</h2>
    <div class="display-5 my-4">{result_title}</div>
    {#if ended}
      <p class="text-muted">
        {ended.casualties.length} casualties
        {#if ended.reason}({ended.reason}){/if}
      </p>
    {/if}
    <button class="btn btn-primary" onclick={leave_match}>
      {texts.combat_results_back_btn}
    </button>
  </div>
{:else}
  <div class="container-fluid py-3">
    <CombatHud
      {phase}
      {battle_time}
      unit_counts={unit_counts}
      {players}
      {connection_status}
      team={my_team}
      {selected_count}
      on_toggle_chat={toggle_chat}
      on_toggle_voice={toggle_voice}
      on_leave={leave_match}
      {chat_open}
      {voice_active}
    />
    {#if error}
      <div class="alert alert-danger my-2">{error}</div>
    {/if}
    <div class="row g-3 mt-1">
      <div class="col-12 col-lg-8">
        <CombatGame
          net={net!}
          player_id={my_player_id}
          {players}
          register_state_handler={(handler) => {
            state_handler = handler;
          }}
          on_phase={(p) => {
            if (p === 'countdown' || p === 'battle') {
              phase = p;
            }
          }}
          on_selection={(count) => {
            selected_count = count;
          }}
        />
      </div>
      <div class="col-12 col-lg-4">
        {#if chat_open}
          <CombatChat
            net={net!}
            register_chat_handler={(handler) => {
              chat_handler = handler;
            }}
            on_close={() => {
              chat_open = false;
            }}
          />
        {/if}
        {#if voice_active}
          <CombatVoice
            net={net!}
            {teammates}
            register_voice_handler={(handler) => {
              voice_handler = handler;
            }}
          />
        {/if}
      </div>
    </div>
  </div>
{/if}
