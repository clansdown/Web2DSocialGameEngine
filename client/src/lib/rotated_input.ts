/**
 * Input bridge for a CSS-rotated game canvas.
 *
 * SimpleGame maps pointer coordinates with canvas.getBoundingClientRect() and
 * canvas.clientWidth/clientHeight. That assumes the canvas is not rotated. When
 * the mini-game shell is rotated 90° to provide a forced-landscape view on a
 * portrait device, the physical pointer coordinates need to be mapped back into
 * the canvas's unrotated local coordinate space.
 *
 * This module intercepts pointer events in the capture phase, maps them, and
 * dispatches synthetic mouse events using canvas-local coordinates. The canvas
 * bounding rect is overridden while active so SimpleGame's existing mapping
 * continues to work without modifying client/SimpleGame/.
 */

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

interface synthetic_mouse_event_options {
  bubbles: boolean;
  cancelable: boolean;
  view: Window;
  clientX: number;
  clientY: number;
  button: number;
  buttons: number;
}

/**
 * Installs the rotated-canvas input bridge.
 *
 * The returned cleanup removes all listeners and restores
 * canvas.getBoundingClientRect.
 *
 * @param canvas - Canvas receiving game input
 * @param shell - Rotated shell ancestor
 * @returns Cleanup function
 */
export function install_rotated_input_bridge(
  canvas: HTMLCanvasElement,
  shell: HTMLElement
): () => void {
  let dispatching = false;
  const original_get_bounding_client_rect = canvas.getBoundingClientRect.bind(canvas);

  // Synthetic events use canvas-local coordinates. Make SimpleGame's
  // getBoundingClientRect() + clientWidth/clientHeight mapping see a local rect.
  canvas.getBoundingClientRect = () =>
    new DOMRect(0, 0, canvas.clientWidth, canvas.clientHeight);

  function dispatch_mouse(
    type: string,
    point: { x: number; y: number },
    button: number,
    buttons: number,
    source: MouseEvent | Touch
  ): void {
    const options: synthetic_mouse_event_options = {
      bubbles: true,
      cancelable: true,
      view: window,
      clientX: point.x,
      clientY: point.y,
      button,
      buttons
    };

    // Preserve modifier keys for input handlers that inspect them.
    const mouse_source = source instanceof MouseEvent ? source : null;
    if (mouse_source) {
      Object.assign(options, {
        shiftKey: mouse_source.shiftKey,
        ctrlKey: mouse_source.ctrlKey,
        altKey: mouse_source.altKey,
        metaKey: mouse_source.metaKey
      });
    }

    dispatching = true;
    try {
      canvas.dispatchEvent(new MouseEvent(type, options));
    } finally {
      dispatching = false;
    }
  }

  function point_for_event(
    client_x: number,
    client_y: number
  ): { x: number; y: number } | null {
    return physical_to_canvas_point(client_x, client_y, shell, canvas);
  }

  function on_mouse(event: MouseEvent): void {
    if (dispatching) return;

    event.preventDefault();
    event.stopImmediatePropagation();

    const point = point_for_event(event.clientX, event.clientY);
    if (!point) return;

    dispatch_mouse(event.type, point, event.button, event.buttons, event);
  }

  function on_touch(event: TouchEvent): void {
    if (dispatching) return;

    event.preventDefault();
    event.stopImmediatePropagation();

    const touch = event.changedTouches[0];
    if (!touch) return;

    const point = point_for_event(touch.clientX, touch.clientY);
    if (!point) return;

    if (event.type === 'touchstart') {
      dispatch_mouse('mousedown', point, 0, 1, touch);
    } else if (event.type === 'touchmove') {
      dispatch_mouse('mousemove', point, 0, 1, touch);
    } else {
      dispatch_mouse('mouseup', point, 0, 0, touch);
    }
  }

  const mouse_events: string[] = ['mousedown', 'mousemove', 'mouseup'];
  const touch_events: string[] = ['touchstart', 'touchmove', 'touchend', 'touchcancel'];

  for (const type of mouse_events) {
    canvas.addEventListener(type, on_mouse as EventListener, true);
  }
  for (const type of touch_events) {
    canvas.addEventListener(type, on_touch as EventListener, {
      capture: true,
      passive: false
    });
  }

  return () => {
    for (const type of mouse_events) {
      canvas.removeEventListener(type, on_mouse as EventListener, true);
    }
    for (const type of touch_events) {
      canvas.removeEventListener(type, on_touch as EventListener, {
        capture: true,
        passive: false
      } as AddEventListenerOptions);
    }
    canvas.getBoundingClientRect = original_get_bounding_client_rect;
  };
}
