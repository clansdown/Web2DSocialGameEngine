#!/usr/bin/env python3
"""
Portrait Panel Extractor

A command-line tool to detect, isolate, and extract individual portrait panels
from composite images (diffusion sheets, collages, sprite grids). It avoids
hardcoded pixel offsets by leveraging adaptive edge detection, contour
topology, and projection analysis.

Three detectors are tried in order, and the first whose geometry validates wins:

1. Contours — panels enclosed by separate closed frames.
2. Variability bands — full-bleed panels separated by uniform (light) gutters.
3. Sobel seams — thin divider lines between otherwise touching panels.

Supports single-row strips (``--rows 1``, the default) and rectangular grids
(``--rows N`` for 2x2 / 3x3 sheets).

Output files are named by number (``1.png``, ``2.png``, ...), matching the
project's portrait pools (``images/avatars/safe/<gender>/`` and
``images/retinue/headshots/<gender>/``). When the output directory already
contains numerically named files, extraction resumes after the highest number.

Dependencies (installed out-of-band, like the other tools in this directory):
    opencv-python, numpy

Usage:
    python3 tools/extract_portraits.py SHEET.png
    python3 tools/extract_portraits.py SHEET.png -o game/images/avatars/safe/female
    python3 tools/extract_portraits.py SHEET.png -n 4 --debug
    python3 tools/extract_portraits.py SHEET.png --rows 2 -n 3 --strict
    python3 tools/extract_portraits.py sheets/*.webp -o out --debug
"""

from __future__ import annotations

import argparse
import sys
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

try:
    import cv2
    import numpy as np
except ImportError as import_error:  # pragma: no cover - environment guard
    print(
        f"[ERROR] extract_portraits.py requires opencv-python and numpy: {import_error}",
        file=sys.stderr,
    )
    sys.exit(1)


# Formats accepted by cv2.imwrite for the --format flag.
SUPPORTED_OUTPUT_FORMATS: tuple[str, ...] = ("png", "jpg", "webp")


class DetectionError(RuntimeError):
    """Raised when the panel grid cannot be confidently detected in strict mode."""


@dataclass(frozen=True)
class BoundingBox:
    """Represents a 2D bounding rectangle within an image."""

    x: int
    y: int
    width: int
    height: int

    @property
    def aspect_ratio(self) -> float:
        """Calculate the height-to-width ratio."""
        return self.height / float(self.width) if self.width > 0 else 0.0

    @property
    def area(self) -> int:
        """Calculate the pixel area."""
        return self.width * self.height

    @property
    def center_y(self) -> float:
        """Vertical center of the box."""
        return self.y + self.height / 2.0

    def intersection_area(self, other: BoundingBox) -> int:
        """Pixel area shared with another box."""
        left: int = max(self.x, other.x)
        top: int = max(self.y, other.y)
        right: int = min(self.x + self.width, other.x + other.width)
        bottom: int = min(self.y + self.height, other.y + other.height)
        if right <= left or bottom <= top:
            return 0
        return (right - left) * (bottom - top)

    def iou(self, other: BoundingBox) -> float:
        """Intersection-over-union against another box (0.0 when disjoint)."""
        intersection: int = self.intersection_area(other)
        if intersection == 0:
            return 0.0
        union: int = self.area + other.area - intersection
        return intersection / float(union) if union > 0 else 0.0

    def inset(
        self,
        horizontal_ratio: float,
        vertical_ratio: float,
        img_width: int,
        img_height: int,
    ) -> BoundingBox:
        """
        Shrink the box boundaries inward to cleanly trim off outer decorative borders.
        Clamps the coordinates to ensure they remain inside image boundaries.
        """
        pad_x: int = int(self.width * horizontal_ratio)
        pad_y: int = int(self.height * vertical_ratio)

        new_x: int = max(0, self.x + pad_x)
        new_y: int = max(0, self.y + pad_y)
        new_w: int = max(1, min(self.width - (2 * pad_x), img_width - new_x))
        new_h: int = max(1, min(self.height - (2 * pad_y), img_height - new_y))

        return BoundingBox(x=new_x, y=new_y, width=new_w, height=new_h)


@dataclass(frozen=True)
class ValidationReport:
    """Outcome of sanity-checking a detected panel grid."""

    ok: bool
    columns: int
    rows: int
    reasons: list[str]
    width_cv: float
    height_cv: float
    max_overlap: float


