# ZERO Content Experience (Mission 003) — binding spec

Owner brief: `docs/zero/MISSION.md` + the Mission 003 text in `HANDOFF.md`. Tokens and components: `docs/zero/DESIGN.md`
(Design v2 is binding: Minecraft theme first, Obsidian fully supported). Shell rules: `docs/zero/SHELL.md`.
This file is the architect's spec for Create Instance, Discover (Modpacks / Mods / Resource Packs / Shaders), the
ZERO Filter System and content motion. Update it in place when a decision changes; record why.

## Principles
- **Real data only.** Every number, badge, label and image comes from a provider response or the local disk. A field
  the provider did not send is hidden, never guessed. "Featured" is shown only for provider-curated lists, labelled
  with the provider's own wording ("Featured on CurseForge"); a list ordered by downloads is "Popular", never "Featured".
- **Preserve engines.** Provider APIs (`ModrinthAPI`, `FlameAPI`, Technic), install tasks (`InstanceImportTask`,
  `VanillaCreationTask`, Technic tasks, `ResourceDownloadTask`), `InstanceList::wrapInstanceTask` and metadata handling
  stay the source of truth. ZERO adds presentation, a pure filter/metadata layer and parser fields. No provider logic is
  re-implemented.
- **Shared infrastructure, specialised presentation.** One filter system, one image cache, one toolbar, one details
  frame, one reveal animation; four different card/tile presentations.
- **Never block the UI, never pop stray modals.** Browse failures are inline states with Retry, not `QMessageBox`.
- **A selection change resets at once.** The moment another item is selected, the old item's install action is gone;
  answers for an item that is no longer selected are dropped (the Technic rule, `TechnicSelection`).

## Layers
| Layer | Where | Contents |
|---|---|---|
| Provider core (Prism) | `launcher/modplatform/` | APIs, parsers. ZERO extends `ModPlatform::IndexedPack` with optional presentation fields (below) and parses them; nothing else changes. |
| Content core (pure) | `launcher/zero/content/` | `ContentKinds` (kinds, facets), `ContentFilter` (state, chips, mapping to `ResourceAPI::SearchArgs`, client predicate), `TechnicMeta` / `TechnicDetailsStore` (existing), `CreateInstanceModel` (version/loader availability). No widgets, no `APPLICATION`, unit-tested. |
| Content UI kit | `launcher/ui/zero/content/` | `RemoteImageCache`, `ContentToolbar`, `FilterChipStrip`, `FilterPanel`, `ContentReveal` (slide-in), card delegates, `ContentDetailsPanel`, `GalleryStrip`, skeleton shapes. |
| Pages / dialogs | `launcher/ui/zero/pages/`, `launcher/ui/zero/create/` | `DiscoverPage` (+ one section widget per kind), `CreateInstanceDialog`, `ModpackInstallDialog`. |

## IndexedPack presentation fields (Prism core extension, `// ZERO` marked)
Added to `ModPlatform::IndexedPack` (all optional; "unknown" values below mean "hide it"):
`qint64 downloads = -1`, `qint64 follows = -1`, `QStringList categories` (provider ids/slugs, e.g. Modrinth
`"optimization"`, CurseForge category names), `QStringList gameVersions` (project-level MC versions),
`QStringList loaderNames` (lower-case provider loader names: `fabric`, `forge`, `neoforge`, `quilt`, `iris`, `optifine`,
`canvas`, `vanilla`, `minecraft`, `datapack`), `QList<GalleryImage> gallery` (`url`, `thumbnailUrl`, `title`,
`description`, `featured`), `QString featuredImageUrl`, `QDateTime dateModified`, `QDateTime dateCreated`,
`QString license`.
- Modrinth search hit: `downloads`, `follows`, `categories` (+ `display_categories` merged, de-duplicated), `versions`
  → gameVersions, `gallery` (array of URL strings → thumbnail = url, url = same; Modrinth `_350` CDN images), 
  `featured_gallery`, `date_modified`, `date_created`, `license` (string id), `author` (already parsed). The
  `categories` array of a Modrinth hit also contains loader names (`fabric`, `iris`…): split them into `loaderNames`
  using the `/tag/loader` vocabulary of known loaders (static list in code).
