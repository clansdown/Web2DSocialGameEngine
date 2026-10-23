#!/usr/bin/env python3
"""
check_art.py — report which game art the config files expect but that is missing.

Walks the JSON config files under `game/config`, resolves every referenced image
to where it must live under `game/images`, checks the disk, and prints a
human-readable, grouped report of what still needs to be authored — including
the exact destination path for each missing asset.

This is a read-only reporting tool. It never creates, modifies, or deletes
anything. (For placeholder generation see `tools/generate_placeholder_art.py`.)

===============================================================================
WHAT IT CHECKS
===============================================================================

Configuration art (explicit paths / basenames in config):

  - fiefdom_building_types.json .... image, construction_image, road_tiles, race_tiles
  - portrait_frames.json ........... frames.<variant>.image
  - mini_games.json ................ <game>.image (campaign cards)
  - ui_textures.json ............... texture_prefix (needs at least one match in images/ui)
  - tower_defense/mobs.json ........ <id>.image_file
  - tower_defense/towers.json ...... <id>.image_file
  - tower_defense/units.json ....... <id>.image_file
  - tower_defense/projectiles.json . image_file + area_effect.image_file
  - tower_defense/maps/*.json ...... imageFilename
  - weeding/plants.json ............ sprite.image_file + damage_sprites variants
  - weeding/tools.json ............. sprite.image_file
  - weeding/specials.json .......... sprite.image_file
  - weeding/maps/*.json ............ image_file
  - combat/maps/*.json ............. image_filename
  - player/enemy_combatants.json ... animations.<clip>/<set>/ (non-empty frame dirs)

Data-driven pools (scanned by the server, no catalog config):

  - images/retinue/headshots/{male,female}/   (must be non-empty)
  - images/avatars/safe/                      (must be non-empty)

Server-scanned entity art (legacy: the fixed directory layout the server/linter
expects for buildings without an explicit `image`, heroes, hero skills, and
official portraits). Grouped separately and clearly marked, since the current
game mostly uses external manor images, retinue headshots, and procedural RTS
sprites instead.

===============================================================================
OUTPUT
===============================================================================

By default only categories with at least one missing asset are shown, and only
the missing assets within them. Nothing is printed for categories that are fully
covered. Pass --all to also list present assets and fully-covered categories.

    ART CHECK   (game/config -> game/images)
    91 missing . 118 present . 209 expected

    Manor building sprites - 18 missing
      x Coppicer                      game/images/manor/buildings/coppicer.png
      x Timber hauler                 game/images/manor/buildings/timber_hauler.png
      ...and 15 more (use --all to show everything)

    Combat - map backgrounds - 1 missing
      x Meadow                        game/images/combat/maps/meadow.png

Buildings are reported most-likely-built-first: sorted by manor-level gate and
then by level-1 build cost (gold-equivalent, priced from economy.json's import
prices), so the art a new player encounters first is listed first. Building
sprites and construction sprites share the same building order.

===============================================================================
USAGE
===============================================================================

    python3 tools/check_art.py                        # human report (missing only)
    python3 tools/check_art.py --all                  # include present assets
    python3 tools/check_art.py --category "manor"     # only matching categories
    python3 tools/check_art.py --paths-only           # just the missing paths
    python3 tools/check_art.py --json                 # machine-readable
    python3 tools/check_art.py --no-fail              # always exit 0

Exit codes:
    0  nothing missing (or --no-fail)
    1  one or more assets are missing
    2  bad arguments / missing config or images directory
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

REPO_ROOT: Path = Path(__file__).resolve().parent.parent

ALLOWED_IMAGE_EXTENSIONS: set[str] = {".png", ".jpg", ".jpeg", ".webp", ".avif"}
MAX_ITEMS_PER_CATEGORY: int = 15
NAME_COLUMN_WIDTH: int = 36

# Human-readable group headings. "configuration" has no heading because those
# categories are the primary content and read fine on their own.
GROUP_HEADINGS: dict[str, str] = {
    "pools": "Data-driven pools",
    "legacy": "Server-scanned entity art (legacy — likely unused)",
}


@dataclass(frozen=True)
class art_category:
    """One report section: a human title plus a coarse grouping."""

    id: str
    title: str
    group: str  # "configuration" | "pools" | "legacy"


@dataclass
class art_expectation:
    """One image (or image directory) the configs say should exist."""

    category_id: str
    name: str
    kind: str  # "file" | "dir" | "glob"
    path: Path
    source: str = ""
    prefix: str = ""


@dataclass
class art_check:
    """The disk result for one expectation."""

    expectation: art_expectation
    present: bool
    existed: bool
    file_count: int


CATEGORIES: list[art_category] = [
    art_category("manor_sprites", "Manor building sprites", "configuration"),
    art_category("manor_construction", "Manor construction sprites", "configuration"),
    art_category("manor_road_tiles", "Manor road tiles", "configuration"),
    art_category("manor_race_tiles", "Manor race tiles", "configuration"),
    art_category("portrait_frames", "Portrait frames", "configuration"),
    art_category("campaign_images", "Campaign images", "configuration"),
    art_category("ui_vellum", "UI text textures", "configuration"),
    art_category("td_mobs", "Tower defense — mobs", "configuration"),
    art_category("td_towers", "Tower defense — towers", "configuration"),
    art_category("td_units", "Tower defense — units", "configuration"),
    art_category("td_projectiles", "Tower defense — projectiles", "configuration"),
    art_category("td_maps", "Tower defense — map backgrounds", "configuration"),
    art_category("weeding_plants", "Weeding — plants", "configuration"),
    art_category("weeding_damage", "Weeding — plant damage sprites", "configuration"),
    art_category("weeding_tools", "Weeding — tools", "configuration"),
    art_category("weeding_specials", "Weeding — specials", "configuration"),
    art_category("weeding_maps", "Weeding — map backgrounds", "configuration"),
    art_category("combat_maps", "Combat — map backgrounds", "configuration"),
    art_category("combatant_animations", "Combatant sprite animations", "configuration"),
    art_category("retinue_headshots", "Retinue headshots", "pools"),
    art_category("safe_avatars", "Safe avatar pool", "pools"),
    art_category("entity_buildings", "Building sprites (server-scanned)", "legacy"),
    art_category("entity_heroes", "Hero sprites", "legacy"),
    art_category("entity_hero_skills", "Hero skill icons", "legacy"),
    art_category("entity_portraits", "Official portraits", "legacy"),
]


def parse_arguments() -> argparse.Namespace:
    """Parse command-line arguments."""
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="Report game art referenced by configs that is missing on disk.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("--config-dir", default="game/config",
                        help="Directory of JSON config files (default: game/config)")
    parser.add_argument("--images-dir", default="game/images",
                        help="Directory of game images (default: game/images)")
    parser.add_argument("--all", action="store_true",
                        help="Also list present assets and fully-covered categories")
    parser.add_argument("--paths-only", action="store_true",
                        help="Print only the missing filesystem paths (one per line)")
    parser.add_argument("--json", action="store_true",
                        help="Print a machine-readable JSON report")
    parser.add_argument("--category", action="append", default=[], metavar="NAME",
                        help="Only include categories whose id or title contains NAME "
                             "(repeatable)")
    parser.add_argument("-q", "--quiet", action="store_true",
                        help="Print only the one-line summary")
    parser.add_argument("--no-fail", action="store_true",
                        help="Always exit 0, even when art is missing")
    return parser.parse_args()


def display_path(path: Path) -> str:
    """Render a path relative to the repository root when possible."""
    try:
        return str(path.relative_to(REPO_ROOT))
    except ValueError:
        return str(path)


def resolve_repo_path(value: str) -> Path:
    """Resolve a CLI path, anchoring relative paths at the repository root."""
    candidate: Path = Path(value)
    if candidate.is_absolute():
        return candidate
    return (REPO_ROOT / candidate).resolve()


def pretty_label(identifier: str) -> str:
    """Turn a snake_case id into a title-cased human label."""
    return identifier.replace("_", " ").strip().title()


def split_extension(filename: str) -> tuple[str, str]:
    """Split 'foo.png' into ('foo', '.png'), tolerating names without an extension."""
    dot: int = filename.rfind(".")
    if dot <= 0:
        return filename, ""
    return filename[:dot], filename[dot:]


def combatant_clip_sets(clip: dict) -> list[str]:
    """The authored frame sets a combatant animation clip references.

    Mirrors the server/client resolver: explicit `views` values (a set name or a
    `{set, mirror}` alias), else all eight compass direction names.
    """
    views: Any = clip.get("views")
    if not isinstance(views, dict):
        return ["e", "se", "s", "sw", "w", "nw", "n", "ne"]
    sets: list[str] = []
    for entry in views.values():
        if isinstance(entry, str):
            set_name: Any = entry
        elif isinstance(entry, dict):
            set_name = entry.get("set")
        else:
            continue
        if isinstance(set_name, str) and set_name and set_name not in sets:
            sets.append(set_name)
    return sets


def served_path_to_disk(served_path: str, images_dir: Path) -> Path:
    """Map a served URL like '/images/manor/buildings/x.png' to its disk path."""
    if served_path.startswith("/images/"):
        return images_dir / served_path[len("/images/"):]
    return images_dir / served_path.lstrip("/")


def load_json(path: Path) -> Any | None:
    """Load a JSON file, warning on stderr and returning None on failure."""
    if not path.exists():
        print(f"warning: config file not found: {display_path(path)}", file=sys.stderr)
        return None
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"warning: could not read {display_path(path)}: {error}", file=sys.stderr)
        return None


def money_value_to_gold(value: Any, pence_per_shilling: float,
                        pence_per_gold: float) -> float:
    """Convert a build-cost amount to gold.

    A plain number is already gold-denominated. A money object
    (`{gold, shillings, pence}`) is converted at the medieval ratios.
    Non-numeric values count as 0.
    """
    if isinstance(value, bool):
        return 0.0
    if isinstance(value, (int, float)):
        return float(value)
    if isinstance(value, dict):
        gold: float = float(value.get("gold", 0) or 0)
        shillings: float = float(value.get("shillings", 0) or 0)
        pence: float = float(value.get("pence", 0) or 0)
        return gold + (shillings * pence_per_shilling + pence) / pence_per_gold
    return 0.0


def load_money_basis(config_dir: Path) -> tuple[dict[str, float], float, float]:
    """Read economy.json and return (import_prices_gold, pence_per_shilling,
    pence_per_gold).

    Import prices may be plain gold numbers or money objects; both are
    normalized to a gold-denominated value. Falls back to empty prices and the
    standard medieval ratios (12 pence/shillling, 240 pence/gold) when the
    economy config is missing or malformed.
    """
    data: Any = load_json(config_dir / "economy.json")
    pence_per_shilling: float = 12.0
    pence_per_gold: float = 240.0
    import_prices_gold: dict[str, float] = {}
    if not isinstance(data, dict):
        return import_prices_gold, pence_per_shilling, pence_per_gold
    currency: Any = data.get("currency")
    if isinstance(currency, dict):
        pps: Any = currency.get("pence_per_shilling")
        ppg: Any = currency.get("pence_per_gold")
        if isinstance(pps, (int, float)) and not isinstance(pps, bool) and pps > 0:
            pence_per_shilling = float(pps)
        if isinstance(ppg, (int, float)) and not isinstance(ppg, bool) and ppg > 0:
            pence_per_gold = float(ppg)
    import_prices: Any = data.get("import_prices")
    if isinstance(import_prices, dict):
        for resource, price in import_prices.items():
            if isinstance(resource, str):
                import_prices_gold[resource] = money_value_to_gold(
                    price, pence_per_shilling, pence_per_gold)
    return import_prices_gold, pence_per_shilling, pence_per_gold


def building_manor_level_gate(building: dict) -> int:
    """The manor level a building first becomes buildable at.

    Reads the first `manor_level` found in `prerequisites` (entries may also
    carry building-id gates like `{"woodcutter": 1}`); 0 when ungated.
    """
    prerequisites: Any = building.get("prerequisites")
    if isinstance(prerequisites, list):
        for entry in prerequisites:
            if isinstance(entry, dict):
                level: Any = entry.get("manor_level")
                if isinstance(level, (int, float)) and not isinstance(level, bool):
                    return int(level)
    return 0


def building_level_one_cost_gold(building: dict, import_prices_gold: dict[str, float],
                                 pence_per_shilling: float, pence_per_gold: float) -> float:
    """Full gold-equivalent cost of a building's level-1 build materials.

    Sums every top-level `<resource>_cost` field at level 1 (the first build),
    excluding `daily_cost` upkeep, converting resources through
    `import_prices_gold` and silver pence at the currency ratio. Gold is treated
    as just another term.
    """
    total: float = 0.0
    for cost_field, costs in building.items():
        if not isinstance(cost_field, str) or not cost_field.endswith("_cost"):
            continue
        if cost_field == "daily_cost":
            continue
        resource: str = cost_field[: -len("_cost")]
        if isinstance(costs, list):
            if not costs:
                continue
            amount: Any = costs[0]
        else:
            amount = costs
        if resource == "gold":
            total += money_value_to_gold(amount, pence_per_shilling, pence_per_gold)
        elif resource == "silver_pence":
            # Penny-market cost: a plain pence number (money objects tolerated).
            if isinstance(amount, dict):
                total += money_value_to_gold(amount, pence_per_shilling, pence_per_gold)
            else:
                total += money_value_to_gold(amount, pence_per_shilling,
                                             pence_per_gold) / pence_per_gold
        else:
            price: float | None = import_prices_gold.get(resource)
            if price is None:
                continue
            total += money_value_to_gold(amount, pence_per_shilling,
                                         pence_per_gold) * price
    return total


def building_sort_key(building_id: str, building: dict,
                      import_prices_gold: dict[str, float], pence_per_shilling: float,
                      pence_per_gold: float, config_index: int) -> tuple[int, float, int]:
    """Sort buildings most-likely-built-first: manor gate, then level-1 cost.

    `config_index` is a stable tiebreak preserving the config author's order.
    """
    return (building_manor_level_gate(building),
            building_level_one_cost_gold(building, import_prices_gold,
                                         pence_per_shilling, pence_per_gold),
            config_index)


def count_image_files(directory: Path) -> int:
    """Count image files (allowed formats) directly inside a directory."""
    if not directory.is_dir():
        return 0
    return sum(
        1
        for entry in directory.iterdir()
        if entry.is_file() and entry.suffix.lower() in ALLOWED_IMAGE_EXTENSIONS
    )


def count_prefixed_images(directory: Path, prefix: str) -> int:
    """Count image files directly inside a directory whose name starts with prefix."""
    if not directory.is_dir():
        return 0
    return sum(
        1
        for entry in directory.iterdir()
        if entry.is_file()
        and entry.name.startswith(prefix)
        and entry.suffix.lower() in ALLOWED_IMAGE_EXTENSIONS
    )


class art_collector:
    """Builds the list of expected art assets from the config files."""

    def __init__(self, config_dir: Path, images_dir: Path) -> None:
        self.config_dir: Path = config_dir
        self.images_dir: Path = images_dir
        self.expectations: list[art_expectation] = []
        self.building_ids: list[str] = []
        self.external_image_building_ids: set[str] = set()

    def collect(self) -> list[art_expectation]:
        """Run every section collector and return the combined expectations."""
        self._collect_buildings()
        self._collect_portrait_frames()
        self._collect_campaign_images()
        self._collect_ui_textures()
        self._collect_tower_defense()
        self._collect_weeding()
        self._collect_combat_maps()
        self._collect_combatant_animations()
        self._collect_pools()
        self._collect_legacy_entities()
        return self.expectations

    def _add(self, category_id: str, name: str, kind: str, path: Path,
             source: str = "", prefix: str = "") -> None:
        self.expectations.append(
            art_expectation(category_id, name, kind, path, source, prefix)
        )

    def _collect_buildings(self) -> None:
        """Manor building sprites, construction sprites, and road/race tiles.

        Buildings are emitted most-likely-built-first (manor-level gate, then
        level-1 gold-equivalent cost) so the report surfaces early-game art
        first, before the per-category display truncation. Sprites and
        construction sprites therefore share the exact same building order.
        """
        data: Any = load_json(self.config_dir / "fiefdom_building_types.json")
        if not isinstance(data, list):
            return
        import_prices_gold, pence_per_shilling, pence_per_gold = \
            load_money_basis(self.config_dir)
        indexed: list[tuple[int, str, dict]] = []
        for entry in data:
            if not isinstance(entry, dict):
                continue
            for building_id, building in entry.items():
                if isinstance(building, dict):
                    indexed.append((len(indexed), building_id, building))
        indexed.sort(
            key=lambda item: building_sort_key(
                item[1], item[2], import_prices_gold, pence_per_shilling,
                pence_per_gold, item[0],
            )
        )
        for _, building_id, building in indexed:
            self.building_ids.append(building_id)
            label: str = str(building.get("display_name") or pretty_label(building_id))

            image: Any = building.get("image")
            if isinstance(image, str) and image:
                self.external_image_building_ids.add(building_id)
                self._add(
                    "manor_sprites", label, "file",
                    served_path_to_disk(image, self.images_dir),
                    f"fiefdom_building_types.json: {building_id}.image",
                )

            construction_image: Any = building.get("construction_image")
            if isinstance(construction_image, str) and construction_image:
                self._add(
                    "manor_construction", f"{label} construction", "file",
                    served_path_to_disk(construction_image, self.images_dir),
                    f"fiefdom_building_types.json: {building_id}.construction_image",
                )

            self._collect_tile_set(building, building_id, label,
                                   "road_tiles", "manor_road_tiles")
            self._collect_tile_set(building, building_id, label,
                                   "race_tiles", "manor_race_tiles")

    def _collect_tile_set(self, building: dict, building_id: str, label: str,
                          field: str, category_id: str) -> None:
        """Collect an auto-tiling tile map (road_tiles / race_tiles)."""
        tiles: Any = building.get(field)
        if not isinstance(tiles, dict):
            return
        for tile_name, tile_path in tiles.items():
            if isinstance(tile_path, str) and tile_path:
                self._add(
                    category_id, f"{label} — {pretty_label(tile_name)}", "file",
                    served_path_to_disk(tile_path, self.images_dir),
                    f"fiefdom_building_types.json: {building_id}.{field}.{tile_name}",
                )

    def _collect_portrait_frames(self) -> None:
        """Decorative portrait-frame overlay images."""
        data: Any = load_json(self.config_dir / "portrait_frames.json")
        if not isinstance(data, dict):
            return
        frames: Any = data.get("frames")
        if not isinstance(frames, dict):
            return
        for variant, frame in frames.items():
            if isinstance(frame, dict):
                image: Any = frame.get("image")
                if isinstance(image, str) and image:
                    self._add(
                        "portrait_frames", f"{pretty_label(variant)} frame", "file",
                        served_path_to_disk(image, self.images_dir),
                        f"portrait_frames.json: frames.{variant}.image",
                    )

    def _collect_campaign_images(self) -> None:
        """Campaign card images from mini_games.json."""
        data: Any = load_json(self.config_dir / "mini_games.json")
        if not isinstance(data, dict):
            return
        for game_id, game in data.items():
            if isinstance(game, dict):
                image: Any = game.get("image")
                if isinstance(image, str) and image:
                    label: str = str(game.get("display_name") or pretty_label(game_id))
                    self._add(
                        "campaign_images", label, "file",
                        served_path_to_disk(image, self.images_dir),
                        f"mini_games.json: {game_id}.image",
                    )

    def _collect_ui_textures(self) -> None:
        """UI texture sets referenced by a filename prefix (e.g. text_vellum_)."""
        data: Any = load_json(self.config_dir / "ui_textures.json")
        if not isinstance(data, dict):
            return
        for component_id, component in data.items():
            if not isinstance(component, dict):
                continue
            prefix: Any = component.get("texture_prefix")
            if isinstance(prefix, str) and prefix:
                self._add(
                    "ui_vellum", f'"{prefix}" textures', "glob",
                    self.images_dir / "ui",
                    f"ui_textures.json: {component_id}.texture_prefix",
                    prefix,
                )

    def _collect_tower_defense(self) -> None:
        """Tower defense mobs, towers, units, projectiles, and map backgrounds."""
        tower_defense_dir: Path = self.config_dir / "tower_defense"
        sections: list[tuple[str, str, str]] = [
            ("td_mobs", "mobs.json", "mobs"),
            ("td_towers", "towers.json", "towers"),
            ("td_units", "units.json", "units"),
            ("td_projectiles", "projectiles.json", "projectiles"),
        ]
        for category_id, filename, container in sections:
            data: Any = load_json(tower_defense_dir / filename)
            if not isinstance(data, dict):
                continue
            entries: Any = data.get(container)
            if not isinstance(entries, dict):
                continue
            for entry_id, entry in entries.items():
                if not isinstance(entry, dict):
                    continue
                label: str = str(entry.get("name") or pretty_label(entry_id))
                image_file: Any = entry.get("image_file")
                if isinstance(image_file, str) and image_file:
                    self._add(
                        category_id, label, "file",
                        self.images_dir / "tower_defense" / container / image_file,
                        f"tower_defense/{filename}: {entry_id}.image_file",
                    )
                area_effect: Any = entry.get("area_effect")
                if isinstance(area_effect, dict):
                    effect_image: Any = area_effect.get("image_file")
                    if isinstance(effect_image, str) and effect_image:
                        effect_id: str = str(area_effect.get("id") or "effect")
                        self._add(
                            category_id, f"{label} — {pretty_label(effect_id)}", "file",
                            self.images_dir / "tower_defense" / container / effect_image,
                            f"tower_defense/{filename}: {entry_id}.area_effect.image_file",
                        )

        for map_file in sorted((tower_defense_dir / "maps").glob("*.json")):
            map_data: Any = load_json(map_file)
            if not isinstance(map_data, dict):
                continue
            image: Any = map_data.get("imageFilename") or map_data.get("image_filename")
            if isinstance(image, str) and image:
                self._add(
                    "td_maps", str(map_data.get("name") or map_file.stem), "file",
                    self.images_dir / "tower_defense" / "maps" / image,
                    f"tower_defense/maps/{map_file.name}: imageFilename",
                )

    def _collect_weeding(self) -> None:
        """Weeding plants (plus damage variants), tools, specials, and maps."""
        weeding_dir: Path = self.config_dir / "weeding"

        plants: Any = load_json(weeding_dir / "plants.json")
        if isinstance(plants, dict):
            for plant_id, plant in plants.items():
                if not isinstance(plant, dict):
                    continue
                label: str = pretty_label(plant_id)
                sprite: Any = plant.get("sprite")
                image_file: Any = sprite.get("image_file") if isinstance(sprite, dict) else None
                if isinstance(image_file, str) and image_file:
                    self._add(
                        "weeding_plants", label, "file",
                        self.images_dir / "weeding" / "plants" / image_file,
                        f"weeding/plants.json: {plant_id}.sprite.image_file",
                    )
                    thresholds: Any = plant.get("damage_sprites")
                    if isinstance(thresholds, list):
                        base, extension = split_extension(image_file)
                        for threshold in thresholds:
                            self._add(
                                "weeding_damage", f"{label} ({threshold} HP)", "file",
                                self.images_dir / "weeding" / "plants"
                                / f"{base}-{threshold}{extension}",
                                f"weeding/plants.json: {plant_id}.damage_sprites",
                            )

        self._collect_sprite_entries(weeding_dir, "tools.json", "tools", "weeding_tools")
        self._collect_sprite_entries(weeding_dir, "specials.json", "specials", "weeding_specials")

        for map_file in sorted((weeding_dir / "maps").glob("*.json")):
            map_data: Any = load_json(map_file)
            if not isinstance(map_data, dict):
                continue
            image: Any = map_data.get("image_file")
            if isinstance(image, str) and image:
                self._add(
                    "weeding_maps", str(map_data.get("id") or map_file.stem), "file",
                    self.images_dir / "weeding" / "maps" / image,
                    f"weeding/maps/{map_file.name}: image_file",
                )

    def _collect_sprite_entries(self, weeding_dir: Path, filename: str,
                                subdir: str, category_id: str) -> None:
        """Collect sprite.image_file entries from a weeding config (tools/specials)."""
        data: Any = load_json(weeding_dir / filename)
        if not isinstance(data, dict):
            return
        for entry_id, entry in data.items():
            if not isinstance(entry, dict):
                continue
            sprite: Any = entry.get("sprite")
            image_file: Any = sprite.get("image_file") if isinstance(sprite, dict) else None
            if isinstance(image_file, str) and image_file:
                self._add(
                    category_id, pretty_label(entry_id), "file",
                    self.images_dir / "weeding" / subdir / image_file,
                    f"weeding/{filename}: {entry_id}.sprite.image_file",
                )

    def _collect_combat_maps(self) -> None:
        """Realtime-combat map background images."""
        for map_file in sorted((self.config_dir / "combat" / "maps").glob("*.json")):
            map_data: Any = load_json(map_file)
            if not isinstance(map_data, dict):
                continue
            image: Any = map_data.get("image_filename") or map_data.get("imageFilename")
            if isinstance(image, str) and image:
                self._add(
                    "combat_maps", str(map_data.get("name") or map_file.stem), "file",
                    self.images_dir / "combat" / "maps" / image,
                    f"combat/maps/{map_file.name}: image_filename",
                )

    def _collect_combatant_animations(self) -> None:
        """Non-empty frame-set directories declared in each combatant's `animations`.

        The frame *count* is discovered from the directory contents at runtime
        (server `authored_combatant_clips`), so this only checks that each authored
        set directory exists and holds at least one image — never how many. A
        view's set is its own direction name or an explicit `{set, mirror}` alias;
        mirrors reuse their source, so only authored sets are listed.
        """
        for filename in ("player_combatants.json", "enemy_combatants.json"):
            data: Any = load_json(self.config_dir / filename)
            if not isinstance(data, dict):
                continue
            for class_id, combatant in data.items():
                if not isinstance(combatant, dict):
                    continue
                animations: Any = combatant.get("animations")
                if not isinstance(animations, dict):
                    continue
                for clip_id, clip in animations.items():
                    if not isinstance(clip, dict):
                        continue
                    for set_name in combatant_clip_sets(clip):
                        self._add(
                            "combatant_animations",
                            f"{pretty_label(class_id)} — {clip_id} ({set_name})",
                            "dir",
                            self.images_dir / "combatants" / class_id / clip_id / set_name,
                            f"{filename}: {class_id}.animations.{clip_id}",
                        )

    def _collect_pools(self) -> None:
        """Data-driven portrait pools the server scans at startup."""
        for gender in ("male", "female"):
            self._add(
                "retinue_headshots", gender.capitalize(), "dir",
                self.images_dir / "retinue" / "headshots" / gender,
                "images/retinue/headshots/",
            )
        for gender in ("male", "female"):
            self._add(
                "safe_avatars", f"Safe avatars ({gender})", "dir",
                self.images_dir / "avatars" / "safe" / gender,
                "images/avatars/safe/",
            )

    def _collect_legacy_entities(self) -> None:
        """Server-scanned entity directories (buildings without `image`, heroes, portraits).

        Combatant directional art is config-driven and collected by
        `_collect_combatant_animations` instead.
        """
        for building_id in dict.fromkeys(self.building_ids):
            if building_id in self.external_image_building_ids:
                continue
            for subtype in ("construction", "idle"):
                self._add(
                    "entity_buildings", f"{pretty_label(building_id)} — {subtype}", "dir",
                    self.images_dir / "buildings" / building_id / subtype,
                    f"images/buildings/{building_id}/{subtype}/",
                )

        heroes: Any = load_json(self.config_dir / "heroes.json")
        if isinstance(heroes, dict):
            for hero_id, hero in heroes.items():
                if not isinstance(hero, dict):
                    continue
                for action in ("idle", "attack"):
                    self._add(
                        "entity_heroes", f"{pretty_label(hero_id)} — {action}", "dir",
                        self.images_dir / "heroes" / hero_id / action,
                        f"images/heroes/{hero_id}/{action}/",
                    )
                skills: Any = hero.get("skills")
                if isinstance(skills, dict):
                    for skill_id in skills.keys():
                        self._add(
                            "entity_hero_skills",
                            f"{pretty_label(hero_id)} — {pretty_label(skill_id)}", "dir",
                            self.images_dir / "heroes" / hero_id / skill_id,
                            f"images/heroes/{hero_id}/{skill_id}/",
                        )

        officials: Any = load_json(self.config_dir / "fiefdom_officials.json")
        if isinstance(officials, dict):
            for official_id, official in officials.items():
                if not isinstance(official, dict):
                    continue
                portrait_id: Any = official.get("portrait_id")
                if isinstance(portrait_id, int):
                    label: str = str(official.get("name") or pretty_label(official_id))
                    self._add(
                        "entity_portraits", f"{label} (portrait {portrait_id})", "dir",
                        self.images_dir / "portraits" / str(portrait_id),
                        f"images/portraits/{portrait_id}/",
                    )


def evaluate(expectation: art_expectation) -> art_check:
    """Check one expectation against the filesystem."""
    path: Path = expectation.path
    if expectation.kind == "file":
        return art_check(expectation, path.is_file(), path.exists(), 0)
    if expectation.kind == "dir":
        count: int = count_image_files(path)
        return art_check(expectation, count > 0, path.is_dir(), count)
    if expectation.kind == "glob":
        count = count_prefixed_images(path, expectation.prefix)
        return art_check(expectation, count > 0, path.is_dir(), count)
    return art_check(expectation, False, False, 0)


def group_checks(checks: list[art_check]) -> dict[str, list[art_check]]:
    """Group checks by category id, preserving input order."""
    grouped: dict[str, list[art_check]] = {}
    for check in checks:
        grouped.setdefault(check.expectation.category_id, []).append(check)
    return grouped


def select_categories(filters: list[str]) -> list[art_category]:
    """Return categories matching any id/title filter (all when no filter given)."""
    if not filters:
        return list(CATEGORIES)
    lowered: list[str] = [value.lower() for value in filters]
    return [
        category for category in CATEGORIES
        if any(needle in category.id.lower() or needle in category.title.lower()
               for needle in lowered)
    ]


def format_path(expectation: art_expectation) -> str:
    """Render an expectation's destination for display."""
    text: str = display_path(expectation.path)
    if expectation.kind in ("dir", "glob"):
        return text + "/"
    return text


