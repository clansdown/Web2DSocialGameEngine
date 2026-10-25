# Minor Content Visibility

This document is the canonical policy for age-relative content.

## Invariant

> A minor viewer receives only server-owned safe content. A minor author may
> submit only server-owned safe content. Adult custom content is never sent to a
> minor, and hidden custom messages produce no notification, unread count,
> preview, mention, or push.

The model is **viewer-relative content projection**, not age-segregated
interaction. Minors and adults may share chats, baronies, and combat. Each
viewer receives a different projection of the same object.

## Content pairs

Every player-authored object has a mandatory safe form and an optional adult
custom form:

| Object | Safe form | Custom form |
|---|---|---|
| Character name | `safe_display_name` | `display_name` |
| Character portrait | `avatar_safe_id` | `avatar_custom_id` |
| Chat message | `safe_message_id` | `body` |
| Message reply | `reply_safe_excerpt` | `reply_excerpt` |
| GIF | catalog `gif_id` (`minor_safe`) | — |
| Reaction | catalog `reaction_id` (`minor_safe`) | — |
| Barony name | `safe_name_id` | `name` |
| Barony description | safe tags | `description` |
| Honor name | `safe_barony_name_id` | `honor_name` |
| Retinue knight name | character safe name | character custom name |
| Combat player/unit name | safe name | — (safe-only wire) |

## Viewer rules

- A minor sees safe names, safe avatars, safe messages, safe barony names/tags,
  and only `minor_safe` reactions/GIFs.
- An adult sees custom content when present, otherwise the safe form.
- Custom avatar storage keys and storage backend names are never sent to
  another player. Only resolved `avatar_url` is returned.
- A minor never receives a custom chat body. Hidden custom messages are omitted
  entirely; they do not become placeholders.
- Unread counts, last-message previews, mentions, bookmarks, replies, and push
  all use the same message-visibility decision.
- Safe messages are server-owned strings and are not translated by the custom
  translation pipeline.

## Authoring rules

- A minor can author only safe catalog IDs (`safe_message_id`, `gif_id`,
  `reaction_id`, safe barony name/tag IDs).
- A minor cannot set a custom display name, upload a custom avatar, join or
  create a user group, or use voice.
- A minor cannot edit a message into custom text.
- Group chats are adult-only.
- A minor-founded barony forces `accepts_minors = 1` and has no custom text.

### Support-ticket exception

Support tickets are a private, staff-only channel and are an explicit exception
to the minor free-text authoring rule. A minor may write free text in a support
ticket and staff may reply in kind for support purposes. Ticket content is
never projected into public chat, barony, combat, or notification surfaces; only
the ticket owner and authorized staff can read it. Notifications generated for
support replies contain the ticket subject and a link, not the free-text body.

## Baronies

- `baronies.safe_name_id` and `safe_tag_ids` are mandatory.
- `baronies.name` stores the custom adult name when present, otherwise the safe
  label; projections choose the viewer-relative value.
- `baronies.accepts_minors` gates minor joins.
- A minor successor forces `accepts_minors = 1` and clears custom text.
- A barony cannot disable `accepts_minors` while a minor is a member.

## Combat and voice

- Player and unit names on the combat wire are safe names.
- Minor combat chat is safe-message IDs only.
- Adult custom combat chat is published only to adult recipients.
- Voice signaling is permitted only between adults. Minors are never offered
  voice and are never signaled.

## Adult status

- `users.adult` is account-level and set by Digital Credentials verification or
  customer service/admin action.
- There is no client-callable age override.
- `adminSetAdultStatus` is admin-only and audited.

## Demotion

When an account is demoted to minor:

1. every character's custom display name and custom avatar are cleared;
2. custom honor names and barony custom text are scrubbed;
3. baronies owned by or containing the account force `accepts_minors = 1`;
4. the account's characters are removed from all group chats;
5. owned groups transfer to the oldest adult member, or archive when empty;
6. every message authored by every character on the account is tombstoned;
7. original message content is copied to the admin-only `message_deletions`
   forensic table;
8. translations, reactions, and reply excerpts for those messages are removed.

Normal readers never query `message_deletions`. The forensic endpoint is
admin-only and audited.

## R2 exception

Custom avatars may live in a public R2 bucket. The server never discloses the
custom key or URL to a minor; an adult who already knows a public capability
link is outside the server disclosure boundary.

## Architecture

- `server/Visibility.{hpp,cpp}` owns viewer context, safe catalogs, identity and
  barony projection, authoring/access policy, and demotion.
- `server/chat/ChatService.cpp` owns message visibility/projection and uses the
  same safe catalogs.
- Handlers must not serialize a player-authored field directly; they call a
  projector or resolver.
