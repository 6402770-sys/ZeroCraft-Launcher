# ZERO Content — Mods (Mission 003, Milestone C) — binding spec

Extends `docs/zero/CONTENT.md` (Principles, Filter System, Remote images, Motion and Discover page stay binding). Written
in session 8 by the orchestrator; CONTENT.md is owner-protected this mission, so the Mods details live here until the owner
folds them in. Prism engine facts with file:line: `.claude/zero-briefs/research-mods.md`.

## Milestone C tasks (order)
1. **Generic section core**: `ModpacksSection` → shared `ContentSection` core + Modpacks presentation; sources parameterised
   by `ModPlatform::ResourceType` (today `Modpack` is hard-coded in `ModpackSources.cpp` 176/285/306). Modpacks behaviour
   and screenshots unchanged.
2. **Target instance** core + control (below).
3. **Discover › Mods** section + the section switcher.
4. **Mod install** review + dependencies.
5. (next session) **Installed mods** manager page in the instance window (id `"mods"`).
6. **Dependency Inspector** on top of `zero/mods/ModHealth` + packwiz metadata (spec below, session 9).

## Target instance (`zero/content/ModTarget`, `ui/zero/content/TargetInstanceButton`)
- `TargetInstance { id, name, iconKey, mcVersion, loaders (ModPlatform::ModLoaderTypes, from
  PackProfile::getSupportedModLoaders()), loaderText ("Fabric", "NeoForge", "Quilt"), running }` read from
  `MinecraftInstance::getPackProfile()` (cheap, GUI-thread safe). Mod-ready = has an MC version and at least one loader
  Modrinth or CurseForge accepts (`ModrinthAPI::validateModLoaders` / `FlameAPI::validateModLoaders`).
- Default target: the Library's selected instance if mod-ready, else `ZeroDiscoverTarget` (instance id) if it still exists
  and is mod-ready, else the most recently played mod-ready instance, else the first mod-ready one by name. A pick is
  saved to `ZeroDiscoverTarget`. Instance list changes (removed, renamed, loader changed) re-validate the target.
- Control (left of the toolbar search, 40 px tall, secondary button look): instance icon 20 px, "For" caption
  (`textSecondary`), instance name (≤ 180 px, elided), `· 1.20.1 Fabric` (`textSecondary`), `chevron-down`. Click → menu:
  mod-ready instances (icon, name, version · loader), the current one checked; then a separator and a disabled caption
  "N instances without a mod loader". Accessible name "Target instance: <name>, <version> <loader>".
- No mod-ready instance: the section body is `ZeroEmptyState` (`blocks`) "No instance can take mods yet" / "Mods need an
  instance with Fabric, Forge, NeoForge or Quilt." + primary **Create instance** (opens Create Instance on Modded).

## Section switcher
- Discover's top bar action: segmented control **Modpacks · Mods** (Resource Packs / Shaders appear only when built —
  no stubs). Remembered in `ZeroDiscoverSection` (read on start; today it is written but never read). Palette entries
  "Browse mods". `MainWindow::showDiscover("mods", provider)` works like the modpacks one.

## Discover › Mods
- Providers: Modrinth (default) and CurseForge (only with `SupportsFlame` and when the target's loaders pass
  `FlameAPI::validateModLoaders`). No Technic.
- Facets (CONTENT.md table): server = Provider, Minecraft version, Loader, Category, Environment (Modrinth: Client / Server;
  CurseForge: disabled "CurseForge has no environment filter"); client = Installed in target, Hide installed.
  **Update available is hidden** until the installed manager provides update data (no stub).
- **Section defaults** = the target's MC version + its loaders, drawn as default chips (CONTENT.md "Chips"); changing the
  target replaces those defaults and starts a new search; "Show everything" removes them (results that may not fit the
  instance; their cards then show "Not for <version> <loader>" in `warning`).
