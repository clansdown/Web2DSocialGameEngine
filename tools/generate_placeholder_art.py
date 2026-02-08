#!/usr/bin/env python3
"""
Placeholder Art Generation Tool

Generates placeholder sprite art for game assets using OpenRouter's
image generation API (Nano Banana / Gemini). Reads configuration files,
identifies missing assets, and creates placeholder images with chroma
key transparency for easy replacement.

Usage:
    python3 tools/generate_placeholder_art.py [extra_instructions]

Arguments:
    extra_instructions    Additional LLM instructions (free text, positional)

Options:
    -h, --help            Show this help message
    -y, --yes             Auto-confirm generation (skip confirmation prompt)
    -a, --animations      Generate optional animation frames
    --model MODEL         OpenRouter model (default: google/gemini-2.5-flash-image)
    --config-dir DIR      Config directory (default: server/config)
    --images-dir DIR      Images output directory (default: server/images)
    --instructions-dir DIR  LLM instructions directory (default: server/config/llm_instructions)
    --reference-dir DIR   Reference images directory (default: server/config/reference_images)
    --resolution WxH      Image resolution (default: 512x512)
    --max-frames NUM      Max animation frames (default: 8)
    --max-images NUM      Maximum number of images to generate
    --max-cost NUM        Maximum cost in USD before stopping
    --dry-run             Validate config and show plan without generating
    -q, --quiet           Minimal output
    -v, --verbose         Detailed output

Examples:
    # Basic usage - generate required images only
    python3 tools/generate_placeholder_art.py

    # Generate with animations
    python3 tools/generate_placeholder_art.py --animations

    # Cost-limited generation
    python3 tools/generate_placeholder_art.py --max-cost 0.50

    # With custom instructions
    python3 tools/generate_placeholder_art.py "Make goblins more menacing"

    # Preview what will be generated
    python3 tools/generate_placeholder_art.py --dry-run
"""

from __future__ import annotations

import argparse
import base64
import io
import json
import sys
from dataclasses import dataclass
from enum import Enum
from pathlib import Path
from typing import Any, TypedDict

import requests
import yaml
from PIL import Image
from tqdm import tqdm


class AssetType(Enum):
    """Enumeration of asset types that can have placeholder art generated."""
    COMBATANT = "combatants"
    BUILDING = "buildings"
    HERO = "heroes"
    OFFICIAL = "portraits"


@dataclass
class AssetInfo:
    """Information about an asset that needs placeholder art."""
    asset_type: AssetType
    id: str
    name: str
    visual_description: str | None
    portrait_description: str | None


@dataclass
class LLMInstructions:
    """Container for merged LLM instructions from all YAML files."""
    base: dict[str, Any]
    combatants: dict[str, Any]
    buildings: dict[str, Any]
    heroes: dict[str, Any]
    officials: dict[str, Any]
    animations: dict[str, Any]


@dataclass
class GenerationConfig:
    """Configuration for image generation execution."""
    model: str
    resolution: tuple[int, int]
    max_frames: int
    generate_animations: bool
    max_cost: float | None
    max_images: int | None
    dry_run: bool
    quiet: bool
    extra_instructions: list[str]


