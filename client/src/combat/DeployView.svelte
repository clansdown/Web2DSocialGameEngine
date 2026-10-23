<script lang="ts">
  /**
   * DeployView — the deploy phase (rts_design.md §4.4): drag squad members
   * inside the team deploy zone, rotate facing, then press Ready. The server
   * auto-deploys on the phase timer if a player never readies.
   *
   * This is a setup screen, not the battle simulation — it uses a plain canvas
   * (the real-time battle uses SimpleGame in CombatGame.svelte) so placement is
   * fully under our control without engine coupling.
   */
  import { onMount } from 'svelte';
  import { loadTexts } from '../lib/text';
  import type { CombatNetClient } from './CombatNetClient';
  import type { combat_welcome_payload } from './protocol';
  import CombatBanner from './CombatBanner.svelte';
  import {
    direction_index,
    get_unit_sprite,
    preload_unit_sprites,
    resolve_body_frame,
    set_combatant_art
  } from './unit_sprites';
  import { load_combat_configs } from './combat_configs';

  interface deploy_zone {
    team: number;
    x: number;
    y: number;
    w: number;
    h: number;
  }

  interface map_metadata {
    width_tiles?: number;
    height_tiles?: number;
    tile_costs?: number[];
    deploy_zones?: deploy_zone[];
    spawn_points?: { id?: string; x: number; y: number; team: number }[];
    obstacles?: {
      id?: string;
      x: number;
      y: number;
      radius?: number;
      blocks_movement?: boolean;
    }[];
  }

  interface placement {
    x: number;
    y: number;
    facing: number;
  }

  interface Props {
    net: CombatNetClient;
    welcome: combat_welcome_payload;
    my_player_id: number;
    my_team: number;
    deploy_time: number;
    on_leave: () => void;
  }

  let { net, welcome, my_player_id, my_team, deploy_time, on_leave }: Props = $props();

  const BW = 960;
  const BH = 640;

  // Per-class body radius (normalized `size`) from the combat configs, used to
  // scale each token/sprite so bigger classes read bigger — independent of the
  // source image dimensions.
  let combatant_radius = $state<Record<string, number>>({});

  /**
   * Drawn token radius (px) for a unit class, from its normalized body radius.
   *
   * @param unit_class - Unit class id
   * @returns Pixel radius for the token/sprite box
   */
  function token_radius_px(unit_class: string): number {
    const radius = combatant_radius[unit_class] ?? 0.03;
    return Math.max(10, Math.round(radius * BW * 0.45));
  }

  let canvas_el: HTMLCanvasElement | undefined = $state();
  let map = $derived((welcome.map ?? {}) as map_metadata);
  let texts = $state<Record<string, string>>({});

  // members = the fielded (roster-filtered) squad from the welcome snapshot.
  let members = $derived(welcome.retinue);

  let placements = $state<Record<number, placement>>({});
  let selected_id = $state<number | null>(null);
  let dragging = $state(false);
  let placed_once = false;

  let zone = $derived.by((): deploy_zone => {
    const zones = map.deploy_zones ?? [];
    return (
      zones.find((z) => z.team === my_team) ?? { team: my_team, x: 0.03, y: 0.35, w: 0.2, h: 0.3 }
    );
  });

  /** Center of the opposing camp — the default facing target. */
  let enemy_center = $derived.by((): { x: number; y: number } => {
    const enemy_team = my_team === 1 ? 2 : 1;
    const zones = map.deploy_zones ?? [];
    const z = zones.find((zz) => zz.team === enemy_team);
    if (z) return { x: z.x + z.w / 2, y: z.y + z.h / 2 };
    const sp = (map.spawn_points ?? []).find((s) => s.team === enemy_team);
    if (sp) return { x: sp.x, y: sp.y };
    return { x: my_team === 1 ? 0.875 : 0.125, y: 0.5 };
  });

  $effect(() => {
    loadTexts([
      'combat_deploy_title',
      'combat_deploy_hint',
      'combat_deploy_ready_btn',
      'combat_deploy_rotate_hint',
      'combat_deploy_face_enemy_btn',
      'combat_deploy_timer_label',
      'combat_results_back_btn'
    ]).then((loaded) => {
      texts = loaded;
    });
  });

  /**
   * Builds the default spread of squad members inside the deploy zone, facing
   * the enemy camp. Runs once when the component mounts.
   *
   * @returns void
   */
  function init_placements(): void {
    if (placed_once) return;
    placed_once = true;
    const z = zone;
    const rows = Math.max(1, Math.ceil(members.length / 2));
    const next: Record<number, placement> = {};
    members.forEach((m, i) => {
      const col = i % 2;
      const row = Math.floor(i / 2);
      const x = z.x + z.w * (col === 0 ? 0.3 : 0.7);
      const y = z.y + (z.h * (row + 0.5)) / rows;
      const facing = Math.atan2(enemy_center.y - y, enemy_center.x - x);
      next[m.member_id] = { x, y, facing };
    });
    placements = next;
  }

  /**
   * Redraws for a short burst after art metadata arrives, so asynchronously
   * decoded directional frames appear without requiring further interaction.
   *
   * @returns void
   */
  function redraw_while_frames_load(): void {
    let frames_left = 90; // ~1.5s at 60fps
    const step = (): void => {
      draw();
      if (frames_left-- > 0) requestAnimationFrame(step);
    };
    requestAnimationFrame(step);
  }

  onMount(() => {
    preload_unit_sprites(members.map((m) => m.unit_class));
    void load_combat_configs()
      .then((configs) => {
        set_combatant_art(configs.combatant_art ?? {});
        combatant_radius = configs.combatant_radius ?? {};
        redraw_while_frames_load();
      })
      .catch(() => {});
    init_placements();
    draw();
  });

  /**
   * Converts a canvas pixel position to normalized map coordinates.
   *
   * @param canvas_x - Canvas-relative x
   * @param canvas_y - Canvas-relative y
   * @returns {x, y} normalized
   */
  function to_normalized(canvas_x: number, canvas_y: number): { x: number; y: number } {
    const rect = canvas_el?.getBoundingClientRect();
    const scale_x = rect ? BW / rect.width : 1;
    const scale_y = rect ? BH / rect.height : 1;
    return { x: (canvas_x * scale_x) / BW, y: (canvas_y * scale_y) / BH };
  }

  /**
   * Hit-tests a normalized point against placed squad tokens.
   *
   * @param nx - Normalized map x
   * @param ny - Normalized map y
   * @returns member id | null
   */
  function token_at(nx: number, ny: number): number | null {
    const px = nx * BW;
    const py = ny * BH;
    for (const m of members) {
      const p = placements[m.member_id];
      if (!p) continue;
      if (Math.hypot(p.x * BW - px, p.y * BH - py) <= token_radius_px(m.unit_class) + 6) {
        return m.member_id;
      }
    }
    return null;
  }

  /**
   * Clamps a placement to the deploy zone bounds.
   *
   * @param p - The candidate placement
   * @returns A placement inside the zone
   */
  function clamp_to_zone(p: placement): placement {
    const z = zone;
    return {
      x: Math.min(Math.max(p.x, z.x + 0.01), z.x + z.w - 0.01),
      y: Math.min(Math.max(p.y, z.y + 0.01), z.y + z.h - 0.01),
      facing: p.facing
    };
  }

  /**
   * Handles pointer-down: selects and starts dragging a squad token.
   *
   * @param event - Pointer event on the canvas
   * @returns void
   */
  function on_pointer_down(event: PointerEvent): void {
    const local = to_normalized(event.offsetX, event.offsetY);
    const hit = token_at(local.x, local.y);
    selected_id = hit;
    if (hit !== null) {
      dragging = true;
      canvas_el?.setPointerCapture(event.pointerId);
    }
    draw();
  }

  /**
   * Handles pointer-move: moves the dragged token inside the zone.
   *
   * @param event - Pointer event on the canvas
   * @returns void
   */
  function on_pointer_move(event: PointerEvent): void {
    if (!dragging || selected_id === null) return;
    const local = to_normalized(event.offsetX, event.offsetY);
    const current = placements[selected_id];
    if (!current) return;
    placements[selected_id] = clamp_to_zone({ ...current, x: local.x, y: local.y });
    draw();
  }

  /**
   * Handles pointer-up: stops dragging and reports the placement to the server.
   *
   * @param event - Pointer event on the canvas
   * @returns void
   */
  function on_pointer_up(event: PointerEvent): void {
    dragging = false;
    if (selected_id !== null) {
      const p = placements[selected_id];
      if (p) net.deploy([selected_id], p.x, p.y, p.facing);
    }
    try {
      canvas_el?.releasePointerCapture(event.pointerId);
    } catch {
      // capture may already be released — ignore.
    }
  }

  /**
   * Rotates the selected member's facing toward the enemy camp.
   *
   * @returns void
   */
  function face_enemy(): void {
    if (selected_id === null) return;
    const p = placements[selected_id];
    if (!p) return;
    p.facing = Math.atan2(enemy_center.y - p.y, enemy_center.x - p.x);
    net.deploy([selected_id], p.x, p.y, p.facing);
    draw();
  }

  /**
   * Sets the selected member's facing from the slider (degrees).
   *
   * @param degrees - Facing in degrees
   * @returns void
   */
  function set_facing(degrees: number): void {
    if (selected_id === null) return;
    const p = placements[selected_id];
    if (!p) return;
    p.facing = (degrees * Math.PI) / 180;
    draw();
  }

  /**
   * Sends every placement to the server and presses Ready.
   *
   * @returns void
   */
  function ready(): void {
    for (const m of members) {
      const p = placements[m.member_id];
      if (p) net.deploy([m.member_id], p.x, p.y, p.facing);
    }
    net.ready();
  }

  /** Renders the map, deploy zone, obstacles, and squad tokens. */
  function draw(): void {
    const canvas = canvas_el;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const cols = map.width_tiles ?? 16;
    const rows = map.height_tiles ?? 16;

    ctx.fillStyle = '#2c3e1f';
    ctx.fillRect(0, 0, BW, BH);

    // Terrain cost shading (0 = impassable water/ditch).
    const costs = map.tile_costs ?? [];
    if (costs.length === cols * rows) {
      for (let ty = 0; ty < rows; ty++) {
        for (let tx = 0; tx < cols; tx++) {
          if (costs[ty * cols + tx] === 0) {
            ctx.fillStyle = 'rgba(40, 80, 140, 0.55)';
            ctx.fillRect((tx * BW) / cols, (ty * BH) / rows, BW / cols + 1, BH / rows + 1);
          }
        }
      }
    }

    // Grid.
    ctx.strokeStyle = 'rgba(255,255,255,0.08)';
    ctx.lineWidth = 1;
    for (let i = 1; i < cols; i++) {
      ctx.beginPath();
      ctx.moveTo((i * BW) / cols, 0);
      ctx.lineTo((i * BW) / cols, BH);
      ctx.stroke();
    }
    for (let i = 1; i < rows; i++) {
      ctx.beginPath();
      ctx.moveTo(0, (i * BH) / rows);
      ctx.lineTo(BW, (i * BH) / rows);
      ctx.stroke();
    }

    // Obstacles.
    for (const ob of map.obstacles ?? []) {
      ctx.fillStyle = ob.blocks_movement ? '#5b5b5b' : '#4a5d3a';
      ctx.beginPath();
      ctx.arc(ob.x * BW, ob.y * BH, (ob.radius ?? 0.04) * BW, 0, Math.PI * 2);
      ctx.fill();
    }

    // Deploy zone.
    const z = zone;
    ctx.fillStyle = 'rgba(77,163,255,0.16)';
    ctx.fillRect(z.x * BW, z.y * BH, z.w * BW, z.h * BH);
    ctx.strokeStyle = '#4da3ff';
    ctx.setLineDash([8, 6]);
    ctx.lineWidth = 2;
    ctx.strokeRect(z.x * BW, z.y * BH, z.w * BW, z.h * BH);
    ctx.setLineDash([]);

    // Tokens.
    for (const m of members) {
      const p = placements[m.member_id];
      if (!p) continue;
      const x = p.x * BW;
      const y = p.y * BH;
      const direction = direction_index(p.facing);
      const token_radius = token_radius_px(m.unit_class);
      const side = token_radius * 3;

      // Body: directional hand-authored art when present (D24/D71), else the
      // legacy rotated sprite, else the team token. Directional frames draw
      // unrotated (optional mirror); the legacy flat sprite rotates to face.
      const animated = resolve_body_frame(m.unit_class, direction, false, 0);
      if (animated) {
        ctx.save();
        ctx.translate(x, y);
        if (animated.mirror) ctx.scale(-1, 1);
        ctx.drawImage(animated.image, -side / 2, -side / 2, side, side);
        ctx.restore();
      } else {
        const sprite = get_unit_sprite(m.unit_class);
        if (sprite) {
          ctx.save();
          ctx.translate(x, y);
          ctx.rotate(p.facing);
          ctx.drawImage(sprite, -side / 2, -side / 2, side, side);
          ctx.restore();
        } else {
          ctx.fillStyle = m.is_knight ? '#ffd94a' : '#4da3ff';
          ctx.beginPath();
          ctx.arc(x, y, token_radius, 0, Math.PI * 2);
          ctx.fill();
        }
      }

      // Outline / selection ring (always drawn).
      ctx.beginPath();
      ctx.arc(x, y, token_radius, 0, Math.PI * 2);
      ctx.strokeStyle = selected_id === m.member_id ? '#ffffff' : 'rgba(0,0,0,0.6)';
      ctx.lineWidth = selected_id === m.member_id ? 3 : 1.5;
      ctx.stroke();

      // Facing chevron (snaps to the drawn sector when directional art is used).
      ctx.save();
      ctx.translate(x, y);
      ctx.rotate(animated ? direction * (Math.PI / 4) : p.facing);
      ctx.fillStyle = 'rgba(255,255,255,0.9)';
      ctx.beginPath();
      ctx.moveTo(token_radius - 2, 0);
      ctx.lineTo(token_radius - 9, -6);
      ctx.lineTo(token_radius - 9, 6);
      ctx.closePath();
      ctx.fill();
      ctx.restore();
    }
  }

  // Redraw whenever placements or selection change.
  $effect(() => {
    placements;
    selected_id;
    draw();
  });
