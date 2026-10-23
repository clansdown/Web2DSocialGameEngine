/* Ravenest chat service worker — Web Push display + click routing.
 *
 * Dedupe is per device: if any client window is focused, the OS notification is
 * suppressed (the open tab will poll the message itself). No server-side
 * activity gate is involved. See the chat design §9.
 */

self.addEventListener('install', (event) => {
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil(self.clients.claim());
});

self.addEventListener('push', (event) => {
  let payload = {};
  try {
    payload = event.data ? event.data.json() : {};
  } catch (error) {
    payload = { title: 'Ravenest', body: event.data ? event.data.text() : '' };
  }

  event.waitUntil(
    self.clients.matchAll({ type: 'window', includeUncontrolled: true }).then((clients) => {
      const focused = clients.some((client) => client.focused);
      if (focused) return undefined;
      return self.registration.showNotification(payload.title || 'Ravenest', {
        body: payload.body || '',
        data: { url: payload.url || '/#/activity/chat' },
        tag: payload.chat_id || undefined
      });
    })
  );
});

self.addEventListener('notificationclick', (event) => {
  event.notification.close();
  const target = (event.notification.data && event.notification.data.url) || '/#/activity/chat';
  event.waitUntil(
    self.clients.matchAll({ type: 'window', includeUncontrolled: true }).then((clients) => {
      for (const client of clients) {
        if ('focus' in client) {
          client.navigate(target);
          return client.focus();
        }
      }
      return self.clients.openWindow(target);
    })
  );
});
