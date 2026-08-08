<script lang="ts">
  /**
   * CombatGame — the realtime RTS battle view.
   *
   * Renders the match on a SimpleGame canvas using afterDraw (no engine
   * classes needed — units are server-simulated and drawn as sprites/shapes).
   * Receives 10 Hz match_state/match_update messages, maintains a local
   * entity store, and interpolates positions at requestAnimationFrame speed.
   *
   * Interaction (scaffold): click an own unit to select it, click anywhere
   * else to issue a move command. Attack/ability commands are wired through
   * CombatNetClient but have no UI yet.
   */
  import { onMount } from 'svelte';
  import {
    initEngine,
    setBoardSize,
    setCameraFollowsPlayer,
    everyTick,
    onMouseClick,
    afterDraw,
    whenLoaded,
    removeEventListeners,
    clear
  } from '../../SimpleGame/ui/src/lib/simplegame';
  import type { CombatNetClient } from './CombatNetClient';
  import type {
    combat_unit_dto,
    combat_state_payload,
    combat_player_dto,
    combat_ended_payload
  } from './protocol';

  interface Props {
    net: CombatNetClient;
    player_id: number;
    players: combat_player_dto[];
    /** Called by the parent with every match_state / match_update payload. */
    register_state_handler: (handler: (payload: combat_state_payload) => void) => void;
    on_phase: (phase: string, ended?: combat_ended_payload) => void;
    /** Reports the number of selected units (HUD display). */
    on_selection: (count: number) => void;
  }

  let { net, player_id, players, register_state_handler, on_phase, on_selection }: Props = $props();

  const BW = 960;
  const BH = 640;

  interface render_entity {
    data: combat_unit_dto;
    px: number;
    py: number;
    tx: number;
    ty: number;
  }

  let canvas_el: HTMLCanvasElement;
  let debug_el: HTMLDivElement;
  let entities = $state<Map<number, render_entity>>(new Map());
  let selected_ids = $state<number[]>([]);
  let phase = $state('lobby');
  let countdown = $state(-1);
  let battle_time = $state(0);
  let last_tick = $state(-1);
  let engine_ready = false;

  /** Looks up a player's team from the welcome player list. */
  function team_of_player(player_id: number): number {
    const player = players.find((p) => p.player_id === player_id);
    return player ? player.team : 0;
  }

  function team_of_unit(unit: combat_unit_dto): number {
    return team_of_player(unit.owner);
  }

  /**
   * Applies a full or partial state message to the local entity store.
   * Partial updates overwrite whole entity rows (idempotent); positions are
   * interpolated from the previous value toward the new one.
   *
   * @param payload - combat_state_payload from the server
   * @returns void
   */
  function apply_state(payload: combat_state_payload): void {
    phase = payload.phase;
    countdown = payload.countdown ?? -1;
    battle_time = payload.battle_time ?? 0;

    const next = new Map<number, render_entity>();
    for (const unit of payload.units) {
      const prev = entities.get(unit.id);
      if (prev) {
        next.set(unit.id, {
          data: unit,
          px: prev.px,
          py: prev.py,
          tx: unit.x,
          ty: unit.y
        });
      } else {
        next.set(unit.id, {
          data: unit,
          px: unit.x,
          py: unit.y,
          tx: unit.x,
          ty: unit.y
        });
      }
    }
    // Keep interpolation sources for units not in this update (partials only).
    if (!payload.full) {
      for (const [id, prev] of entities) {
        if (!next.has(id)) {
          next.set(id, prev);
        }
      }
    }
    for (const removed_id of payload.removed) {
      next.delete(removed_id);
      selected_ids = selected_ids.filter((id) => id !== removed_id);
    }
    entities = next;

    for (const event of payload.events) {
      if (event.type === 'system') {
        console.log('[combat] system event:', event.payload.message);
      }
    }

    if (payload.phase === 'battle' || payload.phase === 'countdown') {
      on_phase(payload.phase);
    }
  }

  /**
   * Finds the unit under a board-pixel position (selection hit test).
   *
   * @param x - Board pixel x
   * @param y - Board pixel y
   * @returns combat_unit_dto | null
   */
  function unit_at(x: number, y: number): combat_unit_dto | null {
    let best: combat_unit_dto | null = null;
    let best_dist = 24;
    for (const entity of entities.values()) {
      const dx = entity.px * BW - x;
      const dy = entity.py * BH - y;
      const dist = Math.hypot(dx, dy);
      if (dist < best_dist) {
        best_dist = dist;
        best = entity.data;
      }
    }
    return best;
  }

  /**
   * Interpolates entity positions toward their server targets each frame.
   *
   * @param dt - Delta time in seconds
   * @returns void
   */
  function interpolate(dt: number): void {
    const factor = Math.min(1, dt * 10);
    for (const entity of entities.values()) {
      entity.px += (entity.tx - entity.px) * factor;
      entity.py += (entity.ty - entity.py) * factor;
    }
  }

  /** Renders the battlefield: field, grid, units, hp bars, selection, HUD. */
  function draw(ctx: CanvasRenderingContext2D): void {
    // Field
    ctx.fillStyle = '#2c3e1f';
    ctx.fillRect(0, 0, BW, BH);
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    for (let i = 1; i < 16; i++) {
      ctx.beginPath();
      ctx.moveTo((i * BW) / 16, 0);
      ctx.lineTo((i * BW) / 16, BH);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(0, (i * BH) / 16);
      ctx.lineTo(BW, (i * BH) / 16);
      ctx.stroke();
    }

    // Units
    for (const entity of entities.values()) {
      const unit = entity.data;
      if (unit.status !== 'alive') continue;
      const x = entity.px * BW;
      const y = entity.py * BH;
      const team = team_of_unit(unit);
      const is_knight = unit.unit_class === 'knight';
      const radius = is_knight ? 18 : 13;

      ctx.save();
      ctx.fillStyle = team === 2 ? '#ff5f56' : '#4da3ff';
      ctx.beginPath();
      ctx.arc(x, y, radius, 0, Math.PI * 2);
      ctx.fill();
      if (selected_ids.includes(unit.id)) {
        ctx.strokeStyle = '#ffd94a';
        ctx.lineWidth = 3;
        ctx.stroke();
      } else {
        ctx.strokeStyle = 'rgba(0,0,0,0.6)';
        ctx.lineWidth = 1.5;
        ctx.stroke();
      }

      // HP bar
      const hp_frac = Math.max(0, Math.min(1, unit.hp / unit.max_hp));
      ctx.fillStyle = 'rgba(0,0,0,0.6)';
      ctx.fillRect(x - 16, y - radius - 12, 32, 5);
      ctx.fillStyle = hp_frac > 0.5 ? '#59d95c' : hp_frac > 0.25 ? '#ffd94a' : '#ff5f56';
      ctx.fillRect(x - 16, y - radius - 12, 32 * hp_frac, 5);
      ctx.restore();
    }

    // Countdown overlay
    if (phase === 'countdown' && countdown > 0) {
      ctx.save();
      ctx.fillStyle = 'rgba(0,0,0,0.45)';
      ctx.fillRect(0, 0, BW, BH);
      ctx.fillStyle = '#ffffff';
      ctx.font = 'bold 72px serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(String(Math.ceil(countdown)), BW / 2, BH / 2);
      ctx.restore();
    }
  }

  /** Sets up the engine after the canvas is mounted. */
  function setup_game(): void {
    setCameraFollowsPlayer(false);
    whenLoaded(() => {
      engine_ready = true;
    });
    // The engine's tick callback type omits the dt parameter — cast like
    // TowerDefense.svelte does (see docs/SimpleGame_changes.md).
    (everyTick as unknown as (fn: (dt: number) => void) => void)((dt: number) => {
      if (!engine_ready) return;
      interpolate(dt);
    });
    onMouseClick(0, (_e, x, y) => {
      if (x >= BW || y >= BH) return;
      const clicked = unit_at(x, y);
      if (clicked && clicked.owner === player_id) {
        if (selected_ids.includes(clicked.id)) {
          selected_ids = selected_ids.filter((id) => id !== clicked.id);
        } else {
          selected_ids = [...selected_ids, clicked.id];
        }
        return;
      }
      if (selected_ids.length > 0) {
        net.move_units(selected_ids, x / BW, y / BH);
        selected_ids = [];
      }
    });
    afterDraw((ctx: CanvasRenderingContext2D) => {
      draw(ctx);
    });
  }

  $effect(() => {
    if (canvas_el && !engine_ready) {
      setBoardSize(BW, BH);
      initEngine(canvas_el, debug_el, false, setup_game);
    }
  });

  onMount(() => {
    register_state_handler(apply_state);
  });

  $effect(() => {
    on_selection(selected_ids.length);
  });

  function handle_destroy(): void {
    try {
      removeEventListeners();
      clear();
    } catch {
      // Engine may already be torn down — ignore.
    }
  }
</script>

<canvas
  bind:this={canvas_el}
  width={BW}
  height={BH}
  class="d-block w-100 border border-secondary rounded"
  style="image-rendering: auto;"
></canvas>
<div bind:this={debug_el} class="d-none"></div>
