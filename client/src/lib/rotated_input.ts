/**
 * Pointer mapping for a CSS-rotated game canvas.
 *
 * SimpleGame maps pointer coordinates with canvas.getBoundingClientRect() and
 * canvas.clientWidth/clientHeight by default. That assumes the canvas is not
 * rotated. When the mini-game shell is rotated 90° to provide a forced-landscape
 * view on a portrait device, the physical pointer coordinates need to be mapped
 * back into the canvas's unrotated local coordinate space.
 *
 * This module installs a native SimpleGame input transform while the shell is
 * rotated; no synthetic mouse events or getBoundingClientRect overrides are
 * needed.
 */

import {
  setInputTransform
} from '../../SimpleGame/ui/src/lib/simplegame';
import type {
  InputPoint,
  InputTransform
} from '../../SimpleGame/ui/src/lib/simplegame';

/** The transform currently installed by this module, if any. */
let active_transform: InputTransform | null = null;

/**
 * Returns the layout offset of an element relative to an ancestor. Uses the
 * offsetParent chain, which is unaffected by CSS transforms.
 *
 * @param element - Element whose offset is wanted
 * @param ancestor - Ancestor whose coordinate system is the target
 * @returns The element's top-left offset relative to the ancestor
 */
export function offset_relative_to(
  element: HTMLElement,
  ancestor: HTMLElement
): { x: number; y: number } {
  let x = 0;
  let y = 0;
  let node: HTMLElement | null = element;

  while (node && node !== ancestor) {
    x += node.offsetLeft;
    y += node.offsetTop;
    node = node.offsetParent as HTMLElement | null;
  }

  return { x, y };
}

/**
 * Maps a physical pointer position into the canvas's unrotated local
 * coordinate system for a shell rotated 90° clockwise.
 *
 * The shell's visual bounding-box centre is the fixed point of the rotation.
 * For a +90° CSS rotation, a physical vector (dx, dy) from the centre maps to
 * the shell-local vector (dy, -dx).
 *
 * @param client_x - Physical viewport x coordinate
 * @param client_y - Physical viewport y coordinate
 * @param shell - Rotated shell element
 * @param canvas - Canvas inside the shell
 * @returns Canvas-local point, or null when the point is outside the canvas
 */
export function physical_to_canvas_point(
  client_x: number,
  client_y: number,
  shell: HTMLElement,
  canvas: HTMLCanvasElement
): { x: number; y: number } | null {
  const shell_rect = shell.getBoundingClientRect();
  const centre_x = shell_rect.left + shell_rect.width / 2;
  const centre_y = shell_rect.top + shell_rect.height / 2;

  const dx = client_x - centre_x;
  const dy = client_y - centre_y;

  const shell_x = shell.clientWidth / 2 + dy;
  const shell_y = shell.clientHeight / 2 - dx;

  const canvas_offset = offset_relative_to(canvas, shell);
  const x = shell_x - canvas_offset.x;
  const y = shell_y - canvas_offset.y;

  if (x < 0 || y < 0 || x > canvas.clientWidth || y > canvas.clientHeight) {
    return null;
  }

  return { x, y };
}

/**
 * Installs SimpleGame's native input transform for a rotated canvas.
 *
 * The transform maps physical client coordinates through the shell rotation
 * into canvas-local CSS coordinates. Points outside the canvas map to an
 * off-canvas sentinel so they cannot trigger hits.
 *
 * The returned cleanup removes the transform only if this installation is
 * still the active one, so a stale cleanup cannot clear a newer game's
 * transform.
 *
 * @param canvas - Canvas receiving game input
 * @param shell - Rotated shell ancestor
 * @returns Cleanup function
 */
export function install_rotated_input_transform(
  canvas: HTMLCanvasElement,
  shell: HTMLElement
): () => void {
  const transform: InputTransform = (client_point: InputPoint): InputPoint => {
    const point = physical_to_canvas_point(
      client_point.x,
      client_point.y,
      shell,
      canvas
    );
    return point ?? { x: -1, y: -1 };
  };

  active_transform = transform;
  setInputTransform(transform);

  return () => {
    if (active_transform === transform) {
      setInputTransform(null);
      active_transform = null;
    }
  };
}