- Modrinth project (`/project/<id>`): `downloads`, `followers`, `categories` + `additional_categories`, `game_versions`,
  `loaders`, `gallery` objects (`url`, `raw_url` ignored, `title`, `description`, `featured`, `ordering` → sort),
  `updated`, `published`, `license.id`. The project endpoint has no `author`: **merging details must keep the
  authors from the search hit** when the response has none (today `*project = *response` drops them).
- CurseForge (`/mods/search`, `/mods/<id>`): `downloadCount`, `categories[].name` (+ `slug`), `screenshots[]`
  (`url`, `thumbnailUrl`, `title`, `description`), `dateModified`, `dateCreated`, `latestFilesIndexes[]` (`gameVersion`
  → gameVersions, `modLoader` int → loaderNames), `thumbsUpCount` is not shown. No license on CurseForge.
- `categoryIds` (as built): filter values. Modrinth = the category slugs (same as `categories`); CurseForge = numeric
  ids as strings while `categories` holds the display names. `ItemFacts.categories` is filled from `categoryIds`.
- `ResourceAPI::SearchArgs::categoryGroups` (as built): OR inside a group, AND across groups; Modrinth only.
  `Zero::Content::toSearchArgs` puts category / feature / resolution / platform / performance there. CurseForge takes one
  `gameVersion`: with several versions selected none is sent and `matchesClient` filters the loaded rows.
- `ModPlatform::mergeProjectDetails(into, details)`: every place that merges `/project` details into a search hit uses
  it (keeps authors and presentation fields the details lack).
- Version-level (`IndexedVersion`): add `qint64 fileSize = -1` (Modrinth primary file `size`, CurseForge `fileLength`)
  and `qint64 downloads = -1` (Modrinth version `downloads`, CurseForge `downloadCount`).
- Categories with groups: `ModPlatform::Category` gains `QString header` (Modrinth `header`: `categories`,
  `features`, `resolutions`, `performance impact`) and `QString iconSvg` is NOT added (we do not render remote SVG).
  Categories can be loaded for any project type: Modrinth `/tag/category` filtered by `project_type`
  (`modpack`, `mod`, `resourcepack`, `shader`), CurseForge `/categories?gameId=432&classId=<4471|6|12|6552>`.

## ZERO Filter System
One system, four configurations. Pure state in `zero/content/ContentFilter`; one set of widgets in `ui/zero/content`.

### State
`FilterState { QString query; QString sortId; QHash<QString facetId, QStringList values> selected; }`.
Facet = `{ id, label, kind (Multi | Single | Toggle), side (Server | Client), options [{value, label, group}] }`.
- **Server facets** are sent to the provider and trigger a new search (debounced 300 ms, request generation token;
  results of an older generation are dropped). **Client facets** filter the loaded rows through a
  `QSortFilterProxyModel` predicate, without a request.
- Combination: values inside one Multi facet are OR, different facets are AND (Modrinth facets semantics; CurseForge
  supports only one game version and one loader per request: the mapping sends the first and the client predicate
  enforces the rest on `gameVersions` / `loaderNames`, marking the result count "of loaded results").