class PortraitExtractor:
    """Detects and isolates portrait sub-panels from a composite image."""

    def __init__(
        self,
        expected_columns: int = 4,
        expected_rows: int = 1,
        min_aspect_ratio: float = 1.0,
        max_aspect_ratio: float = 4.5,
        min_area_fraction: float = 0.08,
        border_inset_x: float = 0.035,
        border_inset_y: float = 0.025,
        canny_low: int = 40,
        canny_high: int = 120,
        output_format: str = "png",
        output_size: int | None = None,
        square_crop: bool = False,
        debug: bool = False,
        strict: bool = False,
        allow_fallback: bool = True,
        row_y_tolerance: float = 0.5,
        width_cv_tolerance: float = 0.35,
        height_cv_tolerance: float = 0.35,
        max_overlap_tolerance: float = 0.05,
        border_color_tolerance: float = 60.0,
        border_fraction_threshold: float = 0.30,
        border_trim_cap: float = 0.1,
        border_uniformity_fraction: float = 0.6,
    ) -> None:
        """
        Initialize extraction parameters.

        :param expected_columns: Number of panels per row.
        :param expected_rows: Number of panel rows (1 = single strip).
        :param min_aspect_ratio: Minimum vertical aspect ratio (height / width).
        :param max_aspect_ratio: Maximum vertical aspect ratio (height / width).
        :param min_area_fraction: Minimum box area as a ratio of total image area.
        :param border_inset_x: Horizontal crop margin to discard the border frame.
        :param border_inset_y: Vertical crop margin to discard the border frame.
        :param canny_low: Canny lower hysteresis threshold.
        :param canny_high: Canny upper hysteresis threshold.
        :param output_format: Output image format (png/jpg/webp).
        :param output_size: Optional fixed output edge length in pixels.
        :param square_crop: Center-crop to a square before resizing to output_size.
        :param debug: Write an annotated overlay showing detected boxes.
        :param strict: Raise DetectionError instead of emitting a best-effort crop.
        :param allow_fallback: Enable the vertical-gutter fallback detector.
        :param row_y_tolerance: Row-grouping tolerance as a ratio of box height.
        :param width_cv_tolerance: Max coefficient of variation for panel widths.
        :param height_cv_tolerance: Max coefficient of variation for panel heights.
        :param max_overlap_tolerance: Max tolerated IoU between panels.
        :param border_color_tolerance: Colour distance (0-255) treated as border.
        :param border_fraction_threshold: Row/column border fraction to trim past.
        :param border_trim_cap: Max fraction of a dimension trimmed as border.
        :param border_uniformity_fraction: Min patch fraction within colour tolerance to count as border.
        """
        self.expected_columns: int = expected_columns
        self.expected_rows: int = expected_rows
        self.min_aspect_ratio: float = min_aspect_ratio
        self.max_aspect_ratio: float = max_aspect_ratio
        self.min_area_fraction: float = min_area_fraction
        self.border_inset_x: float = border_inset_x
        self.border_inset_y: float = border_inset_y
        self.canny_low: int = canny_low
        self.canny_high: int = canny_high
        self.output_format: str = output_format
        self.output_size: int | None = output_size
        self.square_crop: bool = square_crop
        self.debug: bool = debug
        self.strict: bool = strict
        self.allow_fallback: bool = allow_fallback
        self.row_y_tolerance: float = row_y_tolerance
        self.width_cv_tolerance: float = width_cv_tolerance
        self.height_cv_tolerance: float = height_cv_tolerance
        self.max_overlap_tolerance: float = max_overlap_tolerance
        self.border_color_tolerance: float = border_color_tolerance
        self.border_fraction_threshold: float = border_fraction_threshold
        self.border_trim_cap: float = border_trim_cap
        self.border_uniformity_fraction: float = border_uniformity_fraction

        # Populated by the most recent detection pass (for reporting).
        self.last_report: ValidationReport | None = None

    def extract_and_save(
        self,
        image_path: Path,
        output_dir: Path,
        prefix: str = "",
        start_index: int = 1,
        debug_label: str = "debug_overlay",
    ) -> list[Path]:
        """
        Load composite image, detect panels, crop inner boundaries, and save outputs.

        :param image_path: Path to the source composite image.
        :param output_dir: Directory where extracted portraits will be written.
        :param prefix: Output filename prefix (numbering resumes per-prefix).
        :param start_index: First number to use for output filenames.
        :param debug_label: Filename stem for the debug overlay (without extension).
        :return: List of paths to the created image files.
        """
        if not image_path.exists():
            raise FileNotFoundError(f"Input file not found: {image_path}")

        composite_bgr: np.ndarray | None = self._read_image(image_path)
        if composite_bgr is None:
            raise ValueError(f"OpenCV failed to decode image: {image_path}")

        image_height, image_width = composite_bgr.shape[:2]
        detected_boxes: list[BoundingBox] = self._detect_panels(
            composite_bgr=composite_bgr,
            img_width=image_width,
            img_height=image_height,
        )

        output_dir.mkdir(parents=True, exist_ok=True)

        if self.debug:
            self._write_debug_overlay(
                composite_bgr=composite_bgr,
                boxes=detected_boxes,
                output_dir=output_dir,
                prefix=prefix,
                debug_label=debug_label,
            )
            self._print_report()

        saved_paths: list[Path] = []

        # Boxes are already final (frame-trimmed for contour/Sobel, exact content
        # for the band detector), so no further inset is applied here.
        for index, final_box in enumerate(detected_boxes):
            panel_crop: np.ndarray = composite_bgr[
                final_box.y : final_box.y + final_box.height,
                final_box.x : final_box.x + final_box.width,
            ]
            panel_crop = self._normalize_panel(panel_crop)

            file_number: int = start_index + index
            target_path: Path = output_dir / f"{prefix}{file_number}.{self.output_format}"
            write_success: bool = cv2.imwrite(str(target_path), panel_crop)

            if not write_success:
                raise OSError(f"Failed to write image to {target_path}")

            saved_paths.append(target_path)
            print(
                f"[INFO] Saved panel {index + 1}/{len(detected_boxes)}: "
                f"{target_path.name} ({final_box.width}x{final_box.height}px)"
            )

        return saved_paths

    def _read_image(self, image_path: Path) -> np.ndarray | None:
        """
        Decode an image to a BGR array.

        cv2.imread applies the EXIF orientation flag by default (the
        IMREAD_IGNORE_ORIENTATION flag exists to opt out), so rotated JPEGs are
        decoded upright without an extra transpose step.
        """
        return cv2.imread(str(image_path))

    def _normalize_panel(self, panel: np.ndarray) -> np.ndarray:
        """
        Apply the optional fixed output size.

        When output_size is set, the panel is resized to a square of that edge
        length. With square_crop enabled it is first center-cropped to a square
        so the resize does not distort the portrait.
        """
        if self.output_size is None:
            return panel

        if self.square_crop:
            height, width = panel.shape[:2]
            side: int = min(height, width)
            top: int = (height - side) // 2
            left: int = (width - side) // 2
            panel = panel[top : top + side, left : left + side]

        interpolation: int = (
            cv2.INTER_AREA if panel.shape[0] > self.output_size else cv2.INTER_CUBIC
        )
        return cv2.resize(
            panel, (self.output_size, self.output_size), interpolation=interpolation
        )

    def _detect_panels(
        self,
        composite_bgr: np.ndarray,
        img_width: int,
        img_height: int,
    ) -> list[BoundingBox]:
        """
        Detection pipeline, in order of preference:

        1. Contours — panels enclosed by separate closed frames.
        2. Variability bands — full-bleed panels separated by uniform gutters.
        3. Sobel seams — thin divider lines.

        The first detector whose geometry validates is used. When none validate,
        a best-effort result is returned (or DetectionError raised with --strict).
        """
        grayscale: np.ndarray = cv2.cvtColor(composite_bgr, cv2.COLOR_BGR2GRAY)
        expected_total: int = self.expected_columns * self.expected_rows

        # --- 1. Contours (framed panels) ---
        contour_candidates: list[BoundingBox] = self._find_boxes_via_contours(
            grayscale=grayscale,
            img_width=img_width,
            img_height=img_height,
        )
        contour_grid: list[BoundingBox] | None = self._assemble_grid(contour_candidates)
        if contour_grid is not None:
            report: ValidationReport = self._validate_grid(
                boxes=contour_grid,
                img_width=img_width,
                img_height=img_height,
            )
            if report.ok:
                self.last_report = report
                return self._apply_contour_inset(
                    contour_grid, grayscale, img_width, img_height
                )
            print(
                f"[WARN] Contour detection failed validation "
                f"({'; '.join(report.reasons)}). Falling back to projection analysis..."
            )
        else:
            print(
                f"[WARN] Contour detection found {len(contour_candidates)} raw "
                f"candidate(s), not a {self.expected_rows}x{self.expected_columns} "
                f"grid ({expected_total} panels). Falling back to projection analysis..."
            )

        if not self.allow_fallback:
            raise DetectionError(
                "Contour detection did not find the expected panel grid and the "
                "projection fallback is disabled (--no-fallback)."
            )

        # --- 2. Variability bands (full-bleed panels + uniform gutters) ---
        band_boxes, band_reasons = self._find_boxes_via_bands(
            composite_bgr=composite_bgr,
            grayscale=grayscale,
            img_width=img_width,
            img_height=img_height,
        )
        band_report: ValidationReport = self._merge_reasons(
            self._validate_grid(band_boxes, img_width, img_height),
            band_reasons,
        )
        if band_report.ok:
            self.last_report = band_report
            # Content bands are already tight to the panel; no inset.
            return band_boxes
        print(
            f"[WARN] Band detection failed validation "
            f"({'; '.join(band_report.reasons)}). Falling back to Sobel seams..."
        )

        # --- 3. Sobel seams (thin divider lines) ---
        gutter_grid, seam_reasons = self._find_boxes_via_gutters(
            grayscale=grayscale,
            img_width=img_width,
            img_height=img_height,
        )
        report = self._merge_reasons(
            self._validate_grid(gutter_grid, img_width, img_height),
            seam_reasons,
        )
        self.last_report = report

        if report.ok:
            return self._apply_inset(gutter_grid, img_width, img_height)

        message: str = (
            f"Projection detection failed validation: {'; '.join(report.reasons)}"
        )
        if self.strict:
            raise DetectionError(message)
        print(f"[WARN] {message}. Emitting best-effort crops (use --strict to fail).")
        return self._apply_inset(gutter_grid, img_width, img_height)

    def _apply_inset(
        self,
        boxes: list[BoundingBox],
        img_width: int,
        img_height: int,
    ) -> list[BoundingBox]:
        """Trim a fixed ratio off every edge (for frame-inclusive boxes)."""
        return [
            box.inset(
                horizontal_ratio=self.border_inset_x,
                vertical_ratio=self.border_inset_y,
                img_width=img_width,
                img_height=img_height,
            )
            for box in boxes
        ]

    def _apply_contour_inset(
        self,
        boxes: list[BoundingBox],
        grayscale: np.ndarray,
        img_width: int,
        img_height: int,
    ) -> list[BoundingBox]:
        """
        Inset a contour box only when its border ring looks like a decorative
        frame; otherwise the box edge is panel content and is kept exactly.
        """
        result: list[BoundingBox] = []
        for box in boxes:
            if self._border_is_frame(grayscale, box):
                result.append(
                    box.inset(
                        horizontal_ratio=self.border_inset_x,
                        vertical_ratio=self.border_inset_y,
                        img_width=img_width,
                        img_height=img_height,
                    )
                )
            else:
                result.append(box)
        return result

    def _border_is_frame(self, grayscale: np.ndarray, box: BoundingBox) -> bool:
        """
        Heuristic: does a contour box have a uniform (frame-like) border ring, or
        is its edge just panel content? A frame's ring is much more uniform than
        the interior it surrounds.
        """
        ring: int = 2
        if box.width <= 4 * ring or box.height <= 4 * ring:
            return False

        region: np.ndarray = grayscale[
            box.y : box.y + box.height, box.x : box.x + box.width
        ]
        border_pixels: np.ndarray = np.concatenate(
            [
                region[:ring, :].ravel(),
                region[-ring:, :].ravel(),
                region[:, :ring].ravel(),
                region[:, -ring:].ravel(),
            ]
        )
        interior: np.ndarray = region[2 * ring : -2 * ring, 2 * ring : -2 * ring]
        if interior.size == 0:
            return False

        border_std: float = float(np.std(border_pixels))
        interior_std: float = float(np.std(interior))
        if interior_std <= 1e-6:
            return False
        return border_std < 0.7 * interior_std

    def _merge_reasons(
        self,
        report: ValidationReport,
        extra_reasons: list[str],
    ) -> ValidationReport:
        """Return a copy of a report with additional failure reasons folded in."""
        if not extra_reasons:
            return report
        return ValidationReport(
            ok=False,
            columns=report.columns,
            rows=report.rows,
            reasons=list(report.reasons) + extra_reasons,
            width_cv=report.width_cv,
            height_cv=report.height_cv,
            max_overlap=report.max_overlap,
        )

    def _find_boxes_via_bands(
        self,
        composite_bgr: np.ndarray,
        grayscale: np.ndarray,
        img_width: int,
        img_height: int,
    ) -> tuple[list[BoundingBox], list[str]]:
        """
        Band detector: find high-variability content blocks (panels) separated by
        low-variability gutters. Unlike the Sobel detector this keys on local
        variance, so wide light gutters (parchment/white) are found by their
        uniformity rather than by a strong edge.

        The outer decorative border is trimmed by colour (it is a distinct
        parchment frame), which follows the torn/wavy edge better than variance.

        Returns exact-content boxes (no inset) plus any failure reasons.
        """
        reasons: list[str] = []

        left_trim, top_trim, right_trim, bottom_trim = self._border_trim(
            composite_bgr=composite_bgr,
            img_width=img_width,
            img_height=img_height,
        )
        column_start: int = left_trim
        column_end: int = img_width - right_trim
        row_start: int = top_trim
        row_end: int = img_height - bottom_trim

        if self.debug:
            print(
                f"[DEBUG] border trims: left={left_trim} top={top_trim} "
                f"right={right_trim} bottom={bottom_trim}"
            )

        column_variability: np.ndarray = self._column_variability(grayscale)
        column_bands, column_reasons = self._find_separator_bands(
            profile=column_variability,
            expected_count=self.expected_columns,
            total=img_width,
            axis_name="vertical",
        )
        column_intervals: list[tuple[int, int]] = self._intervals_between_bands(
            bands=column_bands,
            content_start=column_start,
            content_end=column_end,
        )
        reasons.extend(column_reasons)

        if self.expected_rows > 1:
            row_variability: np.ndarray = self._row_variability(grayscale)
            row_bands, row_reasons = self._find_separator_bands(
                profile=row_variability,
                expected_count=self.expected_rows,
                total=img_height,
                axis_name="horizontal",
            )
            row_intervals: list[tuple[int, int]] = self._intervals_between_bands(
                bands=row_bands,
                content_start=row_start,
                content_end=row_end,
            )
            reasons.extend(row_reasons)
        else:
            row_intervals = [(row_start, row_end)]

        boxes: list[BoundingBox] = []
        for y1, y2 in row_intervals:
            for x1, x2 in column_intervals:
                boxes.append(
                    BoundingBox(x=x1, y=y1, width=x2 - x1, height=y2 - y1)
                )

        if len(boxes) != self.expected_columns * self.expected_rows:
            reasons.append(
                f"expected {self.expected_columns * self.expected_rows} panel(s), "
                f"found {len(boxes)}"
            )

        return boxes, reasons

    def _find_separator_bands(
        self,
        profile: np.ndarray,
        expected_count: int,
        total: int,
        axis_name: str,
    ) -> tuple[list[tuple[int, int]], list[str]]:
        """
        Locate the interior separator bands (gutters/dividers) between panels.

        Anchored to the expected seam positions: near each nominal seam the
        lowest-variability column is found (the divider/gutter), then the band is
        expanded outward only while variability stays near that minimum, so it
        stops at panel content. A large uniform region inside a panel can
        therefore never be mistaken for a separator.

        Returns the bands (start, end) and any confidence warnings.
        """
        reasons: list[str] = []
        if profile.size == 0 or float(np.max(profile)) <= 1e-6:
            return [], [f"no {axis_name} structure detected"]

        smoothed: np.ndarray = self._smooth_profile(profile, max(3, total // 400))
        panel_width: float = total / max(1, expected_count)
        search_radius: int = max(1, int(panel_width * 0.4))

        bands: list[tuple[int, int]] = []
        for seam_index in range(1, expected_count):
            nominal: int = int(seam_index * panel_width)
            window_start: int = max(0, nominal - search_radius)
            window_end: int = min(total, nominal + search_radius)
            if window_end <= window_start:
                continue

            window: np.ndarray = smoothed[window_start:window_end]
            minimum_index: int = window_start + int(np.argmin(window))
            level: float = float(smoothed[minimum_index])
            window_median: float = float(np.median(window))

            if window_median <= 1e-6:
                reasons.append(f"no {axis_name} separator near {minimum_index}")
                bands.append((minimum_index, minimum_index + 1))
                continue
            if level > 0.6 * window_median:
                reasons.append(
                    f"weak {axis_name} separator near {minimum_index} "
                    f"(level={level:.1f}, panel={window_median:.1f})"
                )

            band_threshold: float = level + 0.7 * (window_median - level)
            left: int = minimum_index
            while left > 0 and smoothed[left - 1] <= band_threshold:
                left -= 1
            right: int = minimum_index
            while right < total - 1 and smoothed[right + 1] <= band_threshold:
                right += 1
            bands.append((left, right + 1))

        if self.debug:
            print(
                f"[DEBUG] {axis_name} variability: min={float(np.min(profile)):.1f} "
                f"p50={float(np.percentile(profile, 50)):.1f} "
                f"max={float(np.max(profile)):.1f} bands={bands}"
            )
        return bands, reasons

    def _intervals_between_bands(
        self,
        bands: list[tuple[int, int]],
        content_start: int,
        content_end: int,
    ) -> list[tuple[int, int]]:
        """Content intervals = the regions between separator bands, within the content extent."""
        intervals: list[tuple[int, int]] = []
        cursor: int = content_start
        for band_start, band_end in bands:
            if band_start > cursor:
                intervals.append((cursor, band_start))
            cursor = max(cursor, band_end)
        if cursor < content_end:
            intervals.append((cursor, content_end))
        return intervals

    def _border_trim(
        self,
        composite_bgr: np.ndarray,
        img_width: int,
        img_height: int,
    ) -> tuple[int, int, int, int]:
        """
        Trim the decorative outer border by colour.

        The border colour is sampled per edge (from the middle of that edge),
        since the corners are darker than the edge middles. Each edge is then
        trimmed inward while enough of the row/column still matches its own border
        colour. This follows a torn/wavy parchment frame better than a variance
        threshold and does not cut uniform panel content (e.g. a plain floor).

        Returns (left, top, right, bottom) trim widths.
        """
        edges: (
            tuple[
                tuple[np.ndarray, bool],
                tuple[np.ndarray, bool],
                tuple[np.ndarray, bool],
                tuple[np.ndarray, bool],
            ]
            | None
        ) = self._sample_border_colors(composite_bgr)
        if edges is None:
            return 0, 0, 0, 0

        (top_color, top_uniform), (bottom_color, bottom_uniform), (
            left_color,
            left_uniform,
        ), (right_color, right_uniform) = edges

        left: int = 0
        if left_uniform:
            left = self._edge_trim(
                self._border_mask(composite_bgr, left_color).mean(axis=0), img_width
            )
        right: int = 0
        if right_uniform:
            right = self._edge_trim(
                self._border_mask(composite_bgr, right_color).mean(axis=0)[::-1],
                img_width,
            )
        top: int = 0
        if top_uniform:
            top = self._edge_trim(
                self._border_mask(composite_bgr, top_color).mean(axis=1), img_height
            )
        bottom: int = 0
        if bottom_uniform:
            bottom = self._edge_trim(
                self._border_mask(composite_bgr, bottom_color).mean(axis=1)[::-1],
                img_height,
            )
        return left, top, right, bottom

    def _border_mask(
        self,
        composite_bgr: np.ndarray,
        border_color: np.ndarray,
    ) -> np.ndarray:
        """Boolean mask of pixels within tolerance of the given border colour."""
        difference: np.ndarray = composite_bgr.astype(np.float32) - border_color
        distance: np.ndarray = np.sqrt(np.sum(difference * difference, axis=2))
        return distance < self.border_color_tolerance

    def _edge_trim(self, border_fraction: np.ndarray, total: int) -> int:
        """
        How far an edge stays border, capped at border_trim_cap.

        First finds the border start (skipping any thin dark frame line at the very
        edge), then returns the first index where a short sustained non-border
        (content) run begins. The short run ignores 1-2px texture dips inside the
        border.
        """
        limit: int = min(int(total * self.border_trim_cap), border_fraction.size)
        threshold: float = self.border_fraction_threshold
        run_length: int = 3

        border_begin: int | None = None
        for index in range(limit):
            if border_fraction[index] > threshold:
                border_begin = index
                break
        if border_begin is None:
            return 0

        index = border_begin
        while index < limit:
            run_end: int = min(index + run_length, border_fraction.size)
            if bool(np.all(border_fraction[index:run_end] <= threshold)):
                return index
            index += 1
        return limit

    def _sample_border_colors(
        self,
        composite_bgr: np.ndarray,
    ) -> (
        tuple[
            tuple[np.ndarray, bool],
            tuple[np.ndarray, bool],
            tuple[np.ndarray, bool],
            tuple[np.ndarray, bool],
        ]
        | None
    ):
        """
        Sample a border colour and uniformity flag from the middle of each edge.

        Returns (top, bottom, left, right), each (colour, is_uniform), or None when
        the image is too small. Sampling per edge matters because the corners are
        darker than the edge middles. The uniformity flag guards against trimming
        a sheet that has no border (its edge would be varied content).
        """
        height, width = composite_bgr.shape[:2]
        patch: int = max(3, min(8, min(height, width) // 100))
        margin: int = max(2, patch // 2)
        half: int = patch // 2
        if width < 2 * (margin + patch) or height < 2 * (margin + patch):
            return None

        center_x: int = width // 2
        center_y: int = height // 2

        top_patch: np.ndarray = composite_bgr[
            margin : margin + patch, center_x - half : center_x - half + patch
        ]
        bottom_patch: np.ndarray = composite_bgr[
            height - margin - patch : height - margin,
            center_x - half : center_x - half + patch,
        ]
        left_patch: np.ndarray = composite_bgr[
            center_y - half : center_y - half + patch, margin : margin + patch
        ]
        right_patch: np.ndarray = composite_bgr[
            center_y - half : center_y - half + patch,
            width - margin - patch : width - margin,
        ]

        return (
            self._patch_color_and_uniformity(top_patch),
            self._patch_color_and_uniformity(bottom_patch),
            self._patch_color_and_uniformity(left_patch),
            self._patch_color_and_uniformity(right_patch),
        )

    def _patch_color_and_uniformity(
        self,
        patch: np.ndarray,
    ) -> tuple[np.ndarray, bool]:
        """Median colour of a patch and whether it is uniform enough to be border."""
        pixels: np.ndarray = patch.reshape(-1, 3).astype(np.float32)
        color: np.ndarray = np.median(pixels, axis=0)
        distances: np.ndarray = np.sqrt(np.sum((pixels - color) ** 2, axis=1))
        uniform_fraction: float = float(
            np.mean(distances < self.border_color_tolerance)
        )
        return color, uniform_fraction >= self.border_uniformity_fraction

    def _content_extent(self, profile: np.ndarray, total: int) -> tuple[int, int]:
        """
        Overall content extent, excluding a uniform outer border.

        The threshold is adaptive: the border level is sampled from the profile at
        the image edges and the content level from the profile median; the
        threshold sits halfway between them, so a thin uniform border is trimmed
        while low-variability content (e.g. a plain wall) is kept.
        """
        smoothed: np.ndarray = self._smooth_profile(profile, max(3, total // 400))
        if smoothed.size == 0 or float(np.max(smoothed)) <= 1e-6:
            return 0, total

        edge_width: int = max(2, min(8, total // 200))
        edge_pixels: np.ndarray = np.concatenate(
            [smoothed[:edge_width], smoothed[-edge_width:]]
        )
        edge_level: float = float(np.median(edge_pixels))
        content_level: float = float(np.median(smoothed))
        if content_level <= edge_level:
            threshold: float = content_level * 0.5
        else:
            threshold = edge_level + 0.5 * (content_level - edge_level)

        active: np.ndarray = np.where(smoothed >= threshold)[0]
        if active.size == 0:
            return 0, total
        if self.debug:
            print(
                f"[DEBUG] extent: edge={edge_level:.1f} content={content_level:.1f} "
                f"threshold={threshold:.1f} -> ({int(active[0])}, {int(active[-1]) + 1})"
            )
        return int(active[0]), int(active[-1]) + 1

    def _column_variability(self, grayscale: np.ndarray) -> np.ndarray:
        """Per-column standard deviation (panels vary vertically, gutters do not)."""
        return np.std(grayscale.astype(np.float32), axis=0)

    def _row_variability(self, grayscale: np.ndarray) -> np.ndarray:
        """Per-row standard deviation (content rows vary, border rows do not)."""
        return np.std(grayscale.astype(np.float32), axis=1)

    def _find_boxes_via_contours(
        self,
        grayscale: np.ndarray,
        img_width: int,
        img_height: int,
    ) -> list[BoundingBox]:
        """
        Isolate rectangular frames using edge detection and morphological closing.
        """
        edges: np.ndarray = cv2.Canny(
            grayscale, threshold1=self.canny_low, threshold2=self.canny_high
        )

        # Bridge fine micro-breaks in diffusion line art
        morphology_kernel: np.ndarray = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
        closed_edges: np.ndarray = cv2.morphologyEx(
            edges, cv2.MORPH_CLOSE, morphology_kernel, iterations=2
        )

        contours, _ = cv2.findContours(
            closed_edges, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE
        )

        min_area_pixels: float = (img_width * img_height) * self.min_area_fraction
        candidates: list[BoundingBox] = []

        for contour in contours:
            x, y, w, h = cv2.boundingRect(contour)
            box = BoundingBox(x=x, y=y, width=w, height=h)

            if box.area >= min_area_pixels and (
                self.min_aspect_ratio <= box.aspect_ratio <= self.max_aspect_ratio
            ):
                candidates.append(box)

        return candidates

    def _assemble_grid(
        self,
        candidates: list[BoundingBox],
    ) -> list[BoundingBox] | None:
        """
        Turn raw contour candidates into a row-major grid matching the expected
        shape, or None when the candidate topology does not fit.
        """
        if not candidates:
            return None

        deduped: list[BoundingBox] = self._dedupe_boxes(candidates)
        rows: list[list[BoundingBox]] = self._group_into_rows(deduped)

        if self.expected_rows > 1:
            if len(rows) != self.expected_rows:
                return None
            for row in rows:
                if len(row) != self.expected_columns:
                    return None
            return self._flatten_rows(rows)

        # Single-row strip: use the most populated row band.
        dominant_row: list[BoundingBox] = max(rows, key=len)
        return sorted(dominant_row, key=lambda box: box.x)

    def _dedupe_boxes(
        self,
        boxes: list[BoundingBox],
        iou_threshold: float = 0.5,
    ) -> list[BoundingBox]:
        """Suppress overlapping duplicate boxes, keeping the largest of each cluster."""
        ordered: list[BoundingBox] = sorted(boxes, key=lambda box: box.area, reverse=True)
        kept: list[BoundingBox] = []
        for candidate in ordered:
            if all(candidate.iou(existing) <= iou_threshold for existing in kept):
                kept.append(candidate)
        return kept

    def _group_into_rows(
        self,
        boxes: list[BoundingBox],
    ) -> list[list[BoundingBox]]:
        """Cluster boxes into rows by vertical-center proximity."""
        if not boxes:
            return []

        ordered: list[BoundingBox] = sorted(boxes, key=lambda box: box.center_y)
        rows: list[list[BoundingBox]] = [[ordered[0]]]

        for box in ordered[1:]:
            current_row: list[BoundingBox] = rows[-1]
            row_center: float = sum(b.center_y for b in current_row) / len(current_row)
            row_height: float = sum(b.height for b in current_row) / len(current_row)
            tolerance: float = max(row_height, box.height) * self.row_y_tolerance
            if abs(box.center_y - row_center) <= tolerance:
                current_row.append(box)
            else:
                rows.append([box])

        return rows

    def _flatten_rows(self, rows: list[list[BoundingBox]]) -> list[BoundingBox]:
        """Return row-major order: top-to-bottom, then left-to-right within each row."""
        ordered_rows: list[list[BoundingBox]] = sorted(
            rows, key=lambda row: min(box.y for box in row)
        )
        flattened: list[BoundingBox] = []
        for row in ordered_rows:
            flattened.extend(sorted(row, key=lambda box: box.x))
        return flattened

    def _validate_grid(
        self,
        boxes: list[BoundingBox],
        img_width: int,
        img_height: int,
    ) -> ValidationReport:
        """
        Sanity-check a detected grid: expected count, row layout, non-overlap,
        similar panel sizes, and in-bounds coordinates.
        """
        reasons: list[str] = []
        expected_total: int = self.expected_columns * self.expected_rows

        if len(boxes) != expected_total:
            reasons.append(f"expected {expected_total} panels, found {len(boxes)}")

        grouped: list[list[BoundingBox]] = self._group_into_rows(boxes)
        row_count: int = len(grouped)
        if row_count != self.expected_rows:
            reasons.append(
                f"expected {self.expected_rows} row(s), found {row_count}"
            )
        for row in grouped:
            if len(row) != self.expected_columns:
                reasons.append(
                    f"row has {len(row)} column(s), expected {self.expected_columns}"
                )

        column_count: int = max((len(row) for row in grouped), default=0)

        width_cv: float = 0.0
        height_cv: float = 0.0
        if boxes:
            widths: np.ndarray = np.array([box.width for box in boxes], dtype=float)
            heights: np.ndarray = np.array([box.height for box in boxes], dtype=float)
            mean_width: float = float(np.mean(widths))
            mean_height: float = float(np.mean(heights))
            if mean_width > 0:
                width_cv = float(np.std(widths) / mean_width)
            if mean_height > 0:
                height_cv = float(np.std(heights) / mean_height)
            if width_cv > self.width_cv_tolerance:
                reasons.append(f"panel widths vary too much (cv={width_cv:.2f})")
            if height_cv > self.height_cv_tolerance:
                reasons.append(f"panel heights vary too much (cv={height_cv:.2f})")

        max_overlap: float = 0.0
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                max_overlap = max(max_overlap, boxes[i].iou(boxes[j]))
        if max_overlap > self.max_overlap_tolerance:
            reasons.append(f"panels overlap (max iou={max_overlap:.2f})")

        for box in boxes:
            if (
                box.x < 0
                or box.y < 0
                or box.x + box.width > img_width
                or box.y + box.height > img_height
            ):
                reasons.append("a panel extends outside the image bounds")
                break

        return ValidationReport(
            ok=not reasons,
            columns=column_count,
            rows=row_count,
            reasons=reasons,
            width_cv=width_cv,
            height_cv=height_cv,
            max_overlap=max_overlap,
        )

    def _find_boxes_via_gutters(
        self,
        grayscale: np.ndarray,
        img_width: int,
        img_height: int,
    ) -> tuple[list[BoundingBox], list[str]]:
        """
        Projection fallback: locate panel seams from gradient profiles. For a
        single row, the content band is detected to exclude header/footer strips;
        for grids, seams are found on both axes.

        Returns the detected boxes plus seam-confidence warnings (weak or missing
        dividers) so the caller can fail loudly when the assumed grid is not real.
        """
        sobel_x: np.ndarray = cv2.Sobel(grayscale, cv2.CV_64F, dx=1, dy=0, ksize=3)
        column_profile: np.ndarray = np.mean(np.abs(sobel_x), axis=0)
        column_seams: list[int] = self._detect_seams(
            profile=column_profile,
            divisions=self.expected_columns,
            total=img_width,
        )
        seam_reasons: list[str] = self._seam_reasons(
            profile=column_profile,
            seams=column_seams,
            total=img_width,
            axis_name="vertical",
        )

        if self.expected_rows > 1:
            sobel_y: np.ndarray = cv2.Sobel(grayscale, cv2.CV_64F, dx=0, dy=1, ksize=3)
            row_profile: np.ndarray = np.mean(np.abs(sobel_y), axis=1)
            row_seams: list[int] = self._detect_seams(
                profile=row_profile,
                divisions=self.expected_rows,
                total=img_height,
            )
            seam_reasons.extend(
                self._seam_reasons(
                    profile=row_profile,
                    seams=row_seams,
                    total=img_height,
                    axis_name="horizontal",
                )
            )
        else:
            top, bottom = self._detect_content_band(grayscale, img_height)
            row_seams = [top, bottom]

        boxes: list[BoundingBox] = []
        for row_index in range(self.expected_rows):
            y1: int = row_seams[row_index]
            y2: int = row_seams[row_index + 1]
            for column_index in range(self.expected_columns):
                x1: int = column_seams[column_index]
                x2: int = column_seams[column_index + 1]
                boxes.append(
                    BoundingBox(x=x1, y=y1, width=x2 - x1, height=y2 - y1)
                )

        return boxes, seam_reasons

    def _seam_reasons(
        self,
        profile: np.ndarray,
        seams: list[int],
        total: int,
        axis_name: str,
    ) -> list[str]:
        """
        Flag interior seams whose local gradient peak is not prominent relative to
        its immediate neighbourhood. Comparing to the local window (rather than the
        global maximum) survives strong interior detail elsewhere in the image.
        """
        reasons: list[str] = []
        if profile.size == 0:
            return [f"no {axis_name} gradient signal"]

        peak: float = float(np.max(profile))
        if peak <= 1e-6:
            return [f"no {axis_name} structure detected"]

        smoothed: np.ndarray = self._smooth_profile(profile, max(5, total // 100))
        window: int = max(1, int(total * 0.02))
        for seam in seams[1:-1]:
            start: int = max(0, seam - window)
            end: int = min(total, seam + window)
            if end <= start:
                reasons.append(f"weak {axis_name} divider near x={seam}")
                continue
            local: np.ndarray = smoothed[start:end]
            local_peak: float = float(np.max(local))
            local_median: float = float(np.median(local))
            prominence: float = (local_peak - local_median) / peak
            if prominence < 0.05:
                reasons.append(f"weak {axis_name} divider near x={seam}")
        return reasons

    def _detect_seams(
        self,
        profile: np.ndarray,
        divisions: int,
        total: int,
    ) -> list[int]:
        """
        Find `divisions` segments along a 1D gradient profile by locating local
        activity peaks near each nominal split position. Returns divisions + 1
        boundary offsets (0 and total included).
        """
        smoothed: np.ndarray = self._smooth_profile(
            profile, max(5, total // 100)
        )
        nominal_segment: float = total / divisions
        search_radius: int = int(nominal_segment * 0.25)

        seams: list[int] = [0]
        for i in range(1, divisions):
            nominal_seam: int = int(i * nominal_segment)
            window_start: int = max(0, nominal_seam - search_radius)
            window_end: int = min(total, nominal_seam + search_radius)
            if window_end <= window_start:
                seams.append(nominal_seam)
                continue
            local_peak: int = window_start + int(
                np.argmax(smoothed[window_start:window_end])
            )
            seams.append(local_peak)
        seams.append(total)

        return seams

    def _detect_content_band(
        self,
        grayscale: np.ndarray,
        img_height: int,
    ) -> tuple[int, int]:
        """Vertical extent of panel content (excludes a uniform top/bottom border)."""
        top, bottom = self._content_extent(self._row_variability(grayscale), img_height)
        if self.debug:
            print(f"[DEBUG] content band rows: {top}..{bottom} (height {bottom - top})")
        return top, bottom

    def _smooth_profile(self, profile: np.ndarray, window: int) -> np.ndarray:
        """Smooth a 1D signal with a moving average, safely clamping the window."""
        clamped_window: int = max(1, min(window, profile.size))
        if clamped_window <= 1:
            return profile
        kernel: np.ndarray = np.ones(clamped_window) / clamped_window
        return np.convolve(profile, kernel, mode="same")

    def _write_debug_overlay(
        self,
        composite_bgr: np.ndarray,
        boxes: list[BoundingBox],
        output_dir: Path,
        prefix: str,
        debug_label: str = "debug_overlay",
    ) -> None:
        """Write an annotated copy of the source showing the final crop boxes."""
        overlay: np.ndarray = composite_bgr.copy()
        for index, box in enumerate(boxes):
            cv2.rectangle(
                overlay,
                (box.x, box.y),
                (box.x + box.width, box.y + box.height),
                (0, 255, 0),
                3,
            )
            cv2.putText(
                overlay,
                str(index + 1),
                (box.x + 8, box.y + 34),
                cv2.FONT_HERSHEY_SIMPLEX,
                1.1,
                (0, 255, 255),
                3,
            )

        debug_path: Path = output_dir / f"{prefix}{debug_label}.png"
        cv2.imwrite(str(debug_path), overlay)
        print(f"[INFO] Wrote debug overlay: {debug_path}")

    def _print_report(self) -> None:
        """Print the last validation report when debugging."""
        report: ValidationReport | None = self.last_report
        if report is None:
            return
        print(
            f"[INFO] Validation: ok={report.ok} rows={report.rows} "
            f"cols={report.columns} width_cv={report.width_cv:.2f} "
            f"height_cv={report.height_cv:.2f} max_overlap={report.max_overlap:.2f}"
        )
        for reason in report.reasons:
            print(f"[INFO]   - {reason}")


def find_next_index(
    output_dir: Path,
    prefix: str,
    exclude: Iterable[Path] | None = None,
) -> int:
    """
    Determine the first free output number in a directory.

    A file counts when, after stripping `prefix`, its stem is all digits —
    regardless of extension. Returns the highest found number plus one, or 1
    when none exist. Source images (exclude) are never counted.

    :param output_dir: Directory to scan (may not exist yet).
    :param prefix: Filename prefix used by the output files.
    :param exclude: Optional source image paths to ignore.
    :return: The next integer to use for output filenames.
    """
    highest: int = 0
    if not output_dir.exists():
        return 1

    excluded_paths: set[Path] = set()
    if exclude is not None:
        excluded_paths = {path.resolve() for path in exclude}

    for entry in output_dir.iterdir():
        if not entry.is_file():
            continue
        if entry.resolve() in excluded_paths:
            continue

        name: str = entry.name
        if prefix:
            if not name.startswith(prefix):
                continue
            name = name[len(prefix) :]

        stem: str = Path(name).stem
        if stem.isdigit():
            highest = max(highest, int(stem))

    return highest + 1


def build_argument_parser() -> argparse.ArgumentParser:
    """Configure command-line flags and positional arguments."""
    parser = argparse.ArgumentParser(
        description=(
            "Extract portrait panels from a composite sheet, strip decorative "
            "frames, and save them as numbered images."
        )
    )
    parser.add_argument(
        "image_paths",
        type=Path,
        nargs="+",
        metavar="image",
        help="One or more source composite image files (shell globs work).",
    )
    parser.add_argument(
        "-o",
        "--output-dir",
        type=Path,
        default=Path("."),
        help="Directory to save the extracted panels (default: current directory).",
    )
    parser.add_argument(
        "-n",
        "--panels",
        type=int,
        default=4,
        help="Number of panels per row, i.e. columns (default: 4).",
    )
    parser.add_argument(
        "--rows",
        type=int,
        default=1,
        help="Number of panel rows, e.g. 2 for a 2xN grid (default: 1).",
    )
    parser.add_argument(
        "--prefix",
        default="",
        help="Output filename prefix; numbering resumes per-prefix (default: none).",
    )
    parser.add_argument(
        "--format",
        choices=SUPPORTED_OUTPUT_FORMATS,
        default="png",
        help="Output image format (default: png).",
    )
    parser.add_argument(
        "--size",
        type=int,
        default=None,
        help="Resize each panel to this square edge length (default: keep original).",
    )
    parser.add_argument(
        "--square",
        action="store_true",
        help="Center-crop to a square before resizing (use with --size).",
    )
    parser.add_argument(
        "--inset-x",
        type=float,
        default=0.035,
        help="Horizontal trim ratio to cut inside border frame (default: 0.035).",
    )
    parser.add_argument(
        "--inset-y",
        type=float,
        default=0.025,
        help="Vertical trim ratio to cut inside border frame (default: 0.025).",
    )
    parser.add_argument(
        "--min-aspect",
        type=float,
        default=1.0,
        help="Minimum panel aspect ratio (height/width) (default: 1.0).",
    )
    parser.add_argument(
        "--max-aspect",
        type=float,
        default=4.5,
        help="Maximum panel aspect ratio (height/width) (default: 4.5).",
    )
    parser.add_argument(
        "--min-area",
        type=float,
        default=0.08,
        help="Minimum panel area as a fraction of the image (default: 0.08).",
    )
    parser.add_argument(
        "--canny-low",
        type=int,
        default=40,
        help="Canny lower threshold (default: 40).",
    )
    parser.add_argument(
        "--canny-high",
        type=int,
        default=120,
        help="Canny upper threshold (default: 120).",
    )
    parser.add_argument(
        "--debug",
        action="store_true",
        help="Write an annotated overlay and print the validation report.",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="Fail instead of emitting best-effort crops when validation fails.",
    )
    parser.add_argument(
        "--no-fallback",
        action="store_true",
        help="Disable the projection fallback (contour detection only).",
    )
    return parser


def main() -> None:
    """CLI execution entrypoint."""
    parser: argparse.ArgumentParser = build_argument_parser()
    args: argparse.Namespace = parser.parse_args()

    if args.panels < 1 or args.rows < 1:
        print("[ERROR] --panels and --rows must both be >= 1.", file=sys.stderr)
        sys.exit(1)

    extractor = PortraitExtractor(
        expected_columns=args.panels,
        expected_rows=args.rows,
        min_aspect_ratio=args.min_aspect,
        max_aspect_ratio=args.max_aspect,
        min_area_fraction=args.min_area,
        border_inset_x=args.inset_x,
        border_inset_y=args.inset_y,
        canny_low=args.canny_low,
        canny_high=args.canny_high,
        output_format=args.format,
        output_size=args.size,
        square_crop=args.square,
        debug=args.debug,
        strict=args.strict,
        allow_fallback=not args.no_fallback,
    )

    image_paths: list[Path] = list(args.image_paths)
    next_index: int = find_next_index(
        output_dir=args.output_dir,
        prefix=args.prefix,
        exclude=image_paths,
    )
    print(
        f"[INFO] Writing to '{args.output_dir}' starting at index {next_index}; "
        f"{len(image_paths)} input file(s)."
    )

    failures: list[Path] = []
    for file_number, image_path in enumerate(image_paths, start=1):
        print(f"[INFO] File {file_number}/{len(image_paths)}: {image_path}")
        debug_label: str = f"{image_path.stem}_debug_overlay"
        try:
            saved: list[Path] = extractor.extract_and_save(
                image_path=image_path,
                output_dir=args.output_dir,
                prefix=args.prefix,
                start_index=next_index,
                debug_label=debug_label,
            )
        except (FileNotFoundError, ValueError, DetectionError, OSError) as err:
            print(f"[ERROR] {image_path}: {err}", file=sys.stderr)
            failures.append(image_path)
            # Recover numbering in case the file wrote some panels before failing.
            next_index = find_next_index(
                output_dir=args.output_dir,
                prefix=args.prefix,
                exclude=image_paths,
            )
            continue
        next_index += len(saved)

    if failures:
        print(
            f"[ERROR] {len(failures)} of {len(image_paths)} file(s) failed.",
            file=sys.stderr,
        )
        sys.exit(1)


if __name__ == "__main__":
    main()