def parse_arguments() -> tuple[argparse.Namespace, list[str]]:
    """Parse command line arguments and return namespace plus positional instructions."""
    parser: argparse.ArgumentParser = argparse.ArgumentParser(
        description="Generate placeholder art for game assets using OpenRouter API",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__
    )
    
    parser.add_argument(
        "extra_instructions",
        type=str,
        nargs="*",
        default=[],
        help="Additional LLM instructions to add to all prompts (positional)"
    )
    
    parser.add_argument(
        "-y", "--yes",
        action="store_true",
        help="Auto-confirm generation (skip confirmation prompt)"
    )
    
    parser.add_argument(
        "-a", "--animations",
        action="store_true",
        help="Generate optional animation frames (attack, defend, die, etc.)"
    )
    
    parser.add_argument(
        "--model",
        default="google/gemini-2.5-flash-image",
        help="OpenRouter model to use for image generation"
    )
    
    parser.add_argument(
        "--config-dir",
        type=Path,
        default=Path("server/config"),
        help="Directory containing config files"
    )
    
    parser.add_argument(
        "--images-dir",
        type=Path,
        default=Path("server/images"),
        help="Directory for generated images"
    )
    
    parser.add_argument(
        "--instructions-dir",
        type=Path,
        default=Path("server/config/llm_instructions"),
        help="Directory containing LLM instruction files"
    )
    
    parser.add_argument(
        "--reference-dir",
        type=Path,
        default=Path("server/config/reference_images"),
        help="Directory containing reference images"
    )
    
    parser.add_argument(
        "--resolution",
        default="512x512",
        help="Image resolution as WxH (e.g., 512x512, 1024x1024)"
    )
    
    parser.add_argument(
        "--max-frames",
        type=int,
        default=8,
        help="Maximum number of frames for each animation (default: 8)"
    )
    
    parser.add_argument(
        "--max-images",
        type=int,
        default=None,
        help="Maximum number of images to generate"
    )
    
    parser.add_argument(
        "--max-cost",
        type=float,
        default=None,
        help="Maximum cost in USD before stopping generation"
    )
    
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Validate config and show plan without generating images"
    )
    
    parser.add_argument(
        "-q", "--quiet",
        action="store_true",
        help="Minimal output"
    )
    
    parser.add_argument(
        "-v", "--verbose",
        action="store_true",
        help="Detailed output"
    )
    
    args: argparse.Namespace = parser.parse_args()
    return args, args.extra_instructions


def load_api_key(api_key_path: Path | None = None) -> str:
    """Load OpenRouter API key from .openrouter file in repo root.
    
    Args:
        api_key_path: Optional custom path to API key file
        
    Returns:
        str: The API key string
        
    Raises:
        FileNotFoundError: If API key file is not found
    """
    if api_key_path is None:
        api_key_path = Path.cwd() / ".openrouter"
    
    if not api_key_path.exists():
        raise FileNotFoundError(
            "OpenRouter API key not found. Please create a '.openrouter' file "
            "in the repository root with your API key."
        )
    
    return api_key_path.read_text().strip()


def parse_resolution(resolution_str: str) -> tuple[int, int]:
    """Parse resolution string like '512x512' to tuple (width, height).
    
    Args:
        resolution_str: Resolution in WxH format
        
    Returns:
        tuple[int, int]: (width, height) tuple
        
    Raises:
        ValueError: If format is invalid
    """
    parts: list[str] = resolution_str.lower().split("x")
    if len(parts) != 2:
        raise ValueError(f"Invalid resolution format: {resolution_str}. Use WxH format (e.g., 512x512)")
    
    width: int = int(parts[0].strip())
    height: int = int(parts[1].strip())
    
    if width < 64 or height < 64:
        raise ValueError(f"Resolution too small: {width}x{height}. Minimum is 64x64.")
    
    if width > 4096 or height > 4096:
        raise ValueError(f"Resolution too large: {width}x{height}. Maximum is 4096x4096.")
    
    return width, height


def load_yaml_file(path: Path, default: dict[str, Any]) -> dict[str, Any]:
    """Load a YAML file safely, returning default if file doesn't exist.
    
    Args:
        path: Path to the YAML file
        default: Default dictionary to return if file doesn't exist
        
    Returns:
        dict[str, Any]: Loaded YAML content or default
    """
    if path.exists():
        with open(path, "r", encoding="utf-8") as f:
            content: Any = yaml.safe_load(f)
            return content if content else default
    return default


def load_llm_instructions(instructions_dir: Path) -> LLMInstructions:
    """Load and merge all LLM instruction YAML files.
    
    Args:
        instructions_dir: Directory containing instruction YAML files
        
    Returns:
        LLMInstructions: Merged instructions from all files
    """
    base: dict[str, Any] = load_yaml_file(
        instructions_dir / "base.yaml",
        {}
    )
    combatants: dict[str, Any] = load_yaml_file(
        instructions_dir / "combatants.yaml",
        {}
    )
    buildings: dict[str, Any] = load_yaml_file(
        instructions_dir / "buildings.yaml",
        {}
    )
    heroes: dict[str, Any] = load_yaml_file(
        instructions_dir / "heroes.yaml",
        {}
    )
    officials: dict[str, Any] = load_yaml_file(
        instructions_dir / "officials.yaml",
        {}
    )
    animations: dict[str, Any] = load_yaml_file(
        instructions_dir / "animations.yaml",
        {}
    )
    
    return LLMInstructions(
        base=base,
        combatants=combatants,
        buildings=buildings,
        heroes=heroes,
        officials=officials,
        animations=animations
    )


