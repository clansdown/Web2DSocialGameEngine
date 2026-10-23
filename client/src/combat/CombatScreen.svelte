<script lang="ts">
  /**
   * CombatScreen — top-level container for the realtime combat game.
   *
   * Owns the WebSocket connection (CombatNetClient) and routes the design's
   * journey (rts_design.md §4): mission pick → roster → lobby → deploy →
   * countdown/battle → results. Combat is NOT a mini-game: it mounts from
   * App.svelte/HubScreen as its own activity.
   */
  import { onMount } from 'svelte';
  import { loadTexts } from '../lib/text';
  import { currentCharacter } from '../lib/stores';
  import { CombatNetClient, type combat_connection_status } from './CombatNetClient';
  import MissionPick from './MissionPick.svelte';
  import MatchLobby from './MatchLobby.svelte';
  import DeployView from './DeployView.svelte';
  import ResultsView from './ResultsView.svelte';
  import CombatGame from './CombatGame.svelte';
  import CombatHud from './CombatHud.svelte';
  import CombatChat from './CombatChat.svelte';
  import CombatVoice from './CombatVoice.svelte';
  import type {
    combat_welcome_payload,
    combat_state_payload,
    combat_chat_payload,
    combat_voice_payload,
    combat_ended_payload
  } from './protocol';

  interface Props {
    mode: 'pve' | 'pvp';
    on_close: () => void;
  }

  let { mode, on_close }: Props = $props();

  let net = $state<CombatNetClient | null>(null);
  let match_id = $state('');
  let phase = $state('browse');
  let connection_status = $state<combat_connection_status>('closed');
  let welcome = $state<combat_welcome_payload | null>(null);
  let ended = $state<combat_ended_payload | null>(null);
  let error = $state('');
  let battle_time = $state(0);
  let deploy_time = $state(0);
  let unit_counts = $state<Map<number, number>>(new Map());
  let selected_count = $state(0);
  let chat_open = $state(false);
  let voice_active = $state(false);
  let texts = $state<Record<string, string>>({});

  let state_handler: ((payload: combat_state_payload) => void) | null = null;
  let chat_handler: ((payload: combat_chat_payload) => void) | null = null;
  let voice_handler: ((payload: combat_voice_payload) => void) | null = null;

  let my_player_id = $derived(welcome?.player_id ?? 0);
  let my_team = $derived(welcome?.team ?? 1);

  let teammates = $derived(
    (welcome?.players ?? [])
      .filter((p) => p.player_id !== my_player_id && p.team === my_team)
      .map((p) => p.player_id)
  );

  /** member_id → display name for results rows. */
  let member_names = $derived.by((): Record<number, string> => {
    const names: Record<number, string> = {};
    for (const m of welcome?.retinue ?? []) names[m.member_id] = m.display_name;
    return names;
  });

  $effect(() => {
    loadTexts([
      'combat_pvp_coming_soon',
      'combat_connecting'
    ]).then((loaded) => {
      texts = loaded;
    });
  });

  /**
   * Establishes the WebSocket connection to an existing match and enters the
   * lobby phase.
   *
   * @param matchId - Match id returned by combatCreate/combatJoin
   * @returns void
   */
  function connect(matchId: string): void {
    match_id = matchId;
    phase = 'lobby';
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
      return;
    }
    if (type === 'match_state' || type === 'match_update') {
      const state = payload as combat_state_payload;
      phase = state.phase;
      battle_time = state.battle_time ?? 0;
      deploy_time = state.deploy_time ?? 0;
      // Track alive unit counts per team for the HUD.
      const counts = new Map<number, number>();
      for (const unit of state.units) {
        if (unit.status !== 'alive') continue;
        counts.set(unit.team, (counts.get(unit.team) ?? 0) + 1);
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

  /** Toggles the team chat panel. */
  function toggle_chat(): void {
    chat_open = !chat_open;
  }

  /** Toggles voice chat (CombatVoice manages its own media). */
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
</script>

{#if mode === 'pvp'}
  <div class="container py-5 text-center">
    <div class="alert alert-info">{texts.combat_pvp_coming_soon ?? ''}</div>
    <button class="btn btn-outline-secondary" onclick={on_close}>Back</button>
  </div>
{:else if !match_id}
  <MissionPick on_joined={connect} on_back={on_close} />
{:else if !welcome}
  <div class="container py-5 text-center">
    <div class="spinner-border" role="status">
      <span class="visually-hidden">{texts.combat_connecting ?? ''}</span>
    </div>
    <div class="mt-3 text-muted">{connection_status}</div>
    {#if error}
      <div class="alert alert-danger mt-3">{error}</div>
    {/if}
  </div>
{:else if phase === 'lobby' || phase === ''}
  <MatchLobby net={net!} welcome={welcome} my_player_id={my_player_id} on_leave={leave_match} />
{:else if phase === 'deploy'}
  <DeployView
    net={net!}
    welcome={welcome}
    my_player_id={my_player_id}
    my_team={my_team}
    deploy_time={deploy_time}
    on_leave={leave_match}
  />
{:else if phase === 'ended'}
  {#if ended}
    <ResultsView ended={ended} my_team={my_team} member_names={member_names} banner={welcome.banner} on_back={leave_match} />
  {/if}
{:else}
  <div class="container-fluid py-3">
    <CombatHud
      {phase}
      {battle_time}
      unit_counts={unit_counts}
      players={welcome.players}
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
          players={welcome.players}
          {welcome}
          register_state_handler={(handler) => {
            state_handler = handler;
          }}
          on_phase={(p) => {
            if (p === 'countdown' || p === 'battle') phase = p;
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
