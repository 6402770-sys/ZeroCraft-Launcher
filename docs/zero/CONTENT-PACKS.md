# ZERO Content — Resource Packs (Mission 003, Milestone D) — binding spec

Extends `docs/zero/CONTENT.md` (Principles, Filter System, Remote images, Motion, Discover page stay binding) the way
`docs/zero/CONTENT-MODS.md` does for Mods. Written in session 9 by the orchestrator while the owner was away (design not
reviewed by the owner yet: decisions are recorded here so they can be changed). Prism engine facts with file:line:
`.claude/zero-briefs/research-resourcepacks.md`.

## Tasks (order)
1. **D1 Discover › Resource Packs**: section + gallery cards + review/add dialog.
2. **D2 Installed resource packs** page in the instance window (id `"resourcepacks"`) with the game's on/off + order
   (`options.txt`), backups and Undo.
3. (later, with E) update checks for packs (the C5b machinery with an empty loader list).

## Minecraft facts the design relies on
- `<gameRoot>/options.txt`, lines `key:value`. Since 1.13: `resourcePacks:["vanilla","file/Faithful 32x.zip"]` (JSON array)
  and `incompatibleResourcePacks:[...]`. Array order = bottom → top: the LAST entry has the highest priority (top of the
  game's "Selected" list). `vanilla` = the default resources; other non-`file/` ids (`fabric`, `mod_resources`,
  `programmer_art`, `high_contrast`, mod ids) are built-in or mod-provided packs.
- A pack in `resourcepacks/` (a `.zip` or a folder with `pack.mcmeta`) not listed in `resourcePacks` is *available*, not on.
- Prism's "disabled" (`x.zip.disabled`) hides the file from the game; folder packs can't be "disabled". ZERO does not use
  this for packs (on/off = `options.txt`); existing `.zip.disabled` files are shown as hidden files (below).
- Ordering support: Minecraft 1.13+ only (older versions use another format): for older instances the page lists the packs
  with a note "Turn packs on in the game for Minecraft 1.12 and older." and no on/off/order controls.

## D1 Discover › Resource Packs
- Section switcher: **Modpacks · Mods · Resource Packs** (`ZeroDiscoverSection` value `resourcepacks`); palette "Browse
  resource packs"; `MainWindow::showDiscover("resourcepacks", provider)`, `showDiscoverPacks(instanceId, query)`.
- **Target**: the same `TargetInstanceButton` as Mods, but every instance with a Minecraft version qualifies (no loader
  needed); summary "· 1.21.1". Default target rules as Mods (`ZeroDiscoverTarget` is shared). No instance → empty state
  "No instance yet" / "Resource packs need an instance." + **Create instance** (Vanilla).
- Providers: Modrinth (default), CurseForge (when `SupportsFlame`). Facets: `ContentFilter` `Kind::ResourcePack` as built
  (provider, Minecraft version, resolution — Modrinth only, CurseForge disabled with its reason —, category, feature,
  installed in target). Section defaults = the target's MC version as a default chip; "Show everything" removes it.
  Search args: `type=ResourcePack`, `versions={mc}`, `loaders={}` (Modrinth pack versions use loader `minecraft`: never
  send mod loaders), categories from the filter. Sort: Relevance, Downloads, Follows (Modrinth), Updated, Newest.
- **Gallery card** (grid only, 260–340 px wide, columns fill the width): 16:9 artwork on top (featured gallery image, else
  first gallery image, else the icon centred at 64 px on `elevated` with nearest-neighbour scaling for icons ≤ 64 px), radius
  lg; under it padding 12: icon 32 + name (heading, 1 line) + author (caption); footer pills: resolution(s) from
  `resolutionsOf(categories)` ("16x", "32x"; at most 2 + "+1"), downloads; right: **Installed** success pill when the target
  has it (packwiz metadata provider + project id in `resourcepacks/.index`). Provider badge on the artwork corner as
  Modpacks. Skeleton `ZeroSkeleton::Gallery`; reveal = galleries numbers (20 px, 300 ms, 28 ms).
- **Details panel** (shared `ContentDetailsPanel`): artwork hero, name, author, stats, resolution pills, primary **Add to
  <instance>** / **Installed** (disabled) / **Update** (target has an older version), Website; tabs Overview, Gallery,
  Versions (compatible with the target MC; "Show all versions" toggle as Mods), Changelog.
