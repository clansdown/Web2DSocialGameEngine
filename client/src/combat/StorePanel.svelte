<script lang="ts">
  /**
   * StorePanel — the premium-variety store and loadout editor (rts_design.md
   * §15). Browse cosmetics by slot, buy them with Platinum, equip/deassign
   * owned items, and (mock) top up Platinum. Purchases are server-authoritative
   * and idempotent; this component never asserts ownership.
   */
  import { loadTexts } from '../lib/text';
  import {
    getStoreRequest,
    getCosmeticsRequest,
    purchasePremiumItemRequest,
    purchasePlatinumRequest,
    settlePlatinumRequest,
    equipCosmeticRequest,
    deassignCosmeticRequest,
    handle_combat_error
  } from '../lib/api';
  import type { store_dto, store_item_dto, cosmetics_state_dto } from '../lib/api';

  interface Props {
    character_id: number;
    on_close: () => void;
  }

  let { character_id, on_close }: Props = $props();

  let store = $state<store_dto | null>(null);
  let cosmetic_state = $state<cosmetics_state_dto>({ owned: [], loadout: {}, platinum: 0 });
  let active_slot = $state('all');
  let busy = $state(false);
  let error = $state('');
  let notice = $state('');
  let texts = $state<Record<string, string>>({});

  const SLOTS = ['banner', 'knight', 'squad_uniform', 'unit', 'structure', 'flourish'];

  $effect(() => {
    loadTexts([
      'combat_store_title',
      'combat_store_balance',
      'combat_store_owned',
      'combat_store_buy',
      'combat_store_equip',
      'combat_store_deassign',
      'combat_store_equipped',
      'combat_store_bundles',
      'combat_store_platinum_heading',
      'combat_store_buy_platinum',
      'combat_store_close',
      'combat_store_loading',
      'combat_store_insufficient',
      'combat_store_all',
      'combat_store_slot_banner',
      'combat_store_slot_knight',
      'combat_store_slot_squad_uniform',
      'combat_store_slot_unit',
      'combat_store_slot_structure',
      'combat_store_slot_flourish'
    ]).then((loaded) => {
      texts = loaded;
    });
    void refresh();
  });

  /** Generates a unique idempotency key for one purchase attempt. */
  function new_key(prefix: string): string {
    const rand = globalThis.crypto?.randomUUID?.() ?? `${Date.now()}-${Math.random()}`;
    return `${prefix}-${rand}`;
  }

  /**
   * Loads the catalog and the character's ownership/loadout.
   *
   * @returns Promise<void>
   */
  async function refresh(): Promise<void> {
    try {
      const [catalog, cosmetics] = await Promise.all([
        getStoreRequest(character_id),
        getCosmeticsRequest(character_id)
      ]);
      store = catalog;
      cosmetic_state = cosmetics;
    } catch (e) {
      error = handle_combat_error(e);
    }
  }

  /**
   * Returns whether the character owns an item id.
   *
   * @param itemId - The cosmetic id
   * @returns boolean
   */
  function owns(itemId: string): boolean {
    return cosmetic_state.owned.includes(itemId);
  }

  /**
   * Returns whether an item is currently equipped in its slot.
   *
   * @param item - The store item
   * @returns boolean
   */
  function equipped(item: store_item_dto): boolean {
    return cosmetic_state.loadout[item.slot] === item.id;
  }

  /**
   * Buys a cosmetic item with Platinum (idempotent).
   *
   * @param item - The item to buy
   * @returns Promise<void>
   */
  async function buy_item(item: store_item_dto): Promise<void> {
    if (busy) return;
    busy = true;
    error = '';
    notice = '';
    try {
      const result = await purchasePremiumItemRequest(character_id, {
        item_id: item.id,
        idempotency_key: new_key('item')
      });
      cosmetic_state = { ...cosmetic_state, owned: result.owned, platinum: result.platinum };
      if (result.result === 'insufficient') notice = texts.combat_store_insufficient ?? '';
    } catch (e) {
      error = handle_combat_error(e);
    } finally {
      busy = false;
    }
  }

  /**
   * Buys a bundle with Platinum (idempotent).
   *
   * @param bundleId - The bundle id
   * @returns Promise<void>
   */
  async function buy_bundle(bundleId: string): Promise<void> {
    if (busy) return;
    busy = true;
    error = '';
    try {
      const result = await purchasePremiumItemRequest(character_id, {
        bundle_id: bundleId,
        idempotency_key: new_key('bundle')
      });
      cosmetic_state = { ...cosmetic_state, owned: result.owned, platinum: result.platinum };
    } catch (e) {
      error = handle_combat_error(e);
    } finally {
      busy = false;
    }
  }

  /**
   * Mock-purchases a Platinum product: create the intent, then settle it.
   *
   * @param productId - The product id
   * @returns Promise<void>
   */
  async function buy_platinum(productId: string): Promise<void> {
    if (busy) return;
    busy = true;
    error = '';
    const key = new_key('plat');
    try {
      await purchasePlatinumRequest(character_id, { product_id: productId, idempotency_key: key });
      const settled = await settlePlatinumRequest(character_id, key);
      cosmetic_state = { ...cosmetic_state, platinum: settled.platinum ?? cosmetic_state.platinum };
    } catch (e) {
      error = handle_combat_error(e);
    } finally {
      busy = false;
    }
  }

  /**
   * Equips an owned item into its slot.
   *
   * @param item - The item to equip
   * @returns Promise<void>
   */
  async function equip(item: store_item_dto): Promise<void> {
    if (busy) return;
    busy = true;
    error = '';
    try {
      const result = await equipCosmeticRequest(character_id, item.slot, item.id);
      cosmetic_state = { ...cosmetic_state, loadout: result.loadout };
    } catch (e) {
      error = handle_combat_error(e);
    } finally {
      busy = false;
    }
  }

  /**
   * Clears an occupied loadout slot.
   *
   * @param slot - The slot to clear
   * @returns Promise<void>
   */
  async function deassign(slot: string): Promise<void> {
    if (busy) return;
    busy = true;
    error = '';
    try {
      const result = await deassignCosmeticRequest(character_id, slot);
      cosmetic_state = { ...cosmetic_state, loadout: result.loadout };
    } catch (e) {
      error = handle_combat_error(e);
    } finally {
      busy = false;
    }
  }

  /**
   * Returns the localized slot label.
   *
   * @param slot - The slot id
   * @returns string
   */
  function slot_label(slot: string): string {
    return texts[`combat_store_slot_${slot}`] ?? slot;
  }

  let visible_items = $derived(
    (store?.items ?? []).filter((item) => active_slot === 'all' || item.slot === active_slot)
  );
