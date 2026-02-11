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

## Future Enhancements

- Quadtree spatial indexing for large fiefdoms
- Zone-based placement restrictions
- Pathfinding integration for access verification