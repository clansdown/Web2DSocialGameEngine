/**
 * Tower Defense engine.
 * Renders map elements from the map metadata format (spawn points, paths,
 * intersections, exclusion zones, endpoints) and handles coordinate
 * conversion from normalized 0-1 to canvas pixel space.
 *
 * Gameplay (wave spawning, tower mechanics) is kept separate for later.
 */

import type { TowerDefenseLevelConfig, MapMetadata, EnemyState, Waypoint } from './tower_defense_types';

export interface TowerDefenseEngineState {
  config: TowerDefenseLevelConfig;
  metadata: MapMetadata | null;
  width: number;
  height: number;
  finished: boolean;
  won: boolean;
  score: number;
  background_image: HTMLImageElement | null;
  enemies: EnemyState[];
  last_time: number;
}

function is_map_metadata(obj: unknown): obj is MapMetadata {
  return typeof obj === 'object' && obj !== null && 'format_version' in obj;
}

export function create_engine(
  config: TowerDefenseLevelConfig,
  width: number,
  height: number
): TowerDefenseEngineState {
  let metadata: MapMetadata | null = null;
  const raw = config.map_metadata;
  if (raw && is_map_metadata(raw)) {
    metadata = raw;
  }

  return {
    config,
    metadata,
    width,
    height,
    finished: false,
    won: false,
    score: 0,
    background_image: null,
    enemies: [],
    last_time: 0
  };
}

function coord_to_pixel(x: number, y: number, width: number, height: number): [number, number] {
  return [x * width, y * height];
}

function draw_path(
  ctx: CanvasRenderingContext2D,
  waypoints: Waypoint[],
  color: string,
  width: number,
  height: number
): void {
  if (waypoints.length < 2) return;

  ctx.beginPath();
  const [sx, sy] = coord_to_pixel(waypoints[0].x, waypoints[0].y, width, height);
  ctx.moveTo(sx, sy);

  for (let i = 1; i < waypoints.length; i++) {
    const [px, py] = coord_to_pixel(waypoints[i].x, waypoints[i].y, width, height);
    ctx.lineTo(px, py);
  }

  ctx.strokeStyle = color;
  ctx.lineWidth = 12;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  ctx.stroke();

  ctx.strokeStyle = '#ffffff';
  ctx.lineWidth = 4;
  ctx.setLineDash([6, 6]);
  ctx.stroke();
  ctx.setLineDash([]);

  for (const wp of waypoints) {
    const [wx, wy] = coord_to_pixel(wp.x, wp.y, width, height);
    ctx.beginPath();
    ctx.arc(wx, wy, 4, 0, Math.PI * 2);
    ctx.fillStyle = '#ffffff';
    ctx.fill();
  }
}

export function render_frame(
  ctx: CanvasRenderingContext2D,
  state: TowerDefenseEngineState
): void {
  const w = state.width;
  const h = state.height;

  ctx.clearRect(0, 0, w, h);

  if (state.background_image && state.background_image.complete && state.background_image.naturalWidth > 0) {
    ctx.drawImage(state.background_image, 0, 0, w, h);
  } else {
    ctx.fillStyle = '#1a1a2e';
    ctx.fillRect(0, 0, w, h);
    const grid_size = 40;
    ctx.strokeStyle = '#2a2a3e';
    ctx.lineWidth = 1;
    for (let x = 0; x < w; x += grid_size) {
      ctx.beginPath();
      ctx.moveTo(x, 0);
      ctx.lineTo(x, h);
      ctx.stroke();
    }
    for (let y = 0; y < h; y += grid_size) {
      ctx.beginPath();
      ctx.moveTo(0, y);
      ctx.lineTo(w, y);
      ctx.stroke();
    }
  }

  const meta = state.metadata;
  if (!meta) {
    ctx.fillStyle = '#888';
    ctx.font = '18px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText('No map metadata loaded', w / 2, h / 2);
    return;
  }

  const path_colors = [
    '#e74c3c', '#3498db', '#2ecc71', '#f39c12',
    '#9b59b6', '#1abc9c', '#e67e22', '#34495e'
  ];

  for (let i = 0; i < meta.paths.length; i++) {
    const color = path_colors[i % path_colors.length];
    draw_path(ctx, meta.paths[i].waypoints, color, w, h);

    ctx.fillStyle = '#aaa';
    ctx.font = '11px sans-serif';
    ctx.textAlign = 'center';
    const last_wp = meta.paths[i].waypoints[meta.paths[i].waypoints.length - 1];
    const [lx, ly] = coord_to_pixel(last_wp.x, last_wp.y, w, h);
    ctx.fillText(meta.paths[i].id, lx, ly - 14);
  }

  for (const sp of meta.spawn_points) {
    const [sx, sy] = coord_to_pixel(sp.x, sp.y, w, h);
    ctx.beginPath();
    ctx.arc(sx, sy, 10, 0, Math.PI * 2);
    ctx.fillStyle = '#e74c3c';
    ctx.fill();
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 2;
    ctx.stroke();
    ctx.fillStyle = '#fff';
    ctx.font = '11px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(sp.label, sx, sy - 16);
  }

  for (const ep of meta.end_points) {
    const [ex, ey] = coord_to_pixel(ep.x, ep.y, w, h);
    ctx.beginPath();
    ctx.arc(ex, ey, 10, 0, Math.PI * 2);
    ctx.fillStyle = '#2ecc71';
    ctx.fill();
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 2;
    ctx.stroke();
    ctx.fillStyle = '#fff';
    ctx.font = '11px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(ep.label, ex, ey - 16);
  }

  for (const inter of meta.intersections) {
    const [ix, iy] = coord_to_pixel(inter.x, inter.y, w, h);
    ctx.beginPath();
    ctx.moveTo(ix, iy - 10);
    ctx.lineTo(ix + 8, iy);
    ctx.lineTo(ix, iy + 10);
    ctx.lineTo(ix - 8, iy);
    ctx.closePath();
    ctx.fillStyle = '#9b59b6';
    ctx.fill();
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 2;
    ctx.stroke();
    ctx.fillStyle = '#fff';
    ctx.font = '10px sans-serif';
    ctx.textAlign = 'center';
    ctx.fillText(inter.label, ix, iy - 16);
  }

  for (const ez of meta.exclusion_zones) {
    ctx.fillStyle = 'rgba(231, 76, 60, 0.15)';
    ctx.strokeStyle = 'rgba(231, 76, 60, 0.5)';
    ctx.lineWidth = 2;

    if (ez.type === 'polygon' && ez.vertices.length >= 3) {
      ctx.beginPath();
      const [vx, vy] = coord_to_pixel(ez.vertices[0].x, ez.vertices[0].y, w, h);
      ctx.moveTo(vx, vy);
      for (let i = 1; i < ez.vertices.length; i++) {
        const [vx2, vy2] = coord_to_pixel(ez.vertices[i].x, ez.vertices[i].y, w, h);
        ctx.lineTo(vx2, vy2);
      }
      ctx.closePath();
      ctx.fill();
      ctx.stroke();
    } else if (ez.type === 'circle') {
      const [cx, cy] = coord_to_pixel(ez.center_x, ez.center_y, w, h);
      const r = ez.radius * Math.max(w, h);
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, Math.PI * 2);
      ctx.fill();
      ctx.stroke();
    }
  }

  for (const enemy of state.enemies) {
    const [ex, ey] = coord_to_pixel(enemy.x, enemy.y, w, h);
    ctx.beginPath();
    ctx.arc(ex, ey, 8, 0, Math.PI * 2);
    ctx.fillStyle = '#f1c40f';
    ctx.fill();
    ctx.strokeStyle = '#fff';
    ctx.lineWidth = 2;
    ctx.stroke();
  }

  ctx.fillStyle = '#555';
  ctx.font = '12px sans-serif';
  ctx.textAlign = 'left';
  ctx.fillText(`Map: ${meta.name}`, 10, 20);
  ctx.fillText(`Difficulty ${state.config.difficulty}`, 10, 36);
}

