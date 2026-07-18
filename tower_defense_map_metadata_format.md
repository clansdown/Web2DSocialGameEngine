# Tower Defense Map Metadata Format

**Version**: 1.0

This document specifies the JSON metadata format produced by TowerDefenseMapper and consumed by tower defense game engines. All coordinates use a **normalized 0–1 system** where `(0, 0)` is the top-left of the map image and `(1, 1)` is the bottom-right.

## Top-Level Structure

```json
{
  "formatVersion": "1.0",
  "name": "MyMap",
  "imageFilename": "my_map.png",
  "difficulty": 1.0,
  "spawnPoints": [ ... ],
  "paths": [ ... ],
  "intersections": [ ... ],
  "exclusionZones": [ ... ],
  "endPoints": [ ... ]
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `formatVersion` | string | yes | Format version string for compatibility checking |
| `name` | string | yes | Human-readable map name |
| `imageFilename` | string | yes | Original map image filename (for reference only) |
| `difficulty` | number | yes | Map difficulty rating (1.0 = easy, higher = harder) |
| `spawnPoints` | SpawnPoint[] | yes | Array of enemy spawn points |
| `paths` | Path[] | yes | Array of path polylines |
| `intersections` | Intersection[] | yes | Array of path intersection nodes |
| `exclusionZones` | ExclusionZone[] | yes | Array of restricted areas |
| `endPoints` | EndPoint[] | yes | Array of map exit points |

## SpawnPoint

A point where enemies enter the map.

```json
{
  "id": "spawn-1",
  "label": "Main Gate",
  "x": 0.05,
  "y": 0.50,
  "intervalMs": 1000,
  "initialDelayMs": 0,
  "targetPathId": "path-1"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | yes | Unique identifier |
| `label` | string | yes | Human-readable label |
| `x` | number | yes | Normalized X position (0–1) |
| `y` | number | yes | Normalized Y position (0–1) |
| `intervalMs` | number | yes | Milliseconds between consecutive enemy spawns |
| `initialDelayMs` | number | yes | Milliseconds before the first enemy spawns |
| `targetPathId` | string | yes | ID of the first path enemies follow |

## Path

An ordered polyline of waypoints that enemies follow.

```json
{
  "id": "path-1",
  "label": "Left Lane",
  "waypoints": [
    { "x": 0.05, "y": 0.50 },
    { "x": 0.20, "y": 0.50 },
    { "x": 0.35, "y": 0.35 }
  ],
  "endAtIntersectionId": "int-1",
  "endAtEndPointId": null
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | yes | Unique identifier |
| `label` | string | yes | Human-readable label |
| `waypoints` | {x: number, y: number}[] | yes | Ordered waypoints forming the path polyline (2+ required) |
| `endAtIntersectionId` | string | no | ID of the intersection this path leads into |
| `endAtEndPointId` | string | no | ID of the endpoint (map exit) this path leads to |

## Intersection

A node where paths converge and enemies choose which outgoing path to follow. Each branch has a weight: higher weight = higher probability.

```json
{
  "id": "int-1",
  "label": "Crossroads",
  "x": 0.50,
  "y": 0.50,
  "branches": [
    { "pathId": "path-2", "weight": 3 },
    { "pathId": "path-3", "weight": 1 }
  ]
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | yes | Unique identifier |
| `label` | string | yes | Human-readable label |
| `x` | number | yes | Normalized X position (0–1) |
| `y` | number | yes | Normalized Y position (0–1) |
| `branches` | IntersectionBranch[] | yes | Weighted outgoing path choices |

### IntersectionBranch

```json
{
  "pathId": "path-2",
  "weight": 3
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `pathId` | string | yes | ID of the outgoing path |
| `weight` | number | yes | Relative selection probability (higher = more likely to be chosen) |

**Weight calculation**: The probability of choosing a branch is `branch.weight / sum(all branch weights)`. For example, with weights 3 and 1, the first path is chosen 75% of the time and the second 25%.

## EndPoint

A point where enemies exit the map.

```json
{
  "id": "end-1",
  "label": "Castle Gate",
  "x": 0.95,
  "y": 0.50
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | yes | Unique identifier |
| `label` | string | yes | Human-readable label |
| `x` | number | yes | Normalized X position (0–1) |
| `y` | number | yes | Normalized Y position (0–1) |

### Path Routing Logic (for game engine implementors)

1. Enemy spawns at a **SpawnPoint** and starts following the path identified by `targetPathId`.
2. The enemy traverses the path's `waypoints` in order from first to last.
3. If the path has an `endAtIntersectionId`, the enemy arrives at that **Intersection**.
4. At the intersection, one outgoing branch is selected randomly according to branch weights.
5. The enemy follows the selected branch's `pathId` to the next path.
6. Steps 3–5 repeat until the enemy reaches a path with an `endAtEndPointId` (the enemy exits the map at the endpoint) or a path with neither `endAtIntersectionId` nor `endAtEndPointId` (the enemy leaves the map at the path's last waypoint).

## ExclusionZone

An area where players cannot place towers. Supports two shapes: polygon and circle.

### Polygon

```json
{
  "id": "ex-1",
  "label": "Rock Formation",
  "type": "polygon",
  "vertices": [
    { "x": 0.30, "y": 0.20 },
    { "x": 0.40, "y": 0.15 },
    { "x": 0.45, "y": 0.25 },
    { "x": 0.35, "y": 0.30 }
  ]
}
```

### Circle

```json
{
  "id": "ex-2",
  "label": "Pond",
  "type": "circle",
  "centerX": 0.70,
  "centerY": 0.80,
  "radius": 0.08
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `id` | string | yes | Unique identifier |
| `label` | string | yes | Human-readable label |
| `type` | "polygon" \| "circle" | yes | Shape type |
| `vertices` | {x: number, y: number}[] | for polygon | Polygon vertices in order (minimum 3) |
| `centerX` | number | for circle | Normalized X position of center |
| `centerY` | number | for circle | Normalized Y position of center |
| `radius` | number | for circle | Radius in normalized units |

## Coordinate System Details

- **Normalized coordinates** range from 0.0 to 1.0, where:
  - X increases left-to-right
  - Y increases top-to-bottom
  - `(0, 0)` is the top-left pixel of the image
  - `(1, 1)` is the bottom-right pixel of the image
- **Values outside 0–1 are valid** (they extend beyond the image bounds), though unusual.
- **Coordinate mapping**: `pixel = normalized * imageDimension`. Games should multiply normalized coordinates by their image width/height to get pixel positions.

## Version History

| Version | Changes |
|---------|---------|
| 1.0 | Initial format |