def scan_for_missing_assets(
    config_dir: Path,
    images_dir: Path
) -> list[AssetInfo]:
    """Scan config files and images directory to identify assets needing images.
    
    Args:
        config_dir: Directory containing JSON config files
        images_dir: Directory where game images are stored
        
    Returns:
        list[AssetInfo]: List of assets that need placeholder art
    """
    assets: list[AssetInfo] = []
    
    # Load combatant configs (both player and enemy)
    player_combatants_file: Path = config_dir / "player_combatants.json"
    enemy_combatants_file: Path = config_dir / "enemy_combatants.json"
    
    all_combatants: dict[str, dict] = {}
    
    if player_combatants_file.exists():
        with open(player_combatants_file, encoding="utf-8") as f:
            player_data: dict[str, dict] = json.load(f)
            all_combatants.update(player_data)
    
    if enemy_combatants_file.exists():
        with open(enemy_combatants_file, encoding="utf-8") as f:
            enemy_data: dict[str, dict] = json.load(f)
            all_combatants.update(enemy_data)
    
    # Check combatant images
    for combatant_id, combatant_data in all_combatants.items():
        # Check if idle directory exists with files
        idle_dir: Path = images_dir / "combatants" / combatant_id / "idle"
        if not idle_dir.exists() or not any(idle_dir.iterdir()):
            assets.append(
                AssetInfo(
                    asset_type=AssetType.COMBATANT,
                    id=combatant_id,
                    name=combatant_data.get("name", combatant_id),
                    visual_description=combatant_data.get("visual_description"),
                    portrait_description=None
                )
            )
    
    # Load buildings config
    buildings_file: Path = config_dir / "fiefdom_building_types.json"
    if buildings_file.exists():
        with open(buildings_file, encoding="utf-8") as f:
            buildings_data: list[dict] = json.load(f)
        
        for building_entry in buildings_data:
            for building_id, building_data in building_entry.items():
                construction_dir: Path = images_dir / "buildings" / building_id / "construction"
                if not construction_dir.exists() or not any(construction_dir.iterdir()):
                    assets.append(
                        AssetInfo(
                            asset_type=AssetType.BUILDING,
                            id=building_id,
                            name=building_id,
                            visual_description=building_data.get("visual_description"),
                            portrait_description=None
                        )
                    )
    
    # Load heroes config
    heroes_file: Path = config_dir / "heroes.json"
    if heroes_file.exists():
        with open(heroes_file, encoding="utf-8") as f:
            heroes_data: dict[str, dict] = json.load(f)
        
        for hero_id, hero_data in heroes_data.items():
            idle_dir: Path = images_dir / "heroes" / hero_id / "idle"
            if not idle_dir.exists() or not any(idle_dir.iterdir()):
                assets.append(
                    AssetInfo(
                        asset_type=AssetType.HERO,
                        id=hero_id,
                        name=hero_data.get("name", hero_id),
                        visual_description=hero_data.get("visual_description"),
                        portrait_description=None
                    )
                )
    
    # Load officials config for portraits
    officials_file: Path = config_dir / "fiefdom_officials.json"
    if officials_file.exists():
        with open(officials_file, encoding="utf-8") as f:
            officials_data: dict[str, dict] = json.load(f)
        
        for official_id, official_data in officials_data.items():
            portrait_id: int = official_data.get("portrait_id", 0)
            portrait_dir: Path = images_dir / "portraits" / str(portrait_id)
            if not portrait_dir.exists() or not any(portrait_dir.iterdir()):
                assets.append(
                    AssetInfo(
                        asset_type=AssetType.OFFICIAL,
                        id=str(portrait_id),
                        name=official_data.get("name", official_id),
                        visual_description=None,
                        portrait_description=official_data.get("portrait_description")
                    )
                )
    
    return assets


