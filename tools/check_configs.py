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
import os
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


# Type definitions for hero configs
class HeroEquipmentTypedDict(TypedDict, total=False):
    """Equipment slots configuration."""
    slots: list[int]
    max: int


class HeroSkillTypedDict(TypedDict, total=False):
    """Hero skill definition."""
    name: str
    damage: list[int]
    damage_max: int
    defense: list[int]
    defense_max: int
    healing: list[int]
    healing_max: int


class HeroStatusEffectTypedDict(TypedDict, total=False):
    """Hero status effect definition."""
    name: str
    type: str
    effect: list[int]
    max: int


class HeroTypedDict(TypedDict, total=False):
    """Hero definition."""
    name: str
    max_level: int
    equipment: dict[str, HeroEquipmentTypedDict]
    skills: dict[str, HeroSkillTypedDict]
    status_effects: dict[str, HeroStatusEffectTypedDict]


# Type definitions for fiefdom officials
class OfficialStatsTypedDict(TypedDict, total=False):
    """Stats object for an official."""
    intelligence: list[int]
    intelligence_max: int
    charisma: list[int]
    charisma_max: int
    wisdom: list[int]
    wisdom_max: int
    diligence: list[int]
    diligence_max: int


class FiefdomOfficialTypedDict(TypedDict, total=False):
    """Fiefdom official template definition."""
    name: str
    max_level: int
    roles: list[str]
    stats: OfficialStatsTypedDict
    portrait_id: int
    description: str


# Type aliases for parsed JSON data structures
DamageTypesJson: TypeAlias = list[str]
CombatantsJson: TypeAlias = dict[str, dict[str, Any]]
BuildingsJson: TypeAlias = list[dict[str, Any]]
HeroesJson: TypeAlias = dict[str, dict[str, Any]]
FiefdomOfficialsJson: TypeAlias = dict[str, dict[str, Any]]
JsonDataType: TypeAlias = DamageTypesJson | CombatantsJson | BuildingsJson | HeroesJson | FiefdomOfficialsJson | None


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
VALID_STATUS_EFFECT_TYPES: Final[set[str]] = {"stun", "mute", "confuse"}
VALID_HERO_SKILL_FIELDS: Final[set[str]] = {"damage", "defense", "healing"}
VALID_HERO_SKILL_MAX_FIELDS: Final[set[str]] = {"damage_max", "defense_max", "healing_max"}
VALID_OFFICIAL_ROLES: Final[set[str]] = {
    "bailiff", "wizard", "architect", "steward", "reeve", "beadle", "constable", "forester"
}
VALID_OFFICIAL_STAT_FIELDS: Final[set[str]] = {
    "intelligence", "charisma", "wisdom", "diligence"
}
VALID_OFFICIAL_STAT_MAX_FIELDS: Final[set[str]] = {
    "intelligence_max", "charisma_max", "wisdom_max", "diligence_max"
}


