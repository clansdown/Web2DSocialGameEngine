/**
 * Tower Defense Map Loader
 *
 * Converts raw map metadata from the server (0-1 normalized coordinates)
 * into board-unit coordinate data structures for the game engine.
 *
 * ===== Coordinate Systems =====
 *
 * Map JSON format (tower_defense_map_metadata_format.md):
 *   All positions use 0-1 normalized coordinates where:
 *     (0, 0) = top-left of map image
 *     (1, 1) = bottom-right of map image
 *   Waypoints, spawn points, exclusion zone centers/radii, end points,
 *   and intersection positions all use this 0-1 space.
 *
 * Board space (SimpleGame engine):
 *   The game uses board units. The board is BW × BH board units.
 *   All GameObject positions, distances, sizes, and velocities are in
 *   board units. The canvas is sized BW × BH pixels, so 1 board unit =
 *   1 canvas pixel at default zoom (no scroll, no scale).
 *
 * Conversion:
 *   X: normalized × BW  →  board unit X
 *   Y: normalized × BH  →  board unit Y
 *   radius: normalized × BW  →  board unit radius
 *
 * The map image is stretched to fill the full BW × BH board.
 * The sidebar (SIDEBAR_W = 420) is an overlay at the right edge,
 * rendered on top of the board at x ∈ [BW - SIDEBAR_W, BW].
 */


// ================================================================
// Board-unit data structures
// ================================================================

/** A 2D point in board units */
export interface BoardPoint {
  /** X in board units */
  x: number;
  /** Y in board units */
  y: number;
}

/** A path waypoint in board units */
export interface BoardMapWaypoint {
  /** X in board units */
  x: number;
  /** Y in board units */
  y: number;
}

/** A path that enemies follow, with waypoints in board units */
export interface BoardMapPath {
  id: string;
  label: string;
  /** Waypoints in board units */
  waypoints: BoardMapWaypoint[];
  endAtIntersectionId?: string;
  endAtEndPointId?: string;
}

/** A weighted branch at a path intersection */
export interface BoardMapIntersectionBranch {
  pathId: string;
  weight: number;
}

/** A path intersection where enemies choose a branch */
export interface BoardMapIntersection {
  id: string;
  label: string;
  /** X in board units */
  x: number;
  /** Y in board units */
  y: number;
  branches: BoardMapIntersectionBranch[];
}

/** An enemy spawn point in board units */
export interface BoardMapSpawnPoint {
  id: string;
  label: string;
  /** X in board units */
  x: number;
  /** Y in board units */
  y: number;
  intervalMs: number;
  initialDelayMs: number;
  targetPathId: string;
}

/** A circular no-build zone in board units */
export interface BoardMapCircleZone {
  id: string;
  label: string;
  type: 'circle';
  /** Center X in board units */
  centerX: number;
  /** Center Y in board units */
  centerY: number;
  /** Radius in board units */
  radius: number;
}

/** A polygonal no-build zone with vertices in board units */
export interface BoardMapPolygonZone {
  id: string;
  label: string;
  type: 'polygon';
  /** Vertices in board units */
  vertices: BoardPoint[];
}

/** A no-build zone (circle or polygon) */
export type BoardMapExclusionZone = BoardMapCircleZone | BoardMapPolygonZone;

/** A map exit point in board units */
export interface BoardMapEndPoint {
  id: string;
  label: string;
  /** X in board units */
  x: number;
  /** Y in board units */
  y: number;
}

/** Fully converted map data — all coordinates in board units */
export interface BoardMapData {
  /** Map image filename (not a coordinate — used as-is) */
  imageFilename: string;
  /** Spawn points in board units */
  spawnPoints: BoardMapSpawnPoint[];
  /** Paths with waypoints in board units */
  paths: BoardMapPath[];
  /** Intersections in board units */
  intersections: BoardMapIntersection[];
  /** No-build zones in board units */
  exclusionZones: BoardMapExclusionZone[];
  /** Exit points in board units */
  endPoints: BoardMapEndPoint[];
}

// ================================================================
// Conversion helpers
// ================================================================

/** Convert 0-1 normalized X to board units */
function toBX(v: number, BW: number): number {
  return v * BW;
}

/** Convert 0-1 normalized Y to board units */
function toBY(v: number, BH: number): number {
  return v * BH;
}

// ================================================================
// Public API
// ================================================================

/**
 * Load and convert raw map metadata from the server into board units.
 *
 * @param raw - Raw map metadata from server (snake_case fields, 0-1 normalized)
 * @param BW  - Board width in board units
 * @param BH  - Board height in board units
 * @returns BoardMapData with all coordinates converted to board units
 *
 * Input example:  raw.spawn_points[0].x = 0.05
 * Output example: result.spawnPoints[0].x = 0.05 * BW
 *
 * The conversion formula for every coordinate field is:
 *   X: raw_value × BW   →   board_units
 *   Y: raw_value × BH   →   board_units
 *   radius: raw_value × BW  →   board_units
 */
export function loadMap(raw: any, BW: number, BH: number): BoardMapData {
  const spawnPoints: BoardMapSpawnPoint[] = (raw.spawn_points || []).map((sp: any) => ({
    id: sp.id,
    label: sp.label || '',
    x: toBX(sp.x, BW),
    y: toBY(sp.y, BH),
    intervalMs: sp.interval_ms || 1000,
    initialDelayMs: sp.initial_delay_ms || 0,
    targetPathId: sp.target_path_id,
  }));

  const paths: BoardMapPath[] = (raw.paths || []).map((p: any) => ({
    id: p.id,
    label: p.label || '',
    waypoints: (p.waypoints || []).map((wp: { x: number; y: number }) => ({
      x: toBX(wp.x, BW),
      y: toBY(wp.y, BH),
    })),
    endAtIntersectionId: p.end_at_intersection_id,
    endAtEndPointId: p.end_at_end_point_id,
  }));

  const intersections: BoardMapIntersection[] = (raw.intersections || []).map((ints: any) => ({
    id: ints.id,
    label: ints.label || '',
    x: toBX(ints.x, BW),
    y: toBY(ints.y, BH),
    branches: (ints.branches || []).map((br: { path_id: string; weight: number }) => ({
      pathId: br.path_id,
      weight: br.weight,
    })),
  }));

  const exclusionZones: BoardMapExclusionZone[] = (raw.exclusion_zones || []).map((z: any) => {
    if (z.type === 'polygon') {
      return {
        id: z.id,
        label: z.label || '',
        type: 'polygon' as const,
        vertices: (z.vertices || []).map((v: { x: number; y: number }) => ({
          x: toBX(v.x, BW),
          y: toBY(v.y, BH),
        })),
      };
    }
    return {
      id: z.id,
      label: z.label || '',
      type: 'circle' as const,
      centerX: toBX(z.center_x, BW),
      centerY: toBY(z.center_y, BH),
      radius: toBX(z.radius || 0, BW),
    };
  });

  const endPoints: BoardMapEndPoint[] = (raw.end_points || []).map((ep: any) => ({
    id: ep.id,
    label: ep.label || '',
    x: toBX(ep.x, BW),
    y: toBY(ep.y, BH),
  }));

  return {
    imageFilename: raw.image_filename || '',
    spawnPoints,
    paths,
    intersections,
    exclusionZones,
    endPoints,
  };
}
