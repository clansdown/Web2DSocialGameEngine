/**
 * Shared canvas viewport fitting and observation helpers.
 *
 * The mini-games use a fixed internal board resolution and scale the canvas
 * with CSS. This module keeps that aspect-preserving fit consistent and gives
 * both games the same mobile-safe viewport observation.
 */

export interface canvas_fit {
  width: number;
  height: number;
  horizontal_blank: number;
  vertical_blank: number;
}

/**
 * Fits a rectangular content area inside a container while preserving its
 * aspect ratio.
 *
 * @param container_width - Available width in CSS pixels
 * @param container_height - Available height in CSS pixels
 * @param content_aspect - Content width divided by content height
 * @returns The fitted size and the leftover space on each axis
 */
export function fit_canvas_contain(
  container_width: number,
  container_height: number,
  content_aspect: number
): canvas_fit {
  const safe_width = Math.max(0, container_width);
  const safe_height = Math.max(0, container_height);
  const aspect = content_aspect > 0 ? content_aspect : 1;

  let width = safe_width;
  let height = safe_width / aspect;
  if (height > safe_height) {
    height = safe_height;
    width = safe_height * aspect;
  }

  return {
    width,
    height,
    horizontal_blank: Math.max(0, safe_width - width),
    vertical_blank: Math.max(0, safe_height - height)
  };
}

/**
 * True when the device's primary input is touch-like.
 *
 * @returns boolean
 */
export function is_touch_device(): boolean {
  return (
    window.matchMedia('(pointer: coarse)').matches ||
    navigator.maxTouchPoints > 0
  );
}

/**
 * True when the game should offer a rotate-to-landscape control.
 *
 * @param container_width - Current viewport width
 * @param container_height - Current viewport height
 * @returns boolean
 */
export function should_show_rotate_button(
  container_width: number,
  container_height: number
): boolean {
  return is_touch_device() && container_height > container_width;
}

/**
 * Observes viewport/container size changes and invokes the callback after a
 * frame. Uses ResizeObserver plus visualViewport/orientation fallbacks for
 * mobile browsers whose dynamic browser chrome changes the visible area.
 *
 * @param element - Element to observe
 * @param on_change - Callback invoked when the size may have changed
 * @returns Cleanup function
 */
export function observe_viewport_changes(
  element: HTMLElement,
  on_change: () => void
): () => void {
  let frame_id: number | null = null;
  let delayed_id: number | null = null;

  function schedule(): void {
    if (frame_id !== null) cancelAnimationFrame(frame_id);
    frame_id = requestAnimationFrame(() => {
      frame_id = null;
      on_change();
    });
  }

  function schedule_delayed(): void {
    schedule();
    if (delayed_id !== null) window.clearTimeout(delayed_id);
    delayed_id = window.setTimeout(() => {
      delayed_id = null;
      schedule();
    }, 250);
  }

  const observer =
    typeof ResizeObserver !== 'undefined' ? new ResizeObserver(schedule) : null;
  observer?.observe(element);

  window.addEventListener('resize', schedule);
  window.addEventListener('orientationchange', schedule_delayed);
  const visual_viewport = window.visualViewport;
  visual_viewport?.addEventListener('resize', schedule_delayed);
  visual_viewport?.addEventListener('scroll', schedule);

  schedule();

  return () => {
    observer?.disconnect();
    window.removeEventListener('resize', schedule);
    window.removeEventListener('orientationchange', schedule_delayed);
    visual_viewport?.removeEventListener('resize', schedule_delayed);
    visual_viewport?.removeEventListener('scroll', schedule);
    if (frame_id !== null) cancelAnimationFrame(frame_id);
    if (delayed_id !== null) window.clearTimeout(delayed_id);
  };
}
