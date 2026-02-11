# Collision Detection System

The collision detection system prevents buildings from overlapping and enforces building placement rules.

## Files

- `server/GridCollision.hpp` - Collision detection structures and declarations
- `server/GridCollision.cpp` - Collision detection implementation

## Coordinate System

Buildings are placed on a 2D grid relative to the fiefdom center (0, 0):
- **X-axis**: Positive values extend to the right
- **Y-axis**: Positive values extend upward

## Building Rectangles

Each building occupies a rectangle defined by:
- `x, y` - Bottom-left corner coordinates
- `width, height` - Dimensions from building config

### Rectangle Overlap Check

Two rectangles overlap if:
```
rect1.x < rect2.x + rect2.width &&
rect1.x + rect1.width > rect2.x &&
rect1.y < rect2.y + rect2.height &&
rect1.y + rect1.height > rect2.y
```

## API Reference

### `checkPlacement()`

Validates if a building can be placed at the specified location.

```cpp
PlacementCheck result = GridCollision::checkPlacement(
    int fiefdom_id,
    const std::string& building_type,
    int x,
    int y,
    bool check_home_base_position = true
);
```

**Parameters:**
- `fiefdom_id` - Fiefdom to check placement in
- `building_type` - Type of building being placed
- `x, y` - Proposed coordinates
- `check_home_base_position` - If true, enforces home_base at (0,0)

**Returns:**
```cpp
struct PlacementCheck {
    bool valid;  // true if placement is valid
    std::vector<int> overlapping_building_ids;  // IDs of overlapping buildings
    std::string error_message;  // Error description if invalid
};
```

**Validation Rules:**
1. Position must be within valid bounds (-1000 to +1000)
2. For `home_base`: Must be at (0, 0)
3. Building must exist in config (valid type)
4. Building must not overlap any existing buildings

### `getBuildingDimensions()`

Retrieves building dimensions from config.

```cpp
BuildingDimensions dims = GridCollision::getBuildingDimensions(building_type);
// Result: { width, height }
```

### `isValidPosition()`

Checks if coordinates are within bounds.

```cpp
bool valid = GridCollision::isValidPosition(x, y);
// Bounds: -1000 to +1000 for both x and y
```

## Building Config Integration

Building dimensions are read from `fiefdom_building_types.json`:

```json
{
  "farm": {
    "width": 2,
    "height": 2,
    ...
  }
}
```

If width/height are not specified, defaults to 1x1.

## Special Rules

### Home Base

The `home_base` building has unique placement rules:
- Must be placed at coordinates (0, 0)
- Only one `home_base` allowed per fiefdom
- Can be built without completing prerequisite check

### Construction Underway

All buildings (including those under construction at `level == 0`) occupy their space and are checked for collision. Once construction begins, the space is reserved.

## Usage Example

```cpp
auto result = GridCollision::checkPlacement(fiefdom_id, "farm", 5, 3);

if (!result.valid) {
    // Handle placement failure
    if (!result.overlapping_building_ids.empty()) {
        std::cout << "Cannot place: overlaps with buildings "
                  << result.overlapping_building_ids.size() << std::endl;
    }
}
```

## Performance Considerations

- Spatial index on `(fiefdom_id, x, y)` for efficient queries
- All buildings are checked (including under construction)
- Simple rectangle overlap check (O(n) where n = buildings in fiefdom)

## Wall Collision Detection

Walls have a special collision detection system because they:
1. Are always centered at (0, 0) - the fiefdom origin
2. Occupy 4 sides (North, South, East, West) based on configuration
3. Are defined by generation in `wall_config.json`

### Wall Dimensions

Wall dimensions are defined in `server/config/wall_config.json`:

```json
{
  "walls": {
    "1": {
      "width": 40,
      "length": 40,
      "thickness": 4,
      "hp": [1000, 1200, 1500],
      ...
    }
  }
}
```

- **width**: Total width of the wall perimeter (extends from -width/2 to +width/2)
- **length**: Total length extending from center in both directions
- **thickness**: Wall thickness (must be divisible by 2)
- Wall positions: North/South walls have width × thickness, East/West walls have thickness × length

### Wall Rectangle Calculation

Each wall generation creates 4 rectangles centered at (0, 0):

```
           North Wall
      ┌─────────────────┐
      │                 │
      │                 │
West  │                 │  East
Wall  │                 │  Wall
      │                 │
      │                 │
           South Wall
```

For a wall with `width=40`, `length=40`, `thickness=4`:

- **North wall**: x: -20 to +20, y: +18 to +22 (top side, extending upward from center)
- **South wall**: x: -20 to +20, y: -22 to -18 (bottom side, extending downward from center)
- **East wall**: x: +18 to +22, y: -20 to +20 (right side, extending right from center)
- **West wall**: x: -22 to -18, y: -20 to +20 (left side, extending left from center)

### API Reference

#### `getWallDimensions()`

Retrieves wall dimensions for a generation.

```cpp
WallDimensions dims = GridCollision::getWallDimensions(int generation);
// Returns: { width, length, thickness }
```

#### `overlapsWalls()`

Checks if a building overlaps with any wall of a generation.

```cpp
bool overlaps = GridCollision::overlapsWalls(
    int fiefdom_id,
    int generation,
    int building_x,
    int building_y,
    int building_width,
    int building_height
);
```

**Parameters:**
- `fiefdom_id` - Fiefdom to check (unused, for API consistency)
- `generation` - Wall generation to check against
- `building_x, building_y` - Building bottom-left coordinates
- `building_width, building_height` - Building dimensions

**Returns:** `true` if the building rectangle overlaps any of the 4 wall rectangles

#### `getOverlappingBuildings()`

Returns all buildings that overlap with a wall generation.

```cpp
std::vector<nlohmann::json> buildings = GridCollision::getOverlappingBuildings(
    int fiefdom_id,
    int generation,
    int wall_x,  // Always 0 for centered walls
    int wall_y,  // Always 0 for centered walls
    int wall_width,
    int wall_height
);
```

**Returns:** JSON array of buildings with `{id, name, level, x, y}` fields

### Wall Building Rules

When building a wall:

1. All overlapping completed buildings (level > 0) are automatically demolished
2. Demolished buildings receive 80% refund of cumulative costs
3. Partial refunds are rounded down
4. Buildings under construction (level == 0) are NOT affected

### Example: Building Wall Generation 1

```cpp
int generation = 1;
auto dims = GridCollision::getWallDimensions(generation);
// dims: { width: 40, length: 40, thickness: 4 }

auto overlapping = GridCollision::getOverlappingBuildings(
    fiefdom_id, generation, 0, 0, dims.width, dims.length
);

for (const auto& building : overlapping) {
    std::cout << "Demolishing building " << building["id"]
              << " (" << building["name"] << ")" << std::endl;
    // Auto-demolish and refund in BuildWallActionHandler
}
```

## Future Enhancements

- Quadtree spatial indexing for large fiefdoms
- Zone-based placement restrictions
- Pathfinding integration for access verification