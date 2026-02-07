#!/usr/bin/env python3
"""Config file linter for Ravenest Build and Battle game configs.

Validates all JSON config files against their schema rules.
Supports lenient JSON parsing (allows comments, trailing commas).

Usage:
    ./tools/check_configs.py          # Show errors and warnings
    ./tools/check_configs.py --no-warnings  # Show errors only
    ./tools/check_configs.py -h       # Show help

Exit codes:
    0: All configs valid (no errors found)
    1: Errors found (warnings return 0)
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import (
    TYPE_CHECKING,
    Any,
    Final,
    Literal,
    TypeAlias,
    TypedDict,
    Union,
)

if TYPE_CHECKING:
    from collections.abc import Iterable


class Severity(Enum):
    """Severity level for linter issues."""
    ERROR = "ERROR"
    WARN = "WARN"


@dataclass(frozen=True)
class LinterIssue:
    """Represents a single linter issue (error or warning)."""
    file: Path
    line: int
    column: int | None = None
    message: str = ""
    severity: Severity = Severity.ERROR

    def format(self, show_column: bool = True) -> str:
        """Format the issue for display."""
        prefix: str = self.severity.value
        location: str = f"{self.file}:{self.line}"
        if show_column and self.column is not None:
            location += f":{self.column}"
        return f"{prefix}: {location}: {self.message}"


# Type definitions for combatant configs
class DamageStatsTypedDict(TypedDict):
    """Damage stats for a single combatant level."""
    melee: int
    ranged: int
    magical: int


class DefenseStatsTypedDict(TypedDict):
    """Defense stats for a single combatant level."""
    melee: int
    ranged: int
    magical: int


class CostStatsTypedDict(TypedDict):
    """Cost stats for a single combatant level."""
    gold: int
    grain: int
    wood: int
    steel: int
    bronze: int
    stone: int
    leather: int


class CombatantTypedDict(TypedDict, total=False):
    """Combatant definition."""
    name: str
    max_level: int
    damage: list[DamageStatsTypedDict]
    defense: list[DefenseStatsTypedDict | None]
    movement_speed: list[float]
    costs: list[CostStatsTypedDict]


# Type definitions for building configs
class ResourceProductionTypedDict(TypedDict, total=False):
    """Resource production specification."""
    amount: float
    amount_multiplier: float
    periodicity: float
    periodicity_multiplier: float


class BuildingTypedDict(TypedDict, total=False):
    """Building type definition."""
    width: int
    height: int
    max_level: int
    can_build_outside_wall: bool
    construction_times: list[float]
    construction_images: list[str]
    idle_images: list[str]
    harvest_images: list[str]
    peasants: ResourceProductionTypedDict
    gold: ResourceProductionTypedDict
    grain: ResourceProductionTypedDict
    wood: ResourceProductionTypedDict
    steel: ResourceProductionTypedDict
    bronze: ResourceProductionTypedDict
    stone: ResourceProductionTypedDict
    leather: ResourceProductionTypedDict
    mana: ResourceProductionTypedDict
    gold_cost: list[float]
    grain_cost: list[float]
    wood_cost: list[float]
    steel_cost: list[float]
    bronze_cost: list[float]
    stone_cost: list[float]
    leather_cost: list[float]
    mana_cost: list[float]


# Type aliases for parsed JSON data structures
DamageTypesJson: TypeAlias = list[str]
CombatantsJson: TypeAlias = dict[str, dict[str, Any]]
BuildingsJson: TypeAlias = list[dict[str, Any]]
JsonDataType: TypeAlias = DamageTypesJson | CombatantsJson | BuildingsJson | None


# Valid field sets - use Final for constants
VALID_DAMAGE_TYPES: Final[set[str]] = {"melee", "ranged", "magical"}
VALID_RESOURCE_TYPES: Final[set[str]] = {
    "gold", "grain", "wood", "steel", "bronze", "stone", "leather"
}
VALID_PRODUCTION_RESOURCES: Final[set[str]] = {
    "peasants", "gold", "grain", "wood", "steel", "bronze", "stone", "leather", "mana"
}
VALID_BUILDING_PRODUCTION_FIELDS: Final[set[str]] = {
    "peasants", "gold", "grain", "wood", "steel", "bronze", "stone", "leather", "mana"
}
VALID_BUILDING_COST_FIELDS: Final[set[str]] = {
    "gold_cost", "grain_cost", "wood_cost", "steel_cost", "bronze_cost",
    "stone_cost", "leather_cost", "mana_cost"
}


class ConfigValidator:
    """Main validator class for all config files."""

    def __init__(self) -> None:
        self.damage_types: list[str] = []
        self.issues: list[LinterIssue] = []
        self.validated_files: list[Path] = []
        self.all_files_present: bool = True

    def _add_issue(
        self,
        file: Path,
        line: int,
        column: int | None,
        message: str,
        severity: Severity
    ) -> None:
        """Add a linter issue."""
        self.issues.append(LinterIssue(file, line, column, message, severity))

    def _get_line_info(self, content: str, position: int) -> tuple[int, int]:
        """Get line and column from character position."""
        line: int = content[:position].count('\n') + 1
        line_start: int = content[:position].rfind('\n') + 1
        column: int = position - line_start + 1
        return line, column

    def _validate_json(self, content: str, file: Path) -> JsonDataType:
        """Parse JSON with lenient handling. Returns None on error."""
        try:
            return json.loads(content)
        except json.JSONDecodeError as e:
            self._add_issue(
                file,
                e.lineno,
                e.colno if e.colno > 0 else None,
                f"JSON syntax error: {e.msg}",
                Severity.ERROR
            )
            return None

    def validate_damage_types(self, file: Path, content: str) -> bool:
        """Validate damage_types.json."""
        data: JsonDataType = self._validate_json(content, file)
        if data is None:
            return False

        if not isinstance(data, list):
            self._add_issue(file, 1, None, "Expected array of damage types", Severity.ERROR)
            return False

        if not data:
            self._add_issue(file, 1, None, "Damage types array is empty", Severity.ERROR)
            return False

        validated_types: set[str] = set()

        for i, item in enumerate(data):
            line: int = content[:].count('\n') + 1
            if i == 0:
                line = 1

            if not isinstance(item, str):
                self._add_issue(file, line, None, f"Expected string for damage type, got {type(item).__name__}", Severity.ERROR)
                continue

            if item in validated_types:
                self._add_issue(file, line, None, f"Duplicate damage type: '{item}'", Severity.ERROR)
            else:
                validated_types.add(item)

            if not item:
                self._add_issue(file, line, None, "Empty damage type string", Severity.ERROR)

        required_types: set[str] = {"melee", "ranged", "magical"}
        missing: set[str] = required_types - validated_types
        if missing:
            self._add_issue(file, 1, None, f"Missing required damage types: {sorted(missing)}", Severity.ERROR)

        self.damage_types = [t for t in data if isinstance(t, str)]

        return len([i for i in self.issues if i.file == file and i.severity == Severity.ERROR]) == 0

    def _validate_combatant_array(
        self,
        file: Path,
        content: str,
        data: dict[str, Any],
        field_name: str,
        required_keys: Union[set[str], frozenset[str]],
        damage_types: Union[set[str], frozenset[str]],
        is_defense: bool = False
    ) -> bool:
        """Validate a combatant array field (damage, defense, costs)."""
        if field_name not in data:
            return True

        array: Any = data[field_name]
        if not isinstance(array, list):
            self._add_issue(
                file, 1, None,
                f"'{field_name}' must be an array",
                Severity.ERROR
            )
            return False

        valid: bool = True
        for i, item in enumerate(array):
            line: int = content[:].count('\n') + 1

            if is_defense and item is None:
                continue

            if not isinstance(item, dict):
                self._add_issue(
                    file, line, None,
                    f"{field_name}[{i}] must be an object or null",
                    Severity.ERROR
                )
                valid = False
                continue

            item_keys: set[str] = set(item.keys())
            unexpected: set[str] = item_keys - set(required_keys)
            if unexpected:
                self._add_issue(
                    file, line, None,
                    f"{field_name}[{i}] has unexpected keys: {sorted(unexpected)}",
                    Severity.WARN
                )

            missing: set[str] = set(required_keys) - item_keys
            if missing:
                self._add_issue(
                    file, line, None,
                    f"{field_name}[{i}] is missing keys: {sorted(missing)}",
                    Severity.ERROR
                )
                valid = False
                continue

            for key in required_keys:
                value: Any = item.get(key, 0)
                if not isinstance(value, (int, float)):
                    self._add_issue(
                        file, line, None,
                        f"{field_name}[{i}].{key} must be a number, got {type(value).__name__}",
                        Severity.ERROR
                    )
                    valid = False

        return valid

    def _validate_combatant(
        self,
        file: Path,
        content: str,
        combatant_id: str,
        data: dict[str, Any]
    ) -> bool:
        """Validate a single combatant definition."""
        valid: bool = True

        if "name" not in data:
            self._add_issue(
                file, 1, None,
                f"Combatant '{combatant_id}' is missing required field 'name'",
                Severity.ERROR
            )
            valid = False
        elif not isinstance(data["name"], str) or not data["name"]:
            self._add_issue(
                file, 1, None,
                f"Combatant '{combatant_id}' has invalid 'name' field",
                Severity.ERROR
            )
            valid = False

        if "max_level" not in data:
            self._add_issue(
                file, 1, None,
                f"Combatant '{combatant_id}' is missing required field 'max_level'",
                Severity.ERROR
            )
            valid = False
        else:
            max_level: Any = data["max_level"]
            if not isinstance(max_level, int):
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.max_level must be an integer, got {type(max_level).__name__}",
                    Severity.ERROR
                )
                valid = False
            elif max_level < 1:
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.max_level must be >= 1, got {max_level}",
                    Severity.ERROR
                )
                valid = False

        damage_types_set: set[str] = set(self.damage_types) if self.damage_types else set(VALID_DAMAGE_TYPES)

        if not self._validate_combatant_array(
            file, content, data, "damage",
            set(damage_types_set), set(damage_types_set), False
        ):
            valid = False

        if not self._validate_combatant_array(
            file, content, data, "defense",
            set(damage_types_set), set(damage_types_set), True
        ):
            valid = False

        if "movement_speed" in data:
            array: list[Any] = data["movement_speed"]
            if not isinstance(array, list):
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.movement_speed must be an array",
                    Severity.ERROR
                )
                valid = False
            else:
                for i, item in enumerate(array):
                    if not isinstance(item, (int, float)):
                        self._add_issue(
                            file, 1, None,
                            f"Combatant '{combatant_id}'.movement_speed[{i}] must be a number",
                            Severity.ERROR
                        )
                        valid = False
                    elif item <= 0:
                        self._add_issue(
                            file, 1, None,
                            f"Combatant '{combatant_id}'.movement_speed[{i}] must be > 0, got {item}",
                            Severity.WARN
                        )

        if "costs" in data:
            array: list[Any] = data["costs"]
            if not isinstance(array, list):
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.costs must be an array",
                    Severity.ERROR
                )
                valid = False
            else:
                for i, item in enumerate(array):
                    line: int = content[:].count('\n') + 1

                    if not isinstance(item, dict):
                        self._add_issue(
                            file, line, None,
                            f"costs[{i}] must be an object",
                            Severity.ERROR
                        )
                        valid = False
                        continue

                    item_keys: set[str] = set(item.keys())
                    unexpected: set[str] = item_keys - set(VALID_RESOURCE_TYPES)
                    if unexpected:
                        self._add_issue(
                            file, line, None,
                            f"costs[{i}] has unexpected keys: {sorted(unexpected)}",
                            Severity.WARN
                        )

                    if not item:
                        self._add_issue(
                            file, line, None,
                            f"costs[{i}] must not be empty",
                            Severity.ERROR
                        )
                        valid = False
                        continue

                    for key, value in item.items():
                        if not isinstance(value, (int, float)):
                            self._add_issue(
                                file, line, None,
                                f"costs[{i}].{key} must be a number, got {type(value).__name__}",
                                Severity.ERROR
                            )
                            valid = False

        return valid

    def _combatant_id_is_valid(self, combatant_id: str, line: int, file: Path) -> bool:
        """Check if combatant ID follows naming conventions."""
        if not combatant_id:
            self._add_issue(file, line, None, "Empty combatant ID", Severity.ERROR)
            return False

        if not combatant_id.replace("_", "").replace("-", "").isalnum():
            self._add_issue(
                file, line, None,
                f"Combatant ID '{combatant_id}' contains invalid characters",
                Severity.ERROR
            )
            return False

        if not combatant_id[0].isalpha():
            self._add_issue(
                file, line, None,
                f"Combatant ID '{combatant_id}' should start with a letter",
                Severity.WARN
            )

        if not combatant_id.islower():
            self._add_issue(
                file, line, None,
                f"Combatant ID '{combatant_id}' should be lowercase",
                Severity.WARN
            )

        return True

    def validate_combatants(self, file: Path, content: str, is_player: bool) -> bool:
        """Validate player_combatants.json or enemy_combatants.json."""
        data: JsonDataType = self._validate_json(content, file)
        if data is None:
            return False

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with combatant definitions", Severity.ERROR)
            return False

        if not data:
            self._add_issue(
                file, 1, None,
                f"Empty {'player' if is_player else 'enemy'} combatants file",
                Severity.WARN
            )

        valid: bool = True
        seen_ids: set[str] = set()

        for combatant_id, combatant_data in data.items():
            line: int = content[:].count('\n') + 1

            if combatant_id in seen_ids:
                self._add_issue(
                    file, line, None,
                    f"Duplicate combatant ID: '{combatant_id}'",
                    Severity.ERROR
                )
                valid = False
                continue

            seen_ids.add(combatant_id)
            self._combatant_id_is_valid(combatant_id, line, file)

            if not isinstance(combatant_data, dict):
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}' must be an object",
                    Severity.ERROR
                )
                valid = False
                continue

            if not self._validate_combatant(file, content, combatant_id, combatant_data):
                valid = False

        return valid

    def _building_id_is_valid(self, building_id: str, line: int, file: Path) -> bool:
        """Check if building ID follows naming conventions."""
        if not building_id:
            self._add_issue(file, line, None, "Empty building ID", Severity.ERROR)
            return False

        if not building_id.replace("_", "").replace("-", "").isalnum():
            self._add_issue(
                file, line, None,
                f"Building ID '{building_id}' contains invalid characters",
                Severity.ERROR
            )
            return False

        if not building_id[0].isalpha():
            self._add_issue(
                file, line, None,
                f"Building ID '{building_id}' should start with a letter",
                Severity.WARN
            )

        if not building_id.islower():
            self._add_issue(
                file, line, None,
                f"Building ID '{building_id}' should be lowercase, use snake_case",
                Severity.WARN
            )

        return True

    def _validate_resource_production(
        self,
        file: Path,
        content: str,
        building_id: str,
        data: dict[str, Any],
        resource_name: str
    ) -> None:
        """Validate a resource production object."""
        if resource_name not in data:
            return

        prod: Any = data[resource_name]
        line: int = content[:].count('\n') + 1

        if not isinstance(prod, dict):
            self._add_issue(
                file, line, None,
                f"Building '{building_id}'.{resource_name} must be an object",
                Severity.ERROR
            )
            return

        expected_keys: set[str] = {"amount", "amount_multiplier", "periodicity", "periodicity_multiplier"}
        actual_keys: set[str] = set(prod.keys())
        unexpected: set[str] = actual_keys - expected_keys

        if unexpected:
            self._add_issue(
                file, line, None,
                f"Building '{building_id}'.{resource_name} has unexpected keys: {sorted(unexpected)}",
                Severity.WARN
            )

        for key in expected_keys:
            if key in prod:
                value: Any = prod[key]
                if not isinstance(value, (int, float)):
                    self._add_issue(
                        file, line, None,
                        f"Building '{building_id}'.{resource_name}.{key} must be a number",
                        Severity.ERROR
                    )

    def _validate_number_array(
        self,
        file: Path,
        content: str,
        building_id: str,
        data: dict[str, Any],
        field_name: str,
        allow_negative: bool = False
    ) -> None:
        """Validate a number array field."""
        if field_name not in data:
            return

        array: Any = data[field_name]
        line: int = content[:].count('\n') + 1

        if not isinstance(array, list):
            self._add_issue(
                file, line, None,
                f"Building '{building_id}'.{field_name} must be an array",
                Severity.ERROR
            )
            return

        for i, item in enumerate(array):
            if not isinstance(item, (int, float)):
                self._add_issue(
                    file, line, None,
                    f"Building '{building_id}'.{field_name}[{i}] must be a number",
                    Severity.ERROR
                )
            elif not allow_negative and item < 0:
                self._add_issue(
                    file, line, None,
                    f"Building '{building_id}'.{field_name}[{i}] must be >= 0, got {item}",
                    Severity.WARN
                )

    def _validate_image_array(
        self,
        file: Path,
        content: str,
        building_id: str,
        data: dict[str, Any],
        field_name: str,
        required: bool = False
    ) -> None:
        """Validate an image array field."""
        if field_name not in data:
            if required:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}' is missing required field '{field_name}'",
                    Severity.ERROR
                )
            return

        array: Any = data[field_name]
        line: int = content[:].count('\n') + 1

        if not isinstance(array, list):
            self._add_issue(
                file, line, None,
                f"Building '{building_id}'.{field_name} must be an array",
                Severity.ERROR
            )
            return

        if required and not array:
            self._add_issue(
                file, line, None,
                f"Building '{building_id}'.{field_name} must not be empty",
                Severity.ERROR
            )

        for i, item in enumerate(array):
            if not isinstance(item, str):
                self._add_issue(
                    file, line, None,
                    f"Building '{building_id}'.{field_name}[{i}] must be a string",
                    Severity.ERROR
                )
            elif not item:
                self._add_issue(
                    file, line, None,
                    f"Building '{building_id}'.{field_name}[{i}] must not be empty",
                    Severity.ERROR
                )

    def _validate_building(
        self,
        file: Path,
        content: str,
        building_id: str,
        data: dict[str, Any]
    ) -> None:
        """Validate a single building definition."""
        required_fields: list[str] = ["width", "height", "max_level", "construction_times", "construction_images", "idle_images"]

        for field in required_fields:
            if field not in data:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}' is missing required field '{field}'",
                    Severity.ERROR
                )

        if "width" in data:
            width: Any = data["width"]
            if not isinstance(width, int) or width < 1:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.width must be an integer >= 1, got {width}",
                    Severity.ERROR
                )

        if "height" in data:
            height: Any = data["height"]
            if not isinstance(height, int) or height < 1:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.height must be an integer >= 1, got {height}",
                    Severity.ERROR
                )

        if "max_level" in data:
            max_level: Any = data["max_level"]
            if not isinstance(max_level, int) or max_level < 1:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.max_level must be an integer >= 1, got {max_level}",
                    Severity.ERROR
                )

        if "can_build_outside_wall" in data:
            if not isinstance(data["can_build_outside_wall"], bool):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.can_build_outside_wall must be a boolean",
                    Severity.ERROR
                )

        self._validate_number_array(file, content, building_id, data, "construction_times", allow_negative=False)
        self._validate_number_array(file, content, building_id, data, "gold_cost")
        self._validate_number_array(file, content, building_id, data, "grain_cost")
        self._validate_number_array(file, content, building_id, data, "wood_cost")
        self._validate_number_array(file, content, building_id, data, "steel_cost")
        self._validate_number_array(file, content, building_id, data, "bronze_cost")
        self._validate_number_array(file, content, building_id, data, "stone_cost")
        self._validate_number_array(file, content, building_id, data, "leather_cost")
        self._validate_number_array(file, content, building_id, data, "mana_cost")

        self._validate_image_array(file, content, building_id, data, "construction_images", required=True)
        self._validate_image_array(file, content, building_id, data, "idle_images", required=True)
        self._validate_image_array(file, content, building_id, data, "harvest_images", required=False)

        for resource in VALID_BUILDING_PRODUCTION_FIELDS:
            self._validate_resource_production(file, content, building_id, data, resource)

    def validate_buildings(self, file: Path, content: str) -> bool:
        """Validate fiefdom_building_types.json."""
        data: JsonDataType = self._validate_json(content, file)
        if data is None:
            return False

        if not isinstance(data, list):
            self._add_issue(file, 1, None, "Expected array of building definitions", Severity.ERROR)
            return False

        if not data:
            self._add_issue(file, 1, None, "Buildings array is empty", Severity.WARN)

        valid: bool = True
        seen_ids: set[str] = set()

        for building_entry in data:
            if not isinstance(building_entry, dict):
                self._add_issue(file, 1, None, "Each building entry must be an object", Severity.ERROR)
                continue

            if len(building_entry) != 1:
                self._add_issue(
                    file, 1, None,
                    "Each building entry should have exactly one key (building ID)",
                    Severity.WARN
                )

            for building_id, building_data in building_entry.items():
                line: int = content[:].count('\n') + 1

                if building_id in seen_ids:
                    self._add_issue(
                        file, line, None,
                        f"Duplicate building ID: '{building_id}'",
                        Severity.ERROR
                    )
                    valid = False
                    continue

                seen_ids.add(building_id)
                self._building_id_is_valid(building_id, line, file)

                if not isinstance(building_data, dict):
                    self._add_issue(
                        file, line, None,
                        f"Building '{building_id}' must be an object",
                        Severity.ERROR
                    )
                    valid = False
                    continue

                self._validate_building(file, content, building_id, building_data)

        return valid

    def validate_all(
        self,
        config_dir: Path,
        show_warnings: bool = True
    ) -> tuple[list[LinterIssue], list[LinterIssue]]:
        """Validate all config files in the given directory."""
        errors: list[LinterIssue] = []
        warnings: list[LinterIssue] = []
        self.validated_files = []

        damage_types_file: Path = config_dir / "damage_types.json"
        player_combatants_file: Path = config_dir / "player_combatants.json"
        enemy_combatants_file: Path = config_dir / "enemy_combatants.json"
        buildings_file: Path = config_dir / "fiefdom_building_types.json"

        files_to_validate: list[tuple[Path, str, bool]] = [
            (damage_types_file, "damage_types.json", True),
            (player_combatants_file, "player_combatants.json", False),
            (enemy_combatants_file, "enemy_combatants.json", False),
            (buildings_file, "fiefdom_building_types.json", False),
        ]

        for file, name, is_player in files_to_validate:
            if not file.exists():
                self._add_issue(
                    file, 1, None,
                    f"Config file not found",
                    Severity.ERROR
                )
                self.all_files_present = False
                continue

            try:
                content: str = file.read_text(encoding="utf-8")
            except Exception as e:
                self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
                continue

            if file == damage_types_file:
                self.validate_damage_types(file, content)
            elif file == player_combatants_file:
                self.validate_combatants(file, content, is_player=True)
            elif file == enemy_combatants_file:
                self.validate_combatants(file, content, is_player=False)
            elif file == buildings_file:
                self.validate_buildings(file, content)

            self.validated_files.append(file)

        for issue in self.issues:
            if issue.severity == Severity.ERROR:
                errors.append(issue)
            else:
                warnings.append(issue)

        if not show_warnings:
            warnings = []

        return errors, warnings


def parse_args() -> argparse.Namespace:
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        prog="check_configs.py",
        description="Validate config files against schema rules",
        epilog="""