def check_reference_images(
    reference_dir: Path,
    assets: list[AssetInfo],
    quiet: bool
) -> None:
    """Check reference images directory and list needed images if missing.
    
    Args:
        reference_dir: Directory containing reference images
        assets: List of assets to check references for
        quiet: If True, suppress informational output
        
    Raises:
        SystemExit: If reference images are missing (exits with helpful message)
    """
    if not reference_dir.exists():
        print(f"\nReference images directory not found: {reference_dir}")
        print("\nPlease create reference images for the following assets:")
        
        for asset in assets:
            if asset.asset_type == AssetType.OFFICIAL:
                continue
            ref_path: Path = reference_dir / f"{asset.id}_ref.png"
            print(f"  - server/config/reference_images/{asset.id}_ref.png")
        
        print("\nReference images provide style guidance for the AI model.")
        print("Once created, run this tool again to generate placeholder art.")
        sys.exit(0)
    
    needed: list[str] = []
    
    for asset in assets:
        if asset.asset_type == AssetType.OFFICIAL:
            continue
        ref_path: Path = reference_dir / f"{asset.id}_ref.png"
        if not ref_path.exists():
            needed.append(f"{asset.id}_ref.png")
    
    if needed:
        print(f"\nReference images needed ({len(needed)} total):")
        for img in sorted(needed):
            print(f"  - server/config/reference_images/{img}")
        print("\nRun this tool again after creating reference images.")
        sys.exit(0)


def calculate_files_to_generate(
    assets: list[AssetInfo],
    instructions: LLMInstructions,
    generate_animations: bool,
    max_frames: int
) -> list[tuple[AssetInfo, str, int | None]]:
    """Calculate the complete list of files to generate.
    
    Args:
        assets: List of assets needing images
        instructions: LLM instructions for animation guidance
        generate_animations: Whether to generate animation frames
        max_frames: Maximum frames per animation
        
    Returns:
        list[tuple[AssetInfo, str, int | None]]: List of (asset, animation_state, frame_num) tuples
    """
    files_to_generate: list[tuple[AssetInfo, str, int | None]] = []
    
    for asset in assets:
        # Determine animation states based on asset type
        if asset.asset_type == AssetType.OFFICIAL:
            # Portraits: single image
            files_to_generate.append((asset, "portrait", None))
            continue
        
        # Default idle animation for all asset types
        files_to_generate.append((asset, "idle", None))
        
        if not generate_animations:
            continue
        
        # Add animation frames based on asset type
        if asset.asset_type == AssetType.COMBATANT:
            for anim in ["attack", "defend", "die"]:
                frames_count: int = min(
                    max_frames,
                    instructions.animations.get(anim, {}).get("recommended_frames", 4)
                )
                for frame_num in range(frames_count):
                    files_to_generate.append((asset, anim, frame_num))
        
        elif asset.asset_type == AssetType.BUILDING:
            for anim in ["construction", "harvest"]:
                frames_count: int = min(
                    max_frames,
                    instructions.animations.get(anim, {}).get("recommended_frames", 4)
                )
                for frame_num in range(frames_count):
                    files_to_generate.append((asset, anim, frame_num))
        
        elif asset.asset_type == AssetType.HERO:
            frames_count: int = min(
                max_frames,
                instructions.animations.get("attack", {}).get("recommended_frames", 4)
            )
            for frame_num in range(frames_count):
                files_to_generate.append((asset, "attack", frame_num))
    
    return files_to_generate