- Search: `ResourceAPI::SearchArgs{type=Mod, loaders, versions={mc}, side, categories}` exactly like
  `ModModel::createSearchArguments`. Sort: Relevance, Downloads, Follows (Modrinth), Updated, Newest, Name (provider sort
  ids as for modpacks).
- **Mod card** (grid, 300–420 px wide, 132 px tall, columns fill the width; no artwork): `surface` card, radius lg,
  padding 16. Left: icon 48 px (radius md; placeholder tile `elevated` + `blocks` 24 px). Right column: name (heading,
  1 line, elided) + author (caption, `textSecondary`); description (body-sm, 2 lines). Footer row (pills 20 px): loaders
  ("Fabric", "Quilt"), environment ("Client", "Server", "Client & server" — only when the provider sent it), downloads
  (`download` icon + `12.3M`), then right-aligned the state: **Installed** (success pill) when the target has this project
  (packwiz metadata provider + project id), else nothing. Hover/selection as Modpacks cards. List mode: 56 px rows (icon 32,
  name + author, description 1 line, pills, downloads, state).
- Motion: `ContentReveal` with the mods numbers (16 px, 260 ms, 24 ms stagger); skeleton = `ZeroSkeleton::Cards` in the
  mod card shape.
- **Details panel** (shared `ContentDetailsPanel`): icon (no hero artwork unless the project has a featured gallery image),
  name, author, stats (downloads, follows, updated, license), primary **Add to <instance name>** (elided ≤ 160 px), or
  **Installed** (disabled, success) when the installed version is the newest compatible one, or **Update** when the target
  has an older version of it; Website ghost. Tabs: Overview, Gallery (only with images), Versions: only versions compatible
  with the target (MC + loader, `ModModel::checkVersionFilters` rules) with a "Show all versions" toggle (others dimmed,
  "Not for 1.20.1 Fabric", not installable), Changelog inline as for modpacks.

## Mod install (`ui/zero/content/ModInstallDialog`, `zero/content/ModInstall`)
- Review dialog 560 px (same frame as `ModpackInstallDialog`): header (icon, name, author, provider badge); line "Adding to
  <icon> <instance> · 1.20.1 Fabric"; **Version** combo of compatible versions (default = newest compatible release, else
  newest compatible) with type / MC / loader pills and file size; **Dependencies** block: while
  `GetModDependenciesTask` runs (headless, started after the target's `loaderModList()` finished loading — it snapshots
  installed mods in its constructor) 2 skeleton rows + "Checking dependencies…"; then rows (icon 24, name, version,
  "Required by X") for each dependency that will be added, and a caption "Already installed: A, B" for skipped ones; none →
  "No other mods needed". Failure → inline "Couldn't check dependencies: <reason>" + Retry + checkbox "Add without
  dependencies" (unchecked). Footer: Cancel ghost, primary "Add mod" / "Add 3 mods" (disabled until the check settled).
- Install = `ResourceDownloadTask(pack, version, inst->loaderModList(), isIndexed, "standalone" | "dependency",
  requiredById)` per mod, all in one `ConcurrentTask` (`NumberOfConcurrentDownloads`), run as a background job of the Downloads center
  (ARCHITECTURE.md, "Downloads center"; no modal dialog), then `loaderModList()->update()` when it ended. Never use
  `ResourceDownloadDialog` / `ResourceUpdateDialog` (their modal boxes). Success → toast "Added <name> to <instance>" (+
  " and N dependencies"); failure → Error toast "Couldn't download <name>" with the action Downloads.
- Update from the details panel = the same dialog in Update mode (version list, the dependency check of the new version);
  `ResourceDownloadTask` replaces the old file after success (Prism behaviour).
- Refused while the target instance runs: the primary button is disabled with the note "Close <instance> to add mods".

