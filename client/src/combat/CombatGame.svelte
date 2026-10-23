<script lang="ts">
  /**
   * CombatGame — the realtime RTS battle view (rts_design.md §12).
   *
   * Renders the server-simulated match on a SimpleGame canvas via afterDraw
   * (units are server-authoritative; we only draw). Maintains a local entity
   * store with interpolation, a pending-damage ledger so damage numbers land
   * with the drawn projectile, and the §12 selection/camera/command toolkit.
   *
   * Input model (§12.1): LMB select / box-select, RMB context order (move /
   * attack / Lob), MMB or space-drag pan, wheel zoom, WASD/arrows, minimap
   * click-jump. Touch parity via a select/pan mode toggle.
   */
  import { onMount } from 'svelte';
  import {
    initEngine,
    setBoardSize,
    setViewportSize,
    setCameraFollowsPlayer,
    setBoardPanEnabled,
    setZoomEnabled,
    setCameraPosition,
    getZoomLevel,
    everyTick,
    afterDraw,
    removeEventListeners,
    clear
  } from '../../SimpleGame/ui/src/lib/simplegame';
  import { load_combat_configs } from './combat_configs';
  import { loadTexts } from '../lib/text';
  import type { combat_structure_def_dto, combat_formation_def_dto } from '../lib/api';
  import type { CombatNetClient } from './CombatNetClient';
  import type {
    combat_unit_dto,
    combat_structure_dto,
    combat_state_payload,
    combat_player_dto,
    combat_ended_payload,
    combat_welcome_payload,
    combat_appearance_dto
  } from './protocol';
  import CombatCommandBar from './CombatCommandBar.svelte';
  import {
    direction_index,
    get_unit_sprite,
    is_moving,
    preload_unit_sprites,
    resolve_body_frame,
    set_combatant_art
  } from './unit_sprites';

  interface Props {
    net: CombatNetClient;
    player_id: number;
    players: combat_player_dto[];
    welcome: combat_welcome_payload;
    register_state_handler: (handler: (payload: combat_state_payload) => void) => void;
    on_phase: (phase: string, ended?: combat_ended_payload) => void;
    on_selection: (count: number) => void;
  }

  let { net, player_id, players, welcome, register_state_handler, on_phase, on_selection }: Props =
    $props();

  const BW = 960;
  const BH = 640;
  const TILE = BW / 16;

  /**
   * Converts a normalized body radius (per-class `size`) to a drawn pixel
   * radius. The default size 0.03 maps to ~13 px, preserving the previous fixed
   * token look while letting each class scale (a troll reads far bigger than a
   * goblin). Clamped to a legible minimum.
   *
   * @param radius - Normalized body radius (unit.radius)
   * @returns Pixel radius for the body / ring / chevron / sprite box
   */
  function body_radius_px(radius: number | undefined): number {
    const normalized = radius && radius > 0 ? radius : 0.03;
    return Math.max(9, Math.round(normalized * BW * 0.45));
  }

  interface render_entity {
    data: combat_unit_dto;
    px: number;
    py: number;
    tx: number;
    ty: number;
    display_hp: number;
    /** Seconds accumulated in the walk cycle (0 while stationary). */
    anim_time: number;
  }

  interface floating_text {
    x: number;
    y: number;
    text: string;
    color: string;
    age: number;
    ttl: number;
    rise: number;
  }

  interface tracer {
    ax: number;
    ay: number;
    bx: number;
    by: number;
    age: number;
    ttl: number;
    color: string;
  }

  interface blast {
    x: number;
    y: number;
    radius: number;
    age: number;
    ttl: number;
    friendly_fire: boolean;
  }

  let canvas_el: HTMLCanvasElement;
  let debug_el: HTMLDivElement;
  let minimap_el: HTMLCanvasElement | undefined = $state();
  let engine_ready = false;

  let entities = $state<Map<number, render_entity>>(new Map());
  let structures = $state<Map<number, combat_structure_dto>>(new Map());
  let selected_ids = $state<number[]>([]);
  let control_groups = $state<Record<number, number[]>>({});
  let phase = $state('lobby');
  let countdown = $state(-1);
  let order_mode = $state('');
  let construct_type = $state('');
  let queue_enabled = $state(false);
  // Touch modality (§12.1): smart-tap issues orders directly; context-panel
  // only selects so the command bar drives the order.
  let touch_mode = $state<'smart' | 'context'>('smart');
  let edge_scroll_enabled = $state(false);
  let texts = $state<Record<string, string>>({});

  let structure_defs = $state<combat_structure_def_dto[]>([]);
  let formation_defs = $state<combat_formation_def_dto[]>([]);

  // Camera (board units) — refreshed from afterDraw each frame.
  let cam_x = 0;
  let cam_y = 0;

  let damage_numbers: floating_text[] = [];
  let tracers: tracer[] = [];
  let blasts: blast[] = [];
  let pending: { target: number; damage: number; delay: number; x: number; y: number; arc: string }[] = [];
  let hold_fire = new Map<number, { reason: string; ttl: number }>();
  let box = { active: false, x0: 0, y0: 0, x1: 0, y1: 0 };
  let pointer_start = { x: 0, y: 0, id: -1, button: 0, moved: false };
  let panning = false;
  let pointer_inside = false;
  let last_pointer = { x: 0, y: 0 };
  let hover_board = { x: -1, y: -1 };
  let long_press_timer: ReturnType<typeof setTimeout> | null = null;
  let keys = new Set<string>();
  let ctrl_down = false;

  /** Approximate AoE splash radius for the Lob preview (§6.2 tuning value). */
  const AOE_SPLASH_RADIUS = 0.09;

  let selected_units = $derived(
    selected_ids.map((id) => entities.get(id)?.data).filter((u): u is combat_unit_dto => !!u)
  );

  /** Class colors for procedural sprites (§12.5). Fallback when no art exists. */
  const CLASS_COLORS: Record<string, string> = {
    knight: '#ffd94a',
    spearman: '#8a8f98',
    archer: '#7f9a58',
    mage: '#b07fd0',
    alchemist: '#7fd0c0'
  };

  // ── Cosmetics (rts_design.md §15): appearance only, overlaid never replaced.
  // Per-member unit-slot overrides travel on the welcome roster rows; the
  // per-player slots (knight/uniform/structure/flourish) travel on `cosmetics`.
  let member_appearance = $derived.by((): Map<number, combat_appearance_dto> => {
    const map = new Map<number, combat_appearance_dto>();
    for (const m of welcome.retinue ?? []) {
      if (m.cosmetic && m.cosmetic.palette) map.set(m.member_id, m.cosmetic);
    }
    return map;
  });
  let squad_appearance = $derived(welcome.cosmetics?.squad_uniform ?? null);
  let knight_appearance = $derived(welcome.cosmetics?.knight ?? null);
  let structure_appearance = $derived(welcome.cosmetics?.structure ?? null);
  let flourish_appearance = $derived(welcome.cosmetics?.flourish ?? null);
  let banner_appearance = $derived(welcome.banner ?? null);

  /**
   * Resolves the cosmetic appearance for a unit (appearance only; the sim
   * never sees this). Enemy units never wear cosmetics.
   *
   * @param unit - The combat unit
   * @returns appearance descriptor | null
   */
  function appearance_for(unit: combat_unit_dto): combat_appearance_dto | null {
    if (unit.is_enemy) return null;
    if (unit.unit_class === 'knight' && knight_appearance) return knight_appearance;
    return member_appearance.get(unit.id) ?? squad_appearance ?? null;
  }

  /**
   * Returns a unit's body color (cosmetic palette first, else class color).
   *
   * @param unit - The combat unit
   * @returns CSS color string
   */
  function unit_color(unit: combat_unit_dto): string {
    if (unit.is_enemy) return '#ff5f56';
    const appearance = appearance_for(unit);
    if (appearance?.palette && appearance.palette.length > 0) return appearance.palette[0];
    return CLASS_COLORS[unit.unit_class] ?? '#4da3ff';
  }

  /**
   * Draws a cosmetic pattern overlay inside a unit's body circle.
   *
   * @param ctx - Canvas context (already translated to the unit)
   * @param appearance - The unit appearance, if any
   * @param radius - Unit body radius in board units
   * @returns void
   */
  function draw_pattern(
    ctx: CanvasRenderingContext2D,
    appearance: combat_appearance_dto | null,
    radius: number
  ): void {
    if (!appearance?.palette || appearance.palette.length < 2) return;
    const accent = appearance.palette[1];
    ctx.save();
    ctx.beginPath();
    ctx.arc(0, 0, radius, 0, Math.PI * 2);
    ctx.clip();
    ctx.fillStyle = accent;
    if (appearance.pattern === 'barry' || appearance.pattern === 'sable') {
      for (let y = -radius; y < radius; y += 6) ctx.fillRect(-radius, y, radius * 2, 2);
    } else if (appearance.pattern === 'quarterly') {
      ctx.fillRect(-radius, -radius, radius, radius);
      ctx.fillRect(0, 0, radius, radius);
    } else if (appearance.pattern === 'chevron') {
      ctx.beginPath();
      ctx.moveTo(-radius, radius * 0.2);
      ctx.lineTo(0, -radius * 0.4);
      ctx.lineTo(radius, radius * 0.2);
      ctx.lineTo(radius, radius);
      ctx.lineTo(-radius, radius);
      ctx.closePath();
      ctx.fill();
    }
    ctx.restore();
  }

  /**
   * Converts a screen (client) position to normalized board coordinates,
   * accounting for the camera and zoom.
   *
   * @param client_x - Client x (event.clientX)
   * @param client_y - Client y (event.clientY)
   * @returns Normalized 0..1 map coordinates
   */
  function to_board(client_x: number, client_y: number): { x: number; y: number } {
    const rect = canvas_el?.getBoundingClientRect();
    if (!rect || rect.width === 0) return { x: 0, y: 0 };
    const zoom = getZoomLevel() || 1;
    const view_w = BW / zoom;
    const view_h = BH / zoom;
    const bx = cam_x + ((client_x - rect.left) / rect.width) * view_w;
    const by = cam_y + ((client_y - rect.top) / rect.height) * view_h;
    return { x: Math.min(1, Math.max(0, bx / BW)), y: Math.min(1, Math.max(0, by / BH)) };
  }

  /**
   * Finds the topmost unit under a normalized point.
   *
   * @param nx - Normalized map x
   * @param ny - Normalized map y
   * @returns combat_unit_dto | null
   */
  function unit_at(nx: number, ny: number): combat_unit_dto | null {
    let best: combat_unit_dto | null = null;
    let best_dist = Infinity;
    for (const entity of entities.values()) {
      const dist = Math.hypot(entity.data.x - nx, entity.data.y - ny);
      // Pick within the unit's own body radius (min 0.03), so big units
      // (trolls) are as easy to click as they are to see.
      const pick_radius = Math.max(0.03, entity.data.radius ?? 0.03);
      if (dist <= pick_radius && dist < best_dist) {
        best_dist = dist;
        best = entity.data;
      }
    }
    return best;
  }

  /**
   * Reports the current selection count to the parent HUD.
   *
   * @returns void
   */
  function emit_selection(): void {
    on_selection(selected_ids.filter((id) => entities.has(id)).length);
  }

  /**
   * Applies a full or partial state message to the local entity store.
   *
   * @param payload - combat_state_payload from the server
   * @returns void
   */
  function apply_state(payload: combat_state_payload): void {
    phase = payload.phase;
    countdown = payload.countdown ?? -1;

    // Optional art (D24/D71): kick off loading for any class we can see. The
    // cache skips already-requested classes, so this is safe on every update.
    preload_unit_sprites(payload.units.map((unit) => unit.unit_class));

    const next = new Map<number, render_entity>();
    for (const unit of payload.units) {
      const prev = entities.get(unit.id);
      const display_hp = payload.full || !prev ? unit.hp : prev.display_hp;
      next.set(unit.id, {
        data: unit,
        px: prev ? prev.px : unit.x,
        py: prev ? prev.py : unit.y,
        tx: unit.x,
        ty: unit.y,
        display_hp,
        anim_time: prev ? prev.anim_time : 0
      });
    }
    if (!payload.full) {
      for (const [id, prev] of entities) if (!next.has(id)) next.set(id, prev);
    }
    for (const removed_id of payload.removed) {
      next.delete(removed_id);
      selected_ids = selected_ids.filter((id) => id !== removed_id);
    }
    entities = next;

    const next_structures = new Map<number, combat_structure_dto>();
    for (const s of payload.structures) next_structures.set(s.id, s);
    if (!payload.full) {
      for (const [id, prev] of structures) if (!next_structures.has(id)) next_structures.set(id, prev);
    }
    structures = next_structures;

    for (const event of payload.events) handle_event(event.type, event.payload);

    if (payload.phase === 'battle' || payload.phase === 'countdown') on_phase(payload.phase);
    emit_selection();
  }

  /**
   * Turns a server event into transient visual feedback (damage numbers,
   * tracers, blasts, hold-fire icons).
   *
   * @param type - Event type
   * @param p - Event payload
   * @returns void
   */
  function handle_event(type: string, p: Record<string, unknown>): void {
    if (type === 'attack') {
      if (p.aoe === true) {
        const cx = Number(p.center_x ?? 0.5);
        const cy = Number(p.center_y ?? 0.5);
        const radius = Number(p.radius ?? 0.09);
        blasts.push({ x: cx, y: cy, radius, age: 0, ttl: 0.6, friendly_fire: false });
        const hits = Array.isArray(p.hits) ? (p.hits as Record<string, unknown>[]) : [];
        for (const hit of hits) {
          const target = Number(hit.target ?? 0);
          const damage = Number(hit.damage ?? 0);
          if (hit.kind === 'unit') {
            const t = entities.get(target);
            if (t) damage_numbers.push({ x: t.data.x, y: t.data.y, text: `-${Math.round(damage)}`, color: '#ffb3a0', age: 0, ttl: 1.0, rise: 0.03 });
          }
        }
        if (p.attacker) hold_fire.delete(Number(p.attacker));
        return;
      }
      const attacker = Number(p.attacker ?? 0);
      const target = Number(p.target ?? 0);
      const damage = Number(p.damage ?? 0);
      const result = String(p.result ?? 'landed');
      const arc = String(p.arc ?? 'front');
      const attack_type = String(p.attack_type ?? 'melee');
      const t = entities.get(target);
      const a = entities.get(attacker);
      hold_fire.delete(attacker);
      if (!t) return;
      const color =
        flourish_appearance?.palette?.[0] ??
        (arc === 'back' ? '#ff8a5c' : arc === 'side' ? '#ffd94a' : '#ffffff');
      if (attack_type === 'melee' || result !== 'landed') {
        if (result === 'blocked') {
          // The arrow stopped at a blocking structure (§5.7): draw the tracer
          // to the wall/tower, not the target.
          const blocker = structures.get(Number(p.blocked_by ?? 0));
          if (a && blocker) {
            const dist = Math.hypot(blocker.x - a.data.x, blocker.y - a.data.y);
            const ttl = Math.min(0.35, 0.12 + dist * 0.25);
            tracers.push({ ax: a.data.x, ay: a.data.y, bx: blocker.x, by: blocker.y, age: 0, ttl, color: '#c0c8d0' });
          }
          damage_numbers.push({
            x: blocker?.x ?? t.data.x,
            y: blocker?.y ?? t.data.y,
            text: result,
            color: '#c0c8d0',
            age: 0,
            ttl: 1.0,
            rise: 0.03
          });
        } else {
          damage_numbers.push({ x: t.data.x, y: t.data.y, text: result === 'landed' ? `-${Math.round(damage)}` : result, color, age: 0, ttl: 1.0, rise: 0.03 });
          if (result === 'landed') t.display_hp = Math.max(0, t.display_hp - damage);
        }
      } else if (a) {
        // Ranged/magical: draw the arrow and land the damage when it arrives.
        const dist = Math.hypot(t.data.x - a.data.x, t.data.y - a.data.y);
        const ttl = Math.min(0.35, 0.12 + dist * 0.25);
        tracers.push({ ax: a.data.x, ay: a.data.y, bx: t.data.x, by: t.data.y, age: 0, ttl, color });
        pending.push({ target, damage, delay: ttl, x: t.data.x, y: t.data.y, arc });
      }
      return;
    }
    if (type === 'hold_fire') {
      hold_fire.set(Number(p.unit_id ?? 0), { reason: String(p.reason ?? ''), ttl: 2.0 });
      return;
    }
    if (type === 'structure_destroyed') {
      const id = Number(p.id ?? 0);
      const s = structures.get(id);
      if (s) blasts.push({ x: s.x, y: s.y, radius: 0.05, age: 0, ttl: 0.5, friendly_fire: true });
    }
  }

  /**
   * Advances interpolation and all transient effects.
   *
   * @param dt - Delta time in seconds
   * @returns void
   */
  function tick_effects(dt: number): void {
    const factor = Math.min(1, dt * 10);
    for (const entity of entities.values()) {
      const moving = is_moving(entity.px, entity.py, entity.tx, entity.ty);
      entity.px += (entity.tx - entity.px) * factor;
      entity.py += (entity.ty - entity.py) * factor;
      // Walk cycle: accumulate while moving, snap back to the rest pose when the
      // unit settles (so a stopped unit is never frozen mid-stride).
      entity.anim_time = moving ? entity.anim_time + dt : 0;
    }

    for (const tracer of tracers) tracer.age += dt;
    tracers = tracers.filter((t) => t.age < t.ttl);

    for (const p of pending) p.delay -= dt;
    const landed = pending.filter((p) => p.delay <= 0);
    pending = pending.filter((p) => p.delay > 0);
    for (const p of landed) {
      const t = entities.get(p.target);
      if (t) {
        t.display_hp = Math.max(0, t.display_hp - p.damage);
        const color = p.arc === 'back' ? '#ff8a5c' : p.arc === 'side' ? '#ffd94a' : '#ffffff';
        damage_numbers.push({ x: p.x, y: p.y, text: `-${Math.round(p.damage)}`, color, age: 0, ttl: 1.0, rise: 0.03 });
      }
    }

    for (const n of damage_numbers) n.age += dt;
    damage_numbers = damage_numbers.filter((n) => n.age < n.ttl);
    for (const b of blasts) b.age += dt;
    blasts = blasts.filter((b) => b.age < b.ttl);

    let hold_dirty = false;
    for (const [id, h] of hold_fire) {
      h.ttl -= dt;
      if (h.ttl <= 0) {
        hold_fire.delete(id);
        hold_dirty = true;
      }
    }
    if (hold_dirty) hold_fire = new Map(hold_fire);

    apply_camera_keys(dt);
    apply_edge_scroll(dt);
  }

  /**
   * Edge-scroll camera (§12.3): with the toggle on, holding the cursor near a
   * viewport edge pans the camera.
   *
   * @param dt - Delta time in seconds
   * @returns void
   */
  function apply_edge_scroll(dt: number): void {
    if (!edge_scroll_enabled || !pointer_inside) return;
    const rect = canvas_el?.getBoundingClientRect();
    if (!rect || rect.width === 0) return;
    const fx = (last_pointer.x - rect.left) / rect.width;
    const fy = (last_pointer.y - rect.top) / rect.height;
    if (fx < 0 || fx > 1 || fy < 0 || fy > 1) return;
    const margin = 0.04;
    const zoom = getZoomLevel() || 1;
    const speed = (BW / zoom) * 0.8 * dt;
    let dx = 0;
    let dy = 0;
    if (fx < margin) dx = -speed;
    else if (fx > 1 - margin) dx = speed;
    if (fy < margin) dy = -speed;
    else if (fy > 1 - margin) dy = speed;
    if (dx !== 0 || dy !== 0) {
      setCameraPosition(cam_x + BW / zoom / 2 + dx, cam_y + BH / zoom / 2 + dy);
    }
  }

  /**
   * Scrolls the camera from WASD/arrow keys.
   *
   * @param dt - Delta time in seconds
   * @returns void
   */
  function apply_camera_keys(dt: number): void {
    const speed = (BW / (getZoomLevel() || 1)) * 0.6 * dt;
    let dx = 0;
    let dy = 0;
    if (keys.has('a') || keys.has('arrowleft')) dx -= speed;
    if (keys.has('d') || keys.has('arrowright')) dx += speed;
    if (keys.has('w') || keys.has('arrowup')) dy -= speed;
    if (keys.has('s') || keys.has('arrowdown')) dy += speed;
    if (dx !== 0 || dy !== 0) {
      const zoom = getZoomLevel() || 1;
      setCameraPosition(cam_x + BW / zoom / 2 + dx, cam_y + BH / zoom / 2 + dy);
    }
  }

  /** Renders the battlefield. */
  function draw(ctx: CanvasRenderingContext2D, ox: number, oy: number): void {
    cam_x = ox;
    cam_y = oy;

    ctx.fillStyle = '#2c3e1f';
    ctx.fillRect(ox, oy, BW, BH);

    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    for (let i = 1; i < 16; i++) {
      ctx.beginPath();
      ctx.moveTo((i * BW) / 16, oy);
      ctx.lineTo((i * BW) / 16, oy + BH);
      ctx.stroke();
      ctx.beginPath();
      ctx.moveTo(ox, (i * BH) / 16);
      ctx.lineTo(ox + BW, (i * BH) / 16);
      ctx.stroke();
    }

    // Structures (§7): squares with HP/build bars.
    for (const s of structures.values()) {
      if (s.state === 'destroyed') continue;
      const x = s.x * BW - ox;
      const y = s.y * BH - oy;
      const half = TILE * 0.4;
      ctx.fillStyle =
        s.team === 2
          ? '#8a3b3b'
          : (structure_appearance?.palette?.[0] ?? (s.type === 'wall' ? '#7a7a7a' : '#6b4f2a'));
      ctx.fillRect(x - half, y - half, half * 2, half * 2);
      ctx.strokeStyle = 'rgba(0,0,0,0.7)';
      ctx.lineWidth = 1.5;
      ctx.strokeRect(x - half, y - half, half * 2, half * 2);
      const frac = Math.max(0, Math.min(1, s.hp / s.max_hp));
      ctx.fillStyle = 'rgba(0,0,0,0.6)';
      ctx.fillRect(x - half, y - half - 8, half * 2, 4);
      ctx.fillStyle = s.state === 'building' ? '#ffd94a' : '#59d95c';
      ctx.fillRect(x - half, y - half - 8, half * 2 * frac, 4);
      if (s.garrisoned_unit_id > 0) {
        ctx.fillStyle = '#4da3ff';
        ctx.beginPath();
        ctx.arc(x, y, 4, 0, Math.PI * 2);
        ctx.fill();
      }
    }

    // AoE blasts.
    for (const b of blasts) {
      const alpha = 1 - b.age / b.ttl;
      const f_line = b.friendly_fire ? '#ff5f56' : (flourish_appearance?.palette?.[0] ?? '#ffb450');
      ctx.strokeStyle = f_line;
      ctx.globalAlpha = alpha;
      ctx.fillStyle = f_line;
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.arc(b.x * BW - ox, b.y * BH - oy, b.radius * BW, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    }
    ctx.globalAlpha = 1;

    // Tracers (drawn before units so arrows pass under sprites).
    for (const t of tracers) {
      const frac = t.age / t.ttl;
      const x = (t.ax + (t.bx - t.ax) * frac) * BW - ox;
      const y = (t.ay + (t.by - t.ay) * frac) * BH - oy;
      ctx.fillStyle = t.color;
      ctx.beginPath();
      ctx.arc(x, y, 3, 0, Math.PI * 2);
      ctx.fill();
    }

    // Units.
    for (const entity of entities.values()) {
      const unit = entity.data;
      if (unit.status !== 'alive') continue;
      const x = entity.px * BW - ox;
      const y = entity.py * BH - oy;
      const radius = body_radius_px(unit.radius);

      // Class body: directional hand-authored art when present (D24/D71), else
      // the legacy rotated sprite, else the procedural class circle. Directional
      // frames are authored per compass view, so they draw unrotated (with an
      // optional horizontal mirror); the legacy flat sprite is rotated to face.
      const direction = direction_index(unit.facing);
      const animated = resolve_body_frame(
        unit.unit_class,
        direction,
        entity.anim_time > 0,
        entity.anim_time
      );
      const side = radius * 3;
      if (animated) {
        ctx.save();
        ctx.translate(x, y);
        if (animated.mirror) ctx.scale(-1, 1);
        ctx.drawImage(animated.image, -side / 2, -side / 2, side, side);
        ctx.restore();
      } else {
        const sprite = get_unit_sprite(unit.unit_class);
        if (sprite) {
          ctx.save();
          ctx.translate(x, y);
          ctx.rotate(unit.facing);
          ctx.drawImage(sprite, -side / 2, -side / 2, side, side);
          ctx.restore();
        } else {
          ctx.fillStyle = unit_color(unit);
          ctx.beginPath();
          ctx.arc(x, y, radius, 0, Math.PI * 2);
          ctx.fill();
        }
      }

      // Team ring (§15.5.2: always drawn, never replaced by cosmetics). Drawn
      // after the body so it stays visible when the sprite exceeds the radius.
      ctx.beginPath();
      ctx.arc(x, y, radius + 3, 0, Math.PI * 2);
      ctx.strokeStyle = unit.team === 2 ? '#ff5f56' : '#4da3ff';
      ctx.lineWidth = 2;
      ctx.stroke();

      // Cosmetic pattern overlay (appearance only; §15.5.2).
      ctx.save();
      ctx.translate(x, y);
      draw_pattern(ctx, appearance_for(unit), radius);
      ctx.restore();

      // Facing chevron. With directional art it snaps to the drawn sector so it
      // agrees with the sprite; otherwise it tracks the exact facing angle.
      ctx.save();
      ctx.translate(x, y);
      ctx.rotate(animated ? direction * (Math.PI / 4) : unit.facing);
      ctx.fillStyle = 'rgba(255,255,255,0.9)';
      ctx.beginPath();
      ctx.moveTo(radius - 2, 0);
      ctx.lineTo(radius - 8, -6);
      ctx.lineTo(radius - 8, 6);
      ctx.closePath();
      ctx.fill();
      ctx.restore();

      // Selection ring.
      if (selected_ids.includes(unit.id)) {
        ctx.strokeStyle = '#ffd94a';
        ctx.lineWidth = 3;
        ctx.beginPath();
        ctx.arc(x, y, radius + 5, 0, Math.PI * 2);
        ctx.stroke();
      }

      // HP bar (display ledger, not server truth).
      const hp_frac = Math.max(0, Math.min(1, entity.display_hp / unit.max_hp));
      ctx.fillStyle = 'rgba(0,0,0,0.6)';
      ctx.fillRect(x - 16, y - radius - 12, 32, 5);
      ctx.fillStyle = hp_frac > 0.5 ? '#59d95c' : hp_frac > 0.25 ? '#ffd94a' : '#ff5f56';
      ctx.fillRect(x - 16, y - radius - 12, 32 * hp_frac, 5);

      // Hold-fire reason icon.
      if (hold_fire.has(unit.id)) {
        ctx.fillStyle = '#ffd94a';
        ctx.font = 'bold 14px serif';
        ctx.fillText('!', x + radius, y - radius - 6);
      }

      // Name label for the player's selected units.
      if (selected_ids.includes(unit.id)) {
        ctx.fillStyle = 'rgba(255,255,255,0.85)';
        ctx.font = '11px sans-serif';
        ctx.textAlign = 'center';
        ctx.fillText(unit.name, x, y + radius + 14);
      }

      // Heraldic pennant above the player's knight (banner cosmetic, §15.2).
      if (unit.unit_class === 'knight' && unit.owner === player_id && banner_appearance) {
        const pal = banner_appearance.palette ?? ['#1b1b2b', '#4da3ff'];
        ctx.fillStyle = pal[0] ?? '#1b1b2b';
        ctx.fillRect(x - 3, y - radius - 30, 22, 14);
        ctx.fillStyle = pal[1] ?? '#4da3ff';
        ctx.fillRect(x - 3, y - radius - 30, 22, 5);
        ctx.strokeStyle = banner_appearance.trim ?? '#c9a227';
        ctx.lineWidth = 1;
        ctx.strokeRect(x - 3, y - radius - 30, 22, 14);
        ctx.fillStyle = '#8a8f98';
        ctx.fillRect(x - 4, y - radius - 31, 2, 17);
      }
    }

    // Damage numbers.
    for (const n of damage_numbers) {
      const frac = n.age / n.ttl;
      ctx.globalAlpha = 1 - frac;
      ctx.fillStyle = n.color;
      ctx.font = 'bold 15px sans-serif';
      ctx.textAlign = 'center';
      ctx.fillText(n.text, n.x * BW - ox, n.y * BH - oy - radiusOffset() - frac * 24);
      ctx.globalAlpha = 1;
    }

    // Risky-Lob preview (§6.3.3/§12.5): while armed, show the blast and glow
    // any friendly caught inside so the risk is legible.
    if (order_mode === 'lob' && hover_board.x >= 0 && hover_board.y >= 0) {
      const cx = hover_board.x * BW - ox;
      const cy = hover_board.y * BH - oy;
      const r = AOE_SPLASH_RADIUS * BW;
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, Math.PI * 2);
      ctx.fillStyle = 'rgba(255,140,40,0.18)';
      ctx.fill();
      ctx.strokeStyle = 'rgba(255,180,80,0.9)';
      ctx.lineWidth = 2;
      ctx.stroke();
      for (const entity of entities.values()) {
        if (entity.data.team !== 1) continue;
        if (Math.hypot(entity.data.x - hover_board.x, entity.data.y - hover_board.y) > AOE_SPLASH_RADIUS) continue;
        ctx.beginPath();
        ctx.arc(entity.data.x * BW - ox, entity.data.y * BH - oy, 18, 0, Math.PI * 2);
        ctx.strokeStyle = 'rgba(255,60,60,0.95)';
        ctx.lineWidth = 3;
        ctx.stroke();
      }
      for (const s of structures.values()) {
        if (s.team !== 1) continue;
        if (Math.hypot(s.x - hover_board.x, s.y - hover_board.y) > AOE_SPLASH_RADIUS) continue;
        ctx.strokeStyle = 'rgba(255,60,60,0.95)';
        ctx.lineWidth = 3;
        ctx.strokeRect(s.x * BW - ox - 22, s.y * BH - oy - 22, 44, 44);
      }
    }

    // Selection box.
    if (box.active) {
      const x0 = Math.min(box.x0, box.x1) * BW - ox;
      const y0 = Math.min(box.y0, box.y1) * BH - oy;
      const w = Math.abs(box.x1 - box.x0) * BW;
      const h = Math.abs(box.y1 - box.y0) * BH;
      ctx.strokeStyle = '#ffd94a';
      ctx.lineWidth = 1.5;
      ctx.strokeRect(x0, y0, w, h);
      ctx.fillStyle = 'rgba(255,217,74,0.12)';
      ctx.fillRect(x0, y0, w, h);
    }

    // Countdown overlay.
    if (phase === 'countdown' && countdown > 0) {
      ctx.fillStyle = 'rgba(0,0,0,0.45)';
      ctx.fillRect(ox, oy, BW, BH);
      ctx.fillStyle = '#ffffff';
      ctx.font = 'bold 72px serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(String(Math.ceil(countdown)), ox + BW / 2, oy + BH / 2);
      ctx.textBaseline = 'alphabetic';
    }
  }

  /** Small vertical offset so damage numbers clear the HP bar. */
  function radiusOffset(): number {
    return 22;
  }

  /** Draws the minimap: terrain dots, structures, viewport rect. */
  function draw_minimap(): void {
    const canvas = minimap_el;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    const w = canvas.width;
    const h = canvas.height;
    ctx.fillStyle = '#1c2414';
    ctx.fillRect(0, 0, w, h);
    for (const entity of entities.values()) {
      ctx.fillStyle = entity.data.team === 2 ? '#ff5f56' : '#4da3ff';
      ctx.fillRect(entity.data.x * w - 1, entity.data.y * h - 1, 3, 3);
    }
    for (const s of structures.values()) {
      ctx.fillStyle = '#9a9a9a';
      ctx.fillRect(s.x * w - 2, s.y * h - 2, 4, 4);
    }
    const zoom = getZoomLevel() || 1;
    ctx.strokeStyle = '#ffffff';
    ctx.lineWidth = 1;
    ctx.strokeRect((cam_x / BW) * w, (cam_y / BH) * h, (BW / zoom / BW) * w, (BH / zoom / BH) * h);
  }

  /**
   * Jumps the camera to a minimap click position.
   *
   * @param event - Pointer event on the minimap canvas
   * @returns void
   */
  function on_minimap_click(event: PointerEvent): void {
    const canvas = minimap_el;
    if (!canvas) return;
    const rect = canvas.getBoundingClientRect();
    const fx = (event.clientX - rect.left) / rect.width;
    const fy = (event.clientY - rect.top) / rect.height;
    setCameraPosition(fx * BW, fy * BH);
  }

  /**
   * Applies an order mode at a board point.
   *
   * @param mode - move | attack | lob | construct
   * @param nx - Normalized map x
   * @param ny - Normalized map y
   * @returns void
   */
  function apply_order(mode: string, nx: number, ny: number): void {
    if (selected_ids.length === 0) return;
    if (mode === 'move') {
      net.move_units(selected_ids, nx, ny, queue_enabled);
    } else if (mode === 'attack_move') {
      net.attack_move(selected_ids, nx, ny, queue_enabled);
    } else if (mode === 'patrol') {
      net.patrol(selected_ids, nx, ny, queue_enabled);
    } else if (mode === 'attack') {
      const target = unit_at(nx, ny);
      if (target && target.is_enemy) net.attack(selected_ids, target.id, queue_enabled);
      else net.attack_move(selected_ids, nx, ny, queue_enabled);
    } else if (mode === 'lob') {
      net.attack_ground(selected_ids, nx, ny, queue_enabled);
    } else if (mode === 'construct') {
      net.construct(selected_ids, construct_type || 'barricade', nx, ny);
    }
    order_mode = '';
  }

  /**
   * Left-click: selects, box-finalizes, or applies a staged order.
   *
   * @param nx - Normalized map x
   * @param ny - Normalized map y
   * @param additive - Shift held (add/toggle selection)
   * @returns void
   */
  function on_left_click(nx: number, ny: number, additive: boolean): void {
    if (order_mode) {
      apply_order(order_mode, nx, ny);
      return;
    }
    const hit = unit_at(nx, ny);
    if (hit && hit.owner === player_id) {
      if (additive) {
        selected_ids = selected_ids.includes(hit.id)
          ? selected_ids.filter((id) => id !== hit.id)
          : [...selected_ids, hit.id];
      } else {
        selected_ids = [hit.id];
      }
      return;
    }
    // Smart-tap (§12.1): tapping an enemy attacks; tapping empty ground moves
    // (or Lob for an alchemist selection). Context-panel mode only selects —
    // the command bar drives the order, so a bare ground tap deselects.
    if (touch_mode === 'smart' && selected_ids.length > 0) {
      if (hit && hit.is_enemy) {
        net.attack(selected_ids, hit.id, queue_enabled);
        return;
      }
      const has_aoe = selected_units.some((u) => u.unit_class === 'alchemist');
      if (has_aoe) net.attack_ground(selected_ids, nx, ny, queue_enabled);
      else net.move_units(selected_ids, nx, ny, queue_enabled);
      return;
    }
    if (!additive) selected_ids = [];
  }

  /**
   * Right-click context order: attack an enemy, else Lob for AoE units, else
   * move.
   *
   * @param nx - Normalized map x
   * @param ny - Normalized map y
   * @returns void
   */
  function on_right_click(nx: number, ny: number): void {
    if (selected_ids.length === 0) return;
    const hit = unit_at(nx, ny);
    if (hit && hit.is_enemy) {
      net.attack(selected_ids, hit.id, queue_enabled);
      return;
    }
    const has_aoe = selected_units.some((u) => u.unit_class === 'alchemist');
    if (has_aoe) net.attack_ground(selected_ids, nx, ny, queue_enabled);
    else net.move_units(selected_ids, nx, ny, queue_enabled);
    order_mode = '';
  }

  /**
   * Selects all of the player's units of the same class as the clicked unit
   * (double-click type select, §12.2).
   *
   * @param unit_class - The class to select
   * @returns void
   */
  function select_all_of_class(unit_class: string): void {
    const ids: number[] = [];
    for (const entity of entities.values()) {
      if (entity.data.owner === player_id && entity.data.unit_class === unit_class) {
        ids.push(entity.data.id);
      }
    }
    selected_ids = ids;
  }

  /**
   * Clears any pending long-press pan timer.
   *
   * @returns void
   */
  function clear_long_press(): void {
    if (long_press_timer !== null) {
      clearTimeout(long_press_timer);
      long_press_timer = null;
    }
  }

  /**
   * Pointer-down on the battle canvas: begins pan (MMB/RMB) or a select/box
   * gesture (LMB). Touch gets a long-press-drag pan (§12.1).
   *
   * @param event - Pointer event
   * @returns void
   */
  function on_pointer_down(event: PointerEvent): void {
    const p = to_board(event.clientX, event.clientY);
    pointer_start = { x: p.x, y: p.y, id: event.pointerId, button: event.button, moved: false };
    clear_long_press();
    if (event.button === 1 || event.button === 2) {
      panning = true;
      canvas_el?.setPointerCapture(event.pointerId);
      event.preventDefault();
      return;
    }
    if (event.button !== 0) return;
    box = { active: false, x0: p.x, y0: p.y, x1: p.x, y1: p.y };
    if (event.pointerType === 'touch') {
      long_press_timer = setTimeout(() => {
        panning = true;
        box = { active: false, x0: 0, y0: 0, x1: 0, y1: 0 };
        long_press_timer = null;
        try {
          canvas_el?.setPointerCapture(event.pointerId);
        } catch {
          // capture may fail if the pointer already lifted — ignore.
        }
      }, 350);
    }
  }

  /**
   * Pointer-move on the battle canvas: tracks the cursor (edge scroll / Lob
   * preview), pans the camera, or grows the selection box.
   *
   * @param event - Pointer event
   * @returns void
   */
  function on_pointer_move(event: PointerEvent): void {
    last_pointer = { x: event.clientX, y: event.clientY };
    pointer_inside = true;
    const p = to_board(event.clientX, event.clientY);
    hover_board = { x: p.x, y: p.y };
    if (pointer_start.id !== event.pointerId) return;
    if (Math.abs(p.x - pointer_start.x) > 0.004 || Math.abs(p.y - pointer_start.y) > 0.004) {
      pointer_start.moved = true;
      if (!panning) clear_long_press();   // a real drag cancels the long-press pan
    }
    if (panning) {
      const rect = canvas_el?.getBoundingClientRect();
      if (!rect) return;
      const zoom = getZoomLevel() || 1;
      const dx = ((event.movementX / rect.width) * (BW / zoom)) as number;
      const dy = ((event.movementY / rect.height) * (BH / zoom)) as number;
      setCameraPosition(cam_x + BW / zoom / 2 - dx, cam_y + BH / zoom / 2 - dy);
      return;
    }
    if (event.buttons & 1 && pointer_start.button === 0) {
      box = { active: pointer_start.moved, x0: pointer_start.x, y0: pointer_start.y, x1: p.x, y1: p.y };
    }
  }

  /**
   * Pointer-up on the battle canvas: finalizes pan, box select, or click.
   *
   * @param event - Pointer event
   * @returns void
   */
  function on_pointer_up(event: PointerEvent): void {
    if (pointer_start.id !== event.pointerId) return;
    clear_long_press();
    try {
      canvas_el?.releasePointerCapture(event.pointerId);
    } catch {
      // ignore
    }
    if (panning) {
      panning = false;
      pointer_start.id = -1;
      return;
    }
    if (box.active) {
      const minx = Math.min(box.x0, box.x1);
      const maxx = Math.max(box.x0, box.x1);
      const miny = Math.min(box.y0, box.y1);
      const maxy = Math.max(box.y0, box.y1);
      const ids: number[] = [];
      for (const entity of entities.values()) {
        const u = entity.data;
        if (u.owner === player_id && u.x >= minx && u.x <= maxx && u.y >= miny && u.y <= maxy) {
          ids.push(u.id);
        }
      }
      if (ids.length > 0) selected_ids = ids;
      box = { active: false, x0: 0, y0: 0, x1: 0, y1: 0 };
    } else {
      const p = to_board(event.clientX, event.clientY);
      on_left_click(p.x, p.y, event.shiftKey);
    }
    pointer_start.id = -1;
  }

  /**
   * Double-click: select all own units of the clicked unit's class.
   *
   * @param event - Pointer event
   * @returns void
   */
  function on_double_click(event: MouseEvent): void {
    const p = to_board(event.clientX, event.clientY);
    const hit = unit_at(p.x, p.y);
    if (hit && hit.owner === player_id) select_all_of_class(hit.unit_class);
  }

  /**
   * Right-click: prevent the browser menu and issue a context order.
   *
   * @param event - Mouse event
   * @returns void
   */
  function on_context_menu(event: MouseEvent): void {
    event.preventDefault();
    const p = to_board(event.clientX, event.clientY);
    on_right_click(p.x, p.y);
  }

  /**
   * Keydown handler: ctrl state, control groups, ESC, camera keys.
   *
   * @param event - Keyboard event
   * @returns void
   */
  function on_key_down(event: KeyboardEvent): void {
    ctrl_down = event.ctrlKey;
    const key = event.key.toLowerCase();
    keys.add(key);
    if (event.ctrlKey && key >= '1' && key <= '9') {
      control_groups = { ...control_groups, [Number(key)]: [...selected_ids] };
      event.preventDefault();
      return;
    }
    if (!event.ctrlKey && key >= '1' && key <= '9') {
      const group = control_groups[Number(key)];
      if (group) selected_ids = [...group];
      return;
    }
    if (key === 'escape') {
      order_mode = '';
      selected_ids = [];
    }
  }

  /**
   * Keyup handler: releases tracked camera keys.
   *
   * @param event - Keyboard event
   * @returns void
   */
  function on_key_up(event: KeyboardEvent): void {
    ctrl_down = event.ctrlKey;
    keys.delete(event.key.toLowerCase());
  }

  /**
   * Sets the staged order mode from the command bar.
   *
   * @param mode - move | attack | lob
   * @returns void
   */
  function set_order_mode(mode: string): void {
    order_mode = order_mode === mode ? '' : mode;
  }

  /** Applies the persistent formation modifier to the selected units. */
  function apply_formation(formationId: string): void {
    net.formation(formationId, 0);
  }

  /** Sets the construct type and stages construct mode. */
  function set_construct(structureType: string): void {
    construct_type = structureType;
    order_mode = 'construct';
  }

  /** Sets up the engine after the canvas mounts. */
  function setup_game(): void {
    setCameraFollowsPlayer(false);
    setBoardPanEnabled(false);
    setZoomEnabled(true, 0.6, 4.0, 0.15);
    setCameraPosition(BW / 2, BH / 2);
    // The engine's tick callback type omits the dt parameter — cast like
    // TowerDefense.svelte does (see docs/SimpleGame_changes.md).
    (everyTick as unknown as (fn: (dt: number) => void) => void)((dt: number) => {
      if (!engine_ready) return;
      tick_effects(dt);
    });
    afterDraw((ctx, ox, oy) => {
      draw(ctx, ox, oy);
      draw_minimap();
    });
    engine_ready = true;
  }

  $effect(() => {
    if (canvas_el && !engine_ready) {
      setBoardSize(BW, BH);
      setViewportSize(BW, BH);
      initEngine(canvas_el, debug_el, false, setup_game);
    }
  });

  $effect(() => {
    loadTexts(['combat_touch_mode_smart', 'combat_touch_mode_context', 'combat_edge_scroll']).then(
      (loaded) => {
        texts = loaded;
      }
    );
  });

  onMount(() => {
    register_state_handler(apply_state);
    void load_combat_configs()
      .then((configs) => {
        structure_defs = Object.values(configs.structures ?? {});
        formation_defs = Object.values(configs.formations ?? {});
        set_combatant_art(configs.combatant_art ?? {});
      })
      .catch(() => {
        structure_defs = [];
        formation_defs = [];
      });
    window.addEventListener('keydown', on_key_down);
    window.addEventListener('keyup', on_key_up);
    return () => {
      window.removeEventListener('keydown', on_key_down);
      window.removeEventListener('keyup', on_key_up);
      try {
        removeEventListeners();
        clear();
      } catch {
        // Engine may already be torn down — ignore.
      }
    };
  });

  $effect(() => {
    selected_ids;
    emit_selection();
  });