def pretty_print_files_to_generate(
    files: list[tuple[AssetInfo, str, int | None]],
    instructions: LLMInstructions,
    api_key: str,
    max_images: int | None = None
) -> None:
    """Print a formatted list of ALL files to be generated.
    
    Args:
        files: List of (asset, animation_state, frame_number) tuples
        instructions: LLM instructions for cost estimation
        api_key: OpenRouter API key for cost estimation
        max_images: Optional limit on number of files to display
    """
    print("\n" + "=" * 60)
    print("FILES TO BE GENERATED")
    print("=" * 60)
    
    # Apply max_images limit if specified
    files_to_show: list[tuple[AssetInfo, str, int | None]] = (
        files[:max_images] if max_images is not None else files
    )
    
    # Count by type
    type_counts: dict[str, int] = {}
    for asset, anim_state, frame_num in files_to_show:
        type_key: str = f"{asset.asset_type.value}/{anim_state}"
        if frame_num is not None:
            type_key += f" (frame {frame_num})"
        type_counts[type_key] = type_counts.get(type_key, 0) + 1
    
    # Print summary
    print(f"\nTotal files to generate: {len(files_to_show)}")
    
    if max_images is not None and len(files) > max_images:
        print(f"  (Note: {len(files) - max_images} additional files excluded by --max-images limit)")
    
    print("\nBreakdown by type:")
    for type_key, count in sorted(type_counts.items()):
        print(f"  {count:4d}x {type_key}")
    
    # Print ALL files (no truncation)
    print("\nDetailed list:")
    for i, (asset, anim_state, frame_num) in enumerate(files_to_show):
        frame_suffix: str = f"_{frame_num}.png" if frame_num is not None else ".png"
        filename: str = f"{anim_state}{frame_suffix}"
        path_str: str = f"images/{asset.asset_type.value}/{asset.id}/{anim_state}/{filename}"
        print(f"  [{i+1:3d}] {path_str}")
    
    # Estimate cost
    print("\n" + "-" * 60)
    input_cost, output_cost = get_model_pricing(api_key)
    cost_per_image: float = (input_cost + output_cost) / 1_000_000
    est_total_cost: float = cost_per_image * len(files_to_show)
    print(f"Estimated cost: ${est_total_cost:.4f} (${cost_per_image:.4f} per image × {len(files_to_show)} images)")
    
    if max_images is not None and len(files) > max_images:
        full_cost: float = cost_per_image * len(files)
        print(f"  (Full generation without limit would cost: ${full_cost:.4f})")
    
    print("-" * 60 + "\n")


def get_user_confirmation() -> bool:
    """Get user confirmation from stdin.
    
    Returns:
        bool: True if user confirmed, False otherwise
    """
    response: str = input("Do you want to proceed? (y/N): ").strip().lower()
    return response == "y" or response == "yes"


def get_model_pricing(api_key: str) -> tuple[float, float]:
    """Get model pricing from OpenRouter API.
    
    Args:
        api_key: OpenRouter API key
        
    Returns:
        tuple[float, float]: (input_cost_per_1M, output_cost_per_1M)
    """
    url: str = "https://openrouter.ai/api/v1/models"
    headers: dict[str, str] = {
        "Authorization": f"Bearer {api_key}"
    }
    
    try:
        response: requests.Response = requests.get(url, headers=headers, timeout=10)
        response.raise_for_status()
        
        models: list[dict[str, Any]] = response.json().get("data", [])
        for model_data in models:
            model_id: str = model_data.get("id", "")
            if model_id.startswith("google/gemini"):
                pricing: dict[str, Any] = model_data.get("pricing", {})
                input_c: float = float(pricing.get("prompt", "0.001"))
                output_c: float = float(pricing.get("completion", "0.001"))
                return input_c, output_c
        
        # Default fallback pricing for unknown models
        return 0.001, 0.001
        
    except (requests.RequestException, ValueError, KeyError):
        # Fallback to default pricing on error
        return 0.001, 0.001


