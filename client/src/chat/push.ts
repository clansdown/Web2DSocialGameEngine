/**
 * Web Push registration. Subscriptions are keyed on the account server-side;
 * the payload names the addressed character. Registration is a no-op when the
 * browser lacks Push support or the server has no VAPID key configured.
 */
import { getPushConfigRequest, subscribePushRequest } from './chat_api';

/**
 * Decodes a base64url VAPID public key into the bytes PushManager expects.
 *
 * @param base64 Base64url-encoded key.
 * @returns Decoded bytes.
 */
function base64url_to_bytes(base64: string): ArrayBuffer {
  const padding = '='.repeat((4 - (base64.length % 4)) % 4);
  const normalized = (base64 + padding).replace(/-/g, '+').replace(/_/g, '/');
  const raw = atob(normalized);
  const buffer = new ArrayBuffer(raw.length);
  const output = new Uint8Array(buffer);
  for (let i = 0; i < raw.length; i++) output[i] = raw.charCodeAt(i);
  return buffer;
}

/**
 * Registers the service worker, requests notification permission, subscribes to
 * push, and reports the subscription to the server.
 *
 * @param character_id The character to associate the subscription with.
 * @returns True when a subscription is active; false when unsupported/unavailable.
 */
export async function register_push(character_id: number): Promise<boolean> {
  if (typeof navigator === 'undefined' || !('serviceWorker' in navigator) || !('PushManager' in window)) {
    return false;
  }
  const config = await getPushConfigRequest(character_id);
  if (!config.enabled || !config.public_key) return false;

  const registration = await navigator.serviceWorker.register('/sw.js');
  const permission = await Notification.requestPermission();
  if (permission !== 'granted') return false;

  const existing = await registration.pushManager.getSubscription();
  const subscription =
    existing ??
    (await registration.pushManager.subscribe({
      userVisibleOnly: true,
      applicationServerKey: base64url_to_bytes(config.public_key)
    }));

  const json = subscription.toJSON();
  await subscribePushRequest(character_id, {
    endpoint: subscription.endpoint,
    p256dh: json.keys?.p256dh ?? '',
    auth: json.keys?.auth ?? ''
  });
  return true;
}