export function update_engine(
  state: TowerDefenseEngineState,
  delta_ms: number
): TowerDefenseEngineState {
  if (!state.metadata || state.finished) return state;

  const meta = state.metadata;
  const path_map = new Map(meta.paths.map(p => [p.id, p]));
  const intersection_map = new Map(meta.intersections.map(i => [i.id, i]));

  let enemies = [...state.enemies];
  let score = state.score;

  for (let i = enemies.length - 1; i >= 0; i--) {
    const enemy = enemies[i];
    const path = path_map.get(enemy.path_id);
    if (!path || path.waypoints.length === 0) {
      enemies.splice(i, 1);
      continue;
    }

    const wps = path.waypoints;
    const current = wps[enemy.waypoint_index];
    const target_idx = Math.min(enemy.waypoint_index + 1, wps.length - 1);
    const target = wps[target_idx];

    const dx = target.x - current.x;
    const dy = target.y - current.y;
    const dist = Math.sqrt(dx * dx + dy * dy);
    const speed_normalized = enemy.speed * delta_ms / 1000;

    enemy.progress += speed_normalized;

    const progress_ratio = Math.min(enemy.progress / dist, 1);
    const px = current.x + dx * progress_ratio;
    const py = current.y + dy * progress_ratio;

    enemy.x = px;
    enemy.y = py;

    if (progress_ratio >= 1) {
      enemy.waypoint_index = target_idx + 1;
      enemy.progress = 0;

      if (target_idx >= wps.length - 1) {
        if (path.end_at_end_point_id) {
          score += 10;
          enemies.splice(i, 1);
          continue;
        }

        if (path.end_at_intersection_id) {
          const intersection = intersection_map.get(path.end_at_intersection_id);
          if (intersection && intersection.branches.length > 0) {
            const total_weight = intersection.branches.reduce((sum, b) => sum + b.weight, 0);
            let roll = Math.random() * total_weight;
            let chosen = intersection.branches[0];
            for (const branch of intersection.branches) {
              roll -= branch.weight;
              if (roll <= 0) {
                chosen = branch;
                break;
              }
            }
            const next_path = path_map.get(chosen.path_id);
            if (next_path) {
              enemy.path_id = chosen.path_id;
              enemy.waypoint_index = 0;
              enemy.progress = 0;
              const start = next_path.waypoints[0];
              enemy.x = start.x;
              enemy.y = start.y;
            } else {
              enemies.splice(i, 1);
            }
          } else {
            enemies.splice(i, 1);
          }
        } else {
          enemies.splice(i, 1);
        }
      } else {
        const next = wps[enemy.waypoint_index];
        const ndx = next.x - enemy.x;
        const ndy = next.y - enemy.y;
        if (ndx !== 0 || ndy !== 0) {
          const ndist = Math.sqrt(ndx * ndx + ndy * ndy);
          enemy.progress = (target_idx >= wps.length - 1) ? 0 : (enemy.progress - dist);
          enemy.x = wps[enemy.waypoint_index - 1].x;
          enemy.y = wps[enemy.waypoint_index - 1].y;
        }
      }
    }
  }

  if (enemies.length === 0 && state.enemies.length > 0) {
    // all enemies exited — tentative test pass
  }

  return {
    ...state,
    enemies,
    score,
    last_time: delta_ms
  };
}