def check_note(check: art_check) -> str:
    """A short parenthetical explaining a non-obvious status."""
    expectation: art_expectation = check.expectation
    if check.present:
        if expectation.kind == "dir":
            return f"({check.file_count} files)"
        if expectation.kind == "glob":
            return f"({check.file_count} matches)"
        return ""
    if expectation.kind == "dir":
        return "(directory missing)" if not check.existed else "(directory is empty)"
    if expectation.kind == "glob":
        return f"(add at least one {expectation.prefix}*)"
    return ""


def build_summary_line(checks: list[art_check], categories: list[art_category],
                       show_all: bool = False) -> str:
    """One-line totals for the header and --quiet."""
    grouped: dict[str, list[art_check]] = group_checks(checks)
    total: int = len(checks)
    missing: int = sum(1 for check in checks if not check.present)
    present: int = total - missing
    covered: int = sum(
        1 for category in categories
        if grouped.get(category.id)
        and all(check.present for check in grouped[category.id])
    )
    line: str = f"{missing} missing · {present} present · {total} expected"
    if covered and not show_all:
        noun: str = "category" if covered == 1 else "categories"
        line += f" · {covered} {noun} fully covered (use --all to list)"
    return line


def render_report(checks: list[art_check], categories: list[art_category],
                  config_dir: Path, images_dir: Path, show_all: bool) -> str:
    """Render the human-readable grouped report."""
    grouped: dict[str, list[art_check]] = group_checks(checks)
    lines: list[str] = [
        f"ART CHECK   ({display_path(config_dir)} → {display_path(images_dir)})",
        build_summary_line(checks, categories, show_all),
    ]

    current_group: str | None = None
    for category in categories:
        items: list[art_check] = grouped.get(category.id, [])
        if not items:
            continue
        missing: list[art_check] = [check for check in items if not check.present]
        present: list[art_check] = [check for check in items if check.present]
        if not missing and not show_all:
            continue

        heading: str | None = GROUP_HEADINGS.get(category.group)
        if heading and category.group != current_group:
            lines.append("")
            lines.append(heading)
        current_group = category.group

        lines.append("")
        if show_all:
            lines.append(f"{category.title} — {len(missing)} missing, {len(present)} present")
        else:
            lines.append(f"{category.title} — {len(missing)} missing")

        shown: list[art_check] = missing + present if show_all else missing
        name_width: int = min(
            max((len(check.expectation.name) for check in shown), default=0),
            NAME_COLUMN_WIDTH,
        )
        displayed: list[art_check] = shown
        truncated: int = 0
        if not show_all and len(shown) > MAX_ITEMS_PER_CATEGORY:
            truncated = len(shown) - MAX_ITEMS_PER_CATEGORY
            displayed = shown[:MAX_ITEMS_PER_CATEGORY]

        for check in displayed:
            mark: str = "✓" if check.present else "✗"
            name: str = check.expectation.name
            padding: str = " " * max(1, name_width - len(name) + 2)
            line: str = f"  {mark} {name}{padding}{format_path(check.expectation)}"
            note: str = check_note(check)
            if note:
                line += f"  {note}"
            lines.append(line)

        if truncated:
            lines.append(f"  ...and {truncated} more (use --all to show everything)")

    return "\n".join(lines)


