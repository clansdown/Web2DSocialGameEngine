#!/usr/bin/env python3
"""Config file linter for Ravenest Build and Battle game configs.

Validates all JSON config files against their schema rules.

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
    """Resource production specification.

    All production and consumption share a fixed 1-day period: `amount` is the
    quantity produced/consumed per day. Legacy per-cycle fields
    (`amount_multiplier`, `periodicity`, `periodicity_multiplier`) are removed.
    """
    amount: float


class BuildingOutputTypedDict(TypedDict, total=False):
    """A single output of a building with its own inputs and unlock level."""
    resource: str
    amount: float
    inputs: dict[str, ResourceProductionTypedDict]
    min_level: int


class BuildingTypedDict(TypedDict, total=False):
    """Building type definition."""
    width: int
    height: int
    max_level: int
    arable_acres: int
    can_build_outside_wall: bool
    display_name: str
    descriptions: list[str]
    construction_times: list[float]
    construction_image: str
    construction_images: list[str]
    idle_images: list[str]
    harvest_images: list[str]
    gold: ResourceProductionTypedDict
    grain: ResourceProductionTypedDict
    wood: ResourceProductionTypedDict
    steel: ResourceProductionTypedDict
    bronze: ResourceProductionTypedDict
    leather: ResourceProductionTypedDict
    mana: ResourceProductionTypedDict
    fancy_ironwork: ResourceProductionTypedDict
    beams: ResourceProductionTypedDict
    boards: ResourceProductionTypedDict
    outputs: list[BuildingOutputTypedDict]
    gold_cost: list[float | dict[str, float]]
    grain_cost: list[float]
    wood_cost: list[float]
    steel_cost: list[float]
    bronze_cost: list[float]
    leather_cost: list[float]
    mana_cost: list[float]
    fancy_ironwork_cost: list[float]
    beams_cost: list[float]
    boards_cost: list[float]


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
    "gold", "grain", "wood", "steel", "bronze", "leather", "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"
}
VALID_PRODUCTION_RESOURCES: Final[set[str]] = {
    "gold", "grain", "wood", "steel", "bronze", "leather", "mana",
    "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"
}
VALID_BUILDING_PRODUCTION_FIELDS: Final[set[str]] = {
    "gold", "grain", "wood", "steel", "bronze", "leather", "mana",
    "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"
}
VALID_BUILDING_COST_FIELDS: Final[set[str]] = {
    "gold_cost", "silver_pence_cost", "grain_cost", "wood_cost", "steel_cost", "bronze_cost",
    "leather_cost", "mana_cost", "charcoal_cost", "iron_cost", "ironwork_cost", "fancy_ironwork_cost",
    "beams_cost", "boards_cost"
}
# Fiefdom starting-resource stockpiles (economy.json "starting_resources").
VALID_STARTING_RESOURCES: Final[set[str]] = {
    "gold", "silver_pence", "grain", "wood", "steel", "bronze",
    "leather", "mana", "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"
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


def _amount_value_is_valid(value: object) -> bool:
    """A production/input amount value may be a plain number (gold-denominated
    for the gold resource), a money object ({gold, shillings, pence} — any
    subset), or a per-level array of numbers or money objects."""
    if isinstance(value, (int, float)):
        return True
    if isinstance(value, dict):
        return set(value.keys()) <= {"gold", "shillings", "pence"}
    if isinstance(value, list):
        return all(
            isinstance(e, (int, float)) or (isinstance(e, dict) and set(e.keys()) <= {"gold", "shillings", "pence"})
            for e in value
        )
    return False


class ConfigValidator:
    """Main validator class for all config files."""

    def __init__(self) -> None:
        self.damage_types: list[str] = []
        self.issues: list[LinterIssue] = []
        self.validated_files: list[Path] = []
        self.all_files_present: bool = True
        # Track IDs for image validation
        self.validated_combatant_ids: set[str] = set()
        self.validated_enemy_combatant_ids: set[str] = set()
        self.validated_building_ids: set[str] = set()
        self.external_image_building_ids: set[str] = set()
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
        """Parse JSON using strict json.loads. Returns None on error."""
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
        allowed_keys: Union[set[str], frozenset[str]],
        is_defense: bool = False
    ) -> bool:
        """Validate a combatant array field (damage, defense, costs).

        `required_keys` must always be present on each entry; `allowed_keys`
        is the full set of known keys (required plus optional damage types
        from damage_types.json). Unknown keys are reported as warnings, and
        values on any known key are type-checked.
        """
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
            unexpected: set[str] = item_keys - set(allowed_keys)
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

            for key in item_keys:
                if key not in allowed_keys:
                    continue
                value: Any = item.get(key, 0)
                if not isinstance(value, (int, float)):
                    self._add_issue(
                        file, line, None,
                        f"{field_name}[{i}].{key} must be a number, got {type(value).__name__}",
                        Severity.ERROR
                    )
                    valid = False

        return valid

    def _validate_combatant_resource_array(
        self,
        file: Path,
        content: str,
        combatant_id: str,
        data: dict[str, Any],
        field_name: str
    ) -> bool:
        """Validate a per-level resource array field (costs, upkeep)."""
        valid: bool = True
        if field_name not in data:
            return valid

        array: Any = data[field_name]
        if not isinstance(array, list):
            self._add_issue(
                file, 1, None,
                f"Combatant '{combatant_id}'.{field_name} must be an array",
                Severity.ERROR
            )
            return False

        for i, item in enumerate(array):
            line: int = content[:].count('\n') + 1

            if not isinstance(item, dict):
                self._add_issue(
                    file, line, None,
                    f"{field_name}[{i}] must be an object",
                    Severity.ERROR
                )
                valid = False
                continue

            item_keys: set[str] = set(item.keys())
            unexpected: set[str] = item_keys - set(VALID_RESOURCE_TYPES)
            if unexpected:
                self._add_issue(
                    file, line, None,
                    f"{field_name}[{i}] has unexpected keys: {sorted(unexpected)}",
                    Severity.WARN
                )

            if not item:
                self._add_issue(
                    file, line, None,
                    f"{field_name}[{i}] must not be empty",
                    Severity.ERROR
                )
                valid = False
                continue

            for key, value in item.items():
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
        core_damage_types: set[str] = set(VALID_DAMAGE_TYPES)

        if not self._validate_combatant_array(
            file, content, data, "damage",
            core_damage_types, damage_types_set, False
        ):
            valid = False

        if not self._validate_combatant_array(
            file, content, data, "defense",
            core_damage_types, damage_types_set, True
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

        self._validate_combatant_resource_array(file, content, combatant_id, data, "costs")
        self._validate_combatant_resource_array(file, content, combatant_id, data, "upkeep")

        if "morale_boost" in data:
            boost_array: Any = data["morale_boost"]
            if not isinstance(boost_array, list):
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.morale_boost must be an array",
                    Severity.ERROR
                )
                valid = False
            else:
                for i, boost_val in enumerate(boost_array):
                    if not isinstance(boost_val, (int, float)):
                        self._add_issue(
                            file, 1, None,
                            f"Combatant '{combatant_id}'.morale_boost[{i}] must be a number, got {type(boost_val).__name__}",
                            Severity.ERROR
                        )
                        valid = False
                    elif boost_val < 0:
                        self._add_issue(
                            file, 1, None,
                            f"Combatant '{combatant_id}'.morale_boost[{i}] must be >= 0, got {boost_val}",
                            Severity.ERROR
                        )
                        valid = False

        if not self._validate_rts_combat_fields(file, combatant_id, data):
            valid = False

        self._validate_visual_description(file, combatant_id, data, "Combatant")

        return valid

    def _validate_rts_combat_fields(
        self,
        file: Path,
        combatant_id: str,
        data: dict[str, Any]
    ) -> bool:
        """Validate the RTS combat-model fields (rts_design.md §5.1): the required
        body `size`, optional per-level `max_hp`/`attack_range`/`attack_interval`
        arrays, the flat per-unit arc triples `direction_defense`/`dodge`,
        `attack_type`, `turn_rate`, and the optional `aoe` splash block."""
        valid: bool = True

        # Per-level scalar arrays.
        for scalar_field in ("max_hp", "attack_range", "attack_interval"):
            if scalar_field not in data:
                continue
            sarr: Any = data[scalar_field]
            if not isinstance(sarr, list) or not sarr:
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.{scalar_field} must be a non-empty per-level array",
                    Severity.ERROR,
                )
                valid = False
                continue
            for i, item in enumerate(sarr):
                if not isinstance(item, (int, float)) or isinstance(item, bool):
                    self._add_issue(
                        file, 1, None,
                        f"Combatant '{combatant_id}'.{scalar_field}[{i}] must be a number",
                        Severity.ERROR,
                    )
                    valid = False
                elif scalar_field == "attack_interval" and item <= 0:
                    self._add_issue(
                        file, 1, None,
                        f"Combatant '{combatant_id}'.{scalar_field}[{i}] must be > 0, got {item}",
                        Severity.ERROR,
                    )
                    valid = False
                elif scalar_field == "attack_range" and item < 0:
                    self._add_issue(
                        file, 1, None,
                        f"Combatant '{combatant_id}'.{scalar_field}[{i}] must be >= 0, got {item}",
                        Severity.ERROR,
                    )
                    valid = False
                elif scalar_field == "max_hp" and item <= 0:
                    self._add_issue(
                        file, 1, None,
                        f"Combatant '{combatant_id}'.{scalar_field}[{i}] must be > 0, got {item}",
                        Severity.ERROR,
                    )
                    valid = False

        # Flat per-unit arc triples.
        for triple_field in ("direction_defense", "dodge"):
            if triple_field not in data:
                continue
            triple: Any = data[triple_field]
            if not isinstance(triple, dict):
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.{triple_field} must be an object with front/side/back",
                    Severity.ERROR,
                )
                valid = False
                continue
            for arc in ("front", "side", "back"):
                if arc not in triple:
                    self._add_issue(
                        file, 1, None,
                        f"Combatant '{combatant_id}'.{triple_field} is missing arc '{arc}'",
                        Severity.ERROR,
                    )
                    valid = False
                    continue
                value: Any = triple[arc]
                if not isinstance(value, (int, float)) or isinstance(value, bool):
                    self._add_issue(
                        file, 1, None,
                        f"Combatant '{combatant_id}'.{triple_field}.{arc} must be a number",
                        Severity.ERROR,
                    )
                    valid = False
                elif not 0 <= value <= 1:
                    self._add_issue(
                        file, 1, None,
                        f"Combatant '{combatant_id}'.{triple_field}.{arc} must be in 0..1, got {value}",
                        Severity.ERROR,
                    )
                    valid = False

        if "attack_type" in data:
            attack_type: Any = data["attack_type"]
            if attack_type not in VALID_DAMAGE_TYPES:
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.attack_type must be one of "
                    f"{sorted(VALID_DAMAGE_TYPES)}, got {attack_type!r}",
                    Severity.ERROR,
                )
                valid = False

        if "turn_rate" in data:
            turn_rate: Any = data["turn_rate"]
            if not isinstance(turn_rate, (int, float)) or isinstance(turn_rate, bool) or turn_rate <= 0:
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.turn_rate must be a number > 0, got {turn_rate!r}",
                    Severity.ERROR,
                )
                valid = False

        # Body size (normalized board units): drives the sim collision radius and
        # the client's drawn sprite size. Required — how big a combatant is must
        # always be authored, never defaulted.
        if "size" not in data:
            self._add_issue(
                file, 1, None,
                f"Combatant '{combatant_id}' is missing required field 'size'",
                Severity.ERROR,
            )
            valid = False
        else:
            size: Any = data["size"]
            if not isinstance(size, (int, float)) or isinstance(size, bool) or size <= 0:
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.size must be a number > 0, got {size!r}",
                    Severity.ERROR,
                )
                valid = False
            elif size > 0.2:
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.size must be <= 0.2 (normalized board "
                    f"units; 0.03 is one standard soldier), got {size}",
                    Severity.ERROR,
                )
                valid = False

        if "aoe" in data:
            aoe: Any = data["aoe"]
            if not isinstance(aoe, dict):
                self._add_issue(
                    file, 1, None,
                    f"Combatant '{combatant_id}'.aoe must be an object",
                    Severity.ERROR,
                )
                valid = False
            else:
                for bool_field in ("splash", "flat_mitigation", "safe_arc_auto_aim",
                                   "hold_fire_on_no_safe_arc"):
                    if bool_field in aoe and not isinstance(aoe[bool_field], bool):
                        self._add_issue(
                            file, 1, None,
                            f"Combatant '{combatant_id}'.aoe.{bool_field} must be a boolean",
                            Severity.ERROR,
                        )
                        valid = False
                if "splash_radius" in aoe:
                    sr: Any = aoe["splash_radius"]
                    if not isinstance(sr, (int, float)) or isinstance(sr, bool) or sr <= 0:
                        self._add_issue(
                            file, 1, None,
                            f"Combatant '{combatant_id}'.aoe.splash_radius must be a number > 0",
                            Severity.ERROR,
                        )
                        valid = False

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

            if "requires_manor_level" in combatant_data:
                rml: object = combatant_data["requires_manor_level"]
                if not isinstance(rml, int) or isinstance(rml, bool) or rml < 1:
                    self._add_issue(
                        file, line, None,
                        f"Combatant '{combatant_id}' 'requires_manor_level' must be a positive integer",
                        Severity.ERROR
                    )
                    valid = False

            if not is_player and "requires" in combatant_data:
                if not self._validate_combatant_requires(
                    file, content, combatant_id, combatant_data["requires"], data
                ):
                    valid = False

            if "animations" in combatant_data:
                if not self._validate_combatant_animations(
                    file, content, combatant_id, combatant_data["animations"]
                ):
                    valid = False

            if not self._validate_combatant(file, content, combatant_id, combatant_data):
                valid = False

        # Track IDs for image validation
        self.validated_combatant_ids.update(seen_ids)
        if not is_player:
            self.validated_enemy_combatant_ids.update(seen_ids)
            self._check_requires_cycles(file, data)

        return valid

    def _validate_combatant_requires(
        self,
        file: Path,
        content: str,
        combatant_id: str,
        requires: object,
        data: dict[str, Any],
    ) -> bool:
        """Validate a combatant's prerequisite list (`[{unit_class, count}]`).

        Warband assembly rolls unmet prerequisites up recursively, so the schema
        is strict: unknown classes and non-positive counts are errors. A
        self-reference still works (the unit satisfies its own requirement), but
        it is warned as an authoring mistake.
        """
        valid: bool = True
        line: int = content[:].count('\n') + 1
        if not isinstance(requires, list) or not requires:
            self._add_issue(
                file, line, None,
                f"Combatant '{combatant_id}' 'requires' must be a non-empty array",
                Severity.ERROR,
            )
            return False
        for i, req in enumerate(requires):
            if not isinstance(req, dict):
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}'.requires[{i}] must be an object",
                    Severity.ERROR,
                )
                valid = False
                continue
            required_class: object = req.get("unit_class")
            if not isinstance(required_class, str) or not required_class:
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}'.requires[{i}].unit_class must be a non-empty string",
                    Severity.ERROR,
                )
                valid = False
            elif required_class not in data:
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}'.requires[{i}] references unknown combatant '{required_class}'",
                    Severity.ERROR,
                )
                valid = False
            elif required_class == combatant_id:
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}'.requires[{i}] references itself",
                    Severity.WARN,
                )
            count: object = req.get("count")
            if not isinstance(count, int) or isinstance(count, bool) or count <= 0:
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}'.requires[{i}].count must be a positive integer",
                    Severity.ERROR,
                )
                valid = False
        return valid

    def _check_requires_cycles(self, file: Path, data: dict[str, Any]) -> None:
        """Warn once when the enemy prerequisite graph contains a cycle."""
        graph: dict[str, list[str]] = {}
        for cid, cdata in data.items():
            if not isinstance(cdata, dict):
                continue
            reqs: object = cdata.get("requires")
            if not isinstance(reqs, list):
                continue
            edges: list[str] = []
            for req in reqs:
                if isinstance(req, dict) and isinstance(req.get("unit_class"), str):
                    edges.append(req["unit_class"])
            graph[cid] = edges

        visiting: set[str] = set()
        visited: set[str] = set()

        def visit(node: str) -> bool:
            if node in visiting:
                return True
            if node in visited:
                return False
            visiting.add(node)
            for nxt in graph.get(node, []):
                if nxt in graph and visit(nxt):
                    return True
            visiting.discard(node)
            visited.add(node)
            return False

        for cid in graph:
            if visit(cid):
                self._add_issue(
                    file, 1, None,
                    f"Combatant prerequisite graph contains a cycle through '{cid}'",
                    Severity.WARN,
                )
                return

    def _validate_combatant_animations(
        self,
        file: Path,
        content: str,
        combatant_id: str,
        animations: Any,
    ) -> bool:
        """Validate a combatant's directional animation block (rts_design.md D24).

        `animations` is keyed by clip id (e.g. "walk"); each clip carries a
        positive numeric `fps` and optional per-direction `views`. A view is either
        a set name or `{set, mirror}`. Frames live at
        `images/combatants/<class>/<clip>/<set>/<n>.png`; the frame *count* is
        discovered from the directory contents at runtime (server), so `frames` is
        no longer declared — a stale `frames` is warned as ignored. Directory
        existence is checked by tools/check_art.py.
        """
        valid: bool = True
        line: int = content[:].count('\n') + 1
        if not isinstance(animations, dict):
            self._add_issue(
                file, line, None,
                f"Combatant '{combatant_id}' 'animations' must be an object",
                Severity.ERROR
            )
            return False
        if not animations:
            self._add_issue(
                file, line, None,
                f"Combatant '{combatant_id}' 'animations' must not be empty",
                Severity.WARN
            )
            return True

        directions: set[str] = {"e", "se", "s", "sw", "w", "nw", "n", "ne"}
        for clip_id, clip in animations.items():
            if not isinstance(clip_id, str) or not clip_id:
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}' animation clip ids must be non-empty strings",
                    Severity.ERROR
                )
                valid = False
                continue
            if not clip_id.replace("_", "").isalnum() or not clip_id.islower():
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}' animation clip '{clip_id}' should be "
                    "lowercase snake_case",
                    Severity.WARN
                )
            if not isinstance(clip, dict):
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}'.animations['{clip_id}'] must be an object",
                    Severity.ERROR
                )
                valid = False
                continue

            if "frames" in clip:
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}'.animations['{clip_id}'].frames is ignored — "
                    "the frame count is discovered from the set directory contents",
                    Severity.WARN
                )

            fps: Any = clip.get("fps")
            if not isinstance(fps, (int, float)) or isinstance(fps, bool) or fps <= 0:
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}'.animations['{clip_id}'].fps must be a "
                    "positive number",
                    Severity.ERROR
                )
                valid = False

            if "views" not in clip:
                continue
            views: Any = clip["views"]
            if not isinstance(views, dict):
                self._add_issue(
                    file, line, None,
                    f"Combatant '{combatant_id}'.animations['{clip_id}'].views must be an object",
                    Severity.ERROR
                )
                valid = False
                continue
            for direction, view in views.items():
                if direction not in directions:
                    self._add_issue(
                        file, line, None,
                        f"Combatant '{combatant_id}'.animations['{clip_id}'].views has unknown "
                        f"direction '{direction}' (expected one of "
                        f"{', '.join(sorted(directions))})",
                        Severity.ERROR
                    )
                    valid = False
                if isinstance(view, str):
                    if not view:
                        self._add_issue(
                            file, line, None,
                            f"Combatant '{combatant_id}'.animations['{clip_id}'].views"
                            f"['{direction}'] set name must be non-empty",
                            Severity.ERROR
                        )
                        valid = False
                elif isinstance(view, dict):
                    set_name: Any = view.get("set")
                    if not isinstance(set_name, str) or not set_name:
                        self._add_issue(
                            file, line, None,
                            f"Combatant '{combatant_id}'.animations['{clip_id}'].views"
                            f"['{direction}'].set must be a non-empty string",
                            Severity.ERROR
                        )
                        valid = False
                    mirror: Any = view.get("mirror")
                    if mirror is not None and not isinstance(mirror, bool):
                        self._add_issue(
                            file, line, None,
                            f"Combatant '{combatant_id}'.animations['{clip_id}'].views"
                            f"['{direction}'].mirror must be a boolean",
                            Severity.ERROR
                        )
                        valid = False
                    unknown_keys: set[str] = set(view.keys()) - {"set", "mirror"}
                    if unknown_keys:
                        self._add_issue(
                            file, line, None,
                            f"Combatant '{combatant_id}'.animations['{clip_id}'].views"
                            f"['{direction}'] has unknown key(s): "
                            f"{', '.join(sorted(unknown_keys))}",
                            Severity.ERROR
                        )
                        valid = False
                else:
                    self._add_issue(
                        file, line, None,
                        f"Combatant '{combatant_id}'.animations['{clip_id}'].views"
                        f"['{direction}'] must be a set-name string or a "
                        "{set, mirror} object",
                        Severity.ERROR
                    )
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

        expected_keys: set[str] = {"amount"}
        actual_keys: set[str] = set(prod.keys())
        unexpected: set[str] = actual_keys - expected_keys

        if unexpected:
            self._add_issue(
                file, line, None,
                f"Building '{building_id}'.{resource_name} has unexpected keys: {sorted(unexpected)}",
                Severity.WARN
            )

        legacy_keys: set[str] = {"amount_multiplier", "periodicity", "periodicity_multiplier"}
        present_legacy: set[str] = actual_keys & legacy_keys
        if present_legacy:
            self._add_issue(
                file, line, None,
                f"Building '{building_id}'.{resource_name} uses removed keys "
                f"{sorted(present_legacy)} (production is now a flat per-day amount)",
                Severity.ERROR
            )

        for key in expected_keys:
            if key in prod:
                value: Any = prod[key]
                if not _amount_value_is_valid(value):
                    self._add_issue(
                        file, line, None,
                        f"Building '{building_id}'.{resource_name}.{key} must be a number, a money object "
                        "({gold, shillings, pence}), or a per-level array of either",
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

    def _validate_money_cost_array(
        self,
        file: Path,
        content: str,
        entity_id: str,
        data: dict[str, Any],
        field_name: str,
        entity_kind: str = "Building",
        allow_negative: bool = False
    ) -> None:
        """Validate a money cost array field. Each element may be a plain number
        (gold) or an object {gold, shillings, pence} with non-negative values.
        Object keys are restricted to gold/shillings/pence and at least one must
        be present."""
        if field_name not in data:
            return

        array: Any = data[field_name]
        line: int = content[:].count('\n') + 1

        if not isinstance(array, list):
            self._add_issue(
                file, line, None,
                f"{entity_kind} '{entity_id}'.{field_name} must be an array",
                Severity.ERROR
            )
            return

        valid_keys: Final[set[str]] = {"gold", "shillings", "pence"}
        for i, item in enumerate(array):
            if isinstance(item, (int, float)):
                if not allow_negative and item < 0:
                    self._add_issue(
                        file, line, None,
                        f"{entity_kind} '{entity_id}'.{field_name}[{i}] must be >= 0, got {item}",
                        Severity.WARN
                    )
                continue

            if not isinstance(item, dict):
                self._add_issue(
                    file, line, None,
                    f"{entity_kind} '{entity_id}'.{field_name}[{i}] must be a number or an object",
                    Severity.ERROR
                )
                continue

            unknown = set(item.keys()) - valid_keys
            if unknown:
                self._add_issue(
                    file, line, None,
                    f"{entity_kind} '{entity_id}'.{field_name}[{i}] has unknown keys: {', '.join(sorted(unknown))} "
                    f"(allowed: {', '.join(sorted(valid_keys))})",
                    Severity.ERROR
                )

            if not item:
                self._add_issue(
                    file, line, None,
                    f"{entity_kind} '{entity_id}'.{field_name}[{i}] must specify at least one of "
                    f"gold/shillings/pence",
                    Severity.ERROR
                )

            for key, value in item.items():
                if not isinstance(value, (int, float)):
                    self._add_issue(
                        file, line, None,
                        f"{entity_kind} '{entity_id}'.{field_name}[{i}].{key} must be a number, got "
                        f"{type(value).__name__}",
                        Severity.ERROR
                    )
                elif not allow_negative and value < 0:
                    self._add_issue(
                        file, line, None,
                        f"{entity_kind} '{entity_id}'.{field_name}[{i}].{key} must be >= 0, got {value}",
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

        if "display_name" in data:
            if not isinstance(data["display_name"], str) or not data["display_name"]:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.display_name must be a non-empty string",
                    Severity.ERROR
                )

        if "construction_image" in data:
            val: Any = data["construction_image"]
            if not isinstance(val, str) or not val:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.construction_image must be a non-empty string",
                    Severity.ERROR
                )

        self._validate_number_array(file, content, building_id, data, "construction_times", allow_negative=False)
        self._validate_money_cost_array(file, content, building_id, data, "gold_cost")
        self._validate_number_array(file, content, building_id, data, "silver_pence_cost", allow_negative=False)
        self._validate_number_array(file, content, building_id, data, "grain_cost")
        self._validate_number_array(file, content, building_id, data, "wood_cost")
        self._validate_number_array(file, content, building_id, data, "steel_cost")
        self._validate_number_array(file, content, building_id, data, "bronze_cost")
        self._validate_number_array(file, content, building_id, data, "stone_cost")
        self._validate_number_array(file, content, building_id, data, "leather_cost")
        self._validate_number_array(file, content, building_id, data, "mana_cost")
        self._validate_number_array(file, content, building_id, data, "charcoal_cost")
        self._validate_number_array(file, content, building_id, data, "iron_cost")
        self._validate_number_array(file, content, building_id, data, "ironwork_cost")
        self._validate_number_array(file, content, building_id, data, "fancy_ironwork_cost")
        self._validate_number_array(file, content, building_id, data, "beams_cost")
        self._validate_number_array(file, content, building_id, data, "boards_cost")

        # Internal notes: `descriptions` is an array of non-empty strings
        # (formal in-config comments, never user-facing; the server strips it
        # from client-facing building-config responses).
        if "descriptions" in data:
            desc_data: Any = data["descriptions"]
            if not isinstance(desc_data, list):
                self._add_issue(
                    file, content.count('\n') + 1, None,
                    f"Building '{building_id}'.descriptions must be an array",
                    Severity.ERROR
                )
            else:
                for di, entry in enumerate(desc_data):
                    if not isinstance(entry, str) or not entry.strip():
                        self._add_issue(
                            file, content.count('\n') + 1, None,
                            f"Building '{building_id}'.descriptions[{di}] must be a non-empty string",
                            Severity.ERROR
                        )

        # The `outputs` array is the canonical production schema. Flat
        # production resource fields (e.g. `charcoal: { amount: ... }`) are
        # disallowed — put outputs (with their own per-output inputs) in the
        # `outputs` array instead.
        for resource in VALID_BUILDING_PRODUCTION_FIELDS:
            if resource in data:
                self._add_issue(
                    file, content.count('\n') + 1, None,
                    f"Building '{building_id}' uses a flat production field "
                    f"'{resource}'; define outputs in the 'outputs' array instead "
                    "(each output declares its own inputs)",
                    Severity.ERROR,
                )

        self._validate_visual_description(file, building_id, data, "Building")

        if "max_per_fiefdom" in data:
            max_count: Any = data["max_per_fiefdom"]
            if not isinstance(max_count, int) or isinstance(max_count, bool):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.max_per_fiefdom must be an integer, got {type(max_count).__name__}",
                    Severity.ERROR
                )
            elif max_count < 0:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.max_per_fiefdom must be >= 0, got {max_count}",
                    Severity.ERROR
                )

        if "arable_acres" in data:
            arable_acres: Any = data["arable_acres"]
            if not isinstance(arable_acres, int) or isinstance(arable_acres, bool):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.arable_acres must be an integer, got {type(arable_acres).__name__}",
                    Severity.ERROR
                )
            elif arable_acres < 0:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.arable_acres must be >= 0, got {arable_acres}",
                    Severity.ERROR
                )

        if "forest_acres" in data:
            forest_acres: Any = data["forest_acres"]
            if not isinstance(forest_acres, int) or isinstance(forest_acres, bool):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.forest_acres must be an integer, got {type(forest_acres).__name__}",
                    Severity.ERROR
                )
            elif forest_acres < 0:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.forest_acres must be >= 0, got {forest_acres}",
                    Severity.ERROR
                )

        if "recovery_multiplier" in data:
            recovery_multiplier: Any = data["recovery_multiplier"]
            if not isinstance(recovery_multiplier, (int, float)) or isinstance(recovery_multiplier, bool):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.recovery_multiplier must be a number, got {type(recovery_multiplier).__name__}",
                    Severity.ERROR
                )
            elif recovery_multiplier < 0:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.recovery_multiplier must be >= 0, got {recovery_multiplier}",
                    Severity.ERROR
                )

        if "tech_trees" in data:
            tt: Any = data["tech_trees"]
            if not isinstance(tt, list) or any(not isinstance(t, str) or not t for t in tt):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.tech_trees must be an array of non-empty strings",
                    Severity.ERROR
                )

        if "tech_xp_per_day" in data:
            txp: Any = data["tech_xp_per_day"]
            if not isinstance(txp, list) or not txp or any(
                not isinstance(x, int) or isinstance(x, bool) or x < 0 for x in txp
            ):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.tech_xp_per_day must be a non-empty array of non-negative integers (per level)",
                    Severity.ERROR
                )

        if "max_concurrent_orders" in data:
            mco: Any = data["max_concurrent_orders"]
            if not isinstance(mco, int) or isinstance(mco, bool) or mco < 1:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.max_concurrent_orders must be a positive integer",
                    Severity.ERROR
                )

        if "class" in data:
            cls: Any = data["class"]
            if not isinstance(cls, str):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.class must be a string, got {type(cls).__name__}",
                    Severity.ERROR
                )
            elif cls != cls.lower() or not all(c.isalnum() or c == "_" for c in cls):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.class should be lowercase snake_case, got {cls!r}",
                    Severity.WARNING
                )

        # Exclusive group: at most one building from the group per fiefdom.
        if "exclusive_group" in data:
            group: Any = data["exclusive_group"]
            if not isinstance(group, str) or not group:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.exclusive_group must be a non-empty string",
                    Severity.ERROR
                )
            elif group != group.lower() or not all(c.isalnum() or c == "_" for c in group):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.exclusive_group should be lowercase snake_case, got {group!r}",
                    Severity.WARNING
                )

        if "morale_boost" in data:
            boost: Any = data["morale_boost"]
            if not isinstance(boost, (int, float)):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.morale_boost must be a number, got {type(boost).__name__}",
                    Severity.ERROR
                )
            elif boost < 0:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.morale_boost must be >= 0, got {boost}",
                    Severity.ERROR
                )

        if "morale_effect_mode" in data:
            mode: Any = data["morale_effect_mode"]
            if not isinstance(mode, str):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.morale_effect_mode must be a string",
                    Severity.ERROR
                )
            elif mode not in {"add", "max", "multiply"}:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.morale_effect_mode must be 'add', 'max', or 'multiply', got '{mode}'",
                    Severity.ERROR
                )

        if "road_morale" in data:
            rm: Any = data["road_morale"]
            if not isinstance(rm, dict):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.road_morale must be an object",
                    Severity.ERROR
                )
            else:
                if "boost" not in rm or not isinstance(rm["boost"], (int, float)) or rm["boost"] <= 0:
                    self._add_issue(
                        file, 1, None,
                        f"Building '{building_id}'.road_morale.boost must be a positive number",
                        Severity.ERROR
                    )
                if "distance" not in rm or not isinstance(rm["distance"], int) or rm["distance"] < 1:
                    self._add_issue(
                        file, 1, None,
                        f"Building '{building_id}'.road_morale.distance must be an integer >= 1",
                        Severity.ERROR
                    )

        if "road_tiles" in data:
            rt: Any = data["road_tiles"]
            if not isinstance(rt, dict) or not rt:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.road_tiles must be a non-empty object of tile-key -> image path",
                    Severity.ERROR
                )
            else:
                for tile_key, tile_path in rt.items():
                    if not isinstance(tile_path, str) or not tile_path:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.road_tiles.{tile_key} must be a non-empty image path string",
                            Severity.ERROR
                        )

        if "road_tiles_canonical" in data:
            rtc: Any = data["road_tiles_canonical"]
            if not isinstance(rtc, dict) or not rtc:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.road_tiles_canonical must be a non-empty object of tile-key -> direction array",
                    Severity.ERROR
                )
            else:
                valid_dirs = {"n", "e", "s", "w"}
                for tile_key, dirs in rtc.items():
                    if not isinstance(dirs, list) or not dirs:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.road_tiles_canonical.{tile_key} must be a non-empty array of directions",
                            Severity.ERROR
                        )
                        continue
                    for d in dirs:
                        if d not in valid_dirs:
                            self._add_issue(
                                file, 1, None,
                                f"Building '{building_id}'.road_tiles_canonical.{tile_key} contains invalid direction '{d}' (must be n/e/s/w)",
                                Severity.ERROR
                            )
                    if len(set(dirs)) != len(dirs):
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.road_tiles_canonical.{tile_key} contains duplicate directions",
                            Severity.ERROR
                        )

        if "water_powered" in data:
            if not isinstance(data["water_powered"], bool):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.water_powered must be a boolean",
                    Severity.ERROR
                )

        if "water_source" in data:
            if not isinstance(data["water_source"], bool):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.water_source must be a boolean",
                    Severity.ERROR
                )

        if "pond_types" in data:
            pts: Any = data["pond_types"]
            if not isinstance(pts, list) or not pts:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.pond_types must be a non-empty array",
                    Severity.ERROR
                )
            else:
                seen_pond_ids: set[str] = set()
                for i, pt in enumerate(pts):
                    if not isinstance(pt, dict):
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.pond_types[{i}] must be an object",
                            Severity.ERROR
                        )
                        continue
                    pid: Any = pt.get("id")
                    if not isinstance(pid, str) or not pid:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.pond_types[{i}].id "
                            "must be a non-empty string",
                            Severity.ERROR
                        )
                    elif pid in seen_pond_ids:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.pond_types[{i}].id "
                            f"'{pid}' is duplicated",
                            Severity.ERROR
                        )
                    else:
                        seen_pond_ids.add(pid)
                    cap: Any = pt.get("capacity")
                    if not isinstance(cap, int) or isinstance(cap, bool) or cap < 1:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.pond_types[{i}].capacity "
                            "must be an integer >= 1",
                            Severity.ERROR
                        )
                    ml: Any = pt.get("max_level")
                    if not isinstance(ml, int) or isinstance(ml, bool) or ml < 1:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.pond_types[{i}].max_level "
                            "must be an integer >= 1",
                            Severity.ERROR
                        )
                    for cost_field in ("gold_cost", "silver_pence_cost", "wood_cost", "stone_cost",
                                       "steel_cost", "bronze_cost", "grain_cost", "leather_cost",
                                       "mana_cost"):
                        if cost_field in pt:
                            self._validate_number_array(
                                file, content, building_id, pt, cost_field, allow_negative=False)
                    if "construction_times" in pt:
                        self._validate_number_array(
                            file, content, building_id, pt,
                            "construction_times", allow_negative=False)

        if "race_tiles" in data:
            rt: Any = data["race_tiles"]
            if not isinstance(rt, dict) or not rt:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.race_tiles must be a non-empty "
                    "object of tile-key -> image path",
                    Severity.ERROR
                )
            else:
                for tile_key, tile_path in rt.items():
                    if not isinstance(tile_path, str) or not tile_path:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.race_tiles.{tile_key} "
                            "must be a non-empty image path string",
                            Severity.ERROR
                        )

        if "race_tiles_canonical" in data:
            rtc: Any = data["race_tiles_canonical"]
            if not isinstance(rtc, dict) or not rtc:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.race_tiles_canonical must be a "
                    "non-empty object of tile-key -> direction array",
                    Severity.ERROR
                )
            else:
                valid_dirs: set[str] = {"n", "e", "s", "w"}
                for tile_key, dirs in rtc.items():
                    if not isinstance(dirs, list) or not dirs:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.race_tiles_canonical."
                            f"{tile_key} must be a non-empty array of directions",
                            Severity.ERROR
                        )
                        continue
                    for d in dirs:
                        if d not in valid_dirs:
                            self._add_issue(
                                file, 1, None,
                                f"Building '{building_id}'.race_tiles_canonical."
                                f"{tile_key} contains invalid direction "
                                f"'{d}' (must be n/e/s/w)",
                                Severity.ERROR
                            )
                    if len(set(dirs)) != len(dirs):
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.race_tiles_canonical."
                            f"{tile_key} contains duplicate directions",
                            Severity.ERROR
                        )

        self._validate_building_modifiers(file, building_id, data)
        self._validate_building_dependencies(file, building_id, data)
        self._validate_building_daily_costs(file, building_id, data)
        self._validate_building_inputs(file, content, building_id, data)
        self._validate_building_outputs(file, content, building_id, data)

    def _validate_building_outputs(
        self,
        file: Path,
        content: str,
        building_id: str,
        data: dict[str, Any],
    ) -> None:
        """Validate the `outputs` array — the canonical production schema. Each
        output has its own resource, amount, optional per-output inputs, and
        optional min_level (unlock). Flat production fields and building-level
        `inputs` are disallowed (validated separately as errors)."""
        if "outputs" not in data:
            return

        outputs: Any = data["outputs"]
        line: int = content[:].count('\n') + 1

        if not isinstance(outputs, list):
            self._add_issue(
                file, line, None,
                f"Building '{building_id}'.outputs must be an array",
                Severity.ERROR,
            )
            return

        max_level: int = data.get("max_level", 1)
        seen_resources: set[str] = set()
        for i, out in enumerate(outputs):
            if not isinstance(out, dict):
                self._add_issue(
                    file, line, None,
                    f"Building '{building_id}'.outputs[{i}] must be an object",
                    Severity.ERROR,
                )
                continue

            res: object = out.get("resource")
            if not isinstance(res, str) or res not in VALID_BUILDING_PRODUCTION_FIELDS:
                self._add_issue(
                    file, line, None,
                    f"Building '{building_id}'.outputs[{i}].resource must be a valid resource",
                    Severity.ERROR,
                )
            else:
                if res in seen_resources:
                    self._add_issue(
                        file, line, None,
                        f"Building '{building_id}'.outputs[{i}].resource '{res}' is duplicated",
                        Severity.ERROR,
                    )
                seen_resources.add(res)

            amount: object = out.get("amount")
            if not _amount_value_is_valid(amount) or (isinstance(amount, (int, float)) and amount <= 0):
                self._add_issue(
                    file, line, None,
                    f"Building '{building_id}'.outputs[{i}].amount must be a positive number, a money object, "
                    "or a per-level array of either",
                    Severity.ERROR,
                )

            min_level: object = out.get("min_level", 1)
            if not isinstance(min_level, int) or not (1 <= min_level <= max_level):
                self._add_issue(
                    file, line, None,
                    f"Building '{building_id}'.outputs[{i}].min_level must be an integer "
                    f"between 1 and max_level ({max_level})",
                    Severity.ERROR,
                )

            inputs_obj: object = out.get("inputs")
            if inputs_obj is not None:
                if not isinstance(inputs_obj, dict):
                    self._add_issue(
                        file, line, None,
                        f"Building '{building_id}'.outputs[{i}].inputs must be an object",
                        Severity.ERROR,
                    )
                else:
                    for ires, ispec in inputs_obj.items():
                        if ires not in VALID_BUILDING_PRODUCTION_FIELDS:
                            self._add_issue(
                                file, line, None,
                                f"Building '{building_id}'.outputs[{i}].inputs has unknown resource '{ires}'",
                                Severity.WARN,
                            )
                        if not isinstance(ispec, dict) or not _amount_value_is_valid(ispec.get("amount")):
                            self._add_issue(
                                file, line, None,
                                f"Building '{building_id}'.outputs[{i}].inputs.{ires}.amount must be a number, "
                                "a money object, or a per-level array of either",
                                Severity.ERROR,
                            )

    def _validate_building_modifiers(
        self,
        file: Path,
        building_id: str,
        data: dict[str, Any]
    ) -> None:
        """Validate modifiers array for a building."""
        if "modifiers" not in data:
            return

        modifiers: Any = data["modifiers"]
        if not isinstance(modifiers, list):
            self._add_issue(
                file, 1, None,
                f"Building '{building_id}'.modifiers must be an array",
                Severity.ERROR
            )
            return

        for i, mod in enumerate(modifiers):
            if not isinstance(mod, dict):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.modifiers[{i}] must be an object",
                    Severity.ERROR
                )
                continue

            required_mod_fields = ["modifier_id", "target_building", "target_resource", "multiplier", "max_targets"]
            for field in required_mod_fields:
                if field not in mod:
                    self._add_issue(
                        file, 1, None,
                        f"Building '{building_id}'.modifiers[{i}] is missing required field '{field}'",
                        Severity.ERROR
                    )

            if "modifier_id" in mod:
                mid: Any = mod["modifier_id"]
                if not isinstance(mid, str) or not mid:
                    self._add_issue(
                        file, 1, None,
                        f"Building '{building_id}'.modifiers[{i}].modifier_id must be a non-empty string",
                        Severity.ERROR
                    )

            if "target_building" in mod:
                tb: Any = mod["target_building"]
                if not isinstance(tb, str) or not tb:
                    self._add_issue(
                        file, 1, None,
                        f"Building '{building_id}'.modifiers[{i}].target_building must be a non-empty string",
                        Severity.ERROR
                    )

            if "target_resource" in mod:
                tr: Any = mod["target_resource"]
                if not isinstance(tr, str) or not tr:
                    self._add_issue(
                        file, 1, None,
                        f"Building '{building_id}'.modifiers[{i}].target_resource must be a non-empty string",
                        Severity.ERROR
                    )

            if "multiplier" in mod:
                self._validate_modifier_numeric(
                    file, building_id, i, mod, "multiplier", allow_negative=False
                )

            if "max_targets" in mod:
                self._validate_modifier_numeric(
                    file, building_id, i, mod, "max_targets", allow_negative=False, integer_only=True
                )

    def _validate_modifier_numeric(
        self,
        file: Path,
        building_id: str,
        mod_index: int,
        mod: dict[str, Any],
        field: str,
        allow_negative: bool = False,
        integer_only: bool = False
    ) -> None:
        """Validate a numeric field in a modifier that can be a number or array of numbers."""
        val: Any = mod[field]
        field_path = f"Building '{building_id}'.modifiers[{mod_index}].{field}"

        if isinstance(val, (int, float)):
            if integer_only and not isinstance(val, int):
                self._add_issue(file, 1, None, f"{field_path} must be an integer", Severity.ERROR)
            elif not allow_negative and val < 0:
                self._add_issue(file, 1, None, f"{field_path} must be >= 0, got {val}", Severity.ERROR)
            return

        if isinstance(val, list):
            for j, item in enumerate(val):
                if not isinstance(item, (int, float)):
                    self._add_issue(
                        file, 1, None,
                        f"{field_path}[{j}] must be a number, got {type(item).__name__}",
                        Severity.ERROR
                    )
                elif integer_only and not isinstance(item, int):
                    self._add_issue(
                        file, 1, None,
                        f"{field_path}[{j}] must be an integer, got {item}",
                        Severity.ERROR
                    )
                elif not allow_negative and item < 0:
                    self._add_issue(
                        file, 1, None,
                        f"{field_path}[{j}] must be >= 0, got {item}",
                        Severity.ERROR
                    )
            return

        self._add_issue(
            file, 1, None,
            f"{field_path} must be a number or array of numbers, got {type(val).__name__}",
            Severity.ERROR
        )

    def _validate_building_dependencies(
        self,
        file: Path,
        building_id: str,
        data: dict[str, Any]
    ) -> None:
        """Validate dependencies array for a building."""
        if "dependencies" not in data:
            return

        deps: Any = data["dependencies"]
        if not isinstance(deps, list):
            self._add_issue(
                file, 1, None,
                f"Building '{building_id}'.dependencies must be an array",
                Severity.ERROR
            )
            return

        for level_idx, level_deps in enumerate(deps):
            if not isinstance(level_deps, list):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.dependencies[{level_idx}] must be an array",
                    Severity.ERROR
                )
                continue

            for dep_idx, dep in enumerate(level_deps):
                if not isinstance(dep, dict):
                    self._add_issue(
                        file, 1, None,
                        f"Building '{building_id}'.dependencies[{level_idx}][{dep_idx}] must be an object",
                        Severity.ERROR
                    )
                    continue

                dep_fields = ["target_building", "count", "shared"]
                for field in dep_fields:
                    if field not in dep:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.dependencies[{level_idx}][{dep_idx}] missing '{field}'",
                            Severity.ERROR
                        )

                if "target_building" in dep:
                    tb: Any = dep["target_building"]
                    if not isinstance(tb, str) or not tb:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.dependencies[{level_idx}][{dep_idx}].target_building must be non-empty string",
                            Severity.ERROR
                        )

                if "count" in dep:
                    cnt: Any = dep["count"]
                    if not isinstance(cnt, int) or cnt < 1:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.dependencies[{level_idx}][{dep_idx}].count must be int >= 1, got {cnt}",
                            Severity.ERROR
                        )

                if "shared" in dep:
                    sh: Any = dep["shared"]
                    if not isinstance(sh, bool):
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.dependencies[{level_idx}][{dep_idx}].shared must be boolean",
                            Severity.ERROR
                        )

                if "min_level" in dep:
                    ml: Any = dep["min_level"]
                    if not isinstance(ml, int) or ml < 1:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.dependencies[{level_idx}][{dep_idx}].min_level must be int >= 1, got {ml}",
                            Severity.ERROR
                        )

    def _validate_building_daily_costs(
        self,
        file: Path,
        building_id: str,
        data: dict[str, Any]
    ) -> None:
        """Validate daily_cost and priority fields.

        `daily_cost` is a map of resource to per-day consumption rate. The
        legacy `hourly_cost` key is reported as an error (renamed to
        `daily_cost` as part of the 1-day period standardization).
        """
        valid_resources = {"gold", "grain", "wood", "steel", "bronze", "leather", "mana", "charcoal", "iron", "ironwork", "fancy_ironwork", "beams", "boards"}

        if "hourly_cost" in data:
            self._add_issue(
                file, 1, None,
                f"Building '{building_id}'.hourly_cost is deprecated; rename it to 'daily_cost'",
                Severity.ERROR
            )

        if "daily_cost" in data:
            dc: Any = data["daily_cost"]
            if not isinstance(dc, dict):
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.daily_cost must be an object",
                    Severity.ERROR
                )
            else:
                for res, rate in dc.items():
                    if res not in valid_resources:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.daily_cost has unknown resource '{res}'",
                            Severity.WARN
                        )
                    if not isinstance(rate, (int, float)) or rate < 0:
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.daily_cost.{res} must be a non-negative number",
                            Severity.ERROR
                        )

        if "priority" in data:
            prio: Any = data["priority"]
            if not isinstance(prio, int) or prio < 0:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}'.priority must be a non-negative integer",
                    Severity.ERROR
                )

    def _validate_building_inputs(
        self,
        file: Path,
        content: str,
        building_id: str,
        data: dict[str, Any]
    ) -> None:
        """The building-level `inputs` map is deprecated/disallowed. Inputs must
        be declared per-output inside the `outputs` array so it is unambiguous
        what each output's inputs gate."""
        if "inputs" not in data:
            return

        line: int = content[:].count('\n') + 1
        self._add_issue(
            file, line, None,
            f"Building '{building_id}' uses a building-level 'inputs' map; "
            "inputs must be declared on each output in the 'outputs' array instead",
            Severity.ERROR,
        )

    def _validate_building_prerequisites(
        self,
        file: Path,
        content: str,
        data: JsonDataType,
        valid_building_ids: set[str]
    ) -> None:
        """Validate prerequisites field for all building types."""
        for building_entry in data:
            if not isinstance(building_entry, dict):
                continue

            for building_id, building_data in building_entry.items():
                if not isinstance(building_data, dict):
                    continue

                if "prerequisites" not in building_data:
                    continue

                prerequisites: Any = building_data["prerequisites"]

                if not isinstance(prerequisites, list):
                    self._add_issue(
                        file, 1, None,
                        f"Building '{building_id}'.prerequisites must be an array, got {type(prerequisites).__name__}",
                        Severity.ERROR
                    )
                    continue

                for i, prereq_obj in enumerate(prerequisites):
                    if not isinstance(prereq_obj, dict):
                        self._add_issue(
                            file, 1, None,
                            f"Building '{building_id}'.prerequisites[{i}] must be an object, got {type(prereq_obj).__name__}",
                            Severity.ERROR
                        )
                        continue

                    for req_building_id, required_level in prereq_obj.items():
                        # Special prerequisite keys that are not building types
                        if req_building_id in {"manor_level"}:
                            if not isinstance(required_level, int):
                                self._add_issue(
                                    file, 1, None,
                                    f"Building '{building_id}'.prerequisites[{i}].{req_building_id} must be an integer, got {type(required_level).__name__}",
                                    Severity.ERROR
                                )
                            elif required_level < 0:
                                self._add_issue(
                                    file, 1, None,
                                    f"Building '{building_id}'.prerequisites[{i}].{req_building_id} must be non-negative, got {required_level}",
                                    Severity.ERROR
                                )
                            continue

                        if req_building_id not in valid_building_ids:
                            self._add_issue(
                                file, 1, None,
                                f"Building '{building_id}'.prerequisites[{i}] references unknown building type '{req_building_id}'",
                                Severity.ERROR
                            )

                        if not isinstance(required_level, int):
                            self._add_issue(
                                file, 1, None,
                                f"Building '{building_id}'.prerequisites[{i}].{req_building_id} must be an integer, got {type(required_level).__name__}",
                                Severity.ERROR
                            )
                        elif required_level < 0:
                            self._add_issue(
                                file, 1, None,
                                f"Building '{building_id}'.prerequisites[{i}].{req_building_id} must be non-negative, got {required_level}",
                                Severity.ERROR
                            )

    def _validate_built_from(self, file: Path, data: JsonDataType, valid_building_ids: set[str]) -> None:
        """Validate the `built_from` stage-chain field for all building types.

        `built_from` declares which building a stage can be converted from. Each
        building may be the source of at most one successor, chains may not
        reference themselves, and the `built_from` graph must be acyclic. Chain
        depth is unbounded — "3 stages" is only a design default, not a limit.
        """
        successors: dict[str, str] = {}  # built_from value -> building that declares it
        parents: dict[str, str] = {}     # building id -> its built_from value
        for building_entry in data:
            if not isinstance(building_entry, dict):
                continue
            for building_id, building_data in building_entry.items():
                if not isinstance(building_data, dict) or "built_from" not in building_data:
                    continue
                source: Any = building_data["built_from"]
                if not isinstance(source, str) or not source:
                    self._add_issue(
                        file, 1, None,
                        f"Building '{building_id}'.built_from must be a non-empty string, got {type(source).__name__}",
                        Severity.ERROR
                    )
                    continue
                if source == building_id:
                    self._add_issue(
                        file, 1, None,
                        f"Building '{building_id}'.built_from must not reference itself",
                        Severity.ERROR
                    )
                if source not in valid_building_ids:
                    self._add_issue(
                        file, 1, None,
                        f"Building '{building_id}'.built_from references unknown building type '{source}'",
                        Severity.ERROR
                    )
                if source in successors:
                    self._add_issue(
                        file, 1, None,
                        f"Building '{source}' is the built_from source of both '{successors[source]}' and "
                        f"'{building_id}'; at most one successor is allowed",
                        Severity.ERROR
                    )
                else:
                    successors[source] = building_id
                parents[building_id] = source

        # Cycle detection over the built_from graph.
        for building_id in parents:
            visited: set[str] = set()
            cur: str = building_id
            while cur in parents and cur not in visited:
                visited.add(cur)
                cur = parents[cur]
            if cur in visited:
                self._add_issue(
                    file, 1, None,
                    f"Building '{building_id}' participates in a built_from cycle",
                    Severity.ERROR
                )

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

                # Buildings with explicit image field use external images (e.g. game/images/manor/)
                # and should not require server-side image directories
                if isinstance(building_data, dict) and "image" in building_data:
                    self.external_image_building_ids.add(building_id)

        self._validate_building_prerequisites(file, content, data, seen_ids)
        self._validate_built_from(file, data, seen_ids)

        # Track IDs for image validation
        self.validated_building_ids.update(seen_ids)

        return valid

    def validate_manor_ui(self, file: Path, content: str, buildings_file: Path) -> bool:
        """Validate manor_ui.json (build-palette ordering).

        `build_order` governs only the order the build buttons are shown in;
        it does not override the client's display filters (manor level,
        display_name/image presence, affordability, etc.).
        """
        data: JsonDataType = self._validate_json(content, file)
        if data is None:
            return False

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected an object", Severity.ERROR)
            return False

        if "build_order" not in data:
            self._add_issue(file, 1, None, "Missing required field 'build_order'", Severity.ERROR)
            return False

        order = data["build_order"]
        if not isinstance(order, list):
            self._add_issue(file, 1, None, "'build_order' must be an array of building type IDs", Severity.ERROR)
            return False

        valid: bool = True
        seen: set[str] = set()
        for i, entry in enumerate(order):
            if not isinstance(entry, str) or not entry:
                self._add_issue(file, 1, None, f"'build_order'[{i}] must be a non-empty string", Severity.ERROR)
                valid = False
                continue
            if entry in seen:
                self._add_issue(file, 1, None, f"Duplicate building type in build_order: '{entry}'", Severity.ERROR)
                valid = False
                continue
            seen.add(entry)
            if entry not in self.validated_building_ids:
                self._add_issue(
                    file, 1, None,
                    f"'build_order' references unknown building type '{entry}'",
                    Severity.ERROR,
                )
                valid = False

        # Warn about buildable buildings not listed — they still appear, just
        # after every listed type (stable order).
        buildable: set[str] = set()
        if buildings_file.exists():
            try:
                building_data = json.loads(buildings_file.read_text(encoding="utf-8"))
                if isinstance(building_data, list):
                    for entry in building_data:
                        if not isinstance(entry, dict):
                            continue
                        for building_id, cfg in entry.items():
                            if (isinstance(cfg, dict) and building_id != "home_base"
                                    and cfg.get("display_name") and cfg.get("image")):
                                buildable.add(building_id)
            except (json.JSONDecodeError, OSError):
                buildable = set(self.validated_building_ids - {"home_base", "house"})

        missing = sorted(buildable - seen)
        if missing:
            self._add_issue(
                file, 1, None,
                f"Buildable buildings missing from build_order (shown last): {', '.join(missing)}",
                Severity.WARN,
            )

        return valid

    def validate_manor_river(self, file: Path, content: str) -> bool:
        """Validate manor_river.json (per-fiefdom river templates).

        Each template is a meandering polyline authored in a canonical corner:
        `points` is a non-empty array of integer [x, y] pairs entering one board
        edge and exiting an adjacent edge; `width` is the band thickness in
        cells. The server rotates templates by 0/90/180/270 degrees per fiefdom.
        """
        data: JsonDataType = self._validate_json(content, file)
        if data is None:
            return False

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected an object", Severity.ERROR)
            return False

        templates = data.get("templates")
        if not isinstance(templates, list) or not templates:
            self._add_issue(file, 1, None, "'templates' must be a non-empty array", Severity.ERROR)
            return False

        valid: bool = True
        seen_ids: set[str] = set()
        for i, tmpl in enumerate(templates):
            if not isinstance(tmpl, dict):
                self._add_issue(file, 1, None, f"templates[{i}] must be an object", Severity.ERROR)
                valid = False
                continue

            tid: Any = tmpl.get("id")
            if not isinstance(tid, str) or not tid:
                self._add_issue(file, 1, None, f"templates[{i}].id must be a "
                                "non-empty string", Severity.ERROR)
                valid = False
            elif tid in seen_ids:
                self._add_issue(file, 1, None, f"templates[{i}].id '{tid}' "
                                "is duplicated", Severity.ERROR)
                valid = False
            else:
                seen_ids.add(tid)

            width: Any = tmpl.get("width", 1)
            if not isinstance(width, int) or isinstance(width, bool) or width < 1:
                self._add_issue(file, 1, None, f"templates[{i}].width must be "
                                "an integer >= 1", Severity.ERROR)
                valid = False

            points: Any = tmpl.get("points")
            if not isinstance(points, list) or not points:
                self._add_issue(file, 1, None, f"templates[{i}].points must be "
                                "a non-empty array of [x,y] pairs", Severity.ERROR)
                valid = False
            else:
                for j, p in enumerate(points):
                    if (not isinstance(p, list) or len(p) != 2
                            or not all(isinstance(v, int) and not isinstance(v, bool) for v in p)):
                        self._add_issue(file, 1, None, f"templates[{i}].points"
                                        f"[{j}] must be an [x,y] integer pair", Severity.ERROR)
                        valid = False

        return valid

    def validate_portrait_frames(self, file: Path, content: str, images_dir: Path) -> bool:
        """Validate portrait_frames.json (decorative portrait overlay geometry).

        Each `frames` entry names an overlay `image` (a served `/images/...`
        URL), its pixel size (`image_width`/`image_height`), and the `portrait`
        rectangle in those same pixels that the portrait image is mapped into.
        The frame is drawn on top of the portrait, so the rectangle is a draw
        box, not a mask.
        """
        data: JsonDataType = self._validate_json(content, file)
        if data is None:
            return False
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected an object", Severity.ERROR)
            return False

        frames: Any = data.get("frames")
        if not isinstance(frames, dict) or not frames:
            self._add_issue(file, 1, None, "'frames' must be a non-empty object", Severity.ERROR)
            return False

        valid: bool = True
        for variant, frame in frames.items():
            if not isinstance(frame, dict):
                self._add_issue(file, 1, None,
                                f"frames['{variant}'] must be an object", Severity.ERROR)
                valid = False
                continue

            image: Any = frame.get("image")
            if not isinstance(image, str) or not image:
                self._add_issue(file, 1, None,
                                f"frames['{variant}'].image must be a non-empty string",
                                Severity.ERROR)
                valid = False
            else:
                # `image` is a served URL (/images/...); map it back to disk and
                # warn (not error) while the art is still being authored.
                relative: str = image[len("/images/"):] if image.startswith("/images/") \
                    else image.lstrip("/")
                if not (images_dir / relative).exists():
                    self._add_issue(file, 1, None,
                                    f"frames['{variant}'].image not found on disk "
                                    f"(TODO: art): {image}", Severity.WARN)

            image_width: Any = frame.get("image_width")
            image_height: Any = frame.get("image_height")
            if not isinstance(image_width, int) or isinstance(image_width, bool) or image_width <= 0:
                self._add_issue(file, 1, None,
                                f"frames['{variant}'].image_width must be a positive integer",
                                Severity.ERROR)
                valid = False
            if not isinstance(image_height, int) or isinstance(image_height, bool) or image_height <= 0:
                self._add_issue(file, 1, None,
                                f"frames['{variant}'].image_height must be a positive integer",
                                Severity.ERROR)
                valid = False

            rect: Any = frame.get("portrait")
            if not isinstance(rect, dict):
                self._add_issue(file, 1, None,
                                f"frames['{variant}'].portrait must be an object",
                                Severity.ERROR)
                valid = False
                continue

            rect_x: Any = rect.get("x")
            rect_y: Any = rect.get("y")
            rect_w: Any = rect.get("width")
            rect_h: Any = rect.get("height")
            rect_ok: bool = True
            for name, value, allow_zero in (("x", rect_x, True), ("y", rect_y, True),
                                            ("width", rect_w, False), ("height", rect_h, False)):
                if not isinstance(value, (int, float)) or isinstance(value, bool):
                    self._add_issue(file, 1, None,
                                    f"frames['{variant}'].portrait.{name} must be a number",
                                    Severity.ERROR)
                    rect_ok = False
                elif allow_zero and value < 0:
                    self._add_issue(file, 1, None,
                                    f"frames['{variant}'].portrait.{name} must be >= 0",
                                    Severity.ERROR)
                    rect_ok = False
                elif not allow_zero and value <= 0:
                    self._add_issue(file, 1, None,
                                    f"frames['{variant}'].portrait.{name} must be > 0",
                                    Severity.ERROR)
                    rect_ok = False
            if not rect_ok:
                valid = False

            numbers_ok: bool = all(
                isinstance(v, (int, float)) and not isinstance(v, bool)
                for v in (image_width, image_height, rect_x, rect_y, rect_w, rect_h)
            )
            if numbers_ok and (rect_x + rect_w > image_width or rect_y + rect_h > image_height):
                self._add_issue(file, 1, None,
                                f"frames['{variant}'].portrait exceeds the frame image "
                                f"bounds ({image_width}x{image_height})", Severity.ERROR)
                valid = False

        self.validated_files.append(file)
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

        if "morale_boost" in data:
            boost_array: Any = data["morale_boost"]
            if not isinstance(boost_array, list):
                self._add_issue(
                    file, 1, None,
                    f"Hero '{hero_id}'.morale_boost must be an array",
                    Severity.ERROR
                )
                valid = False
            else:
                for i, boost_val in enumerate(boost_array):
                    if not isinstance(boost_val, (int, float)):
                        self._add_issue(
                            file, 1, None,
                            f"Hero '{hero_id}'.morale_boost[{i}] must be a number, got {type(boost_val).__name__}",
                            Severity.ERROR
                        )
                        valid = False
                    elif boost_val < 0:
                        self._add_issue(
                            file, 1, None,
                            f"Hero '{hero_id}'.morale_boost[{i}] must be >= 0, got {boost_val}",
                            Severity.ERROR
                        )
                        valid = False

        return valid

    def validate_manor_strategies(self, file: Path, content: str) -> bool:
        """Validate analyzer_manor_strategies.json (analyzer-only).

        Each heuristic defines a weighted build policy plus optional minimum
        ratios between buildings. Weights must be non-negative with a positive
        total; min_ratio counts must be non-negative integers. All referenced
        building types must exist in the building registry.
        """
        data: JsonDataType = self._validate_json(content, file)
        if data is None:
            return False
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected an object", Severity.ERROR)
            return False
        if "heuristics" not in data:
            self._add_issue(file, 1, None, "Missing required field 'heuristics'", Severity.ERROR)
            return False
        if not isinstance(data["heuristics"], list):
            self._add_issue(file, 1, None, "'heuristics' must be an array", Severity.ERROR)
            return False

        valid: bool = True
        seen_ids: set[str] = set()
        for i, h in enumerate(data["heuristics"]):
            if not isinstance(h, dict):
                self._add_issue(file, 1, None, f"'heuristics'[{i}] must be an object", Severity.ERROR)
                valid = False
                continue

            hid = h.get("id")
            if not isinstance(hid, str) or not hid:
                self._add_issue(file, 1, None, f"'heuristics'[{i}].id must be a non-empty string", Severity.ERROR)
                valid = False
                continue
            if hid in seen_ids:
                self._add_issue(file, 1, None, f"Duplicate heuristic id '{hid}'", Severity.ERROR)
                valid = False
            seen_ids.add(hid)

            weights = h.get("weights", {})
            if not isinstance(weights, dict):
                self._add_issue(file, 1, None, f"Heuristic '{hid}'.weights must be an object", Severity.ERROR)
                valid = False
            else:
                total = 0.0
                for wid, w in weights.items():
                    if wid not in self.validated_building_ids:
                        self._add_issue(
                            file, 1, None,
                            f"Heuristic '{hid}'.weights references unknown building type '{wid}'",
                            Severity.ERROR,
                        )
                        valid = False
                    if not isinstance(w, (int, float)):
                        self._add_issue(
                            file, 1, None,
                            f"Heuristic '{hid}'.weights.{wid} must be a number",
                            Severity.ERROR,
                        )
                        valid = False
                    elif w < 0:
                        self._add_issue(
                            file, 1, None,
                            f"Heuristic '{hid}'.weights.{wid} must be >= 0, got {w}",
                            Severity.ERROR,
                        )
                        valid = False
                    else:
                        total += float(w)
                if total <= 0.0:
                    self._add_issue(
                        file, 1, None,
                        f"Heuristic '{hid}' has no positive weights (total <= 0); it can never build anything",
                        Severity.WARN,
                    )

            ratios = h.get("min_ratios", {})
            if ratios is not None and not isinstance(ratios, dict):
                self._add_issue(file, 1, None, f"Heuristic '{hid}'.min_ratios must be an object", Severity.ERROR)
                valid = False
            elif isinstance(ratios, dict):
                for aid, spec in ratios.items():
                    if aid not in self.validated_building_ids:
                        self._add_issue(
                            file, 1, None,
                            f"Heuristic '{hid}'.min_ratios references unknown building type '{aid}'",
                            Severity.ERROR,
                        )
                        valid = False
                    if not isinstance(spec, dict):
                        self._add_issue(
                            file, 1, None,
                            f"Heuristic '{hid}'.min_ratios.{aid} must be an object of building -> count",
                            Severity.ERROR,
                        )
                        valid = False
                        continue
                    for bid, n in spec.items():
                        if bid not in self.validated_building_ids:
                            self._add_issue(
                                file, 1, None,
                                f"Heuristic '{hid}'.min_ratios.{aid} references unknown building type '{bid}'",
                                Severity.ERROR,
                            )
                            valid = False
                        if not isinstance(n, int) or isinstance(n, bool) or n < 0:
                            self._add_issue(
                                file, 1, None,
                                f"Heuristic '{hid}'.min_ratios.{aid}.{bid} must be a non-negative integer, got {n!r}",
                                Severity.ERROR,
                            )
                            valid = False

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

        if "morale_boost" in data:
            boost_array: Any = data["morale_boost"]
            if not isinstance(boost_array, list):
                self._add_issue(
                    file, 1, None,
                    f"Official '{official_id}'.morale_boost must be an array",
                    Severity.ERROR
                )
                valid = False
            else:
                for i, boost_val in enumerate(boost_array):
                    if not isinstance(boost_val, (int, float)):
                        self._add_issue(
                            file, 1, None,
                            f"Official '{official_id}'.morale_boost[{i}] must be a number, got {type(boost_val).__name__}",
                            Severity.ERROR
                        )
                        valid = False
                    elif boost_val < 0:
                        self._add_issue(
                            file, 1, None,
                            f"Official '{official_id}'.morale_boost[{i}] must be >= 0, got {boost_val}",
                            Severity.ERROR
                        )
                        valid = False

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

    def validate_wall_config(self, file: Path, content: str) -> bool:
        """Validate wall_config.json."""
        data: JsonDataType = self._validate_json(content, file)
        if data is None:
            return False

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object for wall configuration", Severity.ERROR)
            return False

        valid: bool = True

        if "walls" not in data:
            self._add_issue(file, 1, None, "Missing 'walls' key", Severity.ERROR)
            return False

        walls = data["walls"]
        if not isinstance(walls, dict):
            self._add_issue(file, 1, None, "'walls' must be an object", Severity.ERROR)
            return False

        for gen_key, wall_data in walls.items():
            try:
                generation = int(gen_key)
            except ValueError:
                self._add_issue(file, 1, None, f"Invalid generation key: {gen_key}", Severity.ERROR)
                valid = False
                continue

            if generation < 1:
                self._add_issue(file, 1, None, f"Generation must be >= 1, got {generation}", Severity.ERROR)
                valid = False

            if not isinstance(wall_data, dict):
                self._add_issue(file, 1, None, f"Wall {gen_key}: must be an object", Severity.ERROR)
                valid = False
                continue

            for field in ["width", "length", "thickness"]:
                if field not in wall_data:
                    self._add_issue(file, 1, None, f"Wall {gen_key}: Missing '{field}'", Severity.ERROR)
                    valid = False
                else:
                    val = wall_data[field]
                    if not isinstance(val, int):
                        self._add_issue(file, 1, None, f"Wall {gen_key}: {field} must be integer", Severity.ERROR)
                        valid = False
                    elif val % 2 != 0:
                        self._add_issue(file, 1, None, f"Wall {gen_key}: {field} must be divisible by 2", Severity.ERROR)
                        valid = False
                    elif val <= 0:
                        self._add_issue(file, 1, None, f"Wall {gen_key}: {field} must be positive", Severity.ERROR)
                        valid = False

            for field in ["gold_cost", "hp", "morale_boost", "construction_times"]:
                if field not in wall_data:
                    continue
                if field == "gold_cost":
                    self._validate_money_cost_array(
                        file, content, f"generation_{gen_key}", wall_data, field, entity_kind="Wall"
                    )
                    continue
                arr = wall_data[field]
                if not isinstance(arr, list):
                    self._add_issue(file, 1, None, f"Wall {gen_key}: {field} must be an array", Severity.ERROR)
                    valid = False
                else:
                    for i, val in enumerate(arr):
                        if not isinstance(val, (int, float)):
                            self._add_issue(file, 1, None, f"Wall {gen_key}: {field}[{i}] must be a number", Severity.ERROR)
                            valid = False

        return valid

    def _validate_image_file_numbering(self, dir_path: Path) -> list[str]:
        """Validate that files in directory follow numeric naming convention."""
        valid_extensions = {".png", ".jpg", ".jpeg", ".gif", ".svg", ".webp", ".avif"}
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

        # Combatants: art is optional (classes without art render procedurally).
        # Directional clips (walk/idle) keep their frames one level deeper under
        # <clip>/<set>/ and are validated by tools/check_art.py; here we only
        # recognize the clip directories so real ones aren't flagged as orphans.
        for combatant_id in self.validated_combatant_ids:
            optional.add(("combatants", combatant_id, "idle"))
            optional.add(("combatants", combatant_id, "walk"))
            optional.add(("combatants", combatant_id, "attack"))
            optional.add(("combatants", combatant_id, "defend"))
            optional.add(("combatants", combatant_id, "die"))

        # Buildings: construction, idle (required), harvest (optional)
        # Skip buildings with explicit image field (they use external images, not server images/)
        for building_id in self.validated_building_ids:
            if building_id in self.external_image_building_ids:
                continue
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
        """Validate the images directory structure against config files.

        Only the entity directories (combatants, buildings, heroes, portraits)
        are validated; unrelated game asset directories (e.g. tower_defense,
        ui, weeding) are ignored.
        """
        entity_types: set[str] = {"combatants", "buildings", "heroes", "portraits"}

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
                if img_type not in entity_types:
                    continue
                item_id = relative_parts[1]
                subtype = relative_parts[2]
                found_dirs.add((img_type, item_id, subtype))
            elif len(relative_parts) == 2:
                img_type = relative_parts[0]
                if img_type not in entity_types:
                    continue
                # A `type/id/` directory that holds clip subdirectories (the
                # normal layout, e.g. combatants/<class>/walk/<set>/) is fine; a
                # leaf at this depth has no recognized subtype and is orphaned.
                if any(child.is_dir() for child in root_path.iterdir()):
                    continue
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

            # Check if directory is empty. Recurse so directional clip dirs
            # (e.g. combatants/<class>/walk/<set>/<n>.png) count as populated.
            has_files = any(entry.is_file() for entry in dir_path.rglob("*"))

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

    def validate_portrait_pools(self, images_dir: Path) -> None:
        """Validate the retinue headshot/avatar pools and frame overlays.

        These are data-driven pools (no catalog config): the server scans and
        sorts the directories at startup. Required:
          - images/retinue/headshots/{male,female}/ (non-empty)
        Missing-until-authored (warnings, so the build stays green before art
        lands):
          - images/retinue/frames/ overlay images (checked by
            validate_portrait_frames against game/config/portrait_frames.json)
          - images/avatars/safe/{male,female}/ (non-empty)
        """
        allowed_ext: set[str] = {".png", ".jpg", ".jpeg", ".webp", ".avif"}

        def pool_files(directory: Path) -> tuple[int, list[str]]:
            if not directory.exists():
                return 0, []
            count = 0
            invalid: list[str] = []
            for entry in directory.iterdir():
                if not entry.is_file():
                    continue
                if entry.suffix.lower() in allowed_ext:
                    count += 1
                elif not entry.name.startswith("."):
                    invalid.append(entry.name)
            return count, invalid

        for gender in ("male", "female"):
            directory = images_dir / "retinue" / "headshots" / gender
            if not directory.exists():
                self._add_issue(
                    directory, 1, None,
                    f"Missing headshot pool: images/retinue/headshots/{gender}/",
                    Severity.ERROR
                )
                continue
            count, invalid = pool_files(directory)
            if count == 0:
                self._add_issue(
                    directory, 1, None,
                    f"Empty headshot pool: images/retinue/headshots/{gender}/",
                    Severity.ERROR
                )
            for name in invalid:
                self._add_issue(
                    directory, 1, None,
                    f"Unsupported image format in images/retinue/headshots/{gender}/: {name}",
                    Severity.WARN
                )

        # The safe player-avatar pool is gender-split like the retinue headshots.
        for gender in ("male", "female"):
            safe_dir = images_dir / "avatars" / "safe" / gender
            if not safe_dir.exists():
                self._add_issue(
                    safe_dir, 1, None,
                    f"Missing safe avatar pool (TODO: art): images/avatars/safe/{gender}/",
                    Severity.WARN
                )
                continue
            count, invalid = pool_files(safe_dir)
            if count == 0:
                self._add_issue(
                    safe_dir, 1, None,
                    f"Empty safe avatar pool (TODO: art): images/avatars/safe/{gender}/",
                    Severity.WARN
                )
            for name in invalid:
                self._add_issue(
                    safe_dir, 1, None,
                    f"Unsupported image format in images/avatars/safe/{gender}/: {name}",
                    Severity.WARN
                )

    def validate_spawn_schedules(self, game_config_dir: Path) -> None:
        """Validate tower defense spawn schedule files."""
        spawn_dir: Path = game_config_dir / "tower_defense" / "spawn_schedules"
        if not spawn_dir.exists():
            return

        # Load mobs.json for enemy_id cross-referencing
        mobs_file: Path = game_config_dir / "tower_defense" / "mobs.json"
        valid_enemy_ids: set[str] = set()

        if mobs_file.exists():
            try:
                mobs_content: str = mobs_file.read_text(encoding="utf-8")
                mobs_data: object = self._validate_json(mobs_content, mobs_file)
                if isinstance(mobs_data, dict) and "mobs" in mobs_data:
                    mobs_obj: object = mobs_data["mobs"]
                    if isinstance(mobs_obj, dict):
                        valid_enemy_ids = set(mobs_obj.keys())
            except Exception:
                pass
        else:
            self._add_issue(
                mobs_file, 1, None,
                "Can't find mobs.json for enemy_id cross-reference; skipping cross-reference checks",
                Severity.WARN
            )

        for schedule_file in sorted(spawn_dir.iterdir()):
            if not schedule_file.is_file() or schedule_file.suffix != ".json":
                continue

            try:
                content: str = schedule_file.read_text(encoding="utf-8")
            except Exception as e:
                self._add_issue(
                    schedule_file, 1, None,
                    f"Failed to read file: {e}",
                    Severity.ERROR
                )
                continue

            data: object = self._validate_json(content, schedule_file)
            if data is None:
                continue

            if not isinstance(data, dict):
                self._add_issue(
                    schedule_file, 1, None,
                    "Expected a JSON object",
                    Severity.ERROR
                )
                continue

            if "display_name_key" not in data:
                self._add_issue(
                    schedule_file, 1, None,
                    "Missing required key: display_name_key",
                    Severity.ERROR
                )
            elif not isinstance(data["display_name_key"], str):
                self._add_issue(
                    schedule_file, 1, None,
                    "display_name_key must be a string",
                    Severity.ERROR
                )

            if "levels" not in data:
                self._add_issue(
                    schedule_file, 1, None,
                    "Missing required key: levels",
                    Severity.ERROR
                )
                continue

            levels_obj: object = data["levels"]
            if not isinstance(levels_obj, dict):
                self._add_issue(
                    schedule_file, 1, None,
                    "levels must be an object",
                    Severity.ERROR
                )
                continue

            for level_key, level_data in levels_obj.items():
                if not level_key.isdigit():
                    self._add_issue(
                        schedule_file, 1, None,
                        f"Level key '{level_key}' is not a numeric string",
                        Severity.WARN
                    )

                if not isinstance(level_data, dict):
                    self._add_issue(
                        schedule_file, 1, None,
                        f"Level '{level_key}' must be an object",
                        Severity.ERROR
                    )
                    continue

                if "rounds" not in level_data:
                    self._add_issue(
                        schedule_file, 1, None,
                        f"Level '{level_key}' missing required key: rounds",
                        Severity.ERROR
                    )
                    continue

                rounds_obj: object = level_data["rounds"]
                if not isinstance(rounds_obj, list):
                    self._add_issue(
                        schedule_file, 1, None,
                        f"Level '{level_key}'.rounds must be an array",
                        Severity.ERROR
                    )
                    continue

                for round_idx, round_data in enumerate(rounds_obj):
                    if not isinstance(round_data, list):
                        self._add_issue(
                            schedule_file, 1, None,
                            f"Level '{level_key}'.rounds[{round_idx}] must be an array of spawn entries",
                            Severity.ERROR
                        )
                        continue

                    for entry_idx, entry in enumerate(round_data):
                        if not isinstance(entry, dict):
                            self._add_issue(
                                schedule_file, 1, None,
                                f"Level '{level_key}'.rounds[{round_idx}][{entry_idx}] must be an object",
                                Severity.ERROR
                            )
                            continue

                        for field in ("enemy_id", "count", "interval_ms", "initial_delay_ms"):
                            if field not in entry:
                                self._add_issue(
                                    schedule_file, 1, None,
                                    f"Level '{level_key}'.rounds[{round_idx}][{entry_idx}] missing required field: {field}",
                                    Severity.ERROR
                                )

                        enemy_id: object = entry.get("enemy_id")
                        if isinstance(enemy_id, str):
                            if valid_enemy_ids and enemy_id not in valid_enemy_ids:
                                self._add_issue(
                                    schedule_file, 1, None,
                                    f"Level '{level_key}'.rounds[{round_idx}][{entry_idx}]: "
                                    f"unknown enemy_id '{enemy_id}'",
                                    Severity.ERROR
                                )
                        elif enemy_id is not None:
                            self._add_issue(
                                schedule_file, 1, None,
                                f"Level '{level_key}'.rounds[{round_idx}][{entry_idx}]: "
                                f"enemy_id must be a string",
                                Severity.ERROR
                            )

                        count_val: object = entry.get("count")
                        if isinstance(count_val, int) and count_val < 1:
                            self._add_issue(
                                schedule_file, 1, None,
                                f"Level '{level_key}'.rounds[{round_idx}][{entry_idx}]: "
                                f"count must be >= 1, got {count_val}",
                                Severity.ERROR
                            )

                        interval_val: object = entry.get("interval_ms")
                        if isinstance(interval_val, int) and interval_val <= 0:
                            self._add_issue(
                                schedule_file, 1, None,
                                f"Level '{level_key}'.rounds[{round_idx}][{entry_idx}]: "
                                f"interval_ms must be > 0, got {interval_val}",
                                Severity.ERROR
                            )

                        delay_val: object = entry.get("initial_delay_ms")
                        if isinstance(delay_val, int) and delay_val < 0:
                            self._add_issue(
                                schedule_file, 1, None,
                                f"Level '{level_key}'.rounds[{round_idx}][{entry_idx}]: "
                                f"initial_delay_ms must be >= 0, got {delay_val}",
                                Severity.ERROR
                            )

            self.validated_files.append(schedule_file)

    def validate_wave_templates(self, game_config_dir: Path) -> None:
        """Validate tower_defense/wave_templates.json."""
        wt_file: Path = game_config_dir / "tower_defense" / "wave_templates.json"
        if not wt_file.exists():
            return

        try:
            content: str = wt_file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(wt_file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, wt_file)
        if data is None:
            return

        if not isinstance(data, dict):
            self._add_issue(wt_file, 1, None, "Expected a JSON object", Severity.ERROR)
            return

        # Validate mob_unlocks
        if "mob_unlocks" in data:
            mu: object = data["mob_unlocks"]
            if isinstance(mu, dict):
                for key, val in mu.items():
                    try:
                        int(key)
                    except ValueError:
                        self._add_issue(wt_file, 1, None, f"mob_unlocks key '{key}' must be an integer", Severity.ERROR)
                    if not isinstance(val, list):
                        self._add_issue(wt_file, 1, None, f"mob_unlocks.{key} must be an array of mob IDs", Severity.ERROR)
            else:
                self._add_issue(wt_file, 1, None, "mob_unlocks must be an object", Severity.ERROR)

        # Validate difficulty_templates
        if "difficulty_templates" in data:
            dt: object = data["difficulty_templates"]
            if isinstance(dt, dict):
                for diff_key, diff_val in dt.items():
                    try:
                        int(diff_key)
                    except ValueError:
                        self._add_issue(wt_file, 1, None, f"difficulty_templates key '{diff_key}' must be an integer", Severity.ERROR)
                        continue

                    if not isinstance(diff_val, dict):
                        self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key} must be an object", Severity.ERROR)
                        continue

                    rounds_val: object = diff_val.get("rounds")
                    if not isinstance(rounds_val, list):
                        self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds must be an array", Severity.ERROR)
                        continue

                    for round_idx, round_val in enumerate(rounds_val):
                        if not isinstance(round_val, list):
                            self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds[{round_idx}] must be an array", Severity.ERROR)
                            continue

                        for entry_idx, entry in enumerate(round_val):
                            if not isinstance(entry, dict):
                                self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds[{round_idx}][{entry_idx}] must be an object", Severity.ERROR)
                                continue

                            has_mobs: bool = "mobs" in entry
                            has_escalation: bool = "escalation" in entry

                            if not has_mobs and not has_escalation:
                                self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds[{round_idx}][{entry_idx}]: missing mobs or escalation", Severity.ERROR)
                                continue

                            if has_mobs:
                                mobs_val: object = entry["mobs"]
                                if not isinstance(mobs_val, list) or len(mobs_val) == 0:
                                    self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds[{round_idx}][{entry_idx}].mobs must be a non-empty array", Severity.ERROR)

                                for field in ("count", "interval_ms", "initial_delay_ms"):
                                    if field not in entry:
                                        self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds[{round_idx}][{entry_idx}].{field} is required", Severity.ERROR)
                                    else:
                                        fv: object = entry[field]
                                        if isinstance(fv, dict):
                                            has_min: bool = "min" in fv
                                            has_max: bool = "max" in fv
                                            if not has_min or not has_max:
                                                self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds[{round_idx}][{entry_idx}].{field} must have min and max", Severity.ERROR)
                                            if has_min and has_max:
                                                mn = fv["min"]
                                                mx = fv["max"]
                                                if not isinstance(mn, (int, float)) or not isinstance(mx, (int, float)):
                                                    self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds[{round_idx}][{entry_idx}].{field} min/max must be numbers", Severity.ERROR)
                                                elif mn > mx:
                                                    self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds[{round_idx}][{entry_idx}].{field} min > max", Severity.ERROR)
                                        else:
                                            self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds[{round_idx}][{entry_idx}].{field} must be an object with min/max", Severity.ERROR)

                            if has_escalation:
                                esc: object = entry["escalation"]
                                if not isinstance(esc, dict):
                                    self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds[{round_idx}][{entry_idx}].escalation must be an object", Severity.ERROR)
                                else:
                                    for ef in ("count", "interval_ms", "initial_delay_ms"):
                                        if ef in esc:
                                            efv: object = esc[ef]
                                            if isinstance(efv, dict):
                                                if "min" not in efv or "max" not in efv:
                                                    self._add_issue(wt_file, 1, None, f"difficulty_templates.{diff_key}.rounds[{round_idx}][{entry_idx}].escalation.{ef} must have min and max", Severity.ERROR)
        else:
            self._add_issue(wt_file, 1, None, "Missing required top-level key: difficulty_templates", Severity.ERROR)

        self.validated_files.append(wt_file)

    def validate_weeding_plants(self, file: Path) -> None:
        """Validate weeding/plants.json — hp, min_difficulty, damage in tool entries."""
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with plant definitions", Severity.ERROR)
            return

        for plant_id, plant_data in data.items():
            if not isinstance(plant_data, dict):
                continue

            is_smother: bool = plant_data.get("is_smother_crop", False)
            if is_smother:
                continue

            if "min_difficulty" not in plant_data:
                self._add_issue(file, 1, None, f"Weed '{plant_id}' is missing required field 'min_difficulty'", Severity.ERROR)
            else:
                md: object = plant_data["min_difficulty"]
                if not isinstance(md, int):
                    self._add_issue(file, 1, None, f"Weed '{plant_id}'.min_difficulty must be an integer, got {type(md).__name__}", Severity.ERROR)
                elif md < 1 or md > 4:
                    self._add_issue(file, 1, None, f"Weed '{plant_id}'.min_difficulty must be 1-4, got {md}", Severity.ERROR)

            # Validate hp field
            if "hp" not in plant_data:
                self._add_issue(file, 1, None, f"Weed '{plant_id}' is missing required field 'hp'", Severity.ERROR)
            else:
                hp_val: object = plant_data["hp"]
                if not isinstance(hp_val, int):
                    self._add_issue(file, 1, None, f"Weed '{plant_id}'.hp must be an integer, got {type(hp_val).__name__}", Severity.ERROR)
                elif hp_val < 1:
                    self._add_issue(file, 1, None, f"Weed '{plant_id}'.hp must be positive, got {hp_val}", Severity.ERROR)

            # Validate damage_sprites if present
            ds: object = plant_data.get("damage_sprites")
            if ds is not None:
                if not isinstance(ds, list) or not all(isinstance(v, int) for v in ds):
                    self._add_issue(file, 1, None, f"Weed '{plant_id}'.damage_sprites must be an array of integers", Severity.ERROR)

            # Validate tool entries have damage field
            tools: object = plant_data.get("tools")
            if isinstance(tools, dict):
                for tool_id, tool_entry in tools.items():
                    if not isinstance(tool_entry, dict):
                        continue
                    if "damage" not in tool_entry:
                        self._add_issue(file, 1, None, f"Weed '{plant_id}'.tools.{tool_id} is missing required field 'damage'", Severity.ERROR)
                    else:
                        dmg: object = tool_entry["damage"]
                        if not isinstance(dmg, int):
                            self._add_issue(file, 1, None, f"Weed '{plant_id}'.tools.{tool_id}.damage must be an integer, got {type(dmg).__name__}", Severity.ERROR)
                        elif dmg < 1 or dmg > 100:
                            self._add_issue(file, 1, None, f"Weed '{plant_id}'.tools.{tool_id}.damage must be 1-100, got {dmg}", Severity.ERROR)

    def validate_mini_games_weeding_levels(self, file: Path) -> None:
        """Validate mini_games.json weeding section — schedule_file and baron_schedule_file must exist."""
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with mini game definitions", Severity.ERROR)
            return

        if "weeding" not in data:
            return

        weeding_config: object = data["weeding"]
        if not isinstance(weeding_config, dict):
            return

        # Weeding levels must not have inline difficulty
        for tier_name in ("levels", "baron_levels"):
            levels_array: object = weeding_config.get(tier_name)
            if not isinstance(levels_array, list):
                continue
            for level_entry in levels_array:
                if not isinstance(level_entry, dict):
                    continue
                if "difficulty" in level_entry:
                    level_id: object = level_entry.get("id")
                    self._add_issue(file, 1, None, f"{tier_name} level {level_id}: 'difficulty' should be in the level config file, not inline", Severity.ERROR)

    def validate_weeding_level_config(self, file: Path) -> None:
        """Validate a weeding level config file (wildlands_marche.json / great_wildlands_marche.json)."""
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with level definitions", Severity.ERROR)
            return

        if "levels" not in data or not isinstance(data["levels"], dict):
            self._add_issue(file, 1, None, "Missing required key 'levels' (must be an object)", Severity.ERROR)
            return

        for level_key, level_data in data["levels"].items():
            if not level_key.isdigit():
                self._add_issue(file, 1, None, f"Level key '{level_key}' is not a numeric string", Severity.ERROR)
                continue

            if not isinstance(level_data, dict):
                self._add_issue(file, 1, None, f"Level '{level_key}' must be an object", Severity.ERROR)
                continue

            if "map" not in level_data:
                self._add_issue(file, 1, None, f"Level '{level_key}' missing required field 'map'", Severity.ERROR)
            elif not isinstance(level_data["map"], str):
                self._add_issue(file, 1, None, f"Level '{level_key}'.map must be a string", Severity.ERROR)

            if "difficulty" not in level_data:
                self._add_issue(file, 1, None, f"Level '{level_key}' missing required field 'difficulty'", Severity.ERROR)
            else:
                diff_val: object = level_data["difficulty"]
                if not isinstance(diff_val, int):
                    self._add_issue(file, 1, None, f"Level '{level_key}'.difficulty must be an integer", Severity.ERROR)
                elif diff_val < 1 or diff_val > 10:
                    self._add_issue(file, 1, None, f"Level '{level_key}'.difficulty must be 1-10, got {diff_val}", Severity.ERROR)

            # Validate allowed_tools if present
            allowed_tools: object = level_data.get("allowed_tools")
            if allowed_tools is not None:
                if not isinstance(allowed_tools, list):
                    self._add_issue(file, 1, None, f"Level '{level_key}'.allowed_tools must be an array", Severity.ERROR)
                elif len(allowed_tools) == 0:
                    self._add_issue(file, 1, None, f"Level '{level_key}'.allowed_tools must not be empty", Severity.WARN)
                else:
                    for idx, tool_id in enumerate(allowed_tools):
                        if not isinstance(tool_id, str):
                            self._add_issue(file, 1, None, f"Level '{level_key}'.allowed_tools[{idx}] must be a string", Severity.ERROR)

            # Validate allowed_weeds if present
            allowed_weeds: object = level_data.get("allowed_weeds")
            if allowed_weeds is not None:
                if not isinstance(allowed_weeds, list):
                    self._add_issue(file, 1, None, f"Level '{level_key}'.allowed_weeds must be an array", Severity.ERROR)
                elif len(allowed_weeds) == 0:
                    self._add_issue(file, 1, None, f"Level '{level_key}'.allowed_weeds must not be empty", Severity.WARN)
                else:
                    for idx, weed_id in enumerate(allowed_weeds):
                        if not isinstance(weed_id, str):
                            self._add_issue(file, 1, None, f"Level '{level_key}'.allowed_weeds[{idx}] must be a string", Severity.ERROR)

            # Validate allowed_smother_crops if present (empty array means none allowed)
            allowed_smother: object = level_data.get("allowed_smother_crops")
            if allowed_smother is not None:
                if not isinstance(allowed_smother, list):
                    self._add_issue(file, 1, None, f"Level '{level_key}'.allowed_smother_crops must be an array", Severity.ERROR)
                else:
                    for idx, crop_id in enumerate(allowed_smother):
                        if not isinstance(crop_id, str):
                            self._add_issue(file, 1, None, f"Level '{level_key}'.allowed_smother_crops[{idx}] must be a string", Severity.ERROR)

            # Validate text_intro if present
            text_intro: object = level_data.get("text_intro")
            if text_intro is not None:
                if not isinstance(text_intro, str):
                    self._add_issue(file, 1, None, f"Level '{level_key}'.text_intro must be a string", Severity.ERROR)
                elif len(text_intro) == 0:
                    self._add_issue(file, 1, None, f"Level '{level_key}'.text_intro must not be empty", Severity.ERROR)

            # Validate text_outro if present
            text_outro: object = level_data.get("text_outro")
            if text_outro is not None:
                if not isinstance(text_outro, str):
                    self._add_issue(file, 1, None, f"Level '{level_key}'.text_outro must be a string", Severity.ERROR)
                elif len(text_outro) == 0:
                    self._add_issue(file, 1, None, f"Level '{level_key}'.text_outro must not be empty", Severity.ERROR)

    def validate_weeding_tools(self, file: Path) -> None:
        """Validate weeding/tools.json — forward_vector required in sprite config."""
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with tool definitions", Severity.ERROR)
            return

        for tool_id, tool_data in data.items():
            if not isinstance(tool_data, dict):
                continue

            sprite: object = tool_data.get("sprite", {})
            if not isinstance(sprite, dict):
                self._add_issue(file, 1, None, f"Tool '{tool_id}'.sprite must be an object", Severity.ERROR)
                continue

            if "forward_vector" not in sprite:
                self._add_issue(file, 1, None, f"Tool '{tool_id}'.sprite missing required field 'forward_vector'", Severity.ERROR)
            else:
                fv: object = sprite["forward_vector"]
                if not isinstance(fv, list) or len(fv) != 2:
                    self._add_issue(file, 1, None, f"Tool '{tool_id}'.sprite.forward_vector must be a 2-element array", Severity.ERROR)
                else:
                    for i, val in enumerate(fv):
                        if not isinstance(val, int) or val < -1 or val > 1:
                            self._add_issue(file, 1, None, f"Tool '{tool_id}'.sprite.forward_vector[{i}] must be -1, 0, or 1", Severity.ERROR)
                    if fv[0] == 0 and fv[1] == 0:
                        self._add_issue(file, 1, None, f"Tool '{tool_id}'.sprite.forward_vector cannot be [0, 0]", Severity.ERROR)

            # Validate sprite_up_vector if present
            suv: object = sprite.get("sprite_up_vector")
            if suv is not None:
                if not isinstance(suv, list) or len(suv) != 2:
                    self._add_issue(file, 1, None, f"Tool '{tool_id}'.sprite.sprite_up_vector must be a 2-element array", Severity.ERROR)
                else:
                    for i, val in enumerate(suv):
                        if not isinstance(val, int) or val < -1 or val > 1:
                            self._add_issue(file, 1, None, f"Tool '{tool_id}'.sprite.sprite_up_vector[{i}] must be -1, 0, or 1", Severity.ERROR)
                    if suv[0] == 0 and suv[1] == 0:
                        self._add_issue(file, 1, None, f"Tool '{tool_id}'.sprite.sprite_up_vector cannot be [0, 0]", Severity.ERROR)

    def validate_ongoing_config(self, file: Path) -> None:
        """Validate a game's ongoing.json (tower_defense/ongoing.json or weeding/ongoing.json)."""
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with ongoing config", Severity.ERROR)
            return

        if "game" not in data or not isinstance(data["game"], str) or not data["game"]:
            self._add_issue(file, 1, None, "Missing required field 'game' (string)", Severity.ERROR)

        # difficulty_options
        diff_opts: object = data.get("difficulty_options")
        if not isinstance(diff_opts, list) or not diff_opts:
            self._add_issue(file, 1, None, "'difficulty_options' must be a non-empty array", Severity.ERROR)
        elif not all(isinstance(d, int) and d >= 1 for d in diff_opts):
            self._add_issue(file, 1, None, "'difficulty_options' must contain only positive integers", Severity.ERROR)

        # default_difficulty must be offered
        default_diff: object = data.get("default_difficulty", 1)
        if isinstance(default_diff, int) and isinstance(diff_opts, list) and default_diff not in diff_opts:
            self._add_issue(file, 1, None, f"'default_difficulty' {default_diff} not in difficulty_options", Severity.ERROR)

        # difficulty_coeff_pence
        coeff: object = data.get("difficulty_coeff_pence", 0)
        if not isinstance(coeff, int) or coeff < 0:
            self._add_issue(file, 1, None, "'difficulty_coeff_pence' must be a non-negative integer", Severity.ERROR)

        # size_options
        size_opts: object = data.get("size_options")
        if not isinstance(size_opts, list) or not size_opts:
            self._add_issue(file, 1, None, "'size_options' must be a non-empty array", Severity.ERROR)
            return

        seen_sizes: set[int] = set()
        for i, opt in enumerate(size_opts):
            if not isinstance(opt, dict):
                self._add_issue(file, 1, None, f"'size_options[{i}]' must be an object", Severity.ERROR)
                continue
            value: object = opt.get("value")
            reward: object = opt.get("reward_pence")
            if not isinstance(value, int) or value < 1:
                self._add_issue(file, 1, None, f"'size_options[{i}].value' must be a positive integer", Severity.ERROR)
            elif value in seen_sizes:
                self._add_issue(file, 1, None, f"Duplicate size option value {value}", Severity.ERROR)
            else:
                seen_sizes.add(value)
            if not isinstance(reward, int) or reward < 0:
                self._add_issue(file, 1, None, f"'size_options[{i}].reward_pence' must be a non-negative integer", Severity.ERROR)

        # default_size must be offered
        default_size: object = data.get("default_size")
        if isinstance(default_size, int) and default_size not in seen_sizes:
            self._add_issue(file, 1, None, f"'default_size' {default_size} not in size_options", Severity.ERROR)

        self.validated_files.append(file)

    def _validate_money_price_map(self, file: Path, section: str, price_map: object) -> None:
        """Validate a resource→price map (import_prices / export_prices). Each
        value is a positive number (gold) or an object {gold, shillings, pence}
        with non-negative values and at least one key (penny market)."""
        if price_map is None:
            return
        if not isinstance(price_map, dict):
            self._add_issue(file, 1, None, f"'{section}' must be an object", Severity.ERROR)
            return
        valid_keys: Final[set[str]] = {"gold", "shillings", "pence"}
        for res, price in price_map.items():
            if isinstance(price, (int, float)):
                if price <= 0:
                    self._add_issue(
                        file, 1, None,
                        f"'{section}.{res}' must be a positive number, got {price}",
                        Severity.ERROR,
                    )
                continue
            if not isinstance(price, dict):
                self._add_issue(
                    file, 1, None,
                    f"'{section}.{res}' must be a number (gold) or an object {{gold, shillings, pence}}",
                    Severity.ERROR,
                )
                continue
            unknown: set[str] = set(price.keys()) - valid_keys
            if unknown:
                self._add_issue(
                    file, 1, None,
                    f"'{section}.{res}' has unknown keys: {', '.join(sorted(unknown))} "
                    f"(allowed: {', '.join(sorted(valid_keys))})",
                    Severity.ERROR,
                )
            if not price:
                self._add_issue(
                    file, 1, None,
                    f"'{section}.{res}' must specify at least one of gold/shillings/pence",
                    Severity.ERROR,
                )
            for key, value in price.items():
                if not isinstance(value, (int, float)) or value < 0:
                    self._add_issue(
                        file, 1, None,
                        f"'{section}.{res}.{key}' must be a non-negative number, got {value}",
                        Severity.ERROR,
                    )

    def validate_economy(self, file: Path) -> None:
        """Validate economy.json — currency and reward_pools blocks."""
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with economy config", Severity.ERROR)
            return

        currency: object = data.get("currency")
        if currency is not None:
            if not isinstance(currency, dict):
                self._add_issue(file, 1, None, "'currency' must be an object", Severity.ERROR)
            else:
                for field, positive in (("pence_per_shilling", True), ("shillings_per_pound", True), ("pence_per_gold", True)):
                    val: object = currency.get(field)
                    if not isinstance(val, int) or val < (1 if positive else 0):
                        self._add_issue(file, 1, None, f"'currency.{field}' must be a positive integer", Severity.ERROR)

        self._validate_money_price_map(file, "import_prices", data.get("import_prices"))
        self._validate_money_price_map(file, "export_prices", data.get("export_prices"))

        export_multipliers: object = data.get("export_sell_multipliers")
        if export_multipliers is not None:
            if not isinstance(export_multipliers, dict):
                self._add_issue(file, 1, None, "'export_sell_multipliers' must be an object", Severity.ERROR)
            else:
                for res, ratio in export_multipliers.items():
                    if not isinstance(ratio, (int, float)) or ratio <= 0:
                        self._add_issue(
                            file, 1, None,
                            f"'export_sell_multipliers.{res}' must be a positive number, got {ratio}",
                            Severity.ERROR,
                        )

        pools: object = data.get("reward_pools")
        if pools is not None:
            if not isinstance(pools, dict):
                self._add_issue(file, 1, None, "'reward_pools' must be an object", Severity.ERROR)
            else:
                for field in ("full_pool_max", "half_pool_max", "replenish_per_day", "min_reward_pence"):
                    val: object = pools.get(field)
                    if not isinstance(val, int) or val < 1:
                        self._add_issue(file, 1, None, f"'reward_pools.{field}' must be a positive integer", Severity.ERROR)
                for field in ("half_multiplier", "quarter_multiplier"):
                    val: object = pools.get(field)
                    if not isinstance(val, (int, float)) or not (0 < val < 1):
                        self._add_issue(file, 1, None, f"'reward_pools.{field}' must be a number between 0 and 1", Severity.ERROR)

        starting: object = data.get("starting_resources")
        if starting is not None:
            if not isinstance(starting, dict):
                self._add_issue(file, 1, None, "'starting_resources' must be an object", Severity.ERROR)
            else:
                for res, amount in starting.items():
                    if res not in VALID_STARTING_RESOURCES:
                        self._add_issue(
                            file, 1, None,
                            f"'starting_resources.{res}' is not a valid fiefdom resource "
                            f"(allowed: {', '.join(sorted(VALID_STARTING_RESOURCES))})",
                            Severity.ERROR,
                        )
                    elif not isinstance(amount, (int, float)) or amount < 0:
                        self._add_issue(
                            file, 1, None,
                            f"'starting_resources.{res}' must be a non-negative number, got {amount}",
                            Severity.ERROR,
                        )

        arable: object = data.get("arable_land_by_level")
        if arable is not None:
            if not isinstance(arable, list):
                self._add_issue(file, 1, None, "'arable_land_by_level' must be an array", Severity.ERROR)
            elif len(arable) != 11:
                self._add_issue(
                    file, 1, None,
                    f"'arable_land_by_level' must have exactly 11 entries (index 0-10 for manor levels), got {len(arable)}",
                    Severity.ERROR,
                )
            else:
                prev: float = -1.0
                for i, val in enumerate(arable):
                    if not isinstance(val, (int, float)) or isinstance(val, bool):
                        self._add_issue(
                            file, 1, None,
                            f"'arable_land_by_level[{i}]' must be a number, got {val!r}",
                            Severity.ERROR,
                        )
                    elif val < 0:
                        self._add_issue(
                            file, 1, None,
                            f"'arable_land_by_level[{i}]' must be non-negative, got {val}",
                            Severity.ERROR,
                        )
                    elif val < prev:
                        self._add_issue(
                            file, 1, None,
                            f"'arable_land_by_level' must be non-decreasing; index {i} ({val}) < index {i-1} ({prev})",
                            Severity.ERROR,
                        )
                    else:
                        prev = val

        forest: object = data.get("forest_land_by_level")
        if forest is not None:
            if not isinstance(forest, list):
                self._add_issue(file, 1, None, "'forest_land_by_level' must be an array", Severity.ERROR)
            elif len(forest) != 11:
                self._add_issue(
                    file, 1, None,
                    f"'forest_land_by_level' must have exactly 11 entries (index 0-10 for manor levels), got {len(forest)}",
                    Severity.ERROR,
                )
            else:
                prev: float = -1.0
                for i, val in enumerate(forest):
                    if not isinstance(val, (int, float)) or isinstance(val, bool):
                        self._add_issue(
                            file, 1, None,
                            f"'forest_land_by_level[{i}]' must be a number, got {val!r}",
                            Severity.ERROR,
                        )
                    elif val < 0:
                        self._add_issue(
                            file, 1, None,
                            f"'forest_land_by_level[{i}]' must be non-negative, got {val}",
                            Severity.ERROR,
                        )
                    elif val < prev:
                        self._add_issue(
                            file, 1, None,
                            f"'forest_land_by_level' must be non-decreasing; index {i} ({val}) < index {i-1} ({prev})",
                            Severity.ERROR,
                        )
                    else:
                        prev = val

        self.validated_files.append(file)

    def validate_retinue(self, file: Path) -> None:
        """Validate retinue.json — capacity curve, market, recovery, morale."""
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return

        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with retinue config", Severity.ERROR)
            return

        capacity = data.get("retinue_capacity_by_level")
        if capacity is not None:
            if not isinstance(capacity, list):
                self._add_issue(file, 1, None, "'retinue_capacity_by_level' must be an array", Severity.ERROR)
            else:
                if len(capacity) != 11:
                    self._add_issue(
                        file, 1, None,
                        "'retinue_capacity_by_level' must have exactly 11 entries (manor levels 0-10)",
                        Severity.ERROR,
                    )
                prev: int = -1
                for idx, val in enumerate(capacity):
                    if not isinstance(val, int) or isinstance(val, bool) or val < 0:
                        self._add_issue(
                            file, 1, None,
                            f"'retinue_capacity_by_level[{idx}]' must be a non-negative integer",
                            Severity.ERROR,
                        )
                    elif val < prev:
                        self._add_issue(
                            file, 1, None,
                            f"'retinue_capacity_by_level' must be non-decreasing (index {idx})",
                            Severity.ERROR,
                        )
                    else:
                        prev = int(val)

        market = data.get("market")
        if market is not None:
            if not isinstance(market, dict):
                self._add_issue(file, 1, None, "'market' must be an object", Severity.ERROR)
            else:
                for field, minimum in (
                    ("candidates_per_class", 1),
                    ("max_candidate_level_offset", 0),
                    ("grace_buckets", 1),
                ):
                    val: object = market.get(field)
                    if not isinstance(val, int) or isinstance(val, bool) or val < minimum:
                        self._add_issue(
                            file, 1, None,
                            f"'market.{field}' must be an integer >= {minimum}",
                            Severity.ERROR,
                        )

        recovery = data.get("recovery")
        if recovery is not None:
            if not isinstance(recovery, dict):
                self._add_issue(file, 1, None, "'recovery' must be an object", Severity.ERROR)
            else:
                max_hours: object = recovery.get("max_recovery_hours")
                if not isinstance(max_hours, (int, float)) or isinstance(max_hours, bool) or max_hours <= 0:
                    self._add_issue(file, 1, None, "'recovery.max_recovery_hours' must be a positive number", Severity.ERROR)
                min_hp: object = recovery.get("min_deploy_hp")
                if not isinstance(min_hp, (int, float)) or isinstance(min_hp, bool) or not (0 <= min_hp <= 100):
                    self._add_issue(file, 1, None, "'recovery.min_deploy_hp' must be a number between 0 and 100", Severity.ERROR)

        morale = data.get("morale")
        if morale is not None:
            if not isinstance(morale, dict):
                self._add_issue(file, 1, None, "'morale' must be an object", Severity.ERROR)
            else:
                max_bonus: object = morale.get("max_bonus_percent")
                if not isinstance(max_bonus, (int, float)) or isinstance(max_bonus, bool) or max_bonus < 0:
                    self._add_issue(file, 1, None, "'morale.max_bonus_percent' must be a non-negative number", Severity.ERROR)
                for field in ("points_per_percent", "victory_points", "defeat_points", "decay_hours"):
                    val: object = morale.get(field)
                    if val is not None and (not isinstance(val, (int, float)) or isinstance(val, bool) or val <= 0):
                        self._add_issue(file, 1, None, f"'morale.{field}' must be a positive number", Severity.ERROR)
                rec_bonus: object = morale.get("recovery_morale_bonus")
                if rec_bonus is not None and (not isinstance(rec_bonus, (int, float)) or isinstance(rec_bonus, bool) or rec_bonus < 0):
                    self._add_issue(file, 1, None, "'morale.recovery_morale_bonus' must be a non-negative number", Severity.ERROR)

        sell_discount: object = data.get("sell_discount")
        if sell_discount is not None:
            if not isinstance(sell_discount, (int, float)) or isinstance(sell_discount, bool) or not (0 <= sell_discount <= 1):
                self._add_issue(file, 1, None, "'sell_discount' must be a number between 0 and 1", Severity.ERROR)

        training = data.get("training")
        if training is not None:
            if not isinstance(training, dict):
                self._add_issue(file, 1, None, "'training' must be an object", Severity.ERROR)
            else:
                for field in (
                    "base_passive_xp_per_day",
                    "teacher_gold_cost",
                    "teacher_xp_grant",
                    "teacher_duration_hours",
                    "book_xp_grant",
                    "book_duration_hours",
                ):
                    val: object = training.get(field)
                    if not isinstance(val, (int, float)) or isinstance(val, bool) or val <= 0:
                        self._add_issue(file, 1, None, f"'training.{field}' must be a positive number", Severity.ERROR)

    def validate_equipment(self, file: Path) -> None:
        """Validate equipment.json — slots + gear items."""
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return
        data: object = self._validate_json(content, file)
        if data is None or not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with equipment config", Severity.ERROR)
            return

        slots = data.get("slots")
        if not isinstance(slots, list) or not slots or any(not isinstance(s, str) or not s for s in slots):
            self._add_issue(file, 1, None, "'slots' must be a non-empty array of non-empty strings", Severity.ERROR)
        slots_set: set[str] = set(slots) if isinstance(slots, list) else set()

        items = data.get("items")
        if not isinstance(items, dict):
            self._add_issue(file, 1, None, "'items' must be an object", Severity.ERROR)
            return
        for item_id, item in items.items():
            if not isinstance(item, dict):
                self._add_issue(file, 1, None, f"item '{item_id}' must be an object", Severity.ERROR)
                continue
            slot: object = item.get("slot")
            if not isinstance(slot, str) or not slot:
                self._add_issue(file, 1, None, f"item '{item_id}'.slot must be a non-empty string", Severity.ERROR)
            elif slots_set and slot not in slots_set:
                self._add_issue(file, 1, None, f"item '{item_id}'.slot '{slot}' is not in 'slots'", Severity.ERROR)
            rl: object = item.get("requires_level")
            if rl is not None and (not isinstance(rl, int) or isinstance(rl, bool) or rl < 1):
                self._add_issue(file, 1, None, f"item '{item_id}'.requires_level must be a positive integer", Severity.ERROR)
            aslots: object = item.get("armory_slots")
            if aslots is not None and (not isinstance(aslots, int) or isinstance(aslots, bool) or aslots < 0):
                self._add_issue(file, 1, None, f"item '{item_id}'.armory_slots must be a non-negative integer", Severity.ERROR)
            bv: object = item.get("base_value")
            if not isinstance(bv, (int, float)) or isinstance(bv, bool) or bv < 0:
                self._add_issue(file, 1, None, f"item '{item_id}'.base_value must be a non-negative number", Severity.ERROR)
            cp: object = item.get("city_price")
            if cp is not None and (not isinstance(cp, (int, float)) or isinstance(cp, bool) or cp <= 0):
                self._add_issue(file, 1, None, f"item '{item_id}'.city_price must be a positive number", Severity.ERROR)
            craft = item.get("craft")
            if craft is not None:
                if not isinstance(craft, dict):
                    self._add_issue(file, 1, None, f"item '{item_id}'.craft must be an object", Severity.ERROR)
                else:
                    dh: object = craft.get("duration_hours")
                    if not isinstance(dh, (int, float)) or isinstance(dh, bool) or dh <= 0:
                        self._add_issue(file, 1, None, f"item '{item_id}'.craft.duration_hours must be a positive number", Severity.ERROR)
                    mats: object = craft.get("materials")
                    if mats is not None and not isinstance(mats, dict):
                        self._add_issue(file, 1, None, f"item '{item_id}'.craft.materials must be an object", Severity.ERROR)

    def validate_items(self, file: Path) -> None:
        """Validate items.json — general item storage consumables (books etc.)."""
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return
        data: object = self._validate_json(content, file)
        if data is None or not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with items config", Severity.ERROR)
            return
        items = data.get("items")
        if not isinstance(items, dict):
            self._add_issue(file, 1, None, "'items' must be an object", Severity.ERROR)
            return
        for item_id, item in items.items():
            if not isinstance(item, dict):
                self._add_issue(file, 1, None, f"item '{item_id}' must be an object", Severity.ERROR)
                continue
            src: object = item.get("source")
            if src not in ("drop", "purchase"):
                self._add_issue(file, 1, None, f"item '{item_id}'.source must be 'drop' or 'purchase'", Severity.ERROR)
            xg: object = item.get("xp_grant")
            if xg is not None and (not isinstance(xg, (int, float)) or isinstance(xg, bool) or xg <= 0):
                self._add_issue(file, 1, None, f"item '{item_id}'.xp_grant must be a positive number", Severity.ERROR)
            td: object = item.get("training_duration_hours")
            if td is not None and (not isinstance(td, (int, float)) or isinstance(td, bool) or td <= 0):
                self._add_issue(file, 1, None, f"item '{item_id}'.training_duration_hours must be a positive number", Severity.ERROR)

    def validate_tech_trees(self, file: Path) -> None:
        """Validate tech_trees.json — tree/node definitions."""
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return
        data: object = self._validate_json(content, file)
        if data is None or not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with tech tree config", Severity.ERROR)
            return
        trees = data.get("trees")
        if not isinstance(trees, dict) or not trees:
            self._add_issue(file, 1, None, "'trees' must be a non-empty object", Severity.ERROR)
            return
        for tree_id, tree in trees.items():
            if not isinstance(tree, dict) or not isinstance(tree.get("nodes"), list):
                self._add_issue(file, 1, None, f"tree '{tree_id}' must have a 'nodes' array", Severity.ERROR)
                continue
            node_ids: set[str] = set()
            for node in tree["nodes"]:
                if not isinstance(node, dict) or not isinstance(node.get("id"), str) or not node["id"]:
                    self._add_issue(file, 1, None, f"tree '{tree_id}' has a node without an 'id' string", Severity.ERROR)
                    continue
                node_ids.add(node["id"])
                cost: object = node.get("xp_cost")
                if not isinstance(cost, (int, float)) or isinstance(cost, bool) or cost <= 0:
                    self._add_issue(file, 1, None, f"tree '{tree_id}' node '{node['id']}'.xp_cost must be a positive number", Severity.ERROR)
                prereqs = node.get("prerequisites", [])
                if not isinstance(prereqs, list):
                    self._add_issue(file, 1, None, f"tree '{tree_id}' node '{node['id']}'.prerequisites must be an array", Severity.ERROR)
                else:
                    for p in prereqs:
                        if not isinstance(p, str) or p not in node_ids:
                            self._add_issue(file, 1, None, f"tree '{tree_id}' node '{node['id']}' has unknown prerequisite '{p}'", Severity.ERROR)
                effects = node.get("effects", [])
                if not isinstance(effects, list):
                    self._add_issue(file, 1, None, f"tree '{tree_id}' node '{node['id']}'.effects must be an array", Severity.ERROR)

    def validate_combat_rulesets(self, file: Path) -> None:
        """Validate game/config/combat/rulesets.json.

        Each ruleset drives a combat mission: mode (pve/pvp), team sizes,
        death handling (permanent / respawn_after_seconds / revive_resource),
        unit caps, duration, and win conditions. Unknown fields are warnings
        only — the format is intentionally flexible (see docs/combat_rulesets.md).
        """
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with 'format_version' and 'rulesets'", Severity.ERROR)
            return

        if not isinstance(data.get("format_version"), str):
            self._add_issue(file, 1, None, "'format_version' must be a string", Severity.ERROR)
        rulesets: object = data.get("rulesets")
        if not isinstance(rulesets, list) or not rulesets:
            self._add_issue(file, 1, None, "'rulesets' must be a non-empty array", Severity.ERROR)
            return

        seen_ids: set[str] = set()
        for idx, ruleset in enumerate(rulesets, start=1):
            if not isinstance(ruleset, dict):
                self._add_issue(file, 1, None, f"rulesets[{idx}] must be an object", Severity.ERROR)
                continue

            ruleset_id: object = ruleset.get("id")
            if not isinstance(ruleset_id, str) or not ruleset_id:
                self._add_issue(file, 1, None, f"rulesets[{idx}].id must be a non-empty string", Severity.ERROR)
            elif ruleset_id in seen_ids:
                self._add_issue(file, 1, None, f"Duplicate ruleset id: {ruleset_id}", Severity.ERROR)
            else:
                seen_ids.add(ruleset_id)
                if any(char.isupper() for char in ruleset_id):
                    self._add_issue(file, 1, None, f"Ruleset id '{ruleset_id}' should be lowercase snake_case", Severity.WARN)

            if not isinstance(ruleset.get("name"), str):
                self._add_issue(file, 1, None, f"rulesets[{idx}].name must be a string", Severity.ERROR)

            mode: object = ruleset.get("mode")
            if mode not in ("pve", "pvp"):
                self._add_issue(file, 1, None, f"rulesets[{idx}].mode must be 'pve' or 'pvp', got {mode!r}", Severity.ERROR)

            players: object = ruleset.get("players")
            if not isinstance(players, dict):
                self._add_issue(file, 1, None, f"rulesets[{idx}].players must be an object with min/max", Severity.ERROR)
            else:
                for key in ("min", "max"):
                    value: object = players.get(key)
                    if not isinstance(value, int) or value < 1:
                        self._add_issue(file, 1, None, f"rulesets[{idx}].players.{key} must be a positive integer", Severity.ERROR)
                p_min: object = players.get("min")
                p_max: object = players.get("max")
                if isinstance(p_min, int) and isinstance(p_max, int) and p_min > p_max:
                    self._add_issue(file, 1, None, f"rulesets[{idx}].players.min cannot exceed max", Severity.ERROR)

            death_handling: object = ruleset.get("death_handling")
            if death_handling not in ("permanent", "wounded", "respawn_after_seconds", "revive_resource"):
                self._add_issue(
                    file, 1, None,
                    f"rulesets[{idx}].death_handling must be 'permanent', 'wounded', 'respawn_after_seconds' or 'revive_resource', got {death_handling!r}",
                    Severity.ERROR,
                )
            respawn_delay: object = ruleset.get("respawn_delay_seconds")
            if respawn_delay is not None and (not isinstance(respawn_delay, int) or respawn_delay < 0):
                self._add_issue(file, 1, None, f"rulesets[{idx}].respawn_delay_seconds must be a non-negative integer", Severity.ERROR)
            if death_handling == "respawn_after_seconds" and not isinstance(respawn_delay, int):
                self._add_issue(file, 1, None, f"rulesets[{idx}] uses respawn_after_seconds but has no respawn_delay_seconds", Severity.ERROR)

            unit_caps: object = ruleset.get("unit_caps")
            if not isinstance(unit_caps, dict):
                self._add_issue(file, 1, None, f"rulesets[{idx}].unit_caps must be an object", Severity.ERROR)
            else:
                for key in ("per_player", "total"):
                    value: object = unit_caps.get(key)
                    if not isinstance(value, int) or value < 1:
                        self._add_issue(file, 1, None, f"rulesets[{idx}].unit_caps.{key} must be a positive integer", Severity.ERROR)

            duration: object = ruleset.get("match_duration_seconds")
            if not isinstance(duration, int) or duration < 1:
                self._add_issue(file, 1, None, f"rulesets[{idx}].match_duration_seconds must be a positive integer", Severity.ERROR)

            win_conditions: object = ruleset.get("win_conditions")
            if not isinstance(win_conditions, list) or not win_conditions:
                self._add_issue(file, 1, None, f"rulesets[{idx}].win_conditions must be a non-empty array", Severity.ERROR)
            elif any(not isinstance(wc, str) for wc in win_conditions):
                self._add_issue(file, 1, None, f"rulesets[{idx}].win_conditions entries must be strings", Severity.ERROR)

            for bool_key in ("fog_of_war", "chat_enabled", "voice_enabled"):
                value: object = ruleset.get(bool_key)
                if value is not None and not isinstance(value, bool):
                    self._add_issue(file, 1, None, f"rulesets[{idx}].{bool_key} must be a boolean", Severity.ERROR)

            # Unknown fields: warnings only (flexible format — see docs/combat_maps.md)
            known_fields: Final[set[str]] = {
                "id", "name", "mode", "players", "teams", "death_handling",
                "respawn_delay_seconds", "revive_resource", "unit_caps",
                "match_duration_seconds", "win_conditions", "fog_of_war",
                "chat_enabled", "voice_enabled", "format_version",
            }
            unknown: set[str] = set(ruleset.keys()) - known_fields
            if unknown:
                self._add_issue(
                    file, 1, None,
                    f"rulesets[{idx}] has unknown fields (ignored): {', '.join(sorted(unknown))}",
                    Severity.WARN,
                )

    def validate_combat_maps(self, file: Path) -> None:
        """Validate game/config/combat/maps/*.json.

        Required: format_version, name, width_tiles, height_tiles, tile_costs
        (length must equal width x height). spawn_points/obstacles are
        optional. Unknown fields are warnings only — the format is
        intentionally flexible (see docs/combat_maps.md).
        """
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with combat map metadata", Severity.ERROR)
            return

        if not isinstance(data.get("format_version"), str):
            self._add_issue(file, 1, None, "'format_version' must be a string", Severity.ERROR)
        if not isinstance(data.get("name"), str) or not data.get("name"):
            self._add_issue(file, 1, None, "'name' must be a non-empty string", Severity.ERROR)

        width: object = data.get("width_tiles")
        height: object = data.get("height_tiles")
        if not isinstance(width, int) or width < 1:
            self._add_issue(file, 1, None, "'width_tiles' must be a positive integer", Severity.ERROR)
        if not isinstance(height, int) or height < 1:
            self._add_issue(file, 1, None, "'height_tiles' must be a positive integer", Severity.ERROR)

        tile_costs: object = data.get("tile_costs")
        if not isinstance(tile_costs, list) or not tile_costs:
            self._add_issue(file, 1, None, "'tile_costs' must be a non-empty array (0 = impassable, 1 = open, >1 = slow)", Severity.ERROR)
        else:
            if isinstance(width, int) and isinstance(height, int):
                expected: int = width * height
                if len(tile_costs) != expected:
                    self._add_issue(
                        file, 1, None,
                        f"'tile_costs' has {len(tile_costs)} entries but width x height = {expected}",
                        Severity.ERROR,
                    )
            passable: int = 0
            for value in tile_costs:
                if not isinstance(value, (int, float)) or isinstance(value, bool) or value < 0:
                    self._add_issue(file, 1, None, f"'tile_costs' entries must be non-negative numbers, got {value!r}", Severity.ERROR)
                elif value > 0:
                    passable += 1
            if passable == 0:
                self._add_issue(file, 1, None, "'tile_costs' must contain at least one passable tile", Severity.ERROR)

        spawn_points: object = data.get("spawn_points")
        if spawn_points is not None:
            if not isinstance(spawn_points, list):
                self._add_issue(file, 1, None, "'spawn_points' must be an array", Severity.ERROR)
            else:
                for idx, sp in enumerate(spawn_points, start=1):
                    if not isinstance(sp, dict):
                        self._add_issue(file, 1, None, f"spawn_points[{idx}] must be an object", Severity.ERROR)
                        continue
                    if not isinstance(sp.get("id"), str):
                        self._add_issue(file, 1, None, f"spawn_points[{idx}].id must be a string", Severity.ERROR)
                    for coord in ("x", "y"):
                        value: object = sp.get(coord)
                        if not isinstance(value, (int, float)) or isinstance(value, bool) or not 0 <= value <= 1:
                            self._add_issue(file, 1, None, f"spawn_points[{idx}].{coord} must be a number in 0..1", Severity.ERROR)
                    team: object = sp.get("team")
                    if team is not None and (not isinstance(team, int) or team < 1):
                        self._add_issue(file, 1, None, f"spawn_points[{idx}].team must be a positive integer", Severity.ERROR)

        obstacles: object = data.get("obstacles")
        if obstacles is not None:
            if not isinstance(obstacles, list):
                self._add_issue(file, 1, None, "'obstacles' must be an array", Severity.ERROR)
            else:
                for idx, obs in enumerate(obstacles, start=1):
                    if not isinstance(obs, dict):
                        self._add_issue(file, 1, None, f"obstacles[{idx}] must be an object", Severity.ERROR)
                        continue
                    if not isinstance(obs.get("id"), str):
                        self._add_issue(file, 1, None, f"obstacles[{idx}].id must be a string", Severity.ERROR)
                    for coord in ("x", "y"):
                        value: object = obs.get(coord)
                        if not isinstance(value, (int, float)) or isinstance(value, bool) or not 0 <= value <= 1:
                            self._add_issue(file, 1, None, f"obstacles[{idx}].{coord} must be a number in 0..1", Severity.ERROR)
                    radius: object = obs.get("radius")
                    if not isinstance(radius, (int, float)) or isinstance(radius, bool) or radius <= 0:
                        self._add_issue(file, 1, None, f"obstacles[{idx}].radius must be a positive number", Severity.ERROR)
                    # Per-obstacle blocking flags (rts_design.md §8.4): all
                    # optional booleans; absent = blocking (default solid).
                    for flag in ("blocks_movement", "blocks_ranged", "blocks_aoe"):
                        flag_val: object = obs.get(flag)
                        if flag_val is not None and not isinstance(flag_val, bool):
                            self._add_issue(
                                file, 1, None,
                                f"obstacles[{idx}].{flag} must be a boolean",
                                Severity.ERROR,
                            )

        deploy_zones: object = data.get("deploy_zones")
        if deploy_zones is not None:
            if not isinstance(deploy_zones, list) or not deploy_zones:
                self._add_issue(file, 1, None, "'deploy_zones' must be a non-empty array", Severity.ERROR)
            else:
                seen_deploy_teams: set[int] = set()
                for idx, dz in enumerate(deploy_zones, start=1):
                    if not isinstance(dz, dict):
                        self._add_issue(file, 1, None, f"deploy_zones[{idx}] must be an object", Severity.ERROR)
                        continue
                    team: object = dz.get("team")
                    if not isinstance(team, int) or team < 1:
                        self._add_issue(file, 1, None, f"deploy_zones[{idx}].team must be a positive integer", Severity.ERROR)
                    else:
                        if team in seen_deploy_teams:
                            self._add_issue(file, 1, None, f"deploy_zones[{idx}] duplicates team {team}", Severity.ERROR)
                        seen_deploy_teams.add(team)
                    for coord in ("x", "y"):
                        value: object = dz.get(coord)
                        if not isinstance(value, (int, float)) or isinstance(value, bool) or not 0 <= value <= 1:
                            self._add_issue(file, 1, None, f"deploy_zones[{idx}].{coord} must be a number in 0..1", Severity.ERROR)
                    for dim in ("w", "h"):
                        value: object = dz.get(dim)
                        if not isinstance(value, (int, float)) or isinstance(value, bool) or value <= 0:
                            self._add_issue(file, 1, None, f"deploy_zones[{idx}].{dim} must be a positive number", Severity.ERROR)
                    xv = dz.get("x", 0)
                    yv = dz.get("y", 0)
                    wv = dz.get("w", 1)
                    hv = dz.get("h", 1)
                    if (isinstance(xv, (int, float)) and isinstance(wv, (int, float)) and xv + wv > 1.0):
                        self._add_issue(file, 1, None, f"deploy_zones[{idx}] extends past the right edge (x + w > 1)", Severity.ERROR)
                    if (isinstance(yv, (int, float)) and isinstance(hv, (int, float)) and yv + hv > 1.0):
                        self._add_issue(file, 1, None, f"deploy_zones[{idx}] extends past the bottom edge (y + h > 1)", Severity.ERROR)
                if seen_deploy_teams and {1, 2} - seen_deploy_teams:
                    missing_teams: set[int] = {1, 2} - seen_deploy_teams
                    self._add_issue(
                        file, 1, None,
                        f"deploy_zones missing team(s): {sorted(missing_teams)} (spawn-point fallback applies)",
                        Severity.WARN,
                    )

        # Unknown fields: warnings only (flexible format — see docs/combat_maps.md)
        known_fields: Final[set[str]] = {
            "format_version", "name", "image_filename", "width_tiles",
            "height_tiles", "tile_size_px", "tile_costs", "spawn_points",
            "start_positions", "obstacles", "deploy_zones",
        }
        unknown: set[str] = set(data.keys()) - known_fields
        if unknown:
            self._add_issue(
                file, 1, None,
                f"Unknown map fields (ignored): {', '.join(sorted(unknown))}",
                Severity.WARN,
            )

    def validate_combat_warbands(self, file: Path, valid_enemy_ids: set[str]) -> None:
        """Validate game/config/combat/warbands.json — mission opportunities.

        Each opportunity bundles a ruleset + map + warband composition + tier +
        XP base + flavor. Warband entries must reference enemy combatant ids that
        exist in enemy_combatants.json (cross-referenced against the validated
        enemy set).
        """
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with 'format_version' and 'opportunities'", Severity.ERROR)
            return

        if not isinstance(data.get("format_version"), str):
            self._add_issue(file, 1, None, "'format_version' must be a string", Severity.ERROR)
        opportunities: object = data.get("opportunities")
        if not isinstance(opportunities, list) or not opportunities:
            self._add_issue(file, 1, None, "'opportunities' must be a non-empty array", Severity.ERROR)
            return

        seen_ids: set[str] = set()
        for idx, opp in enumerate(opportunities, start=1):
            if not isinstance(opp, dict):
                self._add_issue(file, 1, None, f"opportunities[{idx}] must be an object", Severity.ERROR)
                continue

            opp_id: object = opp.get("id")
            if not isinstance(opp_id, str) or not opp_id:
                self._add_issue(file, 1, None, f"opportunities[{idx}].id must be a non-empty string", Severity.ERROR)
            elif opp_id in seen_ids:
                self._add_issue(file, 1, None, f"Duplicate opportunity id: {opp_id}", Severity.ERROR)
            else:
                seen_ids.add(opp_id)

            if not isinstance(opp.get("name"), str):
                self._add_issue(file, 1, None, f"opportunities[{idx}].name must be a string", Severity.ERROR)
            tier: object = opp.get("tier")
            if not isinstance(tier, int) or tier < 1:
                self._add_issue(file, 1, None, f"opportunities[{idx}].tier must be a positive integer", Severity.ERROR)
            if not isinstance(opp.get("ruleset"), str):
                self._add_issue(file, 1, None, f"opportunities[{idx}].ruleset must be a string", Severity.ERROR)
            if not isinstance(opp.get("map"), str):
                self._add_issue(file, 1, None, f"opportunities[{idx}].map must be a string", Severity.ERROR)
            xp_base: object = opp.get("xp_base")
            if not isinstance(xp_base, (int, float)) or isinstance(xp_base, bool) or xp_base < 0:
                self._add_issue(file, 1, None, f"opportunities[{idx}].xp_base must be a non-negative number", Severity.ERROR)
            if "flavor" in opp and (not isinstance(opp["flavor"], str) or not opp["flavor"]):
                self._add_issue(file, 1, None, f"opportunities[{idx}].flavor must be a non-empty string", Severity.WARN)

            warband: object = opp.get("warband")
            if not isinstance(warband, list) or not warband:
                self._add_issue(file, 1, None, f"opportunities[{idx}].warband must be a non-empty array", Severity.ERROR)
                continue
            for wb_idx, entry in enumerate(warband, start=1):
                if not isinstance(entry, dict):
                    self._add_issue(file, 1, None, f"opportunities[{idx}].warband[{wb_idx}] must be an object", Severity.ERROR)
                    continue
                unit_class: object = entry.get("unit_class")
                if not isinstance(unit_class, str) or not unit_class:
                    self._add_issue(file, 1, None, f"opportunities[{idx}].warband[{wb_idx}].unit_class must be a non-empty string", Severity.ERROR)
                elif valid_enemy_ids and unit_class not in valid_enemy_ids:
                    self._add_issue(
                        file, 1, None,
                        f"opportunities[{idx}].warband[{wb_idx}] references unknown enemy combatant '{unit_class}'",
                        Severity.ERROR,
                    )
                lvl: object = entry.get("level")
                if not isinstance(lvl, int) or lvl < 1:
                    self._add_issue(file, 1, None, f"opportunities[{idx}].warband[{wb_idx}].level must be a positive integer", Severity.ERROR)
                count: object = entry.get("count")
                if not isinstance(count, int) or count < 1:
                    self._add_issue(file, 1, None, f"opportunities[{idx}].warband[{wb_idx}].count must be a positive integer", Severity.ERROR)
                if ("group" in entry and (not isinstance(entry["group"], str) or not entry["group"])):
                    self._add_issue(file, 1, None, f"opportunities[{idx}].warband[{wb_idx}].group must be a non-empty string", Severity.WARN)
                if "tactic" in entry and entry["tactic"] not in ("charge", "hold", "skirmish", "guard", "reinforce"):
                    self._add_issue(file, 1, None, f"opportunities[{idx}].warband[{wb_idx}].tactic must be one of charge/hold/skirmish/guard/reinforce", Severity.ERROR)
                if "aggro_model" in entry and entry["aggro_model"] not in ("local", "call_to_arms", "global"):
                    self._add_issue(file, 1, None, f"opportunities[{idx}].warband[{wb_idx}].aggro_model must be one of local/call_to_arms/global", Severity.ERROR)

        self.validated_files.append(file)

    def validate_combat_structures(self, file: Path) -> None:
        """Validate game/config/combat/structures.json.

        Each temporary structure: footprint (tiles), max_hp, build_time_seconds,
        per-player cap, and the ranged/magic block-flag property pair
        (rts_design.md §6.4/§7). `arrow_slit` marks a tower that passes ranged
        in and out; `garrison` defines the archer-tower buff.
        """
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with 'format_version' and 'structures'", Severity.ERROR)
            return

        if not isinstance(data.get("format_version"), str):
            self._add_issue(file, 1, None, "'format_version' must be a string", Severity.ERROR)
        structures: object = data.get("structures")
        if not isinstance(structures, dict) or not structures:
            self._add_issue(file, 1, None, "'structures' must be a non-empty object", Severity.ERROR)
            return

        for struct_id, struct_data in structures.items():
            if not isinstance(struct_data, dict):
                self._add_issue(file, 1, None, f"Structure '{struct_id}' must be an object", Severity.ERROR)
                continue
            footprint: object = struct_data.get("footprint")
            if not isinstance(footprint, int) or footprint < 1:
                self._add_issue(file, 1, None, f"Structure '{struct_id}'.footprint must be an integer >= 1", Severity.ERROR)
            max_hp: object = struct_data.get("max_hp")
            if not isinstance(max_hp, (int, float)) or isinstance(max_hp, bool) or max_hp <= 0:
                self._add_issue(file, 1, None, f"Structure '{struct_id}'.max_hp must be a positive number", Severity.ERROR)
            build_time: object = struct_data.get("build_time_seconds")
            if not isinstance(build_time, (int, float)) or isinstance(build_time, bool) or build_time <= 0:
                self._add_issue(file, 1, None, f"Structure '{struct_id}'.build_time_seconds must be a positive number", Severity.ERROR)
            cap: object = struct_data.get("max_per_player")
            if not isinstance(cap, int) or cap < 1:
                self._add_issue(file, 1, None, f"Structure '{struct_id}'.max_per_player must be a positive integer", Severity.ERROR)
            for flag in ("blocks_ranged", "blocks_physical_ranged", "blocks_magical_ranged", "arrow_slit"):
                flag_val: object = struct_data.get(flag)
                if flag_val is not None and not isinstance(flag_val, bool):
                    self._add_issue(file, 1, None, f"Structure '{struct_id}'.{flag} must be a boolean", Severity.ERROR)

            garrison: object = struct_data.get("garrison")
            if garrison is not None:
                if not isinstance(garrison, dict):
                    self._add_issue(file, 1, None, f"Structure '{struct_id}'.garrison must be an object", Severity.ERROR)
                else:
                    cap_v: object = garrison.get("capacity")
                    if not isinstance(cap_v, int) or cap_v < 1:
                        self._add_issue(file, 1, None, f"Structure '{struct_id}'.garrison.capacity must be a positive integer", Severity.ERROR)
                    allowed: object = garrison.get("allowed_classes")
                    if allowed is not None and (not isinstance(allowed, list) or any(not isinstance(c, str) or not c for c in allowed)):
                        self._add_issue(file, 1, None, f"Structure '{struct_id}'.garrison.allowed_classes must be an array of non-empty strings", Severity.ERROR)
                    for buff_field in ("damage_bonus_pct", "range_bonus_pct"):
                        buff: object = garrison.get(buff_field)
                        if buff is not None and (not isinstance(buff, (int, float)) or isinstance(buff, bool) or buff < 0):
                            self._add_issue(file, 1, None, f"Structure '{struct_id}'.garrison.{buff_field} must be a non-negative number", Severity.ERROR)

        self.validated_files.append(file)

    def validate_combat_formations(self, file: Path) -> None:
        """Validate game/config/combat/formations.json.

        Persistent movement modifiers (rts_design.md §8.3): line/column/wedge/
        circle shapes with generation params (spacing, row_spacing, radius_step).
        """
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with 'format_version' and 'formations'", Severity.ERROR)
            return

        if not isinstance(data.get("format_version"), str):
            self._add_issue(file, 1, None, "'format_version' must be a string", Severity.ERROR)
        formations: object = data.get("formations")
        if not isinstance(formations, dict) or not formations:
            self._add_issue(file, 1, None, "'formations' must be a non-empty object", Severity.ERROR)
            return

        valid_shapes: Final[set[str]] = {"line", "column", "wedge", "circle"}
        for form_id, form_data in formations.items():
            if not isinstance(form_data, dict):
                self._add_issue(file, 1, None, f"Formation '{form_id}' must be an object", Severity.ERROR)
                continue
            if not isinstance(form_data.get("name"), str):
                self._add_issue(file, 1, None, f"Formation '{form_id}'.name must be a string", Severity.ERROR)
            shape: object = form_data.get("shape")
            if shape not in valid_shapes:
                self._add_issue(file, 1, None, f"Formation '{form_id}'.shape must be one of {sorted(valid_shapes)}, got {shape!r}", Severity.ERROR)
            for num_field in ("spacing", "row_spacing", "radius_step"):
                nv: object = form_data.get(num_field)
                if nv is not None and (not isinstance(nv, (int, float)) or isinstance(nv, bool) or nv <= 0):
                    self._add_issue(file, 1, None, f"Formation '{form_id}'.{num_field} must be a positive number", Severity.ERROR)

        self.validated_files.append(file)

    def validate_combat_profiles(self, file: Path) -> None:
        """Validate game/config/combat/profiles.json.

        Per-class fight styles + the group-level behavioral axes (tactic,
        aggro_model, engagement, retreat, siege) consumed by the enemy AI
        (rts_design.md §11).
        """
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with 'format_version', 'class_styles' and 'group_axes'", Severity.ERROR)
            return

        if not isinstance(data.get("format_version"), str):
            self._add_issue(file, 1, None, "'format_version' must be a string", Severity.ERROR)

        class_styles: object = data.get("class_styles")
        if class_styles is not None:
            if not isinstance(class_styles, dict) or not class_styles:
                self._add_issue(file, 1, None, "'class_styles' must be a non-empty object", Severity.ERROR)
            else:
                for style_id, style_data in class_styles.items():
                    if not isinstance(style_data, dict):
                        self._add_issue(file, 1, None, f"class_styles['{style_id}'] must be an object", Severity.ERROR)
                        continue
                    if not isinstance(style_data.get("name"), str):
                        self._add_issue(file, 1, None, f"class_styles['{style_id}'].name must be a string", Severity.ERROR)
                    for key, val in style_data.items():
                        if key == "name":
                            continue
                        if key in ("retreat_threshold", "preferred_range"):
                            if not isinstance(val, (int, float)) or isinstance(val, bool):
                                self._add_issue(file, 1, None, f"class_styles['{style_id}'].{key} must be a number", Severity.ERROR)
                        elif not isinstance(val, bool):
                            self._add_issue(file, 1, None, f"class_styles['{style_id}'].{key} must be a boolean", Severity.WARN)

        group_axes: object = data.get("group_axes")
        if group_axes is not None:
            if not isinstance(group_axes, dict):
                self._add_issue(file, 1, None, "'group_axes' must be an object", Severity.ERROR)
            else:
                valid_tactics: Final[set[str]] = {"charge", "hold", "skirmish", "guard", "reinforce"}
                valid_aggro: Final[set[str]] = {"local", "call_to_arms", "global"}
                tactic_list: object = group_axes.get("tactic")
                if tactic_list is not None:
                    if not isinstance(tactic_list, list) or any(t not in valid_tactics for t in tactic_list):
                        self._add_issue(file, 1, None, f"group_axes.tactic must be a list of {sorted(valid_tactics)}", Severity.ERROR)
                aggro_list: object = group_axes.get("aggro_model")
                if aggro_list is not None:
                    if not isinstance(aggro_list, list) or any(t not in valid_aggro for t in aggro_list):
                        self._add_issue(file, 1, None, f"group_axes.aggro_model must be a list of {sorted(valid_aggro)}", Severity.ERROR)

        self.validated_files.append(file)

    def validate_combat_rewards(self, file: Path) -> None:
        """Validate game/config/combat/rewards.json.

        The rewards pipeline (rts_design.md §14): xp_curve (level-up thresholds),
        victory multiplier, draw = base XP, morale stamps, and the empty
        gold/equipment slots reserved for the future pipeline.
        """
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with 'format_version' and reward slots", Severity.ERROR)
            return

        if not isinstance(data.get("format_version"), str):
            self._add_issue(file, 1, None, "'format_version' must be a string", Severity.ERROR)

        xp_curve: object = data.get("xp_curve")
        if not isinstance(xp_curve, list) or not xp_curve:
            self._add_issue(file, 1, None, "'xp_curve' must be a non-empty array of level-up thresholds", Severity.ERROR)
        else:
            prev: float = 0.0
            for i, threshold in enumerate(xp_curve):
                if not isinstance(threshold, (int, float)) or isinstance(threshold, bool) or threshold <= prev:
                    self._add_issue(
                        file, 1, None,
                        f"'xp_curve[{i}]' must be a strictly increasing non-negative number (got {threshold!r})",
                        Severity.ERROR,
                    )
                else:
                    prev = float(threshold)

        victory_multiplier: object = data.get("victory_multiplier")
        if not isinstance(victory_multiplier, (int, float)) or isinstance(victory_multiplier, bool) or victory_multiplier <= 0:
            self._add_issue(file, 1, None, "'victory_multiplier' must be a positive number", Severity.ERROR)
        if "draw_xp" in data and data["draw_xp"] != "base":
            self._add_issue(file, 1, None, "'draw_xp' must be \"base\" (draw = base XP, no multiplier)", Severity.ERROR)
        morale: object = data.get("morale")
        if morale is not None:
            if not isinstance(morale, dict):
                self._add_issue(file, 1, None, "'morale' must be an object", Severity.ERROR)
            else:
                for key, val in morale.items():
                    if not isinstance(val, (int, float)) or isinstance(val, bool) or val < 0:
                        self._add_issue(file, 1, None, f"'morale.{key}' must be a non-negative number", Severity.ERROR)

        # Reserved adventure-reward seam (§15.6) — an empty list in v1.
        cosmetics_slot: object = data.get("cosmetics")
        if cosmetics_slot is not None and not isinstance(cosmetics_slot, list):
            self._add_issue(file, 1, None, "'cosmetics' must be an array (adventure-reward seam; empty in v1)", Severity.ERROR)

        self.validated_files.append(file)

    def validate_cosmetics(self, file: Path) -> None:
        """Validate game/config/cosmetics.json (rts_design.md §15).

        The premium-variety catalog. Each item is validated by a
        DENY-BY-DEFAULT whitelist of appearance/meta keys: any unknown key is an
        ERROR, so a cosmetic can carry no gameplay stat by construction
        (§15.5.1). Slots, tiers, sources, and product kinds are enumerated;
        prices are non-negative; bundle items must reference real item ids.
        """
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with 'format_version' and 'items'", Severity.ERROR)
            return

        if not isinstance(data.get("format_version"), str):
            self._add_issue(file, 1, None, "'format_version' must be a string", Severity.ERROR)

        valid_slots: Final[set[str]] = {"banner", "knight", "squad_uniform", "unit", "structure", "flourish"}
        valid_tiers: Final[set[str]] = {"common", "uncommon", "rare", "legendary"}
        valid_sources: Final[set[str]] = {"store", "adventure_reward"}
        valid_kinds: Final[set[str]] = {"platinum_pack", "monthly_pass"}
        item_keys: Final[set[str]] = {"id", "name", "slot", "tier", "price_platinum", "source",
                                      "description", "appearance", "sort"}
        appearance_keys: Final[set[str]] = {"palette", "pattern", "emblem", "trim"}
        bundle_keys: Final[set[str]] = {"id", "name", "price_platinum", "description", "items", "sort"}
        product_keys: Final[set[str]] = {"id", "name", "price_real", "platinum", "kind", "description", "sort"}

        items: object = data.get("items")
        if not isinstance(items, list) or not items:
            self._add_issue(file, 1, None, "'items' must be a non-empty array", Severity.ERROR)
            return

        seen_item_ids: set[str] = set()
        for idx, item in enumerate(items, start=1):
            if not isinstance(item, dict):
                self._add_issue(file, 1, None, f"items[{idx}] must be an object", Severity.ERROR)
                continue
            # Deny-by-default: any key beyond the whitelist is a pay-to-win risk.
            for key in item.keys():
                if key not in item_keys:
                    self._add_issue(
                        file, 1, None,
                        f"items[{idx}] has unknown key '{key}' — cosmetics are cosmetic only "
                        "(no gameplay keys; §15.5.1)",
                        Severity.ERROR,
                    )
            iid: object = item.get("id")
            if not isinstance(iid, str) or not iid:
                self._add_issue(file, 1, None, f"items[{idx}].id must be a non-empty string", Severity.ERROR)
            elif iid in seen_item_ids:
                self._add_issue(file, 1, None, f"Duplicate cosmetic id '{iid}'", Severity.ERROR)
            else:
                seen_item_ids.add(iid)
            if not isinstance(item.get("name"), str):
                self._add_issue(file, 1, None, f"items[{idx}].name must be a string", Severity.ERROR)
            slot: object = item.get("slot")
            if slot not in valid_slots:
                self._add_issue(file, 1, None, f"items[{idx}].slot must be one of {sorted(valid_slots)}", Severity.ERROR)
            tier: object = item.get("tier")
            if tier not in valid_tiers:
                self._add_issue(file, 1, None, f"items[{idx}].tier must be one of {sorted(valid_tiers)}", Severity.ERROR)
            source: object = item.get("source")
            if source not in valid_sources:
                self._add_issue(file, 1, None, f"items[{idx}].source must be one of {sorted(valid_sources)}", Severity.ERROR)
            price: object = item.get("price_platinum")
            if not isinstance(price, int) or isinstance(price, bool) or price < 0:
                self._add_issue(file, 1, None, f"items[{idx}].price_platinum must be a non-negative integer", Severity.ERROR)
            appearance: object = item.get("appearance")
            if not isinstance(appearance, dict):
                self._add_issue(file, 1, None, f"items[{idx}].appearance must be an object", Severity.ERROR)
            else:
                for key in appearance.keys():
                    if key not in appearance_keys:
                        self._add_issue(
                            file, 1, None,
                            f"items[{idx}].appearance has unknown key '{key}' — appearance is visual only (§15.5.1)",
                            Severity.ERROR,
                        )
                palette: object = appearance.get("palette")
                if not isinstance(palette, list) or not palette or any(
                    not isinstance(c, str) or not c for c in palette
                ):
                    self._add_issue(file, 1, None, f"items[{idx}].appearance.palette must be a non-empty array of colour strings", Severity.ERROR)
                for text_key in ("pattern", "emblem", "trim"):
                    val: object = appearance.get(text_key)
                    if val is not None and (not isinstance(val, str) or not val):
                        self._add_issue(file, 1, None, f"items[{idx}].appearance.{text_key} must be a non-empty string", Severity.ERROR)

        bundles: object = data.get("bundles")
        if bundles is not None:
            if not isinstance(bundles, list):
                self._add_issue(file, 1, None, "'bundles' must be an array", Severity.ERROR)
            else:
                seen_bundle_ids: set[str] = set()
                for idx, bundle in enumerate(bundles, start=1):
                    if not isinstance(bundle, dict):
                        self._add_issue(file, 1, None, f"bundles[{idx}] must be an object", Severity.ERROR)
                        continue
                    for key in bundle.keys():
                        if key not in bundle_keys:
                            self._add_issue(file, 1, None, f"bundles[{idx}] has unknown key '{key}'", Severity.ERROR)
                    bid: object = bundle.get("id")
                    if not isinstance(bid, str) or not bid:
                        self._add_issue(file, 1, None, f"bundles[{idx}].id must be a non-empty string", Severity.ERROR)
                    elif bid in seen_bundle_ids:
                        self._add_issue(file, 1, None, f"Duplicate bundle id '{bid}'", Severity.ERROR)
                    else:
                        seen_bundle_ids.add(bid)
                    if not isinstance(bundle.get("name"), str):
                        self._add_issue(file, 1, None, f"bundles[{idx}].name must be a string", Severity.ERROR)
                    bprice: object = bundle.get("price_platinum")
                    if not isinstance(bprice, int) or isinstance(bprice, bool) or bprice < 0:
                        self._add_issue(file, 1, None, f"bundles[{idx}].price_platinum must be a non-negative integer", Severity.ERROR)
                    bitems: object = bundle.get("items")
                    if not isinstance(bitems, list) or not bitems:
                        self._add_issue(file, 1, None, f"bundles[{idx}].items must be a non-empty array", Severity.ERROR)
                    else:
                        for ref in bitems:
                            if not isinstance(ref, str) or ref not in seen_item_ids:
                                self._add_issue(file, 1, None, f"bundles[{idx}].items references unknown item {ref!r}", Severity.ERROR)

        products: object = data.get("products")
        if products is not None:
            if not isinstance(products, list):
                self._add_issue(file, 1, None, "'products' must be an array", Severity.ERROR)
            else:
                seen_product_ids: set[str] = set()
                for idx, product in enumerate(products, start=1):
                    if not isinstance(product, dict):
                        self._add_issue(file, 1, None, f"products[{idx}] must be an object", Severity.ERROR)
                        continue
                    for key in product.keys():
                        if key not in product_keys:
                            self._add_issue(file, 1, None, f"products[{idx}] has unknown key '{key}'", Severity.ERROR)
                    pid: object = product.get("id")
                    if not isinstance(pid, str) or not pid:
                        self._add_issue(file, 1, None, f"products[{idx}].id must be a non-empty string", Severity.ERROR)
                    elif pid in seen_product_ids:
                        self._add_issue(file, 1, None, f"Duplicate product id '{pid}'", Severity.ERROR)
                    else:
                        seen_product_ids.add(pid)
                    if not isinstance(product.get("name"), str):
                        self._add_issue(file, 1, None, f"products[{idx}].name must be a string", Severity.ERROR)
                    kind: object = product.get("kind")
                    if kind not in valid_kinds:
                        self._add_issue(file, 1, None, f"products[{idx}].kind must be one of {sorted(valid_kinds)}", Severity.ERROR)
                    real: object = product.get("price_real")
                    if not isinstance(real, (int, float)) or isinstance(real, bool) or real < 0:
                        self._add_issue(file, 1, None, f"products[{idx}].price_real must be a non-negative number", Severity.ERROR)
                    plat: object = product.get("platinum", 0)
                    if not isinstance(plat, int) or isinstance(plat, bool) or plat < 0:
                        self._add_issue(file, 1, None, f"products[{idx}].platinum must be a non-negative integer", Severity.ERROR)

        self.validated_files.append(file)

    def validate_chat(self, file: Path, content: str) -> None:
        """Validate chat.json (per-chat limits, group policy, friends, rate limit)."""
        data = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected an object", Severity.ERROR)
            return

        non_negative_int_fields = [
            "group_max_members", "group_name_change_cooldown_seconds",
            "invite_expiry_seconds", "max_groups_per_character",
            "max_pending_invites_per_group", "max_friend_requests_per_day",
            "friend_cap", "translation_reap_age_seconds",
            "translation_top_languages",
        ]
        for field in non_negative_int_fields:
            value = data.get(field)
            if not isinstance(value, int) or isinstance(value, bool) or value < 0:
                self._add_issue(file, 1, None, f"'{field}' must be a non-negative integer", Severity.ERROR)

        if not isinstance(data.get("mods_see_blocked"), bool):
            self._add_issue(file, 1, None, "'mods_see_blocked' must be a boolean", Severity.ERROR)

        limits = data.get("limits")
        if not isinstance(limits, dict) or not limits:
            self._add_issue(file, 1, None, "'limits' must be a non-empty object", Severity.ERROR)
        else:
            for key in ("wildlands", "barony", "dukedom", "group", "direct", "direct_friend"):
                entry = limits.get(key)
                if not isinstance(entry, dict):
                    self._add_issue(file, 1, None, f"limits.{key} must be an object", Severity.ERROR)
                    continue
                for field in ("max_characters", "max_lines"):
                    value = entry.get(field)
                    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
                        self._add_issue(file, 1, None, f"limits.{key}.{field} must be a positive integer", Severity.ERROR)

        rate = data.get("rate_limit")
        if not isinstance(rate, dict):
            self._add_issue(file, 1, None, "'rate_limit' must be an object", Severity.ERROR)
        else:
            for field in ("per_second", "per_minute", "per_5_minutes", "decay_seconds"):
                value = rate.get(field)
                if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
                    self._add_issue(file, 1, None, f"rate_limit.{field} must be a positive integer", Severity.ERROR)
            escalation = rate.get("escalation_seconds")
            if not isinstance(escalation, list) or not escalation:
                self._add_issue(file, 1, None, "rate_limit.escalation_seconds must be a non-empty array", Severity.ERROR)
            else:
                for i, value in enumerate(escalation):
                    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
                        self._add_issue(file, 1, None, f"rate_limit.escalation_seconds[{i}] must be a positive integer", Severity.ERROR)

        self.validated_files.append(file)

    def validate_safe_catalog(self, file: Path, content: str, root_key: str,
                              text_fields: tuple[str, ...] = (),
                              require_minor_safe: bool = False,
                              require_contexts: bool = False) -> None:
        """Validate one server-owned safe content catalog.

        Safe catalogs are the only content a minor may see or author. They must
        therefore be complete, uniquely identified, and explicitly marked
        minor-safe where the catalog can contain adult-only entries.
        """
        data = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected an object", Severity.ERROR)
            return
        entries = data.get(root_key)
        if not isinstance(entries, list) or not entries:
            self._add_issue(file, 1, None, f"'{root_key}' must be a non-empty array", Severity.ERROR)
            return
        seen: set[str] = set()
        valid_contexts = {"direct", "group", "barony", "dukedom", "wildlands", "combat"}
        for i, entry in enumerate(entries):
            if not isinstance(entry, dict):
                self._add_issue(file, 1, None, f"{root_key}[{i}] must be an object", Severity.ERROR)
                continue
            entry_id = entry.get("id")
            if not isinstance(entry_id, str) or not entry_id:
                self._add_issue(file, 1, None, f"{root_key}[{i}].id must be a non-empty string", Severity.ERROR)
            elif entry_id in seen:
                self._add_issue(file, 1, None, f"Duplicate {root_key} id: {entry_id}", Severity.ERROR)
            else:
                seen.add(entry_id)
            if text_fields and not any(isinstance(entry.get(field), str) and entry.get(field)
                                       for field in text_fields):
                self._add_issue(file, 1, None,
                                f"{root_key}[{i}] needs one non-empty text field "
                                f"({', '.join(text_fields)})", Severity.ERROR)
            if require_minor_safe and not isinstance(entry.get("minor_safe"), bool):
                self._add_issue(file, 1, None,
                                f"{root_key}[{i}].minor_safe must be a boolean", Severity.ERROR)
            if require_contexts:
                contexts = entry.get("contexts")
                if not isinstance(contexts, list) or not contexts:
                    self._add_issue(file, 1, None,
                                    f"{root_key}[{i}].contexts must be a non-empty array",
                                    Severity.ERROR)
                else:
                    for context in contexts:
                        if context not in valid_contexts:
                            self._add_issue(file, 1, None,
                                            f"{root_key}[{i}].contexts contains invalid "
                                            f"chat type: {context}", Severity.ERROR)
        self.validated_files.append(file)

    def validate_chat_admins(self, file: Path, content: str) -> None:
        """Validate chat_admins.json (account-id allowlist)."""
        data = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected an object", Severity.ERROR)
            return
        ids = data.get("account_ids")
        if not isinstance(ids, list):
            self._add_issue(file, 1, None, "'account_ids' must be an array", Severity.ERROR)
        else:
            for i, value in enumerate(ids):
                if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
                    self._add_issue(file, 1, None, f"account_ids[{i}] must be a positive integer", Severity.ERROR)
        self.validated_files.append(file)

    def validate_translation(self, file: Path, content: str) -> None:
        """Validate translation.json (translation sidecar connection)."""
        data = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected an object", Severity.ERROR)
            return
        if not isinstance(data.get("sidecar_url"), str) or not data.get("sidecar_url"):
            self._add_issue(file, 1, None, "'sidecar_url' must be a non-empty string", Severity.ERROR)
        if not isinstance(data.get("enabled"), bool):
            self._add_issue(file, 1, None, "'enabled' must be a boolean", Severity.ERROR)
        threshold = data.get("detection_confidence_threshold")
        if (not isinstance(threshold, (int, float)) or isinstance(threshold, bool)
                or not (0 <= threshold <= 1)):
            self._add_issue(file, 1, None, "'detection_confidence_threshold' must be a number in [0,1]", Severity.ERROR)
        timeout = data.get("request_timeout_ms")
        if not isinstance(timeout, int) or isinstance(timeout, bool) or timeout <= 0:
            self._add_issue(file, 1, None, "'request_timeout_ms' must be a positive integer", Severity.ERROR)
        self.validated_files.append(file)

    def validate_push(self, file: Path, content: str) -> None:
        """Validate push.json (VAPID keyring + Web Push defaults)."""
        data = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected an object", Severity.ERROR)
            return
        vapid = data.get("vapid")
        if not isinstance(vapid, dict):
            self._add_issue(file, 1, None, "'vapid' must be an object", Severity.ERROR)
        else:
            current = vapid.get("current")
            keys = vapid.get("keys")
            if not isinstance(current, str) or not current:
                self._add_issue(file, 1, None, "vapid.current must be a non-empty string", Severity.ERROR)
            if not isinstance(keys, dict) or not keys:
                self._add_issue(file, 1, None, "vapid.keys must be a non-empty object", Severity.ERROR)
            elif isinstance(current, str) and current not in keys:
                self._add_issue(file, 1, None, "vapid.current must name a key in vapid.keys", Severity.ERROR)
            if isinstance(keys, dict):
                for kid, entry in keys.items():
                    if not isinstance(entry, dict):
                        self._add_issue(file, 1, None, f"vapid.keys.{kid} must be an object", Severity.ERROR)
                        continue
                    for field in ("public", "private"):
                        if not isinstance(entry.get(field), str):
                            self._add_issue(file, 1, None, f"vapid.keys.{kid}.{field} must be a string", Severity.ERROR)
        if not isinstance(data.get("sub"), str) or not data.get("sub"):
            self._add_issue(file, 1, None, "'sub' must be a non-empty string", Severity.ERROR)
        ttl = data.get("default_ttl")
        if not isinstance(ttl, int) or isinstance(ttl, bool) or ttl <= 0:
            self._add_issue(file, 1, None, "'default_ttl' must be a positive integer", Severity.ERROR)
        self.validated_files.append(file)

    def validate_registration(self, file: Path, content: str) -> None:
        """Validate registration.json (registration gate + rate limiter)."""
        data = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected an object", Severity.ERROR)
            return

        mode = data.get("mode")
        if mode not in ("off", "global_code", "invite_only"):
            self._add_issue(
                file, 1, None,
                "'mode' must be one of 'off', 'global_code', 'invite_only'",
                Severity.ERROR,
            )

        global_code = data.get("global_code")
        if not isinstance(global_code, str):
            self._add_issue(file, 1, None, "'global_code' must be a string", Severity.ERROR)
        elif mode == "global_code" and not global_code.strip():
            self._add_issue(
                file, 1, None,
                "'global_code' must be non-empty when mode is 'global_code'",
                Severity.ERROR,
            )
        elif isinstance(global_code, str) and global_code.strip() and mode != "global_code":
            self._add_issue(
                file, 1, None,
                "'global_code' is set but mode is not 'global_code'",
                Severity.WARN,
            )

        if not isinstance(data.get("players_can_generate"), bool):
            self._add_issue(file, 1, None, "'players_can_generate' must be a boolean", Severity.ERROR)

        for field in ("player_daily_limit", "player_lifetime_limit"):
            value = data.get(field)
            if not isinstance(value, int) or isinstance(value, bool) or value < 0:
                self._add_issue(
                    file, 1, None, f"'{field}' must be a non-negative integer", Severity.ERROR
                )

        players_can_generate = data.get("players_can_generate")
        daily_limit = data.get("player_daily_limit")
        lifetime_limit = data.get("player_lifetime_limit")
        if players_can_generate is True:
            if mode != "invite_only":
                self._add_issue(
                    file, 1, None,
                    "'players_can_generate' is only effective in 'invite_only' mode",
                    Severity.WARN,
                )
            if (isinstance(daily_limit, int) and not isinstance(daily_limit, bool)
                    and daily_limit == 0) or (
                    isinstance(lifetime_limit, int) and not isinstance(lifetime_limit, bool)
                    and lifetime_limit == 0):
                self._add_issue(
                    file, 1, None,
                    "player generation is disabled because at least one player limit is 0",
                    Severity.WARN,
                )

        rate_limit = data.get("create_account_rate_limit")
        if not isinstance(rate_limit, dict):
            self._add_issue(
                file, 1, None, "'create_account_rate_limit' must be an object", Severity.ERROR
            )
        else:
            if not isinstance(rate_limit.get("enabled"), bool):
                self._add_issue(
                    file, 1, None,
                    "'create_account_rate_limit.enabled' must be a boolean",
                    Severity.ERROR,
                )
            positive_fields = (
                "window_seconds", "max_attempts_per_network", "max_entries",
                "prune_interval_seconds",
            )
            for field in positive_fields:
                value = rate_limit.get(field)
                if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
                    self._add_issue(
                        file, 1, None,
                        f"'create_account_rate_limit.{field}' must be a positive integer",
                        Severity.ERROR,
                    )
            prefix_bits = rate_limit.get("ipv6_prefix_bits")
            if (not isinstance(prefix_bits, int) or isinstance(prefix_bits, bool)
                    or not (1 <= prefix_bits <= 128)):
                self._add_issue(
                    file, 1, None,
                    "'create_account_rate_limit.ipv6_prefix_bits' must be an integer in 1..128",
                    Severity.ERROR,
                )

        self.validated_files.append(file)

    def validate_combat_offerings(self, file: Path) -> None:
        """Validate combat/offerings.json — auto-generated PvE mission offerings.

        Board/tier/quantizer/difficulty/encounter/XP/danger-band rules; the
        enemy-class weighting and prerequisite graph live on the combatants
        themselves (`requires`, cross-checked by validate_combatants).
        """
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return
        data: object = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with offerings config", Severity.ERROR)
            return

        if not isinstance(data.get("format_version"), str):
            self._add_issue(file, 1, None, "'format_version' must be a string", Severity.ERROR)

        board: object = data.get("board")
        if not isinstance(board, dict):
            self._add_issue(file, 1, None, "'board' must be an object", Severity.ERROR)
        else:
            per_tier: object = board.get("per_tier")
            if not isinstance(per_tier, int) or isinstance(per_tier, bool) or per_tier <= 0:
                self._add_issue(file, 1, None, "'board.per_tier' must be a positive integer", Severity.ERROR)
            validity: object = board.get("validity_hours")
            if not isinstance(validity, int) or isinstance(validity, bool) or validity <= 0:
                self._add_issue(file, 1, None, "'board.validity_hours' must be a positive integer", Severity.ERROR)

        power_scale: object = data.get("power_scale")
        if not isinstance(power_scale, dict):
            self._add_issue(file, 1, None, "'power_scale' must be an object", Severity.ERROR)
        else:
            if power_scale.get("mode") not in ("geometric", "linear"):
                self._add_issue(file, 1, None, "'power_scale.mode' must be 'geometric' or 'linear'", Severity.ERROR)
            base: object = power_scale.get("base")
            if not isinstance(base, (int, float)) or isinstance(base, bool) or base <= 0:
                self._add_issue(file, 1, None, "'power_scale.base' must be a positive number", Severity.ERROR)
            ratio: object = power_scale.get("ratio")
            if not isinstance(ratio, (int, float)) or isinstance(ratio, bool) or ratio <= 0:
                self._add_issue(file, 1, None, "'power_scale.ratio' must be a positive number", Severity.ERROR)

        difficulty: object = data.get("difficulty")
        if not isinstance(difficulty, dict):
            self._add_issue(file, 1, None, "'difficulty' must be an object", Severity.ERROR)
        else:
            band_min: object = difficulty.get("band_min")
            band_max: object = difficulty.get("band_max")
            if not isinstance(band_min, (int, float)) or isinstance(band_min, bool) or band_min <= 0:
                self._add_issue(file, 1, None, "'difficulty.band_min' must be a positive number", Severity.ERROR)
            if not isinstance(band_max, (int, float)) or isinstance(band_max, bool) or band_max <= 0:
                self._add_issue(file, 1, None, "'difficulty.band_max' must be a positive number", Severity.ERROR)
            if isinstance(band_min, (int, float)) and isinstance(band_max, (int, float)) and band_max < band_min:
                self._add_issue(file, 1, None, "'difficulty.band_max' must be >= band_min", Severity.ERROR)

        tiers: object = data.get("tiers")
        if not isinstance(tiers, list) or not tiers:
            self._add_issue(file, 1, None, "'tiers' must be a non-empty array", Severity.ERROR)
        else:
            seen_tiers: set[str] = set()
            for i, tier in enumerate(tiers):
                if not isinstance(tier, dict):
                    self._add_issue(file, 1, None, f"'tiers[{i}]' must be an object", Severity.ERROR)
                    continue
                tid: object = tier.get("id")
                if not isinstance(tid, str) or not tid:
                    self._add_issue(file, 1, None, f"'tiers[{i}].id' must be a non-empty string", Severity.ERROR)
                elif tid in seen_tiers:
                    self._add_issue(file, 1, None, f"duplicate tier id '{tid}'", Severity.ERROR)
                else:
                    seen_tiers.add(tid)
                tmin: object = tier.get("min_players")
                tmax: object = tier.get("max_players")
                if not isinstance(tmin, int) or isinstance(tmin, bool) or tmin < 1:
                    self._add_issue(file, 1, None, f"'tiers[{i}].min_players' must be a positive integer", Severity.ERROR)
                if not isinstance(tmax, int) or isinstance(tmax, bool) or tmax < 1:
                    self._add_issue(file, 1, None, f"'tiers[{i}].max_players' must be a positive integer", Severity.ERROR)
                if isinstance(tmin, int) and isinstance(tmax, int) and tmax < tmin:
                    self._add_issue(file, 1, None, f"'tiers[{i}].max_players' must be >= min_players", Severity.ERROR)
                if not isinstance(tier.get("requires_barony"), bool):
                    self._add_issue(file, 1, None, f"'tiers[{i}].requires_barony' must be a boolean", Severity.ERROR)

        encounter: object = data.get("encounter")
        if not isinstance(encounter, dict):
            self._add_issue(file, 1, None, "'encounter' must be an object", Severity.ERROR)
        else:
            max_units: object = encounter.get("max_units")
            if not isinstance(max_units, int) or isinstance(max_units, bool) or max_units <= 0:
                self._add_issue(file, 1, None, "'encounter.max_units' must be a positive integer", Severity.ERROR)
            prerequisite_depth: object = encounter.get("prerequisite_depth")
            if not isinstance(prerequisite_depth, int) or isinstance(prerequisite_depth, bool) or prerequisite_depth < 0:
                self._add_issue(file, 1, None, "'encounter.prerequisite_depth' must be a non-negative integer", Severity.ERROR)
            engagement_exponent: object = encounter.get("engagement_exponent")
            if not isinstance(engagement_exponent, (int, float)) or isinstance(engagement_exponent, bool) or engagement_exponent < 1:
                self._add_issue(file, 1, None, "'encounter.engagement_exponent' must be a number >= 1", Severity.ERROR)
            defense_weights: object = encounter.get("defense_weights")
            if not isinstance(defense_weights, dict):
                self._add_issue(file, 1, None, "'encounter.defense_weights' must be an object", Severity.ERROR)
            else:
                for weight_key in ("melee", "ranged", "magical"):
                    weight_value: object = defense_weights.get(weight_key)
                    if not isinstance(weight_value, (int, float)) or isinstance(weight_value, bool) or weight_value < 0:
                        self._add_issue(file, 1, None, f"'encounter.defense_weights.{weight_key}' must be a non-negative number", Severity.ERROR)
            aoe_multiplier: object = encounter.get("aoe_multiplier")
            if not isinstance(aoe_multiplier, (int, float)) or isinstance(aoe_multiplier, bool) or aoe_multiplier <= 0:
                self._add_issue(file, 1, None, "'encounter.aoe_multiplier' must be a positive number", Severity.ERROR)
            ranged_bonus_per_unit: object = encounter.get("ranged_bonus_per_unit")
            if not isinstance(ranged_bonus_per_unit, (int, float)) or isinstance(ranged_bonus_per_unit, bool) or ranged_bonus_per_unit < 0:
                self._add_issue(file, 1, None, "'encounter.ranged_bonus_per_unit' must be a non-negative number", Severity.ERROR)
            ranged_melee_ref: object = encounter.get("ranged_melee_ref")
            if not isinstance(ranged_melee_ref, (int, float)) or isinstance(ranged_melee_ref, bool) or ranged_melee_ref < 0:
                self._add_issue(file, 1, None, "'encounter.ranged_melee_ref' must be a non-negative number", Severity.ERROR)
            eliteness: object = encounter.get("eliteness")
            if not isinstance(eliteness, dict):
                self._add_issue(file, 1, None, "'encounter.eliteness' must be an object", Severity.ERROR)
            else:
                eliteness_fields: tuple[str, ...] = (
                    "seed_min", "seed_max", "players_ref", "troops_ref",
                    "players_coeff", "troops_coeff", "min", "max",
                )
                eliteness_numbers: dict[str, float] = {}
                for key in eliteness_fields:
                    value: object = eliteness.get(key)
                    if not isinstance(value, (int, float)) or isinstance(value, bool):
                        self._add_issue(file, 1, None, f"'encounter.eliteness.{key}' must be a number", Severity.ERROR)
                    else:
                        eliteness_numbers[key] = float(value)
                if "seed_min" in eliteness_numbers and "seed_max" in eliteness_numbers:
                    if eliteness_numbers["seed_max"] < eliteness_numbers["seed_min"]:
                        self._add_issue(file, 1, None, "'encounter.eliteness.seed_max' must be >= seed_min", Severity.ERROR)
                if "min" in eliteness_numbers and "max" in eliteness_numbers:
                    if eliteness_numbers["max"] < eliteness_numbers["min"]:
                        self._add_issue(file, 1, None, "'encounter.eliteness.max' must be >= min", Severity.ERROR)
                for key in ("players_ref", "troops_ref"):
                    if key in eliteness_numbers and eliteness_numbers[key] <= 0:
                        self._add_issue(file, 1, None, f"'encounter.eliteness.{key}' must be positive", Severity.ERROR)

        xp: object = data.get("xp")
        if not isinstance(xp, dict):
            self._add_issue(file, 1, None, "'xp' must be an object", Severity.ERROR)
        else:
            per_power: object = xp.get("per_power")
            if not isinstance(per_power, (int, float)) or isinstance(per_power, bool) or per_power < 0:
                self._add_issue(file, 1, None, "'xp.per_power' must be a non-negative number", Severity.ERROR)
            min_xp: object = xp.get("min")
            if not isinstance(min_xp, (int, float)) or isinstance(min_xp, bool) or min_xp < 0:
                self._add_issue(file, 1, None, "'xp.min' must be a non-negative number", Severity.ERROR)

        bands: object = data.get("danger_bands")
        if not isinstance(bands, list) or not bands:
            self._add_issue(file, 1, None, "'danger_bands' must be a non-empty array", Severity.ERROR)
        else:
            for i, band in enumerate(bands):
                if not isinstance(band, dict):
                    self._add_issue(file, 1, None, f"'danger_bands[{i}]' must be an object", Severity.ERROR)
                    continue
                if not isinstance(band.get("id"), str) or not band.get("id"):
                    self._add_issue(file, 1, None, f"'danger_bands[{i}].id' must be a non-empty string", Severity.ERROR)
                max_ratio: object = band.get("max_ratio")
                if max_ratio is not None and (not isinstance(max_ratio, (int, float)) or isinstance(max_ratio, bool) or max_ratio <= 0):
                    self._add_issue(file, 1, None, f"'danger_bands[{i}].max_ratio' must be null or a positive number", Severity.ERROR)

        map_pool: object = data.get("map_pool")
        if map_pool is not None:
            if not isinstance(map_pool, list) or not map_pool:
                self._add_issue(file, 1, None, "'map_pool' must be null or a non-empty array", Severity.ERROR)
            else:
                for i, m in enumerate(map_pool):
                    if not isinstance(m, str) or not m:
                        self._add_issue(file, 1, None, f"'map_pool[{i}]' must be a non-empty string", Severity.ERROR)

        self.validated_files.append(file)

    def validate_progression(self, file: Path) -> None:
        """Validate progression.json — freehold/mesne-lord gates + barony cooldown.

        `milestones` are counter-based conditions (e.g. slain.ettin >= 1); a
        `slain.<class>` counter is cross-checked against enemy_combatants.json.
        """
        try:
            content: str = file.read_text(encoding="utf-8")
        except Exception as e:
            self._add_issue(file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            return

        data: object = self._validate_json(content, file)
        if data is None:
            return
        if not isinstance(data, dict):
            self._add_issue(file, 1, None, "Expected object with progression config", Severity.ERROR)
            return

        mesne: object = data.get("mesne_lord")
        if mesne is None:
            self._add_issue(file, 1, None, "Missing 'mesne_lord' block", Severity.ERROR)
        elif not isinstance(mesne, dict):
            self._add_issue(file, 1, None, "'mesne_lord' must be an object", Severity.ERROR)
        else:
            requirements: object = mesne.get("requirements")
            if not isinstance(requirements, dict):
                self._add_issue(file, 1, None, "'mesne_lord.requirements' must be an object", Severity.ERROR)
            else:
                manor_level: object = requirements.get("manor_level")
                if not isinstance(manor_level, int) or isinstance(manor_level, bool) or manor_level < 0:
                    self._add_issue(file, 1, None, "'mesne_lord.requirements.manor_level' must be a non-negative integer", Severity.ERROR)
                milestones: object = requirements.get("milestones")
                if not isinstance(milestones, list):
                    self._add_issue(file, 1, None, "'mesne_lord.requirements.milestones' must be an array", Severity.ERROR)
                else:
                    for i, milestone in enumerate(milestones):
                        if not isinstance(milestone, str) or not milestone:
                            self._add_issue(file, 1, None, f"'mesne_lord.requirements.milestones[{i}]' must be a non-empty string", Severity.ERROR)

        boss: object = data.get("boss_milestones")
        if boss is not None:
            self._add_issue(file, 1, None, "'boss_milestones' is deprecated — use counter-based 'milestones'", Severity.WARN)

        milestones: object = data.get("milestones")
        if milestones is not None:
            if not isinstance(milestones, list):
                self._add_issue(file, 1, None, "'milestones' must be an array", Severity.ERROR)
            else:
                for i, cond in enumerate(milestones):
                    if not isinstance(cond, dict):
                        self._add_issue(file, 1, None, f"'milestones[{i}]' must be an object", Severity.ERROR)
                        continue
                    if not isinstance(cond.get("id"), str) or not cond.get("id"):
                        self._add_issue(file, 1, None, f"'milestones[{i}].id' must be a non-empty string", Severity.ERROR)
                    counter: object = cond.get("counter")
                    if not isinstance(counter, str) or not counter:
                        self._add_issue(file, 1, None, f"'milestones[{i}].counter' must be a non-empty string", Severity.ERROR)
                    elif counter.startswith("slain.") and counter[len("slain."):] not in self.validated_enemy_combatant_ids:
                        self._add_issue(file, 1, None, f"'milestones[{i}].counter' references unknown enemy class '{counter[len('slain.'):]}'", Severity.WARN)
                    min_val: object = cond.get("min")
                    if not isinstance(min_val, int) or isinstance(min_val, bool) or min_val < 1:
                        self._add_issue(file, 1, None, f"'milestones[{i}].min' must be a positive integer", Severity.ERROR)

        barony: object = data.get("barony")
        if barony is not None:
            if not isinstance(barony, dict):
                self._add_issue(file, 1, None, "'barony' must be an object", Severity.ERROR)
            else:
                cooldown: object = barony.get("join_cooldown_days")
                if not isinstance(cooldown, int) or isinstance(cooldown, bool) or cooldown < 0:
                    self._add_issue(file, 1, None, "'barony.join_cooldown_days' must be a non-negative integer", Severity.ERROR)

        self.validated_files.append(file)

    def validate_all(
        self,
        config_dir: Path,
        game_config_dir: Path | None = None,
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
        wall_config_file: Path = config_dir / "wall_config.json"

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

        # Validate wall_config.json
        if wall_config_file.exists():
            try:
                wall_content: str = wall_config_file.read_text(encoding="utf-8")
                self.validate_wall_config(wall_config_file, wall_content)
            except Exception as e:
                self._add_issue(wall_config_file, 1, None, f"Failed to read file: {e}", Severity.ERROR)

        # Validate manor_ui.json (needs validated_building_ids, so it runs after
        # the buildings loop above)
        manor_ui_file: Path = config_dir / "manor_ui.json"
        if manor_ui_file.exists():
            try:
                manor_ui_content: str = manor_ui_file.read_text(encoding="utf-8")
                self.validate_manor_ui(manor_ui_file, manor_ui_content, buildings_file)
            except Exception as e:
                self._add_issue(manor_ui_file, 1, None, f"Failed to read file: {e}", Severity.ERROR)

        # Validate manor_river.json (river templates)
        manor_river_file: Path = config_dir / "manor_river.json"
        if manor_river_file.exists():
            try:
                river_content: str = manor_river_file.read_text(encoding="utf-8")
                self.validate_manor_river(manor_river_file, river_content)
            except Exception as e:
                self._add_issue(manor_river_file, 1, None,
                                f"Failed to read file: {e}", Severity.ERROR)

        # Validate analyzer_manor_strategies.json (analyzer-only; needs
        # validated_building_ids from the buildings loop above)
        manor_strategies_file: Path = config_dir / "analyzer_manor_strategies.json"
        if manor_strategies_file.exists():
            try:
                ms_content: str = manor_strategies_file.read_text(encoding="utf-8")
                self.validate_manor_strategies(manor_strategies_file, ms_content)
            except Exception as e:
                self._add_issue(manor_strategies_file, 1, None,
                                f"Failed to read file: {e}", Severity.ERROR)

        # Validate progression.json (needs validated_enemy_combatant_ids from the
        # combatants loop above)
        progression_file: Path = config_dir / "progression.json"
        if progression_file.exists():
            try:
                self.validate_progression(progression_file)
            except Exception as e:
                self._add_issue(progression_file, 1, None,
                                f"Failed to read file: {e}", Severity.ERROR)

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
                self.validate_portrait_pools(images_dir)

        # Validate tower defense spawn schedules
        if game_config_dir is not None:
            if not game_config_dir.exists():
                self._add_issue(
                    game_config_dir, 1, None,
                    "Game config directory not found",
                    Severity.WARN
                )
            else:
                self.validate_spawn_schedules(game_config_dir)
                self.validate_wave_templates(game_config_dir)

        # Re-sort issues after image validation
        errors = [issue for issue in self.issues if issue.severity == Severity.ERROR]
        warnings = [issue for issue in self.issues if issue.severity == Severity.WARN]

        # Validate weeding configs
        if game_config_dir is not None and game_config_dir.exists():
            weeding_plants_file: Path = game_config_dir / "weeding" / "plants.json"
            if weeding_plants_file.exists():
                self.validate_weeding_plants(weeding_plants_file)

            weeding_tools_file: Path = game_config_dir / "weeding" / "tools.json"
            if weeding_tools_file.exists():
                self.validate_weeding_tools(weeding_tools_file)

            # Validate ongoing-mode configs (server-served difficulty/size/rewards)
            td_ongoing_file: Path = game_config_dir / "tower_defense" / "ongoing.json"
            if td_ongoing_file.exists():
                self.validate_ongoing_config(td_ongoing_file)
            else:
                self._add_issue(td_ongoing_file, 1, None, "Config file not found", Severity.ERROR)

            wd_ongoing_file: Path = game_config_dir / "weeding" / "ongoing.json"
            if wd_ongoing_file.exists():
                self.validate_ongoing_config(wd_ongoing_file)
            else:
                self._add_issue(wd_ongoing_file, 1, None, "Config file not found", Severity.ERROR)

            # Validate economy.json (currency + reward_pools)
            economy_file: Path = game_config_dir / "economy.json"
            if economy_file.exists():
                self.validate_economy(economy_file)

            # Validate retinue.json (capacity curve, market, recovery, morale)
            retinue_file: Path = game_config_dir / "retinue.json"
            if retinue_file.exists():
                self.validate_retinue(retinue_file)

            # Validate portrait_frames.json (decorative portrait overlay geometry)
            portrait_frames_file: Path = game_config_dir / "portrait_frames.json"
            if portrait_frames_file.exists():
                try:
                    pf_content: str = portrait_frames_file.read_text(encoding="utf-8")
                    self.validate_portrait_frames(portrait_frames_file, pf_content,
                                                  game_config_dir.parent / "images")
                except Exception as e:
                    self._add_issue(portrait_frames_file, 1, None,
                                    f"Failed to read file: {e}", Severity.ERROR)
            else:
                self._add_issue(portrait_frames_file, 1, None,
                                "Config file not found", Severity.WARN)

            # Validate gear/tech/storage configs
            equipment_file: Path = game_config_dir / "equipment.json"
            if equipment_file.exists():
                self.validate_equipment(equipment_file)
            items_file: Path = game_config_dir / "items.json"
            if items_file.exists():
                self.validate_items(items_file)
            tech_trees_file: Path = game_config_dir / "tech_trees.json"
            if tech_trees_file.exists():
                self.validate_tech_trees(tech_trees_file)

            # Validate combat configs (rulesets + maps)
            combat_rulesets_file: Path = game_config_dir / "combat" / "rulesets.json"
            if combat_rulesets_file.exists():
                self.validate_combat_rulesets(combat_rulesets_file)
            else:
                self._add_issue(combat_rulesets_file, 1, None, "Config file not found", Severity.ERROR)

            combat_maps_dir: Path = game_config_dir / "combat" / "maps"
            if combat_maps_dir.exists():
                for map_file in sorted(combat_maps_dir.glob("*.json")):
                    self.validate_combat_maps(map_file)
                    self.validated_files.append(map_file)
            else:
                self._add_issue(combat_maps_dir, 1, None, "Combat maps directory not found", Severity.ERROR)

            # Combat content configs (warbands, structures, formations,
            # profiles, rewards). Cross-references enemy combatant ids.
            combat_warbands_file: Path = game_config_dir / "combat" / "warbands.json"
            if combat_warbands_file.exists():
                self.validate_combat_warbands(combat_warbands_file, self.validated_enemy_combatant_ids)
            else:
                self._add_issue(combat_warbands_file, 1, None, "Config file not found", Severity.ERROR)

            combat_structures_file: Path = game_config_dir / "combat" / "structures.json"
            if combat_structures_file.exists():
                self.validate_combat_structures(combat_structures_file)
            else:
                self._add_issue(combat_structures_file, 1, None, "Config file not found", Severity.ERROR)

            combat_formations_file: Path = game_config_dir / "combat" / "formations.json"
            if combat_formations_file.exists():
                self.validate_combat_formations(combat_formations_file)
            else:
                self._add_issue(combat_formations_file, 1, None, "Config file not found", Severity.ERROR)

            combat_profiles_file: Path = game_config_dir / "combat" / "profiles.json"
            if combat_profiles_file.exists():
                self.validate_combat_profiles(combat_profiles_file)
            else:
                self._add_issue(combat_profiles_file, 1, None, "Config file not found", Severity.ERROR)

            combat_rewards_file: Path = game_config_dir / "combat" / "rewards.json"
            if combat_rewards_file.exists():
                self.validate_combat_rewards(combat_rewards_file)
            else:
                self._add_issue(combat_rewards_file, 1, None, "Config file not found", Severity.ERROR)

            combat_offerings_file: Path = game_config_dir / "combat" / "offerings.json"
            if combat_offerings_file.exists():
                self.validate_combat_offerings(combat_offerings_file)
            else:
                self._add_issue(combat_offerings_file, 1, None, "Config file not found", Severity.ERROR)

            # Premium-variety catalog (Platinum cosmetics; rts_design.md §15).
            cosmetics_file: Path = game_config_dir / "cosmetics.json"
            if cosmetics_file.exists():
                self.validate_cosmetics(cosmetics_file)
            else:
                self._add_issue(cosmetics_file, 1, None, "Config file not found", Severity.ERROR)

            # Chat configs: chat.json (limits/policy), chat_admins.json (admin
            # allowlist), translation.json (sidecar), push.json (VAPID).
            chat_file: Path = game_config_dir / "chat.json"
            if chat_file.exists():
                try:
                    self.validate_chat(chat_file, chat_file.read_text(encoding="utf-8"))
                except Exception as e:
                    self._add_issue(chat_file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            else:
                self._add_issue(chat_file, 1, None, "Config file not found", Severity.ERROR)

            chat_admins_file: Path = game_config_dir / "chat_admins.json"
            if chat_admins_file.exists():
                try:
                    self.validate_chat_admins(chat_admins_file, chat_admins_file.read_text(encoding="utf-8"))
                except Exception as e:
                    self._add_issue(chat_admins_file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            else:
                self._add_issue(chat_admins_file, 1, None, "Config file not found", Severity.WARN)

            # Server-owned safe content catalogs (minor visibility/authoring).
            safe_catalogs: list[tuple[str, str, tuple[str, ...], bool, bool]] = [
                ("safe_messages.json", "messages", ("body", "label"), False, True),
                ("safe_barony_names.json", "names", ("name",), False, False),
                ("safe_barony_tags.json", "tags", ("name",), False, False),
                ("safe_reactions.json", "reactions", ("emoji",), True, False),
                ("safe_gifs.json", "gifs", ("asset_url",), True, False),
                ("safe_ban_reasons.json", "ban_reasons", ("label",), False, False),
            ]
            for filename, root_key, text_fields, require_minor_safe, require_contexts in safe_catalogs:
                catalog_file: Path = game_config_dir / filename
                if not catalog_file.exists():
                    self._add_issue(catalog_file, 1, None, "Config file not found", Severity.ERROR)
                    continue
                try:
                    self.validate_safe_catalog(
                        catalog_file,
                        catalog_file.read_text(encoding="utf-8"),
                        root_key,
                        text_fields,
                        require_minor_safe,
                        require_contexts,
                    )
                except Exception as e:
                    self._add_issue(catalog_file, 1, None, f"Failed to read file: {e}", Severity.ERROR)

            translation_file: Path = game_config_dir / "translation.json"
            if translation_file.exists():
                try:
                    self.validate_translation(translation_file, translation_file.read_text(encoding="utf-8"))
                except Exception as e:
                    self._add_issue(translation_file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            else:
                self._add_issue(translation_file, 1, None, "Config file not found", Severity.WARN)

            push_file: Path = game_config_dir / "push.json"
            if push_file.exists():
                try:
                    self.validate_push(push_file, push_file.read_text(encoding="utf-8"))
                except Exception as e:
                    self._add_issue(push_file, 1, None, f"Failed to read file: {e}", Severity.ERROR)
            else:
                self._add_issue(push_file, 1, None, "Config file not found", Severity.WARN)

            registration_file: Path = game_config_dir / "registration.json"
            if registration_file.exists():
                try:
                    self.validate_registration(
                        registration_file, registration_file.read_text(encoding="utf-8")
                    )
                except Exception as e:
                    self._add_issue(
                        registration_file, 1, None, f"Failed to read file: {e}", Severity.ERROR
                    )
            else:
                self._add_issue(
                    registration_file, 1, None, "Config file not found", Severity.ERROR
                )

            mini_games_file: Path = game_config_dir / "mini_games.json"
            if mini_games_file.exists():
                self.validate_mini_games_weeding_levels(mini_games_file)

            # Validate weeding level config files referenced in mini_games.json
            if mini_games_file.exists():
                try:
                    mg_content: str = mini_games_file.read_text(encoding="utf-8")
                    mg_data: object = self._validate_json(mg_content, mini_games_file)
                    if isinstance(mg_data, dict) and "weeding" in mg_data:
                        wc: object = mg_data["weeding"]
                        if isinstance(wc, dict):
                            for field in ("schedule_file", "baron_schedule_file"):
                                sched_file: object = wc.get(field)
                                if isinstance(sched_file, str):
                                    sched_path: Path = game_config_dir / "weeding" / sched_file
                                    if sched_path.exists():
                                        self.validate_weeding_level_config(sched_path)
                                    else:
                                        self._add_issue(mini_games_file, 1, None, f"Referenced weeding level config not found: weeding/{sched_file}", Severity.ERROR)
                                elif sched_file is not None:
                                    self._add_issue(mini_games_file, 1, None, f"'{field}' must be a string", Severity.ERROR)
                except Exception as e:
                    self._add_issue(mini_games_file, 1, None, f"Failed to read or parse mini_games.json: {e}", Severity.ERROR)

        # Re-sort after all validation
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
  ./tools/check_configs.py                    # Show errors and warnings
  ./tools/check_configs.py --no-warnings      # Show errors only
  ./tools/check_configs.py -h                 # Show this help
  ./tools/check_configs.py --verbose          # Show validated files on success
  ./tools/check_configs.py -c game/config -g game/config     # Explicit paths

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
        default=Path("game/config"),
        help="Directory containing config files (default: game/config)"
    )
    parser.add_argument(
        "--game-config-dir", "-g",
        type=Path,
        default=Path("game/config"),
        help="Directory containing game config files (default: game/config)"
    )
    return parser.parse_args()


def main() -> Literal[0, 1]:
    """Main entry point."""
    args: argparse.Namespace = parse_args()

    project_root: Path = Path(__file__).parent.parent

    config_dir: Path = args.config_dir
    if not config_dir.is_absolute():
        config_dir = project_root / config_dir

    if not config_dir.exists():
        print(f"Error: Config directory not found: {config_dir}", file=sys.stderr)
        return 1

    game_config_dir: Path = args.game_config_dir
    if not game_config_dir.is_absolute():
        game_config_dir = project_root / game_config_dir

    validator: ConfigValidator = ConfigValidator()
    errors, warnings = validator.validate_all(
        config_dir,
        game_config_dir=game_config_dir,
        show_warnings=not args.no_warnings
    )

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