</script>

<div class="container py-4">
  <div class="d-flex justify-content-between align-items-center mb-3">
    <h2 class="mb-0">{texts.combat_store_title ?? ''}</h2>
    <div class="d-flex align-items-center gap-2">
      <span class="badge text-bg-info fs-6">
        {texts.combat_store_balance ?? ''}: {cosmetic_state.platinum}
      </span>
      <button class="btn btn-outline-secondary" onclick={on_close}>
        {texts.combat_store_close ?? ''}
      </button>
    </div>
  </div>

  {#if error}
    <div class="alert alert-danger py-2">{error}</div>
  {/if}
  {#if notice}
    <div class="alert alert-warning py-2">{notice}</div>
  {/if}

  {#if !store}
    <div class="text-muted">{texts.combat_store_loading ?? ''}</div>
  {:else}
    <div class="btn-group mb-3 flex-wrap" role="group">
      <button
        class="btn btn-sm {active_slot === 'all' ? 'btn-primary' : 'btn-outline-secondary'}"
        onclick={() => (active_slot = 'all')}
      >
        {texts.combat_store_all ?? ''}
      </button>
      {#each SLOTS as slot}
        <button
          class="btn btn-sm {active_slot === slot ? 'btn-primary' : 'btn-outline-secondary'}"
          onclick={() => (active_slot = slot)}
        >
          {slot_label(slot)}
        </button>
      {/each}
    </div>

    <div class="row g-3">
      {#each visible_items as item}
        <div class="col-md-4">
          <div class="card border-secondary h-100">
            <div class="card-body">
              <div class="d-flex justify-content-between">
                <h6 class="card-title mb-1">{item.name}</h6>
                <span class="badge text-bg-dark">{item.tier}</span>
              </div>
              <div class="small text-muted">{slot_label(item.slot)}</div>
              {#if item.description}
                <p class="small text-muted mt-2 mb-2">{item.description}</p>
              {/if}
            </div>
            <div class="card-footer d-flex justify-content-between align-items-center">
              <span class="badge text-bg-info">{item.price_platinum}</span>
              {#if !owns(item.id)}
                <button
                  class="btn btn-sm btn-primary"
                  onclick={() => buy_item(item)}
                  disabled={busy || item.price_platinum > cosmetic_state.platinum}
                >
                  {texts.combat_store_buy ?? ''}
                </button>
              {:else if equipped(item)}
                <button class="btn btn-sm btn-outline-warning" onclick={() => deassign(item.slot)} disabled={busy}>
                  {texts.combat_store_deassign ?? ''}
                </button>
              {:else}
                <button class="btn btn-sm btn-success" onclick={() => equip(item)} disabled={busy}>
                  {texts.combat_store_equip ?? ''}
                </button>
              {/if}
            </div>
          </div>
        </div>
      {/each}
    </div>

    {#if store.bundles.length > 0}
      <h5 class="mt-4">{texts.combat_store_bundles ?? ''}</h5>
      <div class="row g-3">
        {#each store.bundles as bundle}
          <div class="col-md-6">
            <div class="card border-primary h-100">
              <div class="card-body">
                <h6 class="card-title">{bundle.name}</h6>
                {#if bundle.description}<p class="small text-muted">{bundle.description}</p>{/if}
                <div class="d-flex gap-2 flex-wrap">
                  {#each bundle.items as ref}
                    {#if owns(ref)}
                      <span class="badge text-bg-success">{texts.combat_store_owned ?? ''}</span>
                    {/if}
                  {/each}
                </div>
              </div>
              <div class="card-footer d-flex justify-content-between align-items-center">
                <span class="badge text-bg-info">{bundle.price_platinum}</span>
                <button
                  class="btn btn-sm btn-primary"
                  onclick={() => buy_bundle(bundle.id)}
                  disabled={busy || bundle.price_platinum > cosmetic_state.platinum}
                >
                  {texts.combat_store_buy ?? ''}
                </button>
              </div>
            </div>
          </div>
        {/each}
      </div>
    {/if}

    {#if store.products.length > 0}
      <h5 class="mt-4">{texts.combat_store_platinum_heading ?? ''}</h5>
      <div class="row g-3">
        {#each store.products as product}
          <div class="col-md-4">
            <div class="card border-secondary h-100">
              <div class="card-body">
                <h6 class="card-title">{product.name}</h6>
                {#if product.description}<p class="small text-muted">{product.description}</p>{/if}
              </div>
              <div class="card-footer d-flex justify-content-between align-items-center">
                <span class="badge text-bg-secondary">{product.price_real}</span>
                <button class="btn btn-sm btn-outline-info" onclick={() => buy_platinum(product.id)} disabled={busy}>
                  {texts.combat_store_buy_platinum ?? ''}
                </button>
              </div>
            </div>
          </div>
        {/each}
      </div>
    {/if}
  {/if}
</div>
