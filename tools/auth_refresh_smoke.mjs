#!/usr/bin/env node
/**
 * Auth refresh/logout smoke test.
 *
 * Verifies the documented slash routes accept the refresh cookie, reject a
 * missing/bogus cookie with invalid_refresh_token, and clear the cookie on
 * logout. The server must never fall through to the generic token_missing path
 * for /api/auth/refresh.
 *
 * Usage: node tools/auth_refresh_smoke.mjs --port 3398
 */
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const port_index = args.indexOf('--port');
const port = port_index >= 0 ? parseInt(args[port_index + 1], 10) : 2290;
const base = `http://127.0.0.1:${port}`;

function safe_words(file) {
  return readFileSync(new URL(`../game/config/${file}`, import.meta.url), 'utf8')
    .split('\n')
    .map((value) => value.trim())
    .filter(Boolean);
}

function fail(message) {
  console.error('AUTH REFRESH SMOKE FAILED:', message);
  process.exit(2);
}

function expect(condition, message) {
  if (!condition) fail(message);
}

async function post_raw(path, body, headers = {}) {
  return await fetch(`${base}/api/${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...headers },
    body: JSON.stringify(body)
  });
}

async function post(path, body, headers = {}) {
  const response = await post_raw(path, body, headers);
  return await response.json();
}

function set_cookie_headers(response) {
  if (typeof response.headers.getSetCookie === 'function') {
    return response.headers.getSetCookie();
  }
  const value = response.headers.get('set-cookie');
  return value ? [value] : [];
}

function refresh_cookie_header(response) {
  return set_cookie_headers(response).find((value) =>
    value.toLowerCase().startsWith('ravenest_refresh=')
  ) ?? '';
}

function refresh_cookie_value(response) {
  const header = refresh_cookie_header(response);
  const match = header.match(/ravenest_refresh=([^;]+)/);
  return match ? match[1] : '';
}

async function refresh_request(cookie) {
  const headers = { 'Content-Type': 'application/json' };
  if (cookie) headers.Cookie = `ravenest_refresh=${cookie}`;
  return await fetch(`${base}/api/auth/refresh`, {
    method: 'POST',
    headers,
    body: '{}'
  });
}

async function refresh_body(cookie) {
  const response = await refresh_request(cookie);
  let body = null;
  try {
    body = await response.json();
  } catch {
    body = null;
  }
  return { response, body };
}

const words1 = safe_words('safe_words_1.txt');
const words2 = safe_words('safe_words_2.txt');
const username = `auth_smoke_${Date.now().toString(36)}`;
const password = 'auth_smoke_pass_123';

const created = await post('createAccount', {
  username,
  password,
  adult: false,
  word1: words1[0],
  word2: words2[0]
});
expect(!created.error, `createAccount: ${JSON.stringify(created)}`);

const login_response = await post_raw('login', {
  auth: { username, password }
});
const login = await login_response.json();
expect(!login.error, `login: ${JSON.stringify(login)}`);
expect(login.data?.token, 'login did not return an access token');

const login_cookie_header = refresh_cookie_header(login_response);
expect(login_cookie_header, 'login did not set ravenest_refresh');
expect(/HttpOnly/i.test(login_cookie_header), 'refresh cookie must be HttpOnly');
expect(/SameSite=Lax/i.test(login_cookie_header), 'refresh cookie must be SameSite=Lax');
expect(/Path=\/api\/auth/i.test(login_cookie_header), 'refresh cookie path must be /api/auth');
const login_cookie = refresh_cookie_value(login_response);
expect(login_cookie, 'login refresh cookie value missing');

const missing = await refresh_body('');
expect(missing.response.status === 401,
  `missing-cookie refresh status=${missing.response.status}, expected 401`);
expect(missing.body?.error === 'invalid_refresh_token',
  `missing-cookie refresh error=${JSON.stringify(missing.body)}, expected invalid_refresh_token`);
expect(missing.body?.error !== 'token_missing',
  'missing-cookie refresh fell through to token_missing');

const bogus = await refresh_body('bogus_refresh_token');
expect(bogus.response.status === 401,
  `bogus-cookie refresh status=${bogus.response.status}, expected 401`);
expect(bogus.body?.error === 'invalid_refresh_token',
  `bogus-cookie refresh error=${JSON.stringify(bogus.body)}, expected invalid_refresh_token`);

const valid = await refresh_body(login_cookie);
expect(valid.response.status === 200,
  `valid refresh status=${valid.response.status}, expected 200`);
expect(valid.body?.data?.token, 'valid refresh did not return a new access token');
const refreshed_cookie_header = refresh_cookie_header(valid.response);
expect(refreshed_cookie_header, 'valid refresh did not re-set ravenest_refresh');
expect(refresh_cookie_value(valid.response) === login_cookie,
  'refresh must reuse the refresh token');

const logout_response = await post_raw('auth/logout', {}, {
  Cookie: `ravenest_refresh=${login_cookie}`
});
const logout = await logout_response.json();
expect(logout_response.status === 200,
  `logout status=${logout_response.status}, expected 200`);
expect(logout?.data?.logged_out === true, `logout body=${JSON.stringify(logout)}`);
const logout_cookie_header = refresh_cookie_header(logout_response);
expect(logout_cookie_header, 'logout did not clear ravenest_refresh');
expect(/Max-Age=0/i.test(logout_cookie_header), 'logout cookie must set Max-Age=0');

const after_logout = await refresh_body(login_cookie);
expect(after_logout.response.status === 401,
  `after-logout refresh status=${after_logout.response.status}, expected 401`);
expect(after_logout.body?.error === 'invalid_refresh_token',
  `after-logout refresh error=${JSON.stringify(after_logout.body)}, expected invalid_refresh_token`);

console.log(`auth_refresh_smoke PASSED (user=${username})`);
process.exit(0);