def build_json_report(checks: list[art_check], categories: list[art_category],
                      config_dir: Path, images_dir: Path) -> dict[str, Any]:
    """Build the machine-readable report structure."""
    grouped: dict[str, list[art_check]] = group_checks(checks)
    missing: int = sum(1 for check in checks if not check.present)

    def serialize(check: art_check) -> dict[str, Any]:
        expectation: art_expectation = check.expectation
        category: art_category | None = next(
            (item for item in categories if item.id == expectation.category_id), None
        )
        return {
            "category": expectation.category_id,
            "category_title": category.title if category else expectation.category_id,
            "name": expectation.name,
            "path": display_path(expectation.path),
            "kind": expectation.kind,
            "source": expectation.source,
            "file_count": check.file_count,
        }

    covered: int = sum(
        1 for category in categories
        if grouped.get(category.id) and all(check.present for check in grouped[category.id])
    )
    return {
        "config_dir": display_path(config_dir),
        "images_dir": display_path(images_dir),
        "summary": {
            "expected": len(checks),
            "present": len(checks) - missing,
            "missing": missing,
            "fully_covered_categories": covered,
        },
        "missing": [serialize(check) for check in checks if not check.present],
        "present": [serialize(check) for check in checks if check.present],
    }


def main() -> int:
    """Entry point. Returns the process exit code."""
    args: argparse.Namespace = parse_arguments()

    config_dir: Path = resolve_repo_path(args.config_dir)
    images_dir: Path = resolve_repo_path(args.images_dir)

    if not config_dir.is_dir():
        print(f"error: config directory not found: {display_path(config_dir)}", file=sys.stderr)
        return 2
    if not images_dir.is_dir():
        print(f"error: images directory not found: {display_path(images_dir)}", file=sys.stderr)
        return 2

    selected_categories: list[art_category] = select_categories(args.category)
    if not selected_categories:
        print(f"error: no categories match: {', '.join(args.category)}", file=sys.stderr)
        return 2

    expectations: list[art_expectation] = art_collector(config_dir, images_dir).collect()
    selected_ids: set[str] = {category.id for category in selected_categories}
    checks: list[art_check] = [
        evaluate(expectation) for expectation in expectations
        if expectation.category_id in selected_ids
    ]

    missing: list[art_check] = [check for check in checks if not check.present]

    if args.paths_only:
        # Stable sort by category only, so within a category the collection order
        # is preserved (build order for manor buildings).
        for check in sorted(missing, key=lambda item: item.expectation.category_id):
            print(display_path(check.expectation.path))
    elif args.json:
        print(json.dumps(
            build_json_report(checks, selected_categories, config_dir, images_dir),
            indent=2,
        ))
    elif args.quiet:
        print(build_summary_line(checks, selected_categories))
    else:
        print(render_report(checks, selected_categories, config_dir, images_dir,
                            show_all=args.all))

    return 1 if (missing and not args.no_fail) else 0


if __name__ == "__main__":
    raise SystemExit(main())