class ConfigValidator:
    """Main validator class for all config files."""

    def __init__(self) -> None:
        self.damage_types: list[str] = []
        self.issues: list[LinterIssue] = []
        self.validated_files: list[Path] = []
        self.all_files_present: bool = True
        # Track IDs for image validation
        self.validated_combatant_ids: set[str] = set()
        self.validated_building_ids: set[str] = set()
        self.validated_hero_ids: set[str] = set()
        self.hero_skills: dict[str, set[str]] = {}  # hero_id -> skill_ids
        self.validated_portrait_ids: set[int] = set()  # portrait_id -> image directory
        self.validated_official_ids: set[str] = set()

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

        self._validate_visual_description(file, combatant_id, data, "Combatant")

        return valid

    def _validate_visual_description(
        self,
        file: Path,
        entity_id: str,
        data: dict[str, Any],
        entity_type: str
    ) -> None:
        """Validate optional visual_description field for combatants, buildings, and heroes."""
        if "visual_description" in data:
            desc: Any = data["visual_description"]
            if not isinstance(desc, str):
                self._add_issue(
                    file, 1, None,
                    f"{entity_type} '{entity_id}'.visual_description must be a string",
                    Severity.WARN
                )
            elif len(desc) > 500:
                self._add_issue(
                    file, 1, None,
                    f"{entity_type} '{entity_id}'.visual_description too long (max 4096 chars, got {len(desc)})",
                    Severity.WARN
                )

    def _validate_portrait_description(
        self,
        file: Path,
        official_id: str,
        data: dict[str, Any]
    ) -> None:
        """Validate optional portrait_description field for officials."""
        if "portrait_description" in data:
            desc: Any = data["portrait_description"]
            if not isinstance(desc, str):
                self._add_issue(
                    file, 1, None,
                    f"Official '{official_id}'.portrait_description must be a string",
                    Severity.WARN
                )
            elif len(desc) > 500:
                self._add_issue(
                    file, 1, None,
                    f"Official '{official_id}'.portrait_description too long (max 4096 chars, got {len(desc)})",
                    Severity.WARN
                )

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

        # Track IDs for image validation
        self.validated_combatant_ids.update(seen_ids)

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
        required_fields: list[str] = ["width", "height", "max_level", "construction_times"]

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

        for resource in VALID_BUILDING_PRODUCTION_FIELDS:
            self._validate_resource_production(file, content, building_id, data, resource)

        self._validate_visual_description(file, building_id, data, "Building")

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

        # Track IDs for image validation
        self.validated_building_ids.update(seen_ids)

        return valid

    def _hero_id_is_valid(self, hero_id: str, line: int, file: Path) -> bool:
        """Check if hero ID follows naming conventions."""
        if not hero_id:
            self._add_issue(file, line, None, "Empty hero ID", Severity.ERROR)
            return False

        if not hero_id.replace("_", "").replace("-", "").isalnum():
            self._add_issue(
                file, line, None,
                f"Hero ID '{hero_id}' contains invalid characters",
                Severity.ERROR
            )
            return False

        if not hero_id[0].isalpha():
            self._add_issue(
                file, line, None,
                f"Hero ID '{hero_id}' should start with a letter",
                Severity.WARN
            )

        if not hero_id.islower():
            self._add_issue(
                file, line, None,
                f"Hero ID '{hero_id}' should be lowercase",
                Severity.WARN
            )

        return True

    def _validate_equipment_slots(
        self,
        file: Path,
        content: str,
        hero_id: str,
        equipment_type: str,
        data: dict[str, Any]
    ) -> bool:
        """Validate an equipment slots configuration."""
        valid: bool = True

        if "slots" not in data:
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}'.equipment.{equipment_type} is missing required field 'slots'",
                Severity.ERROR
            )
            valid = False
        else:
            slots: Any = data["slots"]
            line: int = content[:].count('\n') + 1

            if not isinstance(slots, list):
                self._add_issue(
                    file, line, None,
                    f"Hero '{hero_id}'.equipment.{equipment_type}.slots must be an array",
                    Severity.ERROR
                )
                valid = False
            else:
                for i, slot_val in enumerate(slots):
                    if not isinstance(slot_val, int):
                        self._add_issue(
                            file, line, None,
                            f"Hero '{hero_id}'.equipment.{equipment_type}.slots[{i}] must be an integer",
                            Severity.ERROR
                        )
                        valid = False

        if "max" not in data:
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}'.equipment.{equipment_type} is missing required field 'max'",
                Severity.ERROR
            )
            valid = False
        else:
            max_val: Any = data["max"]
            if not isinstance(max_val, int):
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.equipment.{equipment_type}.max must be an integer",
                    Severity.ERROR
                )
                valid = False
            elif max_val < 0:
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.equipment.{equipment_type}.max must be >= 0, got {max_val}",
                    Severity.ERROR
                )
                valid = False

        return valid

    def _validate_skill_stats(
        self,
        file: Path,
        content: str,
        hero_id: str,
        skill_id: str,
        stat_name: str,
        data: dict[str, Any]
    ) -> bool:
        """Validate a skill stat array (damage, defense, healing)."""
        if stat_name not in data:
            return True

        array: Any = data[stat_name]
        line: int = content[:].count('\n') + 1
        valid: bool = True

        if not isinstance(array, list):
            self._add_issue(
                file, line, None,
                f"Hero '{hero_id}'.skills.{skill_id}.{stat_name} must be an array",
                Severity.ERROR
            )
            return False

        for i, val in enumerate(array):
            if not isinstance(val, int):
                self._add_issue(
                    file, line, None,
                    f"Hero '{hero_id}'.skills.{skill_id}.{stat_name}[{i}] must be an integer",
                    Severity.ERROR
                )
                valid = False

        return valid

    def _validate_skill_max(
        self,
        file: Path,
        content: str,
        hero_id: str,
        skill_id: str,
        stat_name: str,
        data: dict[str, Any]
    ) -> None:
        """Validate a skill max field (damage_max, defense_max, healing_max)."""
        max_field: str = f"{stat_name}_max"
        if max_field not in data:
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}'.skills.{skill_id} with '{stat_name}' is missing required field '{max_field}'",
                Severity.ERROR
            )
            return

        max_val: Any = data[max_field]
        if not isinstance(max_val, int):
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}'.skills.{skill_id}.{max_field} must be an integer",
                Severity.ERROR
            )
        elif max_val < 0:
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}'.skills.{skill_id}.{max_field} must be >= 0, got {max_val}",
                Severity.ERROR
            )

    def _validate_skill(
        self,
        file: Path,
        content: str,
        hero_id: str,
        skill_id: str,
        data: dict[str, Any]
    ) -> bool:
        """Validate a single skill definition."""
        valid: bool = True

        if "name" not in data:
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}'.skills.{skill_id} is missing required field 'name'",
                Severity.ERROR
            )
            valid = False
        else:
            name: Any = data["name"]
            if not isinstance(name, str) or not name:
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.skills.{skill_id}.name must be a non-empty string",
                    Severity.ERROR
                )
                valid = False

        for stat in VALID_HERO_SKILL_FIELDS:
            if not self._validate_skill_stats(file, content, hero_id, skill_id, stat, data):
                valid = False

        for stat in VALID_HERO_SKILL_FIELDS:
            if stat in data:
                self._validate_skill_max(file, content, hero_id, skill_id, stat, data)

        return valid

    def _validate_status_effect(
        self,
        file: Path,
        content: str,
        hero_id: str,
        effect_id: str,
        data: dict[str, Any]
    ) -> bool:
        """Validate a single status effect definition."""
        valid: bool = True

        if "name" not in data:
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}'.status_effects.{effect_id} is missing required field 'name'",
                Severity.ERROR
            )
            valid = False
        else:
            name: Any = data["name"]
            if not isinstance(name, str) or not name:
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.status_effects.{effect_id}.name must be a non-empty string",
                    Severity.ERROR
                )
                valid = False

        if "type" not in data:
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}'.status_effects.{effect_id} is missing required field 'type'",
                Severity.ERROR
            )
            valid = False
        else:
            effect_type: Any = data["type"]
            if not isinstance(effect_type, str):
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.status_effects.{effect_id}.type must be a string",
                    Severity.ERROR
                )
                valid = False
            elif effect_type not in VALID_STATUS_EFFECT_TYPES:
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.status_effects.{effect_id}.type must be one of {sorted(VALID_STATUS_EFFECT_TYPES)}, got '{effect_type}'",
                    Severity.ERROR
                )
                valid = False

        if "effect" not in data:
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}'.status_effects.{effect_id} is missing required field 'effect'",
                Severity.ERROR
            )
            valid = False
        else:
            effect_array: Any = data["effect"]
            line: int = content[:].count('\n') + 1

            if not isinstance(effect_array, list):
                self._add_issue(
                    file, line, None,
                    f"Hero '{hero_id}'.status_effects.{effect_id}.effect must be an array",
                    Severity.ERROR
                )
                valid = False
            else:
                for i, val in enumerate(effect_array):
                    if not isinstance(val, int):
                        self._add_issue(
                            file, line, None,
                            f"Hero '{hero_id}'.status_effects.{effect_id}.effect[{i}] must be an integer",
                            Severity.ERROR
                        )
                        valid = False

        if "max" not in data:
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}'.status_effects.{effect_id} is missing required field 'max'",
                Severity.ERROR
            )
            valid = False
        else:
            max_val: Any = data["max"]
            if not isinstance(max_val, int):
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.status_effects.{effect_id}.max must be an integer",
                    Severity.ERROR
                )
                valid = False
            elif max_val < 0:
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.status_effects.{effect_id}.max must be >= 0, got {max_val}",
                    Severity.ERROR
                )
                valid = False

        return valid

    def _validate_hero(
        self,
        file: Path,
        content: str,
        hero_id: str,
        data: dict[str, Any]
    ) -> bool:
        """Validate a single hero definition."""
        valid: bool = True

        if "name" not in data:
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}' is missing required field 'name'",
                Severity.ERROR
            )
            valid = False
        else:
            name: Any = data["name"]
            if not isinstance(name, str) or not name:
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.name must be a non-empty string",
                    Severity.ERROR
                )
                valid = False

        if "max_level" not in data:
            self._add_issue(
                file, 1, None,
                f"Hero '{hero_id}' is missing required field 'max_level'",
                Severity.ERROR
            )
            valid = False
        else:
            max_level: Any = data["max_level"]
            if not isinstance(max_level, int):
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.max_level must be an integer, got {type(max_level).__name__}",
                    Severity.ERROR
                )
                valid = False
            elif max_level < 1:
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.max_level must be >= 1, got {max_level}",
                    Severity.ERROR
                )
                valid = False

        if "equipment" in data:
            equipment: Any = data["equipment"]
            if not isinstance(equipment, dict):
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.equipment must be an object",
                    Severity.ERROR
                )
            else:
                for equip_type, equip_data in equipment.items():
                    if isinstance(equip_data, dict):
                        if not self._validate_equipment_slots(file, content, hero_id, equip_type, equip_data):
                            valid = False

        if "skills" in data:
            skills: Any = data["skills"]
            if not isinstance(skills, dict):
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.skills must be an object",
                    Severity.ERROR
                )
            else:
                for skill_id, skill_data in skills.items():
                    if isinstance(skill_data, dict):
                        if not self._validate_skill(file, content, hero_id, skill_id, skill_data):
                            valid = False

        if "status_effects" in data:
            effects: Any = data["status_effects"]
            if not isinstance(effects, dict):
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.status_effects must be an object",
                    Severity.ERROR
                )
            else:
                for effect_id, effect_data in effects.items():
                    if isinstance(effect_data, dict):
                        if not self._validate_status_effect(file, content, hero_id, effect_id, effect_data):
                            valid = False

        self._validate_visual_description(file, hero_id, data, "Hero")

        return valid

    def validate_heroes(self, file: Path, content: str) -> bool:
        """Validate heroes.json."""
        data: JsonDataType = self._validate_json(content, file)
        if data is None:
            return False

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with hero definitions", Severity.ERROR)
            return False

        if not data:
            self._add_issue(file, 1, None, "Empty heroes file", Severity.WARN)

        valid: bool = True
        seen_ids: set[str] = set()

        for hero_id, hero_data in data.items():
            line: int = content[:].count('\n') + 1

            if hero_id in seen_ids:
                self._add_issue(
                    file, line, None,
                    f"Duplicate hero ID: '{hero_id}'",
                    Severity.ERROR
                )
                valid = False
                continue

            seen_ids.add(hero_id)
            self._hero_id_is_valid(hero_id, line, file)

            if not isinstance(hero_data, dict):
                self._add_issue(
                    file, line, None,
                    f"Hero '{hero_id}' must be an object",
                    Severity.ERROR
                )
                valid = False
                continue

            # Track skills for this hero
            if "skills" in hero_data and isinstance(hero_data["skills"], dict):
                skill_ids: set[str] = set(hero_data["skills"].keys())
                self.hero_skills[hero_id] = skill_ids

            if not self._validate_hero(file, content, hero_id, hero_data):
                valid = False

        # Track IDs for image validation
        self.validated_hero_ids.update(seen_ids)

        return valid

    def _official_id_is_valid(self, official_id: str, line: int, file: Path) -> bool:
        """Check if official ID follows naming conventions."""
        if not official_id:
            self._add_issue(file, line, None, "Empty official ID", Severity.ERROR)
            return False

        if not official_id.replace("_", "").replace("-", "").isalnum():
            self._add_issue(
                file, line, None,
                f"Official ID '{official_id}' contains invalid characters",
                Severity.ERROR
            )
            return False

        if not official_id[0].isalpha():
            self._add_issue(
                file, line, None,
                f"Official ID '{official_id}' should start with a letter",
                Severity.WARN
            )

        if not official_id.islower():
            self._add_issue(
                file, line, None,
                f"Official ID '{official_id}' should be lowercase",
                Severity.WARN
            )

        return True

    def _validate_stat_array(
        self,
        file: Path,
        content: str,
        official_id: str,
        stats_obj: dict[str, Any],
        stat_name: str
    ) -> bool:
        """Validate a stat array field."""
        if stat_name not in stats_obj:
            return True

        array: Any = stats_obj[stat_name]
        line: int = content[:].count('\n') + 1
        valid: bool = True

        if not isinstance(array, list):
            self._add_issue(
                file, line, None,
                f"Official '{official_id}'.stats.{stat_name} must be an array",
                Severity.ERROR
            )
            return False

        for i, val in enumerate(array):
            if not isinstance(val, int):
                self._add_issue(
                    file, line, None,
                    f"Official '{official_id}'.stats.{stat_name}[{i}] must be an integer",
                    Severity.ERROR
                )
                valid = False
            elif val < 0 or val > 255:
                self._add_issue(
                    file, line, None,
                    f"Official '{official_id}'.stats.{stat_name}[{i}] must be 0-255, got {val}",
                    Severity.ERROR
                )
                valid = False

        return valid

    def _validate_stat_max(
        self,
        file: Path,
        content: str,
        official_id: str,
        stats_obj: dict[str, Any],
        stat_name: str
    ) -> None:
        """Validate a stat max field."""
        max_field: str = f"{stat_name}_max"
        if max_field not in stats_obj:
            self._add_issue(
                file, 1, None,
                f"Official '{official_id}' with '{stat_name}' is missing required field 'stats.{max_field}'",
                Severity.ERROR
            )
            return

        max_val: Any = stats_obj[max_field]
        if not isinstance(max_val, int):
            self._add_issue(
                file, 1, None,
                f"Official '{official_id}'.stats.{max_field} must be an integer",
                Severity.ERROR
            )
        elif max_val < 0:
            self._add_issue(
                file, 1, None,
                f"Official '{official_id}'.stats.{max_field} must be >= 0, got {max_val}",
                Severity.ERROR
            )

    def _validate_official(
        self,
        file: Path,
        content: str,
        official_id: str,
        data: dict[str, Any]
    ) -> bool:
        """Validate a single official definition."""
        valid: bool = True

        if "name" not in data:
            self._add_issue(
                file, 1, None,
                f"Official '{official_id}' is missing required field 'name'",
                Severity.ERROR
            )
            valid = False
        else:
            name: Any = data["name"]
            if not isinstance(name, str) or not name:
                self._add_issue(
                    file, 1, None,
                    f"Official '{official_id}'.name must be a non-empty string",
                    Severity.ERROR
                )
                valid = False

        if "max_level" not in data:
            self._add_issue(
                file, 1, None,
                f"Official '{official_id}' is missing required field 'max_level'",
                Severity.ERROR
            )
            valid = False
        else:
            max_level: Any = data["max_level"]
            if not isinstance(max_level, int):
                self._add_issue(
                    file, 1, None,
                    f"Official '{official_id}'.max_level must be an integer, got {type(max_level).__name__}",
                    Severity.ERROR
                )
                valid = False
            elif max_level < 1:
                self._add_issue(
                    file, 1, None,
                    f"Official '{official_id}'.max_level must be >= 1, got {max_level}",
                    Severity.ERROR
                )
                valid = False

        if "roles" not in data:
            self._add_issue(
                file, 1, None,
                f"Official '{official_id}' is missing required field 'roles'",
                Severity.ERROR
            )
            valid = False
        else:
            roles: Any = data["roles"]
            if not isinstance(roles, list):
                self._add_issue(
                    file, 1, None,
                    f"Official '{official_id}'.roles must be an array",
                    Severity.ERROR
                )
                valid = False
            elif len(roles) == 0:
                self._add_issue(
                    file, 1, None,
                    f"Official '{official_id}'.roles must have at least one role",
                    Severity.ERROR
                )
                valid = False
            else:
                for i, role in enumerate(roles):
                    if not isinstance(role, str):
                        self._add_issue(
                            file, 1, None,
                            f"Official '{official_id}'.roles[{i}] must be a string",
                            Severity.ERROR
                        )
                        valid = False
                    elif role not in VALID_OFFICIAL_ROLES:
                        self._add_issue(
                            file, 1, None,
                            f"Official '{official_id}'.roles[{i}] must be one of {sorted(VALID_OFFICIAL_ROLES)}, got '{role}'",
                            Severity.ERROR
                        )
                        valid = False

        if "stats" not in data:
            self._add_issue(
                file, 1, None,
                f"Official '{official_id}' is missing required field 'stats'",
                Severity.ERROR
            )
            valid = False
        else:
            stats: Any = data["stats"]
            if not isinstance(stats, dict):
                self._add_issue(
                    file, 1, None,
                    f"Official '{official_id}'.stats must be an object",
                    Severity.ERROR
                )
                valid = False
            else:
                # Validate each stat array
                for stat in VALID_OFFICIAL_STAT_FIELDS:
                    if not self._validate_stat_array(file, content, official_id, stats, stat):
                        valid = False

                # Validate each stat max
                for stat in VALID_OFFICIAL_STAT_FIELDS:
                    if stat in stats:
                        self._validate_stat_max(file, content, official_id, stats, stat)

        if "portrait_id" not in data:
            self._add_issue(
                file, 1, None,
                f"Official '{official_id}' is missing required field 'portrait_id'",
                Severity.ERROR
            )
            valid = False
        else:
            portrait_id: Any = data["portrait_id"]
            if not isinstance(portrait_id, int):
                self._add_issue(
                    file, 1, None,
                    f"Official '{official_id}'.portrait_id must be an integer",
                    Severity.ERROR
                )
                valid = False
            elif portrait_id < 1:
                self._add_issue(
                    file, 1, None,
                    f"Official '{official_id}'.portrait_id must be >= 1, got {portrait_id}",
                    Severity.ERROR
                )
                valid = False
            else:
                self.validated_portrait_ids.add(portrait_id)

        self._validate_portrait_description(file, official_id, data)

        return valid

    def validate_fiefdom_officials(self, file: Path, content: str) -> bool:
        """Validate fiefdom_officials.json."""
        data: JsonDataType = self._validate_json(content, file)
        if data is None:
            return False

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with official definitions", Severity.ERROR)
            return False

        if not data:
            self._add_issue(file, 1, None, "Empty fiefdom officials file", Severity.WARN)

        valid: bool = True
        seen_ids: set[str] = set()

        for official_id, official_data in data.items():
            line: int = content[:].count('\n') + 1

            if official_id in seen_ids:
                self._add_issue(
                    file, line, None,
                    f"Duplicate official ID: '{official_id}'",
                    Severity.ERROR
                )
                valid = False
                continue

            seen_ids.add(official_id)
            self._official_id_is_valid(official_id, line, file)

            if not isinstance(official_data, dict):
                self._add_issue(
                    file, line, None,
                    f"Official '{official_id}' must be an object",
                    Severity.ERROR
                )
                valid = False
                continue

            if not self._validate_official(file, content, official_id, official_data):
                valid = False

        # Track IDs for validation
        self.validated_official_ids.update(seen_ids)

        return valid

    def _validate_image_file_numbering(self, dir_path: Path) -> list[str]:
        """Validate that files in directory follow numeric naming convention."""
        valid_extensions = {".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp"}
        invalid_files: list[str] = []

        if not dir_path.exists():
            return invalid_files

        for file_path in dir_path.iterdir():
            if file_path.is_file():
                ext = file_path.suffix.lower()
                name = file_path.stem
                if ext not in valid_extensions:
                    invalid_files.append(f"{file_path.name} (invalid extension)")
                elif not name.isdigit():
                    invalid_files.append(f"{file_path.name} (non-numeric name)")
                elif int(name) < 1:
                    invalid_files.append(f"{file_path.name} (must be >= 1)")

        return invalid_files

    def _get_expected_image_dirs(self) -> tuple[set[tuple], set[tuple]]:
        """Return sets of (type, id, subtype) tuples for expected and optional directories."""
        required: set[tuple] = set()
        optional: set[tuple] = set()

        # Combatants: idle, attack, defend, die (all required)
        for combatant_id in self.validated_combatant_ids:
            required.add(("combatants", combatant_id, "idle"))
            required.add(("combatants", combatant_id, "attack"))
            required.add(("combatants", combatant_id, "defend"))
            required.add(("combatants", combatant_id, "die"))

        # Buildings: construction, idle (required), harvest (optional)
        for building_id in self.validated_building_ids:
            required.add(("buildings", building_id, "construction"))
            required.add(("buildings", building_id, "idle"))
            optional.add(("buildings", building_id, "harvest"))

        # Heroes: idle, attack (required)
        for hero_id in self.validated_hero_ids:
            required.add(("heroes", hero_id, "idle"))
            required.add(("heroes", hero_id, "attack"))

        # Hero skills: each skill needs its own directory for icons
        for hero_id, skill_ids in self.hero_skills.items():
            for skill_id in skill_ids:
                required.add(("heroes", hero_id, skill_id))

        # Portraits: required directories (portrait_id used as ID, no subtype)
        for portrait_id in self.validated_portrait_ids:
            required.add(("portraits", str(portrait_id), ""))

        return required, optional

    def validate_images_directory(self, images_dir: Path) -> None:
        """Validate the images directory structure against config files."""
        if not images_dir.exists():
            return

        # Build sets of expected directories
        expected_required, expected_optional = self._get_expected_image_dirs()
        expected_all = expected_required | expected_optional

        # Walk images directory and build set of found directories
        found_dirs: set[tuple] = set()

        for root, dirs, files in os.walk(images_dir):
            root_path = Path(root)
            relative_parts = root_path.relative_to(images_dir).parts

            if len(relative_parts) >= 3:
                img_type = relative_parts[0]
                item_id = relative_parts[1]
                subtype = relative_parts[2]
                found_dirs.add((img_type, item_id, subtype))
            elif len(relative_parts) == 2:
                img_type = relative_parts[0]
                item_id = relative_parts[1]
                self._add_issue(
                    images_dir, 1, None,
                    f"Orphaned images directory: {root_path.relative_to(images_dir)}/",
                    Severity.WARN
                )

        # Check for missing required directories
        for dir_tuple in expected_required:
            if dir_tuple not in found_dirs:
                type_name, item_id, subtype = dir_tuple
                self._add_issue(
                    images_dir, 1, None,
                    f"Missing required images directory: images/{type_name}/{item_id}/{subtype}/",
                    Severity.WARN
                )

        # Check for orphaned directories
        for dir_tuple in found_dirs:
            if dir_tuple not in expected_all:
                type_name, item_id, subtype = dir_tuple
                self._add_issue(
                    images_dir, 1, None,
                    f"Orphaned images directory: images/{type_name}/{item_id}/{subtype}/ (no matching config)",
                    Severity.WARN
                )

        # Validate each found directory
        for dir_tuple in found_dirs:
            type_name, item_id, subtype = dir_tuple
            dir_path = images_dir / type_name / item_id / subtype

            if not dir_path.exists():
                continue

            # Check if directory is empty
            has_files = any(f.is_file() for f in dir_path.iterdir())

            if not has_files:
                if dir_tuple in expected_required:
                    self._add_issue(
                        dir_path, 1, None,
                        f"Empty required directory: images/{type_name}/{item_id}/{subtype}/",
                        Severity.ERROR
                    )
                elif dir_tuple in expected_optional:
                    self._add_issue(
                        dir_path, 1, None,
                        f"Empty optional directory: images/{type_name}/{item_id}/{subtype}/",
                        Severity.WARN
                    )
                else:
                    self._add_issue(
                        dir_path, 1, None,
                        f"Empty orphaned directory: images/{type_name}/{item_id}/{subtype}/",
                        Severity.WARN
                    )
            else:
                # Validate file naming
                invalid_files = self._validate_image_file_numbering(dir_path)
                for invalid_file in invalid_files:
                    self._add_issue(
                        dir_path, 1, None,
                        f"Invalid filename in images/{type_name}/{item_id}/{subtype}/: {invalid_file}",
                        Severity.WARN
                    )

        # Check for activate/ subdirectories (optional for skills)
        for hero_id, skill_ids in self.hero_skills.items():
            for skill_id in skill_ids:
                activate_path = images_dir / "heroes" / hero_id / skill_id / "activate"
                if activate_path.exists():
                    has_files = any(f.is_file() for f in activate_path.iterdir())
                    if not has_files:
                        self._add_issue(
                            activate_path, 1, None,
                            f"Empty optional activate/ directory: images/heroes/{hero_id}/{skill_id}/activate/",
                            Severity.WARN
                        )

        # Check for orphaned files at directory level
        for type_name in ["combatants", "buildings", "heroes"]:
            type_dir = images_dir / type_name
            if type_dir.exists():
                for item_dir in type_dir.iterdir():
                    if item_dir.is_dir():
                        item_id = item_dir.name
                        # Check if this item has a valid config
                        if type_name == "combatants" and item_id not in self.validated_combatant_ids:
                            self._add_issue(
                                item_dir, 1, None,
                                f"Orphaned images directory: images/{type_name}/{item_id}/",
                                Severity.WARN
                            )
                        elif type_name == "buildings" and item_id not in self.validated_building_ids:
                            self._add_issue(
                                item_dir, 1, None,
                                f"Orphaned images directory: images/{type_name}/{item_id}/",
                                Severity.WARN
                            )
                        elif type_name == "heroes" and item_id not in self.validated_hero_ids:
                            self._add_issue(
                                item_dir, 1, None,
                                f"Orphaned images directory: images/{type_name}/{item_id}/",
                                Severity.WARN
                            )

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
        heroes_file: Path = config_dir / "heroes.json"
        officials_file: Path = config_dir / "fiefdom_officials.json"

        files_to_validate: list[tuple[Path, str, bool]] = [
            (damage_types_file, "damage_types.json", True),
            (player_combatants_file, "player_combatants.json", False),
            (enemy_combatants_file, "enemy_combatants.json", False),
            (buildings_file, "fiefdom_building_types.json", False),
            (heroes_file, "heroes.json", False),
            (officials_file, "fiefdom_officials.json", False),
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
            elif file == heroes_file:
                self.validate_heroes(file, content)
            elif file == officials_file:
                self.validate_fiefdom_officials(file, content)

            self.validated_files.append(file)

        # Separate errors and warnings before image validation
        for issue in self.issues:
            if issue.severity == Severity.ERROR:
                errors.append(issue)
            else:
                warnings.append(issue)

        # Only validate images if configs have no errors
        if not errors:
            images_dir = config_dir.parent / "images"
            if images_dir.exists():
                self.validate_images_directory(images_dir)

        # Re-sort issues after image validation
        errors = [issue for issue in self.issues if issue.severity == Severity.ERROR]
        warnings = [issue for issue in self.issues if issue.severity == Severity.WARN]

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