- **Chips** = one per selected value (`facet label: value label`, e.g. `Loader: Fabric`), plus the search text as
  `"text"` chip. Removing a chip removes exactly that value. **Clear all** resets every facet and the query to the
  section defaults (it keeps sort and view mode). The defaults of a section may be non-empty (Mods: the target
  instance's MC version and loader) and are drawn as chips with a lock-free "default" look; Clear all returns to them,
  and a separate "Show everything" removes them too.
- Persisted per section in settings (`ZeroDiscover<Kind>Filter`, JSON) except the query.

### Facets per section
| Section | Server facets | Client facets |
|---|---|---|
| Modpacks | Provider (single choice: Modrinth (default) / CurseForge / Technic — one source at a time, so sort and paging stay honest; Technic can only be searched by name, its other facets show "Technic can only be searched by name"), Minecraft version, Loader (Fabric, Forge, NeoForge, Quilt), Category (provider categories, `header = categories`) | Installed (instances whose managed pack id matches) |
| Mods | Provider (single choice), Minecraft version, Loader, Category, Environment (Modrinth only) | Installed in target, Update available (from installed metadata), Hide installed |
| Resource packs | Provider, Minecraft version, Resolution (Modrinth `resolutions` header), Category / Features | Installed in target |
| Shaders | Minecraft version, Platform (Iris / OptiFine / Canvas / Vanilla, Modrinth loaders), Performance impact (Modrinth `performance impact`: potato / low / medium / high), Features / Category | Installed in target |
A facet whose provider cannot apply it is shown disabled with the reason in its tooltip ("CurseForge has no
resolution data"), never silently ignored.

**Shader performance profile** (only Modrinth, provider metadata): `potato` → Lightweight, `low` → Lightweight,
`medium` → Balanced, `high` → High quality; a pack tagged with several shows the range ("Lightweight – High quality",
Modrinth uses these tags for supported profiles). `screenshot` (if the provider sends it) → Cinematic. No FPS or
benchmark numbers anywhere. Caption in the filter panel: "From the shader's Modrinth tags".

### Widgets
- **ContentToolbar** (one row, 40 px, inside the page under the top bar): search field (flexible, min 240, max 420,
  `search` icon, placeholder per section, Ctrl+F, Esc clears), **Filters** button (secondary, `sliders-horizontal`,
  label "Filters" + a count pill when > 0; opens the panel), stretch, results caption (`12,431 results` /
  `48 of 312 loaded`; `textSecondary`), sort combo, grid/list toggle (only where both exist).
- **FilterChipStrip** (under the toolbar, 28 px chips, wraps to at most 2 lines then scrolls horizontally): removable
  chips (`x` 12 px, hover `danger` x), **Clear all** ghost button at the end. Hidden when there are no chips.
  Chip entry/exit animate width + opacity 140 ms.
- **FilterPanel**: popover anchored under the Filters button (360 px wide, max 70 % window height, scrolls), `surface`,
  1 px `border`, radius lg, popover shadow; fades + rises 6 px in 160 ms. Groups with overline headers; options are
  toggle chips (multi) or a segmented row (single); Minecraft version = searchable list with "Releases only" toggle.
  Changes apply live (debounced); footer: "Reset" ghost + result count + "Done". Esc/outside click closes.
  Keyboard: Tab through groups, Space toggles.
- Empty result: `ZeroEmptyState` (`funnel-x`) "Nothing matches these filters" + body naming the most restrictive
  chip when known + **Clear all** primary (+ "Show everything" when defaults exist). Network error: `triangle-alert`
  "Couldn't reach Modrinth" + the error text + **Retry**.

## Remote images (`RemoteImageCache`)
URL → disk cache through `APPLICATION->metacache()` base `ZeroContent` (registered in `Application.cpp`), key
`<sha1(url)>`; download with `Net::ApiRequest::makeCached` (or `Net::Download` for non-API hosts), at most 6 in
flight, priority to visible items; decode on `QThreadPool` with `QImageReader::setScaledSize` to the requested
bucket (64, 128, 400, 1280 px wide); `QCache` LRU (~64 MB cost in pixels); signal `imageReady(url, bucket)`;
failures remembered for the session (retry on explicit Refresh). Requests carry a requester token so a scrolled-away
card can cancel. Never decode on the GUI thread. Fade-in 160 ms on arrival (as `ThumbnailCache`).

## Motion
- **ContentReveal (slide-in, binding for Modpacks):** new rows enter **from the right**: start x + 24 px, opacity 0.15,
  scale 0.985 → final in 320 ms `OutCubic`, stagger 32 ms by **row order** (not position), capped at 14 steps
  (later rows start with the 14th). It animates only rows inserted since the last frame (first page, next page on
  scroll, a new search result set) — never rows that merely emitted `dataChanged` (image arrived, installed state).
  A new search while an entrance runs replaces it (rows of the old generation stop animating). Painting is done in
  the delegate wrapper (like `ItemReveal`), no per-item widgets, no layout animation, one `QTimer` at 60 Hz that stops
  when nothing animates. Motion off → instant. Mods / packs / shaders use the same class (mods: 16 px, 260 ms, 24 ms;
  galleries: 20 px, 300 ms, 28 ms).
- Details panel: slides in from the right 24 px + fade, 200 ms; switching items crossfades the content 140 ms.
- Loading: skeleton cards in the section's real card shape (new `ZeroSkeleton` shapes `Cards`, `Gallery`,
  `Cinematic`) with the sweep; the first page replaces them through the reveal.
- Filter change: the grid keeps its rows dimmed (opacity 0.5, no spinner flash < 150 ms) until the new answer arrives,
  then the new rows reveal.

## Discover page (rail)
Rail item **Discover** (`sparkles`) right after Library (Alt+3; Worlds becomes Alt+4 …, Appearance Alt+8). Top bar:
title "Discover", subtitle = current section's results caption; actions: a segmented control **Modpacks · Mods ·
Resource Packs · Shaders** (remembered, `ZeroDiscoverSection`). Palette: "Go to Discover", "Browse modpacks" …
Body: `[toolbar / chips / grid | details panel 400 px (hidden until a selection, the grid reflows)]`.
Mods / Resource Packs / Shaders also show a **target instance** control at the left of the toolbar ("For: <icon>
<name> · 1.20.1 Fabric ▾"; default = Library selection, else last used `ZeroDiscoverTarget`); without a suitable
instance the section shows an empty state with "Create instance".

### Modpacks
- **Featured strip** (only on the default view: no query, no chips): up to 5 hero cards from CurseForge's Featured sort
  (`sortField=1`), labelled "Featured on CurseForge"; when CurseForge is not available, "Popular on Modrinth"
  (downloads sort). Hero: 16:9 artwork (featured gallery image, else first screenshot, else the icon on a `surface`
  tile at 96 px), name (title role), author, description (2 lines), Minecraft · loader pills, provider badge, Install
  primary. Horizontal snap scrolling with chevrons.
- **Grid card** 280–340 px wide (columns fill the width), 16:9 artwork top (cover crop; icon fallback as above), the
  48 px icon overlapping the artwork bottom-left, name (heading, 1 line), author (caption), description (2 lines),
  footer pills: Minecraft range ("1.20.1" or "1.18 – 1.20.1"), loader, provider badge, downloads (`download` icon +
  `12.3M`), "Installed" (success) when an instance manages this pack. List mode: 72 px rows with the same data.
- **Details**: hero artwork, icon, name, author, stats row (downloads, updated relative, license), Install / Update
  primary, Website ghost; tabs Overview (markdown body, images through `ProjectDescriptionPage`'s cache), Gallery
  (thumbnail strip → in-page viewer), Versions (list: name, type pill, Minecraft, loader, date, size; changelog
  expands inline, loaded on demand), What's new (Technic feed / latest changelog). Mod list only when the provider
  gives one (Technic Solder builds do not; Modrinth/CurseForge packs do not expose it before download → hidden).
- **Install** → `ModpackInstallDialog` (review). First decision, top of the dialog: "What do you want to do?" with two
  radio cards, **New instance** ("A separate copy") and **Update an instance** ("One you already have"). Update is
  disabled with an honest caption when no instance manages the pack ("None of your instances has this pack") or the
  provider cannot update in place (Technic). When instances manage the pack Update starts selected (Discover's Update
  passes the instance; otherwise the most recently played comes first) and "Instance to update" lists all of them (name,
  version, last played; more than four scroll). Update shows the version, "Keep my game settings" (on by default:
  `options.txt`, `optionsof.txt`, `optionsshaders.txt`, `servers.dat`, `config/iris.properties`, `config/oculus.properties`
  are copied aside and put back after a successful update of that instance, `zero/content/ModpackKeep`) and the primary
  "Update"; New shows version, instance name (default pack name), group, location (only with several instance dirs),
  template, icon from the pack logo, and "Install". The choice is made once: the provider's own "Similar modpack was
  found!" question never shows (`setConfirmUpdate(false)`; a New instance also sets `InstanceTask::setAlwaysCreateNew`, so
  the provider does not look for an instance to update). MainWindow takes the pre-update copy (Undo) first. The task runs through `InstanceList::wrapInstanceTask` (Milestone F moves it into
  the Install Queue). Providers: Modrinth and CurseForge via `ResourceAPI` (`ResourceType::Modpack`), Technic via
  `TechnicMeta` + `TechnicDetailsStore`. ATLauncher, FTB, legacy FTB and FTB App import stay in the classic dialog,
  reachable from Create Instance → "More sources".

## Create Instance (`CreateInstanceDialog`)
Replaces `NewInstanceDialog` for "New instance" and "Import" (Library, Home, palette, Ctrl+N). Deep links
(`modrinth://`, `curseforge://`, URLs/files dropped on the window) and "More sources" keep using `NewInstanceDialog`
unchanged. 1000 × 680 default (min 860 × 600), `bg` with the theme texture, radius xl.
- **Header** (64 px): title "Create instance" + caption "Choose what to start from".
- **Type cards** row (4 equal cards, 96 px tall, `ZeroCard` clickable, keyboard 1–4 / arrows): **Vanilla** (`cuboid`,
  "The game as Mojang made it"), **Modded** (`blocks`, "Fabric, Forge, NeoForge or Quilt"), **Modpack** (`package`,
  "A ready-made pack from Modrinth, CurseForge or Technic"), **Import** (`file-archive`, "A .zip or .mrpack you
  already have"). Selected card: `elevated` + 2 px `accent2` bottom indicator that glides between cards (200 ms
  OutQuint), icon in `accent`. Choosing a card crossfades the body (140 ms).
- **Body** `[configuration | live preview 300 px]`:
  - Vanilla / Modded: **Minecraft version** list (painted rows 40 px: version, type pill `Release` / `Snapshot` /
    `Beta` / `Alpha` / `Experiment`, release date caption, "Latest" success pill on the newest release; filter chips
    Releases (default) / Snapshots / Old (beta + alpha) / Experiments; search field). Modded adds **Loader** segmented
    row showing only loaders that have at least one version for the chosen Minecraft version (from metadata:
    Forge / NeoForge / LiteLoader by `requires net.minecraft == mc`; Fabric / Quilt when `net.fabricmc.intermediary`
    has the Minecraft version), unavailable loaders hidden with a caption "Not available for 1.2.5: Fabric, Quilt…";
    and the **loader version** list (version, "Recommended" pill, date). Loading = row skeletons; failure = inline
    error + Retry (no modal box). The meta lists are loaded once and cached by Prism.
  - Modpack: three source tiles (Modrinth, CurseForge, Technic) + "Browse all modpacks" (opens Discover › Modpacks
    and closes the dialog) + "More sources" (ATLauncher, FTB, FTB App import, legacy FTB → classic dialog page).
  - Import: drop zone (dashed `borderStrong`, `upload` icon, "Drop a .zip or .mrpack here", Browse…), URL field
    (http/https), the detected format caption once known ("CurseForge pack", "Modrinth pack", "Prism / MultiMC
    export", "Technic pack" — detected only by the file name for now, otherwise "Will be detected when importing").
  - **Live preview** (right): artwork tile (Vanilla: `cuboid`; Modded: `blocks` with the loader name; Import: the
    file's icon if any), instance name (heading), rows Type, Minecraft, Loader (+ version), Source file; only real
    values, "—" never shown: missing rows are hidden.
- **Footer** (72 px, `surface` top border): icon button (current instance icon, click → icon picker), **Name** field
  (prefilled from the suggestion until the user edits it; validation: not empty, ≤ 128 chars, inline error),
  **Group** combo (editable, "No group"), **Advanced ▸** (location combo when several instance dirs, "Use instance
  template" when a template dir exists; collapsed by default, remembered), Cancel ghost, **Create** primary lg
  (disabled until valid; Enter).
- Accept: builds exactly what `NewInstanceDialog::extractTask()` builds (name, original name/version, group, icon key,
  target dir, copy template) and returns the task; `MainWindow` runs it as before (`instanceFromInstanceTask`). The
  dialog stays alive while the task runs (tasks parent dialogs to their widget).
- Keyboard: Tab order cards → config → name → Create; Esc cancels; every control has an accessible name.

## Installed content (Milestones C–E, spec to be detailed when started)
- Installed Mods / Resource Packs / Shaders live in the instance window (replacing Prism's pages for those ids) on top
  of the existing `ResourceFolderModel`s. Disable = rename to `.disabled` (never delete); Remove = Recycle Bin only,
  with confirmation, **no permanent-delete fallback**; update checks through `ResourceUpdateDialog`'s tasks.
- Resource-pack order: `options.txt` `resourcePacks` read/write with a backup of the previous order (restore), refused
  while the game runs. Dependency Inspector on top of `zero/mods` (`ModHealth`) + provider metadata.

## Install Queue (Milestone F)
App-level `Zero::InstallQueue` (non-UI core + toast/panel UI): pending / installing / completed / failed / cancelled,
retry, at most N concurrent (`NumberOfConcurrentDownloads`), dependency tasks added as children. Until then installs
use the existing modal progress dialogs.