</script>

<div class="container-fluid py-3">
  <div class="d-flex justify-content-between align-items-center mb-2">
    <div class="d-flex align-items-center gap-3">
      <CombatBanner banner={welcome.banner} size={32} title={texts.combat_deploy_title ?? ''} />
      <h3 class="mb-0">{texts.combat_deploy_title ?? ''}</h3>
    </div>
    <span class="badge text-bg-warning fs-6">
      {texts.combat_deploy_timer_label ?? ''} {Math.max(0, Math.ceil(deploy_time))}s
    </span>
  </div>
  <p class="text-muted">{texts.combat_deploy_hint ?? ''}</p>

  <div class="row g-3">
    <div class="col-12 col-lg-9">
      <canvas
        bind:this={canvas_el}
        width={BW}
        height={BH}
        class="d-block w-100 border border-secondary rounded"
        style="touch-action: none;"
        onpointerdown={on_pointer_down}
        onpointermove={on_pointer_move}
        onpointerup={on_pointer_up}
      ></canvas>
    </div>
    <div class="col-12 col-lg-3">
      {#if selected_id !== null && placements[selected_id]}
        <div class="card border-secondary mb-3">
          <div class="card-header">{texts.combat_deploy_rotate_hint ?? ''}</div>
          <div class="card-body">
            <input
              type="range"
              class="form-range"
              min="0"
              max="359"
              value={Math.round((placements[selected_id].facing * 180) / Math.PI) % 360}
              oninput={(e) => set_facing(Number((e.currentTarget as HTMLInputElement).value))}
            />
            <button class="btn btn-sm btn-outline-light w-100" onclick={face_enemy}>
              {texts.combat_deploy_face_enemy_btn ?? ''}
            </button>
          </div>
        </div>
      {/if}
      <button class="btn btn-primary w-100 mb-2" onclick={ready}>
        {texts.combat_deploy_ready_btn ?? ''}
      </button>
      <button class="btn btn-outline-danger w-100" onclick={on_leave}>
        {texts.combat_results_back_btn ?? ''}
      </button>
    </div>
  </div>
</div>