def build_llm_prompt(
    instructions: LLMInstructions,
    asset: AssetInfo,
    resolution: tuple[int, int],
    extra_instructions: list[str],
    animation_state: str | None = None,
    frame_num: int | None = None
) -> str:
    """Build LLM prompt for image generation.
    
    Args:
        instructions: Merged LLM instructions
        asset: Asset information
        resolution: (width, height) tuple
        extra_instructions: Additional instructions from CLI
        animation_state: Current animation state (if applicable)
        frame_num: Current frame number (if applicable)
        
    Returns:
        str: Complete prompt for the image generation API
    """
    prompt_parts: list[str] = []
    
    # Art direction from base instructions
    general_dir: dict[str, Any] = instructions.base.get("general_art_direction", {})
    prompt_parts.append("### Art Direction")
    prompt_parts.append(f"Style: {general_dir.get('style', 'Clean digital art')}")
    prompt_parts.append(f"Color Palette: {general_dir.get('color_palette', 'medieval colors')}")
    prompt_parts.append(f"Detail Level: {general_dir.get('detail_level', 'moderate detail')}")
    
    # Technical requirements
    tech_req: dict[str, Any] = instructions.base.get("technical_requirements", {})
    prompt_parts.append(f"\n### Technical Requirements")
    prompt_parts.append(f"Resolution: {resolution[0]}x{resolution[1]} pixels")
    
    bg_color: str = tech_req.get("background", {}).get("color", "#FF00FF")
    prompt_parts.append(f"Background: SOLID {bg_color}")
    prompt_parts.append(f"Centering: {tech_req.get('pose_centering', 'centered in frame')}")
    
    # Asset-specific guidance
    type_instructions: dict[str, Any] = {}
    if asset.asset_type == AssetType.COMBATANT:
        type_instructions = instructions.combatants
    elif asset.asset_type == AssetType.BUILDING:
        type_instructions = instructions.buildings
    elif asset.asset_type == AssetType.HERO:
        type_instructions = instructions.heroes
    elif asset.asset_type == AssetType.OFFICIAL:
        type_instructions = instructions.officials
    
    prompt_parts.append("\n### Asset Requirements")
    for priority in type_instructions.get("priority_visuals", []):
        prompt_parts.append(f"- {priority}")
    
    # Animation state guidance
    if animation_state and animation_state != "idle" and animation_state != "portrait":
        anim_guide: dict[str, Any] = type_instructions.get("animations", {})
        if animation_state in anim_guide:
            prompt_parts.append(f"\n### {animation_state.title()} Animation")
            prompt_parts.append(anim_guide[animation_state].get("description", ""))
            if frame_num is not None:
                prompt_parts.append(f"Frame {frame_num + 1} of animation sequence")
    
    # Visual/portrait description
    if asset.visual_description:
        prompt_parts.append(f"\n### Subject Description")
        prompt_parts.append(f"Name: {asset.name}")
        prompt_parts.append(asset.visual_description)
    elif asset.portrait_description:
        prompt_parts.append(f"\n### Subject Description")
        prompt_parts.append(f"Name: {asset.name}")
        prompt_parts.append(asset.portrait_description)
    else:
        prompt_parts.append(f"\n### Subject Description")
        prompt_parts.append(f"Name: {asset.name}")
        prompt_parts.append(f"Create placeholder art for a {asset.name}.")
    
    # Extra instructions from CLI
    if extra_instructions:
        prompt_parts.append("\n### Additional Instructions")
        for instruction in extra_instructions:
            prompt_parts.append(instruction)
    
    return "\n".join(prompt_parts)


def call_openrouter_api(
    api_key: str,
    model: str,
    prompt: str
) -> str:
    """Call OpenRouter API to generate image.
    
    Args:
        api_key: OpenRouter API key
        model: Model identifier
        prompt: LLM prompt for image generation
        
    Returns:
        str: Base64 data URL from the response
        
    Raises:
        ValueError: If no images in response
        requests.RequestException: If API call fails
    """
    url: str = "https://openrouter.ai/api/v1/chat/completions"
    
    payload: dict[str, Any] = {
        "model": model,
        "messages": [
            {
                "role": "user",
                "content": prompt
            }
        ],
        "modalities": ["image", "text"]
    }
    
    headers: dict[str, str] = {
        "Authorization": f"Bearer {api_key}",
        "Content-Type": "application/json"
    }
    
    response: requests.Response = requests.post(url, headers=headers, json=payload, timeout=120)
    response.raise_for_status()
    
    result: dict[str, Any] = response.json()
    message: dict[str, Any] = result.get("choices", [{}])[0].get("message", {})
    images: list[dict[str, Any]] = message.get("images", [])
    
    if not images:
        raise ValueError("No images in API response")
    
    return images[0].get("image_url", {}).get("url", "")