## Installed mods page (C5; instance window, page id `"mods"`)
Replaces `ModFolderPage` in `InstancePageProvider::getPages()` (same id, display name "Mods", same icon); Core/Nil mod pages
untouched. Source model: `inst->loaderModList()` (`startWatching()` in `openedImpl`, stop in `closedImpl` only if this page
started it). Never call `ModFolderModel::setResourceEnabled` (modal) or `deleteResources` (permanent fallback).
- **Toolbar** (40 px): search (name, mod id, file name; Ctrl+F, Esc clears), segmented filter **All · Enabled · Disabled ·
  Issues** (+ **Updates** once a check found any), caption "48 mods · 3 disabled", **Check for updates** secondary (C5b),
  **Add mods** primary → Discover › Mods with this instance as target (`MainWindow::showDiscover("mods")` + set target).
  Overflow `…` menu: Open mods folder, Open the classic mods page (Prism's `ModFolderPage` in a dialog — escape hatch).
- **Rows** (painted delegate, 56 px, multi-select Ctrl/Shift, keyboard Up/Down, Space toggles, Delete removes): toggle switch
  (enabled), icon 32 (mod logo from `Mod::iconPath()`/metadata, else `blocks` tile), name (heading, elided) + version
  (caption `textSecondary`), pills: provider ("Modrinth" / "CurseForge" / "Local file" when no metadata), environment
  when known, "Not for 1.21.1" `warning` (from `Resource::issues()`), "Required by N" (`requiredByCount`), right side
  "Update → 0.6.14" accent pill (C5b). Disabled rows: text `textSecondary`, icon at 0.5 opacity, "Disabled" pill.
  Hover `…` button: Enable/Disable, Update (C5b), Open website (homepage / provider page), Show in folder, Remove.
- **Selection bar** (≥ 2 selected, replaces the caption): "N selected · Enable · Disable · Remove".
- **Enable/disable**: qualified base `ResourceFolderModel::setResourceEnabled(indexes, action)`. Before disabling, if
  `getAffectedMods(indexes, DISABLE)` names enabled mods outside the selection, a ZERO dialog: "Disabling Sodium affects
  Iris Shaders, which requires it." → **Disable only Sodium** / **Disable all 2** / Cancel; enabling mirrors it with
  required-but-disabled mods ("Enable Fabric API too?"). While the instance runs: a `warning` banner "Close the game to
  change mods" and toggles/Remove disabled (never the base model's modal).
- **Remove**: ZERO confirm dialog listing up to 8 names (+ "and N more"), "They go to the Recycle Bin." → for each:
  `FS::trash(file)`; only on success remove its packwiz metadata (`deleteMetadata` for that index); a failure keeps the
  file and ends in one toast "Couldn't move 2 mods to the Recycle Bin" (no permanent delete, ever); then `update()`;
  success toast "Removed 3 mods" (no undo: the Recycle Bin is the undo; the text says so in the dialog).
- **Empty / loading**: skeleton rows while the first load runs; no mods → `ZeroEmptyState` (`blocks`) "No mods yet" /
  "Find mods for <instance> in Discover." + **Add mods**; no mod loader → "This instance has no mod loader" (no Add).
- **C5b Updates**: Check for updates = headless `ModrinthCheckUpdate` + `FlameCheckUpdate` over enabled+disabled resources
  WITH metadata (copy the `Resource*` list and the version vector; pause toggles/remove/refresh while checking; caption
  "Checking 41 mods…"); results → "Update → <version>" pills + Updates filter; "7 mods have no provider info and can't be
  checked" caption; failures per mod in the row tooltip. Update one / Update all N → the `Update::download`
  `ResourceDownloadTask`s in a `ConcurrentTask` as a background Downloads-center job (kind Update), then `update()` + toast.
  Then Discover › Mods may show "Update available" (share results via the target instance id; optional).

## Dependency Inspector (C6; instance Mods page) — owner approved the mockup in session 9
Source of truth: ZERO's own jar analysis (`zero/mods/ModMetadata` parse incl. nested jars and `provides`, `Versions`
predicates / Maven ranges — the same rules `ModHealth::analyze` uses, so the page and Tools › Mod Health never disagree),
plus Prism's packwiz relations (`ModFolderModel::requiredByList`) for "Needed by". No network.
- **Core** `zero/mods/DependencyInspector` (pure): input = parsed enabled + disabled jars (file name, path, metadata),
  `InstanceContext`; output per top-level file: `needs` (mandatory deps minus environment ids), `game` (minecraft, the
  loader ids, java), `optional` (recommends), `conflicts` (breaks + conflicts), `neededBy` (files whose mandatory deps resolve
  to this file), `readable`. Each dependency entry: id, display name (the installed mod's name when found, else the id),
  predicate text ("" = any), state **Installed** (version), **Bundled** (inside which file), **Disabled** (which file),
  **WrongVersion** (installed version), **Missing**, **Unknown** (predicate "?" or version unknown). Game entries: Ok /
  Mismatch / Unknown (java is always Unknown: not checked). Quilt `unless` and Forge `side` rules as in ModHealth.
- **Scan**: async on the global pool after every `ModFolderModel` reload (debounced 300 ms), generation token, parse cache keyed
  by path + size + mtime (only changed jars are parsed again). Read-only.
- **Row pills** (priority over provider / environment / Required by, which drop first when the row is narrow): Missing →
  `danger` "Needs Indium" ("Needs Indium +2" when several); Disabled → `warning` "Needs Sodium · disabled"; WrongVersion →
  `warning` "Wrong Sodium version"; an installed+enabled conflict → `danger` "Conflicts with OptiFabric". Only for enabled
  mods (a disabled mod's needs don't matter until it is enabled). Rows with any of these count in **Issues** and in the caption
  "· 2 need attention" ("1 needs attention").
- **Panel** (right, 320 px, `surface` card, radius lg, padding 16, scrolls on its own): opens on a plain click on a row body or
  Enter (not on the toggle or `…`); closes on ×, Esc, a Ctrl/Shift multi-selection, or when the mod disappears. Header: icon
  40, name, "version · file name" (caption, elided, tooltip = full path). Sections (overline titles; empty ones hidden except
  Needs / Needed by): **Needs** (name, predicate caption "any version" / text, state pill: success "0.116.18", success
  "Bundled" + tooltip "Inside x.jar", warning "Disabled" + **Enable** small secondary (the page's normal enable flow incl.
  affected-mods question and running lock), warning "Has 0.5.1", danger "Missing" + **Find** small secondary → Discover ›
  Mods with this instance as target and the dependency id as the search text, neutral "Can't tell"); empty → "Needs no other
  mods."; **Game** (Minecraft / loader / Java with the predicate; success with the instance's version, warning "Needs 1.20.1",
  neutral for Java); **Optional** (Installed / Not installed, neutral, no actions); **Needed by** (installed mods that need this
  one, from the scan ∪ Prism's packwiz relations; a disabled one says "disabled"; click → select that mod and show its panel);
  empty → "No installed mod needs <name>."; **Conflicts**: only conflicts with installed mods (danger "Installed"); the
  others become one caption "Declares 12 more conflicts with mods that aren't installed." (tooltip = their names); the
  section is hidden when there are none of either. **Bundled jars** (review fix, session 9): Needs / Game / Optional list the
  top-level jar's own entries; a bundled jar's dependency appears only when it is a problem (Missing / Disabled / WrongVersion)
  with the caption "for bundled <name>"; one row per id (no duplicates). Fabric predicates with a trailing "-" (">=1.19.4-",
  lowest pre-release) are evaluated, and shown without the "-". Unreadable jar:
  "ZERO couldn't read this mod's details." + Needed by only. While the first scan runs: 4 skeleton lines.
- `MainWindow::showDiscoverMods(instanceId, query)` (+ `DiscoverPage::showModsFor(id, query)` → `ContentSection::setSearchText`).

## Tests
Pure parts (target selection, compatibility, search args, install plan) unit-tested offscreen without network; widgets
with fake sources as in `ZeroDiscover_test`; screenshots both themes at 1280×800 and 1600×900.
