#!/usr/bin/env node
/**
 * Monetization smoke test — Milestone 11 verification.
 *
 * Exercises the Platinum / cosmetics store against a running server:
 *   createAccount → login → getStore → fail an unaffordable purchase →
 *   purchasePlatinum (pending) → settlePlatinum (grant, exactly once) →
 *   buy a cosmetic → replay keys (idempotency) → equip/deassign →
 *   getCosmetics.
 *
 * All success payloads are wrapped by the API as `{ status, data: {...} }`;
 * error responses carry a top-level `error`.
 *
 * Usage: node tools/monetization_smoke.mjs --port 3390
 * (see tools/monetization_smoke.sh for the server-harness wrapper)
 */
import { readFileSync } from 'node:fs';

const args = process.argv.slice(2);
const portIdx = args.indexOf('--port');
const port = portIdx >= 0 ? parseInt(args[portIdx + 1], 10) : 2290;
const base = `http://127.0.0.1:${port}`;

async function safe_words(file) {
  return readFileSync(new URL(`../game/config/${file}`, import.meta.url), 'utf8')
    .split('\n')
    .map((s) => s.trim())
    .filter(Boolean);
}

async function post(path, body) {
  const r = await fetch(`${base}/api/${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body)
  });
  return r.json();
}

/** Extracts the wrapped success payload (empty object on error). */
function data(response) {
  return response && typeof response.data === 'object' && response.data !== null
    ? response.data
    : {};
}

const out = [];
function log(...parts) {
  out.push(parts.join(' '));
}
function fail(msg) {
  console.error('MONETIZATION SMOKE FAILED:', msg);
  console.error(out.join('\n'));
  process.exit(1);
}

const w1 = await safe_words('safe_words_1.txt');
const w2 = await safe_words('safe_words_2.txt');
const username = `shop_${Date.now().toString(36)}`;
const password = 'shop_pass_123';

const acct = await post('createAccount', { username, password, adult: false, word1: w1[0], word2: w2[0] });
if (!acct.data && !acct.token) fail(`createAccount: ${JSON.stringify(acct)}`);
const login = await post('login', { auth: { username, password } });
const token = login.data?.token ?? login.token;
const character_id = login.data?.characters?.[0]?.id;
if (!token || !character_id) fail(`login: ${JSON.stringify(login)}`);
const auth = { username, token };
log(`account=${username} character_id=${character_id}`);

// 1. Fresh store: no Platinum, catalog present.
const store = data(await post('getStore', { auth, character_id }));
if (store.platinum !== 0) fail(`new account should have 0 platinum, got ${store.platinum}`);
if (!Array.isArray(store.items) || store.items.length === 0) fail('store has no items');
log(`getStore items=${store.items.length} bundles=${store.bundles?.length ?? 0} products=${store.products?.length ?? 0} platinum=${store.platinum}`);

// 2. Unaffordable purchase is rejected and charges nothing.
const poor = await post('purchasePremiumItem', {
  auth, character_id, item_id: 'banner_emberfield', idempotency_key: 'poor-1'
});
if (poor.error !== 'insufficient platinum') fail(`expected insufficient platinum, got ${JSON.stringify(poor)}`);
log('unaffordable purchase correctly rejected');

// 3. Buy Platinum: pending → duplicate → settle (exactly once) → already_settled.
const buyKey = `buy-${Date.now()}`;
const intent = data(await post('purchasePlatinum', { auth, character_id, product_id: 'platinum_pouch', idempotency_key: buyKey }));
if (intent.result !== 'pending') fail(`purchasePlatinum expected pending, got ${JSON.stringify(intent)}`);
const replay = data(await post('purchasePlatinum', { auth, character_id, product_id: 'platinum_pouch', idempotency_key: buyKey }));
if (replay.result !== 'duplicate') fail(`purchasePlatinum replay expected duplicate, got ${JSON.stringify(replay)}`);
const settle = data(await post('settlePlatinum', { auth, character_id, idempotency_key: buyKey }));
if (settle.result !== 'settled' || settle.granted !== 500 || settle.platinum !== 500) {
  fail(`settle expected 500 granted/balance, got ${JSON.stringify(settle)}`);
}
const settleReplay = data(await post('settlePlatinum', { auth, character_id, idempotency_key: buyKey }));
if (settleReplay.result !== 'already_settled') fail(`settle replay expected already_settled, got ${JSON.stringify(settleReplay)}`);
log(`purchasePlatinum pending→duplicate; settle granted=${settle.granted} balance=${settle.platinum}; replay already_settled`);

// 4. Buy a cosmetic: ok → replay duplicate → new key already_owned. One debit only.
const itemKey = `item-${Date.now()}`;
const buy = data(await post('purchasePremiumItem', { auth, character_id, item_id: 'banner_emberfield', idempotency_key: itemKey }));
if (buy.result !== 'ok' || buy.platinum !== 50) fail(`buy expected ok/50, got ${JSON.stringify(buy)}`);
const buyReplay = data(await post('purchasePremiumItem', { auth, character_id, item_id: 'banner_emberfield', idempotency_key: itemKey }));
if (buyReplay.result !== 'duplicate') fail(`buy replay expected duplicate, got ${JSON.stringify(buyReplay)}`);
const buyAgain = data(await post('purchasePremiumItem', { auth, character_id, item_id: 'banner_emberfield', idempotency_key: `item2-${Date.now()}` }));
if (buyAgain.result !== 'already_owned' || buyAgain.platinum !== 50) fail(`re-buy expected already_owned/50, got ${JSON.stringify(buyAgain)}`);
if (!Array.isArray(buyAgain.owned) || !buyAgain.owned.includes('banner_emberfield')) fail('owned set missing purchased cosmetic');
log(`purchase ok (balance 500→${buy.platinum}); replay duplicate; re-buy already_owned (no second debit)`);

// 5. Equip / deassign.
const equip = data(await post('equipCosmetic', { auth, character_id, slot: 'banner', cosmetic_id: 'banner_emberfield' }));
if (equip.loadout?.banner !== 'banner_emberfield') fail(`equip failed: ${JSON.stringify(equip)}`);
const badEquip = await post('equipCosmetic', { auth, character_id, slot: 'knight', cosmetic_id: 'banner_emberfield' });
if (!badEquip.error) fail('equipping a banner into the knight slot should fail');
const deassign = data(await post('deassignCosmetic', { auth, character_id, slot: 'banner' }));
if (deassign.loadout && deassign.loadout.banner) fail(`deassign failed: ${JSON.stringify(deassign)}`);
log('equip → slot checked → deassign ok');

// 6. getCosmetics reflects ownership + loadout + balance.
const state = data(await post('getCosmetics', { auth, character_id }));
if (!Array.isArray(state.owned) || !state.owned.includes('banner_emberfield') || state.platinum !== 50) {
  fail(`getCosmetics mismatch: ${JSON.stringify(state)}`);
}
log(`getCosmetics owned=${state.owned.length} platinum=${state.platinum}`);

// 7. Refund revokes + clears the loadout (M13; rts_design.md §15.4/§15.8).
const re_equip = data(await post('equipCosmetic', { auth, character_id, slot: 'banner', cosmetic_id: 'banner_emberfield' }));
if (re_equip.loadout?.banner !== 'banner_emberfield') fail(`re-equip for refund failed: ${JSON.stringify(re_equip)}`);
const refund = data(await post('refundPremiumItem', { auth, character_id, item_id: 'banner_emberfield' }));
if (refund.result !== 'revoked') fail(`refund expected revoked, got ${JSON.stringify(refund)}`);
if ((refund.owned ?? []).includes('banner_emberfield')) fail('refunded item still owned');
if (refund.loadout && refund.loadout.banner) fail('refund did not clear the loadout slot');
const refund_replay = data(await post('refundPremiumItem', { auth, character_id, item_id: 'banner_emberfield' }));
if (refund_replay.result !== 'not_owned') fail(`refund replay expected not_owned, got ${JSON.stringify(refund_replay)}`);
const after_refund = data(await post('getCosmetics', { auth, character_id }));
if (after_refund.owned.includes('banner_emberfield')) fail('getCosmetics still reports a refunded item');
log('refund revoked + cleared loadout; replay not_owned; no Platinum credited');

console.log(out.join('\n'));
console.log('Monetization smoke PASSED');
process.exit(0);