def post_process_image(
    image_data: bytes,
    output_path: Path,
    background_color: str = "#FF00FF"
) -> None:
    """Post-process image: replace background color with transparency.
    
    Args:
        image_data: Raw image bytes (PNG format)
        output_path: Path to save the processed image
        background_color: Hex color to replace with transparency
    """
    img: Image.Image = Image.open(io.BytesIO(image_data))
    
    if img.mode != "RGBA":
        img = img.convert("RGBA")
    
    # Parse hex color to RGB tuple
    bg_rgb: tuple[int, int, int] = (
        int(background_color[1:3], 16),
        int(background_color[3:5], 16),
        int(background_color[5:7], 16)
    )
    
    # Replace matching pixels with transparent alpha
    pixels: Any = img.load()
    width: int = img.width
    height: int = img.height
    
    tolerance: int = 10  # Allow slight color variation
    
    for x in range(width):
        for y in range(height):
            pixel: tuple[int, int, int, int] = pixels[x, y]
            if (abs(pixel[0] - bg_rgb[0]) <= tolerance and
                abs(pixel[1] - bg_rgb[1]) <= tolerance and
                abs(pixel[2] - bg_rgb[2]) <= tolerance):
                pixels[x, y] = (0, 0, 0, 0)
    
    output_path.parent.mkdir(parents=True, exist_ok=True)
    img.save(output_path, "PNG")