Examples:
  ./tools/check_configs.py              # Show errors and warnings
  ./tools/check_configs.py --no-warnings  # Show errors only
  ./tools/check_configs.py -h           # Show this help
  ./tools/check_configs.py --verbose    # Show validated files on success

Exit codes:
  0: All configs valid (no errors)
  1: Errors found
        """,
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--no-warnings", "-w",
        action="store_true",
        help="Suppress warnings, only show errors"
    )
    parser.add_argument(
        "--verbose", "-v",
        action="store_true",
        help="Show list of validated files on success"
    )
    parser.add_argument(
        "--config-dir", "-c",
        type=Path,
        default=Path("server/config"),
        help="Directory containing config files (default: server/config)"
    )
    return parser.parse_args()


def main() -> Literal[0, 1]:
    """Main entry point."""
    args: argparse.Namespace = parse_args()

    config_dir: Path = args.config_dir
    if not config_dir.is_absolute():
        project_root: Path = Path(__file__).parent.parent
        config_dir = project_root / config_dir

    if not config_dir.exists():
        print(f"Error: Config directory not found: {config_dir}", file=sys.stderr)
        return 1

    validator: ConfigValidator = ConfigValidator()
    errors, warnings = validator.validate_all(config_dir, show_warnings=not args.no_warnings)

    has_output: bool = False

    for warning in warnings:
        print(warning.format(show_column=False))
        has_output = True

    for error in errors:
        print(error.format(show_column=False), file=sys.stderr)
        has_output = True

    if has_output:
        print("", file=sys.stderr)

    error_count: int = len(errors)
    warning_count: int = len(warnings) if not args.no_warnings else 0

    if error_count > 0:
        print(f"Summary: {error_count} error(s), {warning_count} warning(s)", file=sys.stderr)
        return 1

    if warning_count > 0:
        print(f"Summary: {error_count} error(s), {warning_count} warning(s)", file=sys.stderr)

    if not validator.all_files_present:
        print(f"Summary: {error_count} error(s), {warning_count} warning(s)", file=sys.stderr)
        return 1

    print("All configs present and valid ✓")

    if args.verbose:
        for file in validator.validated_files:
            print(f"  - {file.name}")

    return 0


if __name__ == "__main__":
    sys.exit(main())