- **Add dialog** (frame of `ModInstallDialog`, no Dependencies block): header, "Adding to <icon> <instance> · 1.21.1",
  Version combo (compatible, newest release preselected), checkbox **Turn it on** (checked; only for 1.13+ targets; disabled
  with "Close <instance> to change its packs" while it runs — the file is still added), Cancel / **Add pack**. Install =
  `ResourceDownloadTask(pack, version, inst->resourcePackList(), indexed, "standalone")` in a `ConcurrentTask` like
  `MainWindow::installMod`, then `update()`; when "Turn it on": the D2 core puts `file/<fileName>` on top of
  `resourcePacks` (same backup + rules as D2). Toast "Added <name> to <instance>" (+ " and turned it on"). D1 ships before
  D2: until D2's core exists the checkbox is hidden (no stub).

## D2 Installed resource packs (instance window, page id `"resourcepacks"`)
Replaces `ResourcePackPage` in `InstancePageProvider::getPages()` (same id, name, icon and `shouldDisplay` rule); Texture
packs / data packs untouched. Source: `inst->resourcePackList()` (watch like the Mods page). Reuse the Mods page frame
(toolbar row + status row, row painting, Remove flow, trash injection, running lock + banner, empty states, Add → Discover)
by extracting shared pieces rather than copying; the Mods page's behaviour and tests must not change.
- **Core** `zero/content/PackOrder` (pure + file I/O helpers, no widgets): parse `options.txt` (keep every other line
  byte-for-byte, the newline style and a missing final newline), read `resourcePacks` / `incompatibleResourcePacks`
  (JSON arrays; tolerate a missing key / missing file), produce the active `file/` list in priority order, and write a new
  order: the non-`file/` entries keep their indexes; the `file/` entries are rewritten in the slots `file/` entries occupied
  (+ new ones appended = top); entries whose file no longer exists are kept untouched (the game drops them itself).
  Missing `options.txt`: create it with the single line `resourcePacks:["vanilla",…]`. Write with `QSaveFile`. Refused
  while the instance runs. Before every write the previous two lines (or "absent") go to `<instance root>/.zero/pack-order.json`
  (history, newest first, cap 20, written with `QSaveFile`); **Undo** = restore the newest history entry.
- **Layout**: toolbar (search, filter **All · On · Off**, caption "12 packs · 3 on", **Add packs** primary → Discover ›
  Resource Packs with this instance as target, `…`: Open resource packs folder, Restore previous order, Open the classic page).
  Body = two groups in one list with overline headers: **ON — top wins** (the active packs, top = highest priority, then a
  fixed last row "Default resources" for `vanilla`, not movable, no toggle) and **AVAILABLE** (the rest, by name). Hidden
  files (`.zip.disabled`) sit at the end of Available with a "Hidden from the game" pill and a row action "Unhide" (base
  `setResourceEnabled` ENABLE, never the modal path).
- **Rows** (64 px): toggle switch (on = in `resourcePacks`), drag handle `grip-vertical` (On group only), `pack.png` 48 px
  (nearest-neighbour when the source is ≤ 64 px, `images` tile when missing), name (file name without `.zip`, heading,
  elided) + description (caption, 1 line, plain text from the JSON component / `§` codes with colours dropped — never the
  raw HTML), pills: provider or "Local file", "Folder", "Made for 1.20" `warning` when the pack format does not fit the
  instance's MC version (from `ResourcePack::mappings()`; nothing when unknown). Row `…`: Turn on / Turn off, Move to top,
  Move up, Move down, Move to bottom (On only), Show in folder, Remove.
- **Order**: drag rows inside On (drop indicator line 2 px `accent`, auto-scroll), Alt+Up / Alt+Down, the row menu. Turning a
  pack on puts it at the top. Every change writes at once (one write per drop) and shows a toast "Moved Faithful to the top" /
  "Turned on Faithful" / "Turned off Faithful" with **Undo**.
- **Remove**: as the Mods page (Recycle Bin only, never `deleteResources`, metadata dropped only for trashed files); a removed
  active pack is also taken out of `resourcePacks` (same write + history).
- **Running**: banner "Close the game to change packs", toggles / drag / Remove / Undo disabled; re-read the running state
  after every dialog.
- **Empty**: "No resource packs yet" / "Find packs for <instance> in Discover." + **Add packs**.
- **Folder packs**: listed like zips; "Folder" pill; on/off/order work (`file/<dir name>`).
- External changes: the game rewrites `options.txt` on exit: re-read it when the page is shown, after the instance stops
  (`instance->runningStatusChanged`) and on the folder watcher's reload.

## Tests
Pure parts offscreen without network: `PackOrder` (parse / write round trips incl. CRLF, no final newline, other keys
untouched, non-`file/` slots kept, missing file, missing key, history cap, Undo, running refusal), description to plain text
(JSON components, arrays, `§` codes), pack-format fit; widgets with fakes as `ZeroModsPage_test` (real
`ResourcePackFolderModel` on a temp folder with small zips + one folder pack); Discover section with fake sources as
`ZeroDiscoverMods_test`. Screenshots both themes at 1280×800 and 1100×700.