</script>

<div class="position-relative">
  <canvas
    bind:this={canvas_el}
    class="d-block w-100 border border-secondary rounded"
    style="touch-action: none;"
    onpointerdown={on_pointer_down}
    onpointermove={on_pointer_move}
    onpointerup={on_pointer_up}
    onpointerenter={() => (pointer_inside = true)}
    onpointerleave={() => {
      pointer_inside = false;
      hover_board = { x: -1, y: -1 };
    }}
    ondblclick={on_double_click}
    oncontextmenu={on_context_menu}
  ></canvas>
  <canvas
    bind:this={minimap_el}
    width="180"
    height="120"
    class="position-absolute end-0 bottom-0 m-2 border border-light rounded"
    style="touch-action: none; cursor: pointer; opacity: 0.9;"
    onpointerdown={on_minimap_click}
  ></canvas>
  <div class="position-absolute start-0 bottom-0 m-2 d-flex gap-1">
    <button
      class="btn btn-sm btn-outline-light"
      onclick={() => (touch_mode = touch_mode === 'smart' ? 'context' : 'smart')}
      title={touch_mode === 'smart'
        ? texts.combat_touch_mode_smart
        : texts.combat_touch_mode_context}
    >
      {touch_mode === 'smart' ? '☝' : '▤'}
    </button>
    <button
      class="btn btn-sm {edge_scroll_enabled ? 'btn-primary' : 'btn-outline-light'}"
      onclick={() => (edge_scroll_enabled = !edge_scroll_enabled)}
      title={texts.combat_edge_scroll}
    >
      ⇔
    </button>
  </div>
</div>

<CombatCommandBar
  selected={selected_units}
  structures={structure_defs}
  formations={formation_defs}
  {order_mode}
  {queue_enabled}
  on_set_order_mode={set_order_mode}
  on_stop={() => net.stop(selected_ids)}
  on_hold={() => net.hold(selected_ids)}
  on_stance={(s) => net.stance(selected_ids, s)}
  on_construct={set_construct}
  on_formation={apply_formation}
  on_toggle_queue={() => (queue_enabled = !queue_enabled)}
/>

<div bind:this={debug_el} class="d-none"></div>