def main() -> None:
    """Main entry point for the placeholder art generation tool."""
    # Parse arguments
    args, extra_instructions = parse_arguments()
    
    config_dir: Path = args.config_dir
    images_dir: Path = args.images_dir
    instructions_dir: Path = args.instructions_dir
    
    # Load API key
    try:
        api_key: str = load_api_key()
    except FileNotFoundError as e:
        print(f"Error: {e}")
        sys.exit(1)
    
    # Parse resolution
    try:
        resolution: tuple[int, int] = parse_resolution(args.resolution)
    except ValueError as e:
        print(f"Error: {e}")
        sys.exit(1)
    
    # Load LLM instructions
    if not args.quiet:
        print(f"Loading LLM instructions from {instructions_dir}")
    instructions: LLMInstructions = load_llm_instructions(instructions_dir)
    
    # Identify assets needing images
    if not args.quiet:
        print(f"Scanning for missing assets...")
    assets_needed: list[AssetInfo] = scan_for_missing_assets(config_dir, images_dir)
    
    if not assets_needed:
        print("\nAll images already present. Nothing to generate.")
        sys.exit(0)
    
    if not args.quiet:
        print(f"Found {len(assets_needed)} assets needing images")
    
    # Calculate files to generate
    files_to_generate: list[tuple[AssetInfo, str, int | None]] = calculate_files_to_generate(
        assets_needed,
        instructions,
        args.animations,
        args.max_frames
    )
    
    # Apply max_images limit
    if args.max_images:
        files_to_generate = files_to_generate[:args.max_images]
    
    if not args.quiet:
        print(f"Will generate {len(files_to_generate)} image file(s)")
    
    # Dry run mode
    if args.dry_run:
        pretty_print_files_to_generate(
            files_to_generate,
            instructions,
            api_key,
            max_images=args.max_images
        )
        print("\n=== Dry Run (No images will be generated) ===")
        sys.exit(0)
    
    # Pretty-print and confirm
    if not args.yes:
        pretty_print_files_to_generate(
            files_to_generate,
            instructions,
            api_key,
            max_images=args.max_images
        )
        
        if not get_user_confirmation():
            print("\nGeneration cancelled by user.")
            sys.exit(0)
    
    # Check reference images
    combatants: list[AssetInfo] = [a for a in assets_needed if a.asset_type == AssetType.COMBATANT]
    buildings: list[AssetInfo] = [a for a in assets_needed if a.asset_type == AssetType.BUILDING]
    heroes: list[AssetInfo] = [a for a in assets_needed if a.asset_type == AssetType.HERO]
    
    check_reference_images(args.reference_dir, combatants + buildings + heroes, args.quiet)
    
    # Get model pricing
    input_cost, output_cost = get_model_pricing(api_key)
    cost_per_image: float = (input_cost + output_cost) / 1_000_000
    
    # Generate images with progress bar and enforced limits
    total_cost: float = 0.0
    images_generated: int = 0
    
    spinner_frames: list[str] = ["⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"]
    spinner_idx: int = 0
    
    if not args.quiet:
        print("\n" + "=" * 60)
        print("GENERATING IMAGES")
        print("=" * 60 + "\n")
    
    with tqdm(
        total=len(files_to_generate),
        desc="Generating",
        disable=args.quiet,
        unit="img"
    ) as pbar:
        for i, (asset, anim_state, frame_num) in enumerate(files_to_generate):
            # Check max_images limit
            if args.max_images and images_generated >= args.max_images:
                if not args.quiet:
                    print(f"\nMaximum image limit reached ({args.max_images}). Stopping generation.")
                break
            
            # Check max_cost limit (stop BEFORE exceeding)
            est_next_cost: float = cost_per_image
            if args.max_cost and (total_cost + est_next_cost) > args.max_cost:
                if not args.quiet:
                    print(f"\nMaximum cost limit reached (${args.max_cost:.2f}). Stopping generation.")
                    print(f"Final cost: ${total_cost:.4f}")
                break
            
            # Update progress bar with spinner
            if not args.quiet:
                frame_suffix: str = f"_{frame_num}.png" if frame_num is not None else ".png"
                filename: str = f"{anim_state}{frame_suffix}"
                spinner_char: str = spinner_frames[spinner_idx % len(spinner_frames)]
                spinner_idx += 1
                pbar.set_postfix_str(
                    f"{spinner_char} {asset.asset_type.value}/{asset.id}/{filename} | ${total_cost:.4f}"
                )
            
            try:
                # Build prompt
                prompt: str = build_llm_prompt(
                    instructions,
                    asset,
                    resolution,
                    extra_instructions,
                    anim_state,
                    frame_num
                )
                
                # Generate image
                b64_data: str = call_openrouter_api(api_key, args.model, prompt)
                image_data: bytes = base64.b64decode(b64_data.split(",", 1)[1])
                
                # Determine output path
                frame_suffix: str = f"_{frame_num}.png" if frame_num is not None else ".png"
                filename: str = f"{anim_state}{frame_suffix}"
                output_path: Path = (
                    images_dir / asset.asset_type.value / asset.id / anim_state / filename
                )
                
                # Post-process and save
                bg_color: str = instructions.base.get("technical_requirements", {}).get(
                    "background", {}
                ).get("color", "#FF00FF")
                post_process_image(image_data, output_path, bg_color)
                
                total_cost += est_next_cost
                images_generated += 1
                
                pbar.update(1)
                
            except Exception as e:
                if not args.quiet:
                    print(f"\nError generating {asset.id}/{filename}: {e}")
                continue
    
    # Print summary
    if not args.quiet:
        print("\n" + "=" * 60)
        print("GENERATION COMPLETE")
        print("=" * 60)
        print(f"\nImages generated:   {images_generated}")
        print(f"Total cost incurred: ${total_cost:.4f}")
        
        if images_generated > 0:
            print(f"Avg cost per image: ${total_cost / images_generated:.4f}")
        
        # Check if limits were reached
        if args.max_images and images_generated >= args.max_images:
            print(f"\nNote: Reached maximum image limit ({args.max_images}).")
        
        if args.max_cost and total_cost >= args.max_cost:
            cost_over: float = total_cost - args.max_cost
            print(f"\nNote: Cost limit reached. Exceeded by ${cost_over:.4f} (unavoidable due to discrete generations).")
        
        remaining: int = len(files_to_generate) - images_generated
        if remaining > 0:
            print(f"\n{remaining} image(s) were not generated due to limits.")
            print("Run again with updated limits to complete generation.")


if __name__ == "__main__":
    main()