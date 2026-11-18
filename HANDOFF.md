# ZERO Launcher — Handoff

Last updated: 2026-11-17 (session 18: Mission 004 **B5 done and committed** — Spanish translation + language picker, Discord Rich Presence; Mission 004 complete, see "Mission 004". Session 17: B4 `26c833081`. Session 16: B2 + B3 + B4 task 1 (Playtime). Session 15: owner list — pack rollback, modpack New/Update, smooth scroll, Shader Packs page, UI sounds; see "Session 15" before "Build & test". Session 14: **1.0.1 PUBLISHED**; 1.0.0 PUBLISHED 2026-11-13) · Branch `zero-development` (local only, never pushed) ·
Base: Prism Launcher `develop` @ 00f216ce5 · Repo folder: `C:\Users\64027\Documents\GitHub\ZeroCraft Launcher`
(renamed from `ZERO-Launcher`; the path now contains a space).

## Read first
`docs/zero/MISSION.md` (owner brief incl. Mission 002: binding) → `CLAUDE.md` → `docs/zero/ARCHITECTURE.md` →
`docs/zero/DESIGN.md` (read the **Design v2** section at the end) → `docs/zero/SHELL.md` → `filemap.md`.

## How the owner works
Speaks Spanish, so reply in Spanish. Runs `/savvy-flow` 1.0 (Opus orchestrates, designs and reviews; `savvy-medium`
implements ordinary work, `savvy-careful` delicate UI refactors/security/migration; one worker at a time). Local commits on
`zero-development` are authorized as checkpoints; **never push**. Autonomous decisions are expected; don't ask for
ordinary engineering choices. Multi-session roadmap: leave every session compiling, tested, committed, documented.

## Session log
- **Session 1**: toolchain, identity, Prism import, Obsidian design system, shell (rail/Home/Library), Crash Detective.
- **Session 2 (Mission 002, Phase A + start of B)**: theme engine v2 + Minecraft Theme, motion system, Play button with
  real launch states, Appearance page, shell v2 (top bar), command palette. Commits:
  `fca565d87` themes · `59e8b94b5` motion · `7316de327` Play + Appearance · `b0b6aa183` shell v2 · `ffde92420` palette
  (+ this handoff commit).
- **Session 3 (Mission 002, Phase B milestone 6)**: Instance Profiles + Library 2.0: favorites, tags, description
  (= Prism notes), per-instance artwork, filter chips, "Most played" sort, favorite star on cards, artwork covers, details
  panel editing, Home favorites strip + artwork hero, palette favorites. Design/data notes: `docs/zero/SHELL.md` (Library,
  Home) and `docs/zero/ARCHITECTURE.md` (Instance profiles).
  Then **Session History** (`launcher/zero/history/SessionHistory`, wired in `Application`; data
  `<data>/zero/session-history.json`) and the Home **Recent activity** section (6 rows, Diagnose on crashed/failed).
  Then the **Worlds Hub** page (rail item after Library: Alt+3 Worlds, Alt+4 Screenshots, Alt+5 Servers, Alt+6 Tools, Alt+7 Appearance), read-only.
  Then **Smart Backup** (manual world backups + verified restore with a safety snapshot, in the Worlds details panel).
- **Session 4 (Mission 002, Phase C)**: owner asleep, autonomous run following this file's priority order.
  `acaf9c883` **automatic world backups** after playing (`launcher/zero/backup/AutoBackup`; job runner now app-level).
  `5b89db256` + `fdcd3fdd0` **Storage Manager** (`launcher/zero/storage/StorageScanner`, `ui/zero/pages/StorageView`, Tools › Storage).
  `fc358bbf4` **Java Doctor** (`launcher/zero/java/JavaDoctor`, `ui/zero/pages/JavaDoctorView`, Tools › Java Doctor;
  shared `ui/zero/PillTag`).
  `6596ab825` + `c22556aaa` **Mod Intelligence** (`launcher/zero/mods/`, `ui/zero/pages/ModHealthView`, Tools › Mod
  Health, Library "Check mods").
  `742869210` **Repair actions** in Mod Health (first Repair Center slice: `launcher/zero/mods/ModRepair`).
  `bb4950481` **Screenshot Gallery** (rail item after Worlds: Alt+4 Screenshots).
  **Server Vault** (rail item after Screenshots: Alt+5 Servers, Alt+6 Tools, Alt+7 Appearance): read-only `servers.dat` of every
  instance grouped by address, status ping (`launcher/zero/servers/`), details panel, Play on server (quick join).
  Downloads/Update center was skipped for now: it hooks into Prism's network/task engine (higher regression risk on a
  core path); Storage Manager and Java Doctor are self-contained and came first.
- **Session 5 (owner present)**: real-app test of the dev build against a temp data dir (rail navigation, Back up now,
  Restore incl. confirm dialog + safety row + Recycle Bin, palette query, theme Apply + restart persistence, servers
  ping against a local fake status server) driven through Windows UI Automation and posted mouse messages. Fixes from
  that run (owner chose "English for now" over translating ZERO): **English-only UI** (`zero/ZeroLanguage.h`; Prism/Qt
  translations no longer mix in, dates en_US, Language settings hidden), Mod Health headline "Couldn't read any of the
  N mods" (neutral) instead of a green "No problems found" when nothing was readable + "Checked X of Y mods" + natural
  sort, search-field icon centred on every page (qss `QLineEdit QToolButton`), accessible clickable cards and palette
  rows, restored-over world goes to the Recycle Bin as `<folder> (replaced <date>)`, Crash Detective no-logs caption
  removed and the top-bar title keeps "Tools › Crash Detective" (search pill shrinks first).
- **Session 6 (owner present, owner's list)**: `68c811a80` gallery blink fix (no model reset when a rescan finds the same
  rows, immediate layout after a real reset; same guard in Worlds/Servers). `ada0a6299` **Minecraft is the default
  theme** (one-time move from Obsidian, `ZeroMinecraftDefaultApplied`) + original 16×16 pixel-art app mark
  (`program_info/zero-logo-minecraft.svg`; window/taskbar icon and rail logo follow the theme). `adc76c5ba` **animations
  on by default** (`ZeroMotion` empty = on) + page-enter `Transition::Kind::Cascade`, `Motion::ItemReveal` list entrances
  (`ui/zero/ZeroReveal`), thumbnail fade-in, `ZeroSkeleton` loading states, Storage bars fill, rail icon pop.
  `1b3b67665` **favorite worlds/servers** (`zero/favorites/FavoriteStore`, `<data>/zero/favorites.json`). `6cfff02e4`
  **Settings inside ZERO** (`ui/zero/pages/ZeroSettingsPage`, `Application::OpenSettings`) and the redesigned
  `PageContainer` (side list with summaries, explanations, option help strip; `ui/zero/ZeroPageNav`, `ZeroPageHelp`) used
  by Settings, the **instance window** (header card, Play/Stop styling) and every page dialog. Then rail tooltips that say
  what each section is for, and selecting a section through accessibility switches the page.

## Mission 003 — Content Experience Rework (session 7, IN PROGRESS)
Owner brief (pasted in chat, summarised here; binding): redesign **Create Instance**, fix/verify **Technic preview**,
premium **Modpacks** discovery + install, **Mods** discover + installed manager + Dependency Inspector, **Resource
Packs** visual gallery + installed order management, **Shaders** cinematic discovery (+ performance labels only from
real metadata), one shared **ZERO Filter System** for the four sections, **modpack cards must enter from the RIGHT**
(~24 px, ~320 ms, ~32 ms stagger, OutCubic, only new rows, reduced motion respected), optional Install Queue /
comparison / collections / What's new. Real data only, no mocks, preserve provider + install engines, English UI.
Milestones A (Create Instance + common content architecture) → B (Modpacks + Technic) → C (Mods) → D (Resource
Packs) → E (Shaders) → F (filters/animation polish, Install Queue); per milestone: review, build, test, real-app
screenshots (both themes, several sizes), **local commit**, update this file. Never push.

**Binding spec written this session: `docs/zero/CONTENT.md`** (architecture layers, IndexedPack fields, Filter System
state/chips/facets per section, RemoteImageCache, motion numbers, Discover page, Modpacks, Create Instance layout).
Read it before continuing. Decision since: Provider facet is **single choice** (one source at a time).

State at the time of writing (repo `C:\Users\64027\Documents\GitHub\ZeroCraft Launcher`, branch `zero-development`):
- Baseline before changes: **47/47 ctest + FileSystem 25/25** at `fe4fab8f9`.
- Committed: `4936eab82` docs(content): `docs/zero/CONTENT.md` + 30 new Lucide icons (sparkles, sliders-horizontal,
  blocks, layers, sun, gauge, cuboid, file-archive, upload, puzzle, grip-vertical, funnel-x, images, …) in `zero.qrc`.
- Technic preview: already fixed and committed in `fe4fab8f9` (display name, author, MC/version, cached details,
  stale answers dropped, inline loading/failure, 43 test cases). Still missing (do it in the Discover Modpacks
  section, not in the old dialog): logo/background artwork, skeleton while loading, installs/runs, feed as "What's new".
- **Work that was running when the session hit its limit (uncommitted, verify before trusting):**
  1. **DONE + committed `635575ec6`** (reviewed, 49/49 ctest + FileSystem 25/25; architect fix: CurseForge with several
     MC versions sends none and filters on the client). Not yet independently audited (audit task pending). Was:
     Task "Content core metadata and filters" in the MAIN checkout (files `launcher/modplatform/ModIndex.*`,
     `ResourceAPI.h`, `modrinth/ModrinthAPI.*`, `ModrinthPackIndex.cpp`, later `flame/*`, new
     `launcher/zero/content/ContentFilter.*`, `tests/ContentMetadata_test.cpp`, `tests/ContentFilter_test.cpp`,
     `tests/testdata/ContentMetadata/`). Goal: new `IndexedPack` fields (downloads, follows, categories, gameVersions,
     loaderNames, gallery, featuredImageUrl, dates, license), `IndexedVersion` fileSize/downloads, `Category::header`,
     categories for modpack/mod/resourcepack/shader on both APIs, details merge keeps search-hit authors, and the
     pure `Zero::Content` filter API (facetsFor, FilterState, chipsFor, withoutChip, clearAll, showEverything,
     needsNewSearch, sortOptions, toSearchArgs, matchesClient, performanceLabel, resolutionsOf). If it is incomplete:
     `git diff` shows how far it got; finish it, run `scripts/zero/build.sh test`, review, commit.
  2. Task "Create Instance redesign" in worktree `.claude/worktrees/agent-a3755eb0ef6f0919a` (branch
     `worktree-agent-a3755eb0ef6f0919a`, from `4936eab82`): `launcher/zero/content/CreateInstanceModel.*` (pure:
     version filters, loader availability from real meta incl. `net.fabricmc.intermediary` for Fabric/Quilt, name
     rules), `launcher/ui/zero/create/CreateInstanceDialog.*` + helpers, MainWindow `addInstance` uses it for New /
     Import (deep links keep `NewInstanceDialog`), dev aid `ZERO_SCREENSHOT_CREATE=<vanilla|modded|modpack|import>`,
     tests `CreateInstanceModel_test` / `ZeroCreateInstance_test`. Workers do not commit: review its diff there
     (`git -C <worktree> diff` + untracked files), bring it over (`git -C <worktree> diff | git apply` + copy new
     files), build + test together, then commit. If the worktree is empty, redo the task from CONTENT.md.
  - Two older worktrees (`agent-a6232f2ff8a9d1a3e`, `agent-a8cfd5f5596cb9bce`, at `4c18e3954`) are leftovers of
    session 6; check they hold nothing needed, then remove them with `git worktree remove`.
  3. Task "Discover kit and Modpacks" **started in the MAIN checkout after `635575ec6`** (uncommitted while running;
     files `launcher/ui/zero/content/**`, `ui/zero/pages/DiscoverPage.*`, MainWindow Discover hooks, Application
     metacache base `ZeroContent` + `ZeroDiscover*` settings, `ZeroSkeleton`/`ZeroReveal`, tests `ZeroDiscover_test`).
     If the session died mid-way: `git status` / `git diff` show the partial state; finish against the description below.
- **Milestone B scope (task 3):** "Discover kit and Modpacks": `ui/zero/content/` kit
  (RemoteImageCache with metacache base `ZeroContent`, ContentToolbar, FilterChipStrip, FilterPanel, ContentReveal
  slide-from-right, skeleton shapes Cards/Gallery/Cinematic), rail page **Discover** after Library (Alt+3; later
  pages shift by one) with sections Modpacks · Mods · Resource Packs · Shaders, Modpacks section (featured strip
  from CurseForge "Featured" sort labelled as such, else "Popular on Modrinth"; cards; details panel with gallery,
  versions, changelog; `ModpackInstallDialog` review → `InstanceImportTask(downloadUrl, true, parent, {pack_id,
  pack_version_id})` like `ModrinthPage::suggestCurrent`, Technic via `TechnicDetailsStore` + Solder/zip tasks;
  "Installed" = `BaseInstance::isManagedPack()` + `getManagedPackType/ID`), then wire Create Instance's
  "Browse all modpacks" and Modrinth/CurseForge/Technic tiles to Discover. Then adversarial review by the orchestrator,
  real-app screenshots, commit, then Milestone C (Mods).
### Session 8 live state (owner rule this mission: **NO commits**, owner commits; HANDOFF/MISSION/CONTENT are protected except this section, which the owner asked for)
Everything below is **uncommitted in the MAIN checkout** on top of `635575ec6`. Worker briefs (self-contained, reusable as-is
with `/savvy-flow`) are in `.claude/zero-briefs/` (untracked). Old worktrees `agent-a3755…` (Create Instance, already
copied into main), `agent-a6232…`, `agent-a8cfd…` (earlier kit attempt, already ported) hold nothing needed any more:
remove them with `git worktree remove --force <path>` once the owner agrees.
- ✅ **Create Instance** brought over from its worktree, reviewed (screenshots both themes), audited (audit A: no
  Critical/High) and fixed (`brief-create-fixes.md`: Modded layout, keyboard-only focus ring, provider-safe
  `toSearchArgs(..., facets)`, `PrismMetaSource` restart guard, remembered kind, Enter/digit keys, name truncation,
  a11y). Tests `CreateInstanceModel`, `ZeroCreateInstance` (17). Known limit: Prism's inner meta entity tasks can still be
  started twice if a list is loading elsewhere (same pattern as the classic dialog; a shared app-level `MetaSource` would
  fix it). CONTENT.md says CurseForge "sends the first" MC version; code sends none when several are selected (spec
  text to update by the owner).
- ✅ **Discover kit** (`launcher/ui/zero/content/`: RemoteImageCache, ContentToolbar, FilterChipStrip, FilterPanel,
  ContentReveal, ContentStates; ZeroSkeleton Cards/Gallery/Cinematic; metacache base `ZeroContent`; test `ZeroContentKit`).
  Visual review pending until the page exists. Not in the kit: rows dimmed to 0.5 during a pending search (page task).
- Last verified totals: **53/53 ctest + FileSystem 25/25** (orchestrator run, after Discover).
- ✅ **Discover page and Modpacks** (`brief-discover.md`): rail Discover after Library (Alt+3; Worlds Alt+4 … Appearance
  Alt+8), only the Modpacks section; `zero/content/ModpackSource*` (Modrinth/CurseForge/Technic adapters, generation
  tokens, `ResultPager` de-duplicates by provider:id), `ui/zero/content/{ModpacksSection,ModpackRows,ContentHeroCard,
  FeaturedStrip,ContentDetailsPanel,ContentGalleryViewer,ContentDraw}`, `ui/zero/pages/DiscoverPage`, settings
  `ZeroDiscoverSection/ModpacksFilter/ModpacksView`, `MainWindow::showDiscover(section, provider)`, palette entries, dev
  aids in SHELL.md, test `ZeroDiscover` (33). Screenshots reviewed by the orchestrator (both themes, 3 sizes, details per
  provider, filters, Technic). Known gaps: Technic never "Installed" (Prism stores no managed pack for it); caption "N
  loaded" (parsers drop the providers' total); Modrinth details make one extra GET for gallery `raw_url`; versions
  beyond 40 behind "Show all". Install/Update only emit `installRequested(item, updateInstanceId)` until the next task.
- ✅ Create Instance → Discover: Modrinth/CurseForge/Technic tiles and "Browse all modpacks" now close the dialog and open
  Discover › Modpacks with that provider (`MainWindow::addInstanceZero`); "More sources" keeps the classic dialog. Not
  clicked in the real app yet.
- ✅ **Modpack install flow** (`brief-install.md`): `zero/content/ModpackInstall.*` (pure `planInstall` /
  `createInstallTask`, checked line by line against Modrinth/Flame/TechnicPage::suggestCurrent + `extractTask`),
  `ui/zero/content/ModpackInstallDialog.*`, `MainWindow::installModpack` (task runs while the dialog lives; toast "Installed
  <name>" + Show in Library), dev aid `ZERO_SCREENSHOT_DISCOVER_INSTALL=1|accept`, test `ZeroModpackInstall` (21). Real
  install verified: Modrinth "Sodium Plus 2.3.7" into a temp data dir → instance with `ManagedPack*` keys, Library shows it,
  Discover card turns Installed + Update. Fixed on the way: stale version pills painted after `deleteLater`.
- ✅ **Audit Discover and Modpacks** (session 8, read-only `savvy-careful`): no Critical. Fixed: High Ctrl+F registered
  twice (ambiguous → dead); Update compared against newest-of-any-type instead of the dialog's default version; "already
  installed" note now says Prism will ask update vs separate instance (creation tasks match by pack name); overview images
  get metacache base `ZeroContent`; Retry also retries failed images, Featured, categories, MC version meta; search text
  percent-encoded for Modrinth/CurseForge (adapter only); description/What's new links open only http/https; install
  dialog Name→Group gap; `m_detailsPending` skew. Open (Low/known): `m_versions`/`m_changelogs` never invalidated after an
  install; FilterPanel rebuild drops a pending change when facets arrive late; primary action chosen by comparing `tr()`
  text; several instances of one pack → only the first counts; CurseForge Name sort hard-coded desc in FlameAPI; Modrinth
  details double GET; gallery viewer no focus trap / alt text; no ZeroContent cache size cap; `ZeroDiscoverSection`
  written but never read; Mods/RP/Shaders segmented control + target instance not built (hidden); caption lacks provider
  total; one-column cards up to 440 px (spec 280-340). No tests drive the real network adapters.
- Milestone B verified at **54/54 ctest + FileSystem 25/25**, then Milestone C started in the same session (all uncommitted).
- **Milestone C (Mods)** — spec `docs/zero/CONTENT-MODS.md` (new; CONTENT.md is owner-protected, fold it in later), Prism
  engine notes with file:line `.claude/zero-briefs/research-mods.md` (download chain, GetModDependenciesTask, ModFolderModel,
  update check tasks, page swap point, modals to avoid). Owner approved the Mods mockup.
  - ✅ C1 generic `ui/zero/content/ContentSection` core (Modpacks is a thin subclass; screenshots byte-identical before/after);
    sources take a `ResourceType`; `setVersionFilter` for target MC/loaders.
  - ✅ C2 `zero/content/ModTarget` (TargetInstance, `chooseTarget`, mod-ready = MC + loader Modrinth/CurseForge accept) +
    `TargetInstanceButton`; setting `ZeroDiscoverTarget`. Orchestrator fix: `targetFromInstance` reloads a lazily-loaded
    PackProfile only when no profile task runs (reload() would abort an online update).
  - ✅ C3 Discover › Mods (`ModsSection`, `ModRows`, `zero/content/InstalledMods`), Modpacks · Mods switcher (reads
    `ZeroDiscoverSection`), target defaults as chips, Installed from packwiz metadata, Add/Installed/Update/running states,
    CurseForge gated by key + loaders, empty state → Create Instance (Modded), palette "Browse mods", dev aid
    `ZERO_SCREENSHOT_DISCOVER_SECTION=mods`. Test `ZeroDiscoverMods` (31).
  - ✅ C4 mod install: `zero/content/ModInstall` (pure), `ModDependencies` (headless `GetModDependenciesTask` after the mods
    model loaded, generation tokens, abort on close), `ui/zero/content/ModInstallDialog` + shared `InstallDialogParts`,
    `MainWindow::installMod` (ConcurrentTask of `ResourceDownloadTask`s, toast + "Show mods"). Test `ZeroModInstall` (22).
    Real install verified: Iris Shaders + required Sodium into a fake Fabric 1.21.1 instance (jars + `.index/*.pw.toml`),
    section shows Installed live.
  - Known gaps: no "Show all versions" toggle; "Update available" facet hidden until the installed manager exists; mod deps
    Prism drops on a failed lookup are invisible; with the details panel open the sort combo clips ("Sort: Rele") and the
    target control collapses to its icon; single-column cards up to 560 px (spec 420); running-target and CurseForge-on-Mods
    states only unit-tested.
  - ✅ **C5a Installed mods page** (session 9 verified it): `ui/zero/instance/{ModsPage,ModsList,ModsDialogs}`, pure
    `zero/mods/InstalledModsModel`, swap in `InstancePageProvider.h`; base `setResourceEnabled` only, Remove = `FS::trash` per
    file + `deleteMetadata` only for trashed files. Orchestrator fix: re-read the running state after the enable/disable
    question (Prism's "game is running" modal was reachable if the game started while it was open). Real-app screenshots
    both themes (rows, selection bar, running banner, empty state). Docs: SHELL.md "Instance window › Mods page", filemap.
  - ✅ **C5b Updates** (session 9, `brief-c5b-updates.md`, `savvy-careful`): `ui/zero/instance/ModsUpdates` (`ModUpdateCheck`
    seam, `PrismModUpdateCheck` headless Modrinth + CurseForge checks, `applyUpdatesWithPrism`), Check for updates → "Update all
    N", Updates segment, accent "Update → x" pills, row/selection Update, "no provider info" line, abort on model reload /
    close / game start (checked resources kept alive by `Resource::Ptr` copies; generation tokens). Shared widgets gained
    `PillTag::Tone::Accent` and `SegmentedControl::setOptions`. Dev aid `ZERO_SCREENSHOT_MODS_CHECK=1`. Real check verified
    against Modrinth (Mod Menu 7.2.1 → 11.0.5 in the test instance). Orchestrator fixes: toolbar split into toolbar + status row
    (it forced the instance window to 1389 px at 1280 and 1142 at 1100; now fits 1100 in both themes); flaky
    `theRowMenuOffersTheUpdateFirst` (popup closed under parallel GUI tests) now tests `ModsPage::fillRowMenu` directly.
    Not verified: a real update APPLY (download + replace) was never run in the app; failure reporting uses Prism's non-modal
    error boxes like `installMod`; Discover's "Update available" facet stays hidden (optional per spec).
  - ✅ **C6 Dependency Inspector** (session 9; owner approved the mockup; spec CONTENT-MODS.md "Dependency Inspector";
    brief `brief-c6-dependency-inspector.md`, `savvy-careful` + one review round): `zero/mods/ModResolver` (rules now shared
    with ModHealth: provider index incl. nested jars / provides, environment ids, predicates), pure `DependencyInspector`,
    async `DependencyScanner` (parse cache by path+size+mtime, generation token, 300 ms debounce after each reload),
    `ui/zero/instance/ModDependencyPanel` (right panel: Needs / Game / Optional / Needed by / Conflicts; Enable through the
    page's normal toggle path; Find → `MainWindow::showDiscoverMods(id, query)`), row pills "Needs X" / "Needs X · disabled" /
    "Wrong X version" / "Conflicts with X", Issues + caption "N need attention". Review fixes: black scroll viewport, icon
    close button, bundled jars' deps only when they are a problem (no duplicates), not-installed conflicts summarised in one
    caption, Fabric ">=1.19.4-" evaluated in the shared `Versions` (ModHealth benefits too). Dev aid
    `ZERO_SCREENSHOT_MODS_INSPECT=<mod name>`. Tests `DependencyInspector` (31), `ZeroModsPage` (83), ModHealth (175).
    Known: Prism's own "Not for 1.21.1" pill (from provider metadata) can disagree with the jar's declared range shown in
    the panel (Mod Menu 7.2.1); Find opens a Discover search by mod id (not by packwiz project id).
- Last verified totals (session 9, after D1): **60/60 ctest + FileSystem 25/25**.
- **Milestone D (Resource Packs)** — spec `docs/zero/CONTENT-PACKS.md` (new, written while the owner was away: **design not
  reviewed by the owner yet**, decisions recorded there), Prism facts `.claude/zero-briefs/research-resourcepacks.md`
  (options.txt semantics marked UNVERIFIED there; spec assumes array order bottom → top, last = highest priority).
  - ✅ **D1 Discover › Resource Packs** (`brief-d1-discover-packs.md`, `savvy-medium`): third segment in the switcher,
    `PacksSection` = `ModsSection` with `kind` (target = any instance with an MC version), `PackRows` gallery cards
    (artwork, resolution pills), pack mode of `ModInstallDialog` (`env.packs`, no dependencies),
    `MainWindow::installResourcePack` / `showDiscoverPacks(id, query)`, palette "Browse resource packs", setting
    `ZeroDiscoverResourcePacksFilter`, test `ZeroDiscoverPacks` (21). Real screenshots reviewed (Modrinth results, add
    dialog). Orchestrator fix: `SegmentedControl::sizeHint` now = n × widest label (segments are equal width; the sum cut
    "Resource P…" in the top bar). Add is refused while the target runs (same as Mods).
  - ✅ **D2 Installed resource packs** (`brief-d2-installed-packs.md`; worker code from session 9, finished and reviewed in
    session 10): `zero/content/PackOrder` (options.txt order core + `.zero/pack-order.json` history/Undo), pure
    `zero/content/InstalledPacks`, `ui/zero/instance/{ResourcePacksPage,PackList}`, swap in `InstancePageProvider.h` (id
    `"resourcepacks"`), "Turn it on" in the D1 add dialog (`Packs::turnOnAtTop`), tests `PackOrder` / `ZeroResourcePacksPage`.
    Session 10: it did not compile (`slots` Qt macro as a variable name, ambiguous `Outcome` in the test); 3 test bugs fixed
    (greedy `"\xBFa"` hex escape, Remove test picked a header row, empty-state test read the icon label); page subtitle in
    `ZeroPageHelp.cpp` no longer says "turn them on in the game's Options"; docs SHELL.md "Instance window › Resource packs page"
    + filemap. Review: PackOrder byte-exact/atomic/history-before-write verified by reading; Remove never deletes for good.
    Screenshots reviewed (obsidian, minecraft, 1100x700, running). Owner QA (session 10): toggle, move, Unhide, Remove,
    Discover "Turn it on" download work. Fixed from QA: the classic page dialog was blank (the `ResourcePackPage` QMainWindow was
    made with a parent, so it was its own hidden window; now made without one and adopted by the layout). Still to QA: Undo
    (toast + "Restore previous order"), classic dialog after the fix. Owner did not find the borderless "…" overflow button
    (same on the Mods page): visibility to decide. Running lock only via screenshot + tests (the fake instance can't launch:
    "Null jar"). Totals **62/62 ctest + FileSystem 25/25**.
  - Test data ready for D2 screenshots: instance `packs-1211` ("Pack Test") in the session-9 data dir below: 3 zips (formats
    34/34/15), folder pack "Clean GUI", hidden `Low Fire.zip.disabled`, CRLF `options.txt` with
    `["vanilla","fabric","file/Stay True.zip","file/Faithful 32x.zip"]`. Shot: `ZERO_SCREENSHOT_INSTANCE_PAGE=packs-1211:resourcepacks`.
  - ⬜ D3 pack update checks (with E). Then **Milestone E (Shaders)**: research notes in research-resourcepacks.md section E.
- **Owner QA pending** (needs real input): Mods page (toggle, Remove → Recycle Bin, a real update apply, Find, Dependency
  panel Enable), Discover › Resource Packs add, and later the packs order page.
- Session 9 screenshots + temp data dir `data`: `%LOCALAPPDATA%\Temp\claude\2a40c03b5f24bee0\b9b3752a-6775-4c00-8a35-cf3101b87464\scratchpad\`
  (`modspage-*`, `modsupd-*`, `deps2-*`, `packs*`). Temporary: may be gone.
- ✅ Mods Discover and Installed: done (Milestone C above).
- Screenshots of this session: `%LOCALAPPDATA%\Temp\claude\9c55b98af98b92a0\f24cd95f-218a-43c8-ab17-29d4f1593af4\scratchpad\`
  (`create-*`, `fix-*`, later `disc-*`, `inst-*`; temp data dir `data7`). Temporary: may be gone.

- **Owner roadmap after D2 QA (session 10, 2026-11-12; owner approved, in this order; owner still commits):**
  1. ✅ Polish (session 10): Cat controls hidden in Prism's `AppearanceWidget` + `actionCAT` never shown (the cat was already
     never painted); `star.svg` widened 1.1× (path transform); primary split `QToolButton` styled in `obsidian.qss`
     (`::menu-button` 32 px, `@accentPressed@` divider, `::menu-arrow` chevron-down onAccent). Screenshots both themes; 62/62.
     Owner to check: Settings › Appearance has no Cat, star width.
  2. ✅ **E1 Discover › Shaders** (session 10, `brief-e1-discover-shaders.md`, `savvy-medium`): `ShadersSection` (= `ModsSection`
     `Kind::Shader`; `isPacks()` became `isFolderKind()` / `isShaders()` / `needsLoader()`), 16:9 `PackRows` cards with Modrinth
     performance label + platform pills, Platform default chip from the target's shader mod, pure `zero/content/ShaderMod`
     (`mods` file names → Iris/Oculus/OptiFine) + `ShaderModNotice` strip ("Find Iris" / "Find Oculus" → Discover › Mods),
     `MainWindow::installFolderContent(kind, …)` behind installResourcePack/installShaderPack, setting `ZeroDiscoverShadersFilter`,
     palette "Browse shaders", test `ZeroDiscoverShaders` (28). Platform is sent as a Modrinth `categories:` group (Modrinth
     indexes loaders as categories). Orchestrator fix: the 4-segment top-bar switcher was clipped at 1100-1280 px →
     `SegmentedControl::setNaturalWidths(true)` (each segment its own label width) for the Discover switcher only. Screenshots
     reviewed (obsidian, minecraft, 1100x700, details, add dialog, notice, Iris chip). Totals **63/63 + FileSystem 25/25**.
     Not built: filter-panel caption "From the shader's Modrinth tags"; installed Shader Packs page (still Prism's); selecting the
     active shader (game config UNVERIFIED). Still open from this item: **D3 pack update checks**.
  3. ✅ **F1 Install cart** (session 10; owner approved the mockup and chose ONE cart across Mods / Resource Packs / Shaders;
     `brief-f1-install-cart.md`, two `savvy-careful` runs). A: pure `zero/content/InstallCart` + `InstallCartModel` (session-only,
     cap 50) owned by `DiscoverPage`, card check boxes (`checkRect`, hover/focus/always while the cart is not empty; mod grid
     cards top-right after review, gallery top-left), Ctrl+click / Space, details "Add to cart" / "In cart", `SelectionBar`.
     B: pure `zero/content/CartInstall`, `CartReviewDialog` (groups by kind, version combos, skipped rows, ONE dependency run
     for all mods via `DependencyResolver::Request::also`, packs "Turn them on" via `Packs::turnOnManyAtTop` = one history
     entry), `MainWindow::installCart` (one ConcurrentTask into the three folders; only successful downloads leave the cart).
     Test `ZeroInstallCart` (43). Real run (orchestrator): Iris + Mod Menu + Lithium into "Pack Test" → review listed Sodium,
     Fabric API, Text Placeholder API as dependencies, "Add 6 mods" → 6 jars + 6 `.pw.toml`, cards Installed, toast. Totals
     **64/64 + FileSystem 25/25**. Not run for real: a mixed-kind cart (the dev aid ticks one section only; unit-tested) and
     several packs turned on (unit-tested on a temp options.txt). Owner QA: mixed cart by hand.
  4. ✅ **G1 Discover › Worlds** (session 10, `brief-g1-discover-worlds.md`, `savvy-medium`): CurseForge only (`ResourceType::World`
     = classId 17 already in Prism's FlameAPI), fifth segment only with a CurseForge key, `WorldsSection` (no default version
     chip, not cart-eligible, no Installed state), 16:9 cards with categories + "Made for 26.3", world mode of `ModInstallDialog`
     (all files, fit note, no downloadUrl → "Open on CurseForge"), pure `zero/content/WorldInstall`, `MainWindow::installWorld`
     (NetJob with CurseForge hash → Prism's `InstallWorldTask` → temp zip removed always; Show worlds selects it on the rail
     Worlds page), `ZeroSearchPill` min width 240 → 200. Test `ZeroDiscoverWorlds`. Orchestrator fixes: `worldFit` judged only
     the newest listed version (a file listing the target AND a newer one got the warning) → target listed = fits, else newest
     older one, warning only when all are newer; new `defaultWorldFileIndex` preselects the newest file the target can open
     (Oneblock's newest file is for 26.3; the dialog now picks its 1.21.1-compatible file). Real run: Oneblock into "Pack Test"
     → `saves/Oneblock Original v3.0.2` with level.dat, temp folder gone, toast. Screenshots: obsidian 1280, minecraft 1100
     (5 segments fit). Totals **65/65 + FileSystem 25/25**. Not done: worlds in the cart; non-zip archives fail with Prism's
     "Failed to import world."
  5. ⏳ **Theme editor — NEXT (session 10 ended here; investigation + mockup DONE, owner approved, no code yet).**
     Investigation (orchestrator): a theme is the runtime `ThemeSpec` (`launcher/ui/zero/ZeroTokens.h:184`: ~25 colors, radii,
     `corners`, `bevel`, `texture`, `pixelRoles`, density); widgets read `Theme::current()` live; QSS is generated from the spec
     (`ZeroStyle.cpp`, `@token@` placeholders); `ZeroTheme` / `ZeroMinecraftTheme` are registered in `ThemeManager.cpp:149`;
     the window background is painted by `ZeroShell::paintEvent` (`ui/zero/shell/ZeroShell.cpp:46`, the Minecraft texture) with
     pages transparent on top → a background image/video goes there. Prism loads `<data>/themes/<dir>/theme.json` or `*.qss`
     (`ThemeManager.cpp:170-193`): ZERO custom themes use `<data>/themes/<slug>/zero-theme.json` + the copied background file,
     add a ZERO branch there (`ZeroCustomTheme : ZeroTheme`, `baseSpec()` from the JSON).
     **Owner-approved design (mockup)**: Appearance › **Create theme**; custom themes as cards next to Obsidian/Minecraft with
     Edit / Duplicate / Delete (Recycle Bin) / **Export (.zip) / Import (.zip)** (owner said yes; zip-slip safe). Editor = right
     panel 250-320 px: Name, Start from Obsidian|Minecraft (inherits corners/bevel/pixel font), COLORS: Accent (+5 presets),
     Background, Surface, Text → the rest derived (hover/pressed/onAccent by contrast, surfaces stepped from bg) with a
     "Readable / Hard to read" contrast label, "All colors" expands every token; BACKGROUND None|Image|Video, file copied into the
     theme folder, Dim slider, Cards (surface opacity) slider; CORNERS Rounded|Square; Cancel / Save theme. **Live preview = the
     whole app** while editing; Cancel restores the previous theme. **Video plays only on Home and Appearance** (owner choice),
     a still frame elsewhere; always paused when minimized, while a game runs, and with motion off.
     **Qt Multimedia INSTALLED** (session 10, owner approved): `mingw-w64-ucrt-x86_64-qt6-multimedia` 6.12.0-1 (= qt6-base
     6.12.0-1, no installed package upgraded), 52 packages ~96 MB incl. ffmpeg 9 backend (`qt6-multimedia-ffmpeg`); CMake package
     `Qt6Multimedia` is in `C:\msys64\ucrt64\lib\cmake`. Not yet referenced by `launcher/CMakeLists.txt` (task (c) adds it).
     Planned tasks (one worker at a time): (a) `savvy-careful` theme core: pure JSON load/save + color derivation + contrast,
     `ZeroCustomTheme` + ThemeManager branch, ZeroShell image background (cover, dim, card opacity via surface alpha),
     export/import zip, tests; (b) `savvy-careful` editor panel + Appearance cards (specs above), live apply/Cancel, tests;
     (c) video: `QMediaPlayer` + `QVideoSink` frames painted in ZeroShell (no QVideoWidget), play/pause rules above, CMake
     `find_package(Qt6 Multimedia)`, windeployqt note for release. Then screenshots both themes + a custom one, update this file.
     ✅ **(a) done (session 11, `brief-theme-a-core.md`, `savvy-careful`)**: pure `zero/theme/CustomTheme` (zero-theme.json
     format 1, slug, `deriveSpec`, `contrastRatio`, `readability`) + `ThemeArchive` (export, zip-slip-safe import into a new
     slug via a `.zero-import-*` staging folder, `trashTheme` = Recycle Bin only), `ui/zero/ZeroCustomTheme`, ThemeManager
     `loadCustomTheme` / `removeCustomTheme` (dot-folders skipped), ThemeSpec `baseId` / `styleId()` / `backgroundImage` /
     `backgroundDim` / `cardOpacity` / `surfaceSolid()`, QSS emits rgba only for translucent colors (menus, combo popups,
     command palette opaque), ZeroShell cover image + dim with a cached pixmap. Test `CustomTheme` (37). Totals **66/66 +
     FileSystem 25/25**. Screenshots reviewed (image bg Home, custom card on Appearance). Owner mockup shows Accent /
     Background / Text only: `surface` is optional in the JSON (derived when absent). Not verified visually: a real QMenu /
     dialog over the image. Video stored, not painted.
     ✅ **(b) ACCEPTED after round 2** (top preview follows the editor via `ThemeEditorPanel::previewed`, selection ring on the
     edited card / none for new via `AppearancePage::refreshRings`, "All colors" = 19 tokens `allColorsKeys()`, no window-level
     Esc exists so the panel's `WidgetWithChildrenShortcut` Esc is safe; the "A" in the name field was not reproducible).
     `ZeroThemeEditor` now 23. Totals **68/68 + FileSystem 25/25**. Round-1 notes kept below.
     ✅ **(c) video ACCEPTED** (`brief-theme-c-video.md`, `savvy-careful`): Qt6 Multimedia in top CMake + linked to
     `Launcher_logic`; `ui/zero/shell/ZeroVideoBackground` (QMediaPlayer + QVideoSink, no audio output, infinite loop, pure
     `shouldPlay`/`coverRect`, first-frame still, one warning + flat bg on failure, releases the decoder when the theme has no
     video); ZeroShell paints cover + dim, inputs = page, window minimized **or hidden** (orchestrator fix: Hide/Show events),
     `Application::updateAllowedChanged` → `ZeroShell::setGameRunning`, motion; `ThemeSpec::backgroundVideo`; editor Video
     segment (mp4 webm mkv mov, 200 MB cap); `ThemeFiles` video save; windeployqt note in ARCHITECTURE.md (Qt6Multimedia.dll,
     `multimedia/ffmpegmediaplugin.dll`, ffmpeg DLLs). Test `ZeroVideoBackground` (11, real clip via ffmpeg.exe). Totals
     **69/69 + FileSystem 25/25**. Screenshots reviewed (editor with Video). Risk to watch: each video frame repaints the whole
     window (transparent pages) → measure CPU on the owner's PC. Not verified by hand: real minimize, a running game.
     **THEME EDITOR (item 5) DONE.** Owner QA: Create/Edit/Save/Cancel, Delete → Recycle Bin, Duplicate, Export/Import .zip,
     image and video backgrounds, CPU with a video on Home.
     Round-1 record of (b) (session 11): `brief-theme-b-editor.md`, `savvy-careful`. New `zero/theme/ThemeEditorModel`,
     `zero/theme/ThemeFiles` (saveTheme with temp copy + backup + rollback, duplicateTheme), `ui/zero/pages/ThemeEditorPanel`,
     `ui/zero/pages/ThemeLibrary` (save/duplicate/export/import/delete + toasts, ThemeManager via hooks); AppearancePage
     Create theme / Import…, 3-column card grid, "⋯" menu (Edit/Duplicate/Export…/Delete); `Zero::applySpecLive` (live apply
     throttled 120 ms, ~113 ms per apply in Debug); ZeroShell drops the image cache on file mtime change; Save also applies +
     persists on edit; dev aids `ZERO_SCREENSHOT_THEME_EDITOR=new|<id>[,minecraft][,hard][,all]`, `ZERO_SCREENSHOT_THEME_MENU=<id>`.
     Tests `ThemeEditorModel` (22), `ZeroThemeEditor` (22): **68/68 + FileSystem 25/25**. Orchestrator reviewed screenshots
     and ThemeFiles save path (OK). Returned for: top preview must show the edited spec while the panel is open; the edited
     card is the selected one (none for a new theme); "All colors" must not repeat Accent/Background/Text; check Esc clash.
     Save/Delete/Import/Export not clicked in the real app (widget tests only) → owner QA.
     Then (c) video. Owner is away (2026-11-13): decide alone, log every finished piece here immediately.
  6. Safe update: automatic snapshot before mod/modpack updates + one-click rollback.
     Split by the orchestrator (owner away, session 11): **6a mods** (Mods page `applyKeys` → snapshot of the replaced files in
     `<data>/zero/snapshots/<instance>/<stamp>/`, toast Undo + notice strip "Updated N mods · Roll back", retention 5, trash
     only) and **6b modpacks** (later: whole-instance snapshot minus saves/screenshots/logs before Prism's in-place pack update).
     ✅ **6a ACCEPTED** (`brief-6a-safe-mod-update.md`, `savvy-careful`; orchestrator read the whole rollback path):
     pure `zero/mods/ModSnapshot` (create = verified copies of the replaced jars + `.index` tomls + `snapshot.json`; finalize =
     `added` by listing diff, changed by hash, prunes no-op snapshots; rollback = verify all copies → stage in
     `mods/.zero-rollback-N` → trash (Recycle Bin, no fallback) added + occupied paths → rename into place, never overwrite;
     a stopped rollback is finished by clicking again; retention 5), `ui/zero/instance/ModSnapshotNotice` strip ("Updated N
     mods · 2 h ago" + Roll back + ×, disabled "Close the game first" while locked), ModsPage `applyKeys` snapshots first (no
     snapshot → no update + error toast), toast "Updated N mods" + Undo, confirm dialog with old ← new versions. Work runs on
     a pool thread with the page locked. Dev aid `ZERO_SCREENSHOT_MODS_SNAPSHOT=dialog`. Tests `ModSnapshot` (25 cases),
     `ZeroModsPage` +11. Totals **70/70 + FileSystem 25/25**. Screenshots reviewed (strip Minecraft, dialog Obsidian).
     Known/low: a crash mid-rollback leaves `mods/.zero-rollback-N` (may show as a folder entry); an unfinished snapshot
     (crash, no outcome) hides older active ones until retention. Worker left 4 tiny temp test files in the real Recycle Bin.
     NOT verified: a real Modrinth update apply + Undo/Roll back click-through → owner QA.
     ⏳ **6b IN PROGRESS**: `brief-6b-safe-modpack-update.md` → `savvy-careful` (zip snapshot of the instance minus
     saves/screenshots/logs/crash-reports/.cache before `installModpack` update; roll back = staged swap keeping play data +
     playtime, only `ManagedPack*` cfg keys restored; Library details "Roll back update"; toast Undo; retention 2). Resume:
     check `git status` for `PackSnapshot`; if present review + build/test, else re-delegate as-is.
     🔄 6b built (round 1): `WorldBackup` gained `Options` (exclude predicate, fixed name, manifest extra, cancel, progress),
     `readBackupInfo`, `extractBackup`, `renamePath` (worlds unchanged, WorldBackup 70 green); new `zero/backup/PackSnapshot`
     + `PackSnapshotTasks` (pool thread, launch lock); `InstanceList::reloadInstance` + `discoverInstances` skips `.zero-*`;
     MainWindow `saveCopyBeforeModpackUpdate` / `rollBackModpackUpdate`, toast Undo; details panel "Roll back update"
     (`arrow-left` icon); outcome/rolledBack in a `.json` sidecar; snapshot bound to pack id. Totals 71/71 + FileSystem 25/25.
     Returned for: startup recovery of interrupted rollbacks (`.zero-restore-*` / `.zero-replaced-*`), carry over
     symlinks/junctions, own wording for reserved file names. Not verified: real pack update + rollback (network).
     ✅ **6b ACCEPTED after round 2**: `Pack::recoverInterrupted(instancesDir)` runs in the `InstanceList` constructor before the
     first discovery (`// ZERO`): replaced-without-instance → renamed back; play data stuck in `.zero-restore-*` → moved back
     (never overwrites; staging kept + logged `ZERO recovery:` if anything of the player remains); replaced-with-instance →
     Recycle Bin; a trash failure after a successful rollback leaves `.zero-leftover-<id>-<ts>` (never renamed back, so a deleted
     instance is not resurrected). Links/junctions carried over in rollback and recovery (`Hooks::isLink`, real `mklink /J`
     run OK). Own reserved-name text (`WorldBackup::Options::unsafeName`). `PackSnapshot` 48, `WorldBackup` 70. Orchestrator
     re-ran: **71/71 + FileSystem 25/25**. Known/low: if the game recreated `saves` before recovery, the stuck world stays in
     the hidden staging folder (only logged, no UI notice). **ITEM 6 DONE.** Owner QA: real modpack update → Undo / Roll back
     update in Library; real mod update → strip Roll back.
  7. Performance profile: one click installs the right Sodium/Lithium/etc. for version+loader and tunes RAM/Java to the PC.
     ✅ **ITEM 7 DONE (session 11)**, owner approved the proposal as written; `brief-7-performance-profile.md`, `savvy-careful`.
     Pure `zero/perf/PerformanceProfile` (curated table per loader, NeoForge sodium→embeddium fallback, row states incl.
     OptiFine conflict / alternative installed, memory rule, texts) + `PerformanceCatalog` (async seam, Modrinth); dialog
     `ui/zero/perf/PerformanceDialog`; `MainWindow::runInstallPlan` = the download half of `installCart` (cart unchanged) used
     by `boostPerformance`; Library details button "Boost performance" (`gauge`, hidden while running) + palette entry; Java
     "Fix" → Tools › Java Doctor (it has no per-instance focus, lists problems first); dev aids `ZERO_SCREENSHOT_BOOST` /
     `_APPLY`. Slugs checked on Modrinth (all 7 OK). Real Apply into a temp Fabric 1.21.1 instance: 6 mods + Fabric API with
     `.pw.toml`, `OverrideMemory=true MaxMemAlloc=5120 MinMemAlloc=1024`. Tests `PerformanceProfile` (34), `ZeroPerformanceDialog`
     (34). Orchestrator reviewed screenshots (both themes, OptiFine + Java line) and the Apply path, re-ran: **73/73 +
     FileSystem 25/25**. Not verified: Library button / palette / "Show mods" clicked in the real app, a real NeoForge/Forge
     instance, loading/error states on screen (tests only). Owner QA: click Boost performance on a real instance.
     Earlier status:
     (was) ⏸ WAITING FOR OWNER APPROVAL (session 11): investigation + design proposal in
     `.claude/zero-briefs/proposal-7-performance-profile.md` (dialog "Boost performance": curated mods per loader resolved on
     Modrinth, memory rule, Java line via Java Doctor; 3 open questions). Not built on purpose: new screen, owner approves
     mockups first. Next session: show the proposal (mockup), get answers, then delegate the brief draft at its end.
  8. Share instance: export as a code/link; a friend pastes it and gets the same instance.
     ❌ **Share code DROPPED by the owner** (session 11; `proposal-8-share-instance.md` kept only as history). Owner wants
     Prism's three exports kept (zip, Modrinth `.mrpack`, CurseForge) with a better design that explains them.
     ✅ **ITEM 8 DONE (session 11)**, `brief-8-export-chooser.md`, `savvy-medium`: `ui/zero/pages/ExportChooserDialog` (three
     cards with the approved copy, info box, keys 1-3/arrows/Enter/double-click, CurseForge disabled with the reason for
     snapshots, last choice in setting `ZeroExportFormat`); details-panel Export emits `exportRequested(id)` →
     `MainWindow::exportInstanceZero(id)` → Prism's `ExportInstanceDialog` / `ExportPackDialog` (unchanged); Library top bar
     **Import** button (`ui/zero/pages/LibraryImportButton`, → existing `importRequested` = Create Instance on Import);
     dev aid `ZERO_SCREENSHOT_EXPORT`. Test `ZeroExportChooser` (8). Orchestrator fix: the button stayed icon-only at 1100
     (looked like a view toggle) → compact only below 900 px page width, label kept at 1100 (subtitle elides). Screenshots
     reviewed; **74/74 + FileSystem 25/25** (one parallel-run flake of `ZeroModpackInstall`, passed alone and on the rerun).
     Not verified: Continue → real Prism export dialogs and the file they write. Owner QA: export each format and import it.
     Design:
     (was ⏸) **New item 8 = Export chooser, mockup shown**: details-panel Export opens a ZERO dialog with three
     cards (Full copy .zip / Modrinth pack .mrpack / CurseForge pack .zip; each "what it is" + "Best for …"), a "How your
     friend opens it" box (ZERO: New instance › Import or drag onto the window; Prism: Add instance › Import; Modrinth App /
     CurseForge app: their Import), Continue → Prism's existing `ExportInstanceDialog` / `ExportPackDialog`
     (`MainWindow.cpp:2435-2463`; CurseForge card disabled with the reason for snapshots instead of Prism's QMessageBox).
     Import already exists: Create Instance "Import" card (.zip/.mrpack), Home/Library Import, drag-drop (`processURLs`).
  9. **Release LAST**: installer, auto-update, own MSA client ID + CurseForge key; the owner's GitHub repo "ZeroCraft Launcher"
     (originally the Prism fork) gets reworked and made pretty for the release.
     ⏳ **Release PREP started (session 11, owner asked while away; owner tests on return)**. Found: `windeployqt6` present,
     **NSIS not installed** (needs owner OK to install), credentials guard exists (`docs/zero/CREDENTIALS.md`), owner repo =
     `github.com/6402770-sys/ZeroCraft-Launcher` (public fork, no releases), Prism deploy script at `launcher/CMakeLists.txt:1842`.
     9a packaging IN PROGRESS: `brief-9a-packaging.md` → `savvy-careful` (`scripts/zero/package.sh`, Release build in
     `build-release/`, self-contained `dist/` portable zip + SHA256SUMS, Setup.exe when makensis exists, updater pointed at the
     owner repo; verify by running the zip with a minimal PATH). Resume: check for `scripts/zero/package.sh`.
     ✅ **9a packaging ACCEPTED**: `scripts/zero/package.sh` (`--version`, `--preview` default, `--release` needs env
     `ZERO_MSA_CLIENT_ID` / `ZERO_CURSEFORGE_API_KEY`, `--no-installer`, `--no-smoke`) → Release build in `build-release/`,
     Prism's install-time Qt deploy + 77 MSYS2 DLLs by `objdump -p` closure → `dist/stage/`, `dist/ZEROLauncher-0.1.0-Windows-
     Portable.zip` (96 MB) + `SHA256SUMS.txt`; Setup.exe only when makensis exists. CMake: `ZERO_VERSION` override;
     `-mguard=cf` only for Clang (GCC rejected it). Updater: repo `github.com/6402770-sys/ZeroCraft-Launcher`, artifact
     `Windows` (assets must be named `…-Windows-Portable.zip` / `…-Windows-Setup.exe`, nothing else containing "windows").
     NSIS template: multimedia folder, quoted Open-with, URL handlers removed only if still ZERO's. Verified: zip runs on a
     minimal PATH (HTTPS OK, video theme plays: `[qt.multimedia.ffmpeg]` + `ZeroVideoBackground: play`); dev build intact,
     **74/74 + FileSystem 25/25**. Untested: installer (no NSIS), `--release` path, upgrade install, GitHub upload. A branch
     build shows `0.1.0-zero-development`; a release must be built from a commit tagged `X.Y.Z`. The preview zip carries
     Prism's dev credentials: test only, never publish. First non-portable run offers (Yes/No) to COPY Prism data
     (`handleDataMigration`, ZERO-guarded: fresh installs only, source untouched).
     🧪 **OWNER QA RESULTS (session 11, 2026-11-13)** — OK: themes (rest), Boost performance, Export/Import, mod safe update
     (except below), modpack safe update (mostly), Discover + cart (mostly); CPU with a video background 2-3 %.
     To fix: (B1) theme editor: QColorDialog becomes translucent and clipped after picking a color (cardOpacity surface on a
     top-level dialog + live re-apply of the app stylesheet while it is open); (B2) after a mod Roll back, Check for updates
     says all up to date and "2 mods have no provider info and can't be checked" (restored mods lose their metadata link);
     (B3) cart "checking dependencies" sometimes never finishes (closing/reopening the cart works) — intermittent;
     (B4) installed build: Microsoft sign-in via browser fails `invalid_request redirect_uri` (`zerolauncher://oauth/microsoft`
     is not registered for Prism's client ID); the device-code path (microsoft.com/link) works; (B5) once: modpack
     26.3 → 26.2 update errored that mods should be compatible with 26.3; not reproduced since (watch).
     To add (theme editor): (F1) custom launcher icon per theme, (F2) optional video sound, (F3) custom fonts.
     ✅ B4 FIXED by the orchestrator: `MSAStep.cpp` uses the localhost reply handler whenever the client ID is Prism's upstream
     one (`zerolauncher://` redirect is only valid for ZERO's own Azure app; RELEASE.md step 2 now lists both redirect URIs).
     Builds; real sign-in not re-tested (owner). ⏳ IN PROGRESS (parallel): `brief-theme-d-qa.md` (B1 + F1-F3) → `savvy-careful`;
     `brief-cart-deps-hang.md` (B3, DONE above). B2 → `brief-6a-fix-provider-info.md` **DELEGATED to `savvy-careful`, IN
     PROGRESS** (if cut: check `git status` for changes in `ModSnapshot*` / `ModsPage*`, else re-delegate; suspected cause: the snapshot never captured the `mods/.index/*.pw.toml` or the update renamed it) →
     `savvy-careful` once a worker slot frees (max 2 at once). Then rebuild `scripts/zero/package.sh` for the owner.
     ✅ **B3 FIXED** (`savvy-heavy`): root cause in `CartReviewDialog::runDependencyCheck()` — it steps aside while any row
     loads and only MOD rows rescheduled it; a resource pack / shader row finishing (or failing / removed) last left the cart
     on "Checking…" forever (network-order dependent; reopen worked because versions were cached). Proven by a test that
     failed before the fix. Fix: postponed flag + `resumeDependencyCheck()` for pack/shader rows; watchdog 60 s → Failed
     "The check took too long." + Retry. Resolver: whole-run limit 45 s (`kRunTimeoutMs`), destroyed model fails at once,
     `finished`-only task fails, single `finish()` path. Tests: `ZeroInstallCart` +5, new `ZeroModDependencies` (11, incl.
     40-run stress). **75/75 + FileSystem 25/25**, cart/resolver tests 5× green. Not reproduced in the real app (owner re-test).
     ✅ **B2 FIXED** (`savvy-careful`; orchestrator read the diff): not the snapshot (it held jar + toml) but a race — rollback
     restores jars then `.index` tomls; the folder watcher reloads in between, reads the jar without its entry, and Prism's
     `ResourceFolderModel::applyUpdates` kept that metadata-less resource because it only compared the jar mtime. Fix (upstream
     file, `// ZERO`, CRLF kept): a kept resource also counts as changed when its metadata appeared / went away / differs
     (slug, projectId, fileId, hash, filename). Tests: `ZeroModsPage` +3 (mid-way reload, Check for updates finds the updates
     again, entry renamed by the update), `ModSnapshot` +1. **76/76 + FileSystem 25/25.** Not run with real Modrinth in the app.
     ✅ **Theme (d) BUILT, review pending** (`savvy-careful`): B1 not reproduced by the worker; made robust (all top-level dialog
     classes + tooltips opaque via new `@bgSolid@` / `@elevatedSolid@` in obsidian.qss, FilterPanel popup solid, colour dialog
     previews in the row and applies once on OK). F1 icon / F2 sound (QAudioOutput only when on) / F3 font (`ui/zero/ZeroCustomFont`,
     from memory) + JSON keys `icon`, `font`, `background.sound/volume`; archive allows `icon.(png|svg|ico)` ≤2 MB, `font.(ttf|otf)`
     ≤20 MB; new test `ZeroThemeAssets`. Screenshots: scratchpad `qa-d/shots/` (b1-after-dialog, f-icon, icon-window-screen,
     f-font-final, f-video) — NOT yet reviewed by the orchestrator.
     🔴 **OPEN CRASH (session 11 end, owner saw it)**: while running the suite, `LauncherTests.exe` (ZeroModsPage test window:
     Alpha/Beta/Gamma, strip "Updated 1 mod · just now", "1 mod has no provider info") died with Windows "excepción con error
     inmediato" (fail-fast, e.g. abort / stack-buffer / invalid parameter). Workers also reported one `ResourceFolderModel`
     segfault in a parallel run. Suspects: the B2 change in `ResourceFolderModel::applyUpdates` (metadata compare on a resource
     whose metadata pointer is touched concurrently by the folder watcher / parse task) or the new B2 page tests (slowed rename
     hook + real watcher). **Next session first:** `cd build; ctest -R "ZeroModsPage|ResourceFolderModel" --repeat until-fail:10`
     (Git Bash with `export PATH=/c/msys64/ucrt64/bin:$PATH`), get the failing test name + gdb backtrace
     (`gdb -batch -ex run -ex bt LauncherTests.exe`-style on that test binary), fix (likely `savvy-heavy`), then full suite.
     After that: review theme (d) screenshots, `scripts/zero/package.sh`, tell the owner what to re-test.
     Narrowed (orchestrator, last run): full suite 76/76 + FileSystem 25/25 once; then `ctest -R "^ResourceFolderModel$|^ZeroModsPage$"`
     ×8 → ResourceFolderModel always green, **ZeroModsPage crashed 1 of 8 (~15.7 s)** → the flaky crash is in the ZeroModsPage
     test (most likely the new B2 rollback tests with the real watcher / slowed rename hook), not in ResourceFolderModel's own test.
     ✅ **CRASH FIXED (session 12, orchestrator)**: test-only bug. gdb run: `QFATAL ... ASSERT failure in QList::at: "index out of
     range"` (qlist.h:514, inline Qt asserts are live in our Debug build → abort → Windows fail-fast 0xc0000602) in
     `undoRollsTheModBack…`: `QTRY_VERIFY(rowAt(rowOf("Alpha")).version == "1.0")` polled while a mid-rollback folder read listed no
     Alpha → `rowAt(-1)`. Fix in `tests/ZeroModsPage_test.cpp`: `Rig::rowNamed` returns an empty `Row` when the mod is not listed,
     the two raw `rowAt(rowOf(...))` calls use it. Rule for future tests: never index `rowAt(rowOf(x))` directly.
     Theme (d) screenshots reviewed by the orchestrator: colour dialog opaque and whole, custom icon in rail + title bar, custom font
     app-wide + preview line, video Sound + Volume rows — accepted.
     Owner (session 12): cannot create the Azure app (Azure refuses prepaid cards). ElyPrismLauncher (`github.com/ElyPrismLauncher/
     Launcher`) ships **Prism's own MSA client ID and CurseForge key unchanged** in CMakeLists.txt (no own Azure app).
     ✅ **Owner decided: releases keep Prism's MSA client ID, like Ely.** `ZERO_RELEASE_BUILD` now only refuses Prism's CurseForge key
     (CurseForge terms require an own key for derivative works) and prints a status line for the MSA ID; `package.sh --release`
     needs only `ZERO_CURSEFORGE_API_KEY` (`ZERO_MSA_CLIENT_ID` optional). Verified: configure ON with defaults fails on CF; with a
     dummy CF key passes. CREDENTIALS.md + RELEASE.md (step 2 optional, step 3 = the one left) updated.
     ✅ **Own CurseForge key received** (owner pasted it in chat, session 12): verified against `api.curseforge.com` (HTTP 200,
     modpack search answers), stored ONLY in `%USERPROFILE%\.zero\curseforge-api-key` (outside the repo, grep-checked: not in any
     repo file). `package.sh --release` reads that file when the env var is unset. Preview packages still use Prism's key. Not yet
     built with `--release` (a release build should come from a tagged commit, owner commits).
     🔎 **Ely.by sign-in, investigated (session 12, owner asked "can users sign in with ely.by?"; not started)**. ElyPrismLauncher
     (GPL-3.0, portable code, keep its headers): `AccountType` gets Ely; `ElyStep` = `MSAStep` subclass (client id
     `Launcher_ELY_CLIENT_ID`, scopes `account_info offline_access minecraft_server_session`, authorize
     `https://account.ely.by/oauth2/v1`, token `https://account.ely.by/api/oauth2/v1/token`), `ElyDeviceCodeStep` (devicecode
     `https://account.ely.by/api/oauth2/v1/devicecode`), `ElyYggdrasilTokenStep` (yggdrasil token = oauth token),
     `MinecraftProfileStepEly` (`https://account.ely.by/api/mojang/services/minecraft/profile`), `ElyLoginDialog`, and at launch
     `ElyPatchTask`: swaps `com.mojang:authlib` for meta `by.ely.authlib` (Ely's patched authlib, skins on any server) or falls back
     to meta `moe.yushi.authlibinjector`. Both meta uids exist only on THEIR meta server (`meta.pineconemc.ru`), not on
     `meta.prismlauncher.org` (404) → ZERO must fetch authlib-injector itself (its own release API) or depend on pineconemc.
     Their Ely client id `pineconemc` is their own registration: ZERO needs its own (free, account.ely.by developer apps).
     Our tree today: `AccountType { MSA, Offline }`, no authlib/yggdrasil support.
     ✅ **Ely.by accounts BUILT + accepted** (owner: "now, before release"; `brief-ely-signin.md`, `savvy-careful`; orchestrator read
     AuthFlow / refresh / session / launch diffs): `AccountType::Ely` (saved as "Ely", token in the msa slot), device-code only
     (`ElyDeviceCodeStep`), own `ElyRefreshStep` (keeps old refresh token, optional `Launcher_ELY_CLIENT_SECRET`),
     `MinecraftProfileStepEly` (sets ownsMinecraft), `zero/auth/{ElyAuth,AuthlibInjector,EnsureAuthlibInjector}` (latest.json +
     SHA-256 verified jar in `<data>/zero/authlib-injector/`, cached fallback offline, `-javaagent:<jar>=ely.by` only for Ely
     sessions), "Add Ely.by" hidden while `Launcher_ELY_CLIENT_ID` is empty (default). Test `ZeroElyAuth` (25, live case behind
     `ZERO_LIVE_NETWORK_TESTS=1`); worker run 77/77 + FileSystem 25/25. NOT verified: real Ely sign-in (owner must register the app,
     RELEASE.md Paso 3b), a real game launch with the agent.
     ✅ **Add account dialog BUILT** (owner approved mockup: step 1 Microsoft/Ely.by/Offline cards, step 2 code + QR + "Open …" button;
     `brief-add-account-dialog.md`, `savvy-careful`; ACCEPTED: orchestrator reviewed choose/Ely/Microsoft/error/offline screenshots in both themes): `launcher/ui/zero/accounts/AddAccountDialog` +
     `AddAccountScreenshot` (docs/zero/ARCHITECTURE.md "Add account dialog"). Wired into Settings > Accounts (one "Add account" action),
     the account menu ("Add account..." above Manage accounts), the setup wizard and `LaunchController` re-login (`run(parent, type)`).
     `ElyLoginDialog` removed, `MSALoginDialog.*` back to upstream (backup of the old Ely mode in the session scratchpad). Test
     `ZeroAddAccount` (31 cases). Dev aid `ZERO_SCREENSHOT_ADD_ACCOUNT=<choose|microsoft|ely|offline|error|success>`. NOT verified: a real
     Microsoft / Ely.by sign-in through the new dialog (needs the owner; Ely.by also needs its client ID). ⏳ **Installer v2** (owner approved animated Minecraft-style mockup;
     own Qt static installer in `installer/`, payload appended, same footprint as NSIS) → `brief-installer-v2.md`, `savvy-careful`,
     RUNNING. `qt6-static` 6.12.0-2 installed with owner OK (3.2 GB, `/c/msys64/ucrt64/qt6-static`).
     ✅ **Installer v2 ACCEPTED** (screenshots reviewed by the orchestrator, one return round): `installer/` standalone CMake project
     (`build.sh dev|release [test]`, README has switches, payload format and the NSIS footprint table), static stub 36 MB (only
     Windows DLLs), Setup.exe 132 MB (NSIS was 96), per-user HKCU footprint identical to `win_install.nsi.in` incl. `/S` `/D=`;
     motion ALWAYS on (owner's Windows has animations off; `ZERO_SETUP_REDUCE_MOTION=1` for tests); uninstall "Also move my worlds
     and settings to the Recycle Bin" (`QFile::moveToTrash`, no permanent fallback; on failure nothing is removed); desktop
     shortcut on by default (NSIS: off). `package.sh` builds it (NSIS fallback when qt6-static missing, `--nsis` forces). Tests 56/56;
     real payload installed with `--target <scratch> --no-system`, 144 hashes identical, launcher starts. NOT run: a real install
     (owner tests it), `WinSystem` registry/shortcut/Recycle Bin code, full `package.sh` end to end. Docs merge into filemap /
     ARCHITECTURE done by the orchestrator.
     ✅ **Session 12 close (orchestrator)**: `scripts/zero/build.sh test` **78/78 + FileSystem 25/25**; `scripts/zero/package.sh` end to
     end OK (ZERO Setup v2 static, payload 96 MB, portable zip smoke OK) → `dist/ZEROLauncher-0.1.0-Windows-Setup.exe` (132 MB) + zip
     (96 MB) + SHA256SUMS. Final Setup.exe checked with `/S --target <scratch> --no-system` (Git Bash needs `MSYS2_ARG_CONV_EXCL='*'`):
     rc 0, 144 files identical to the stage + `.zero-install.json` + `uninstall.exe`. Owner re-test list given: real install of the new
     Setup (animation, Options, shortcuts, Launch ZERO, later Uninstall), Add account dialog (Microsoft sign-in in the installed build,
     Offline), colour dialog, icon, sound, font, cart, mod roll back. Nothing committed (owner commits). Next: owner registers Ely.by
     (RELEASE.md Paso 3b) → set `Launcher_ELY_CLIENT_ID`, real Ely sign-in + game launch with authlib-injector.
     🧪 Owner QA (session 12): new installer works. Microsoft "Open Microsoft sign-in" opens login.live.com with
     `redirect_uri=http://127.0.0.1:<port>/` (localhost flow, as intended); completion not yet reported. Owner can't find where to
     register an Ely.by app (Ely docs: `account.ely.by/dev/applications/new`, SPA, needs Ely login) → decision pending: Ely sign-in by
     email + password via `authserver.ely.by` (no registration needed) vs keep waiting for an own client ID.
     ✅ First run with no account: Prism's setup wizard page "Add Microsoft account" (`LoginWizardPage`) is no longer added;
     `Application::performMainStartupAction` queues `MainWindow::addAccountZero` (Add account dialog, step 1) over the main window
     when no valid account exists and MSA or Ely is available. Verified in the real dev build (fresh `-d` temp dir: "Add account"
     window opens after the temp-folder notice). 78/78 + FileSystem 25/25; `package.sh` rebuilt `dist/` (Setup 132 MB, zip 96 MB).
     ✅ **Session 13**: Ely password work found complete in code; `scripts/zero/build.sh test` **78/78 + FileSystem 25/25**. Owner
       registered ZERO's Ely.by app: client ID **`zerocraft-launcher`** (Website type) is now the `Launcher_ELY_CLIENT_ID` default in
       `CMakeLists.txt`; `package.sh` passes `-ULauncher_ELY_CLIENT_ID` so old caches pick it up; `build/` reconfigured; tests still green.
       Live check: `POST account.ely.by/api/oauth2/v1/devicecode` with it → HTTP 200. Not checked: the token exchange (may need the
       client secret of a Website app — owner says Ely issued none; if the real code sign-in fails at the token step, that is why).
       Orchestrator review of the password work: OK (no password in accounts.json or logs, wiped after the body is built; Net
       layer logs URLs only; tests cover 2FA, texts, refresh refused/offline, prefill, abort). Docs: ARCHITECTURE "Add account
       dialog" + "Ely.by accounts", RELEASE Paso 3b (hecho), filemap, SHELL dev aid states. Screenshots (both themes: choose, ely,
       ely-password, ely-2fa, ely-wrong) reviewed, fine. `package.sh` OK → `dist/` Setup 132 MB + zip 96 MB, smoke OK; Setup
       `/S --target <scratch> --no-system`: rc 0, 144 files identical, exe contains the client ID. Nothing committed (owner commits).
       🧪 Owner QA (session 13): **Ely.by code sign-in works in the installed build** (client ID `zerocraft-launcher`, no secret needed).
       🧪 Owner QA (session 13): Ely.by email+password sign-in (incl. wrong password text), playing with the Ely account (game starts,
       Ely skin shows) and Microsoft sign-in completion in the installed build: **all OK**.
       🧪 Owner QA (session 13): theme editor (colour dialog solid, custom icon, custom font, video sound), mixed install cart (deps
       check finishes) and mod Roll back → Check for updates offers the update again: **all OK**. Session 12/13 QA list closed.
       ✅ **Session 13 release prep**: commits `9a9079f70` (sessions 8-13), `240d7142a` (1.0.0, ZERO issue templates, Prism CI removed,
       bug tracker → repo issues), `0e41cd110` (first-run intro + guided tour, 80/80 + 25/25), `f472db9b6` (README + `docs/readme/`).
       Owner: publish 1.0.0 with Prism's MSA client ID (own one for 1.0.1). `package.sh --release` → `dist/` Setup 132 MB + zip 96 MB +
       SHA256SUMS, smoke OK, silent install 144 files identical. GitHub repo `6402770-sys/ZeroCraft-Launcher` is a fork of Prism at
       `00f216ce5` (our base) → publishing = fast-forward push of `zero-development` to its `develop` + tag `v1.0.0` + Release with the
       three files (`gh`), ONLY on the owner's explicit OK. Intro/tour not yet clicked by a human on a real desktop.
       🚀 **PUBLISHED 2026-11-13 (owner OK)**: `zero-development` pushed as `develop` to remote `zero`
       (https://github.com/6402770-sys/ZeroCraft-Launcher, fast-forward from `00f216ce5`), tag `v1.0.0`, Release "ZERO Launcher 1.0.0"
       (latest) with Setup.exe, Portable.zip, SHA256SUMS (GitHub digests = local). `origin` is still Prism upstream: push to `zero`.
       Follow-ups: own MSA client ID for 1.0.1 (RELEASE.md Paso 2); real click-through of intro/tour; repo description still "Currently
       Developing"; the in-app updater against this repo's releases is untested (first real check = 1.0.1).
       🐞 **Session 14 (tester reports, for 1.0.1)**: (1) **1.0.0 offered "1.0.0" as an update to itself** — root cause: tag
       `v1.0.0`; FlexVer (`launcher/Version.cpp`) compares the text "v" against the numeric "1" and ranks any v-tag newer. Fix:
       `versionFromReleaseTag()` in `updater/prismupdater/GitHubRelease.h` drops a leading v/V before a digit (release parsing +
       `--install-version`), test `Version_test::test_releaseTagVersion`; plus CMake: `ZERO_RELEASE_BUILD` forces refspec
       `refs/heads/stable`, so release builds say "1.0.0", not "1.0.0-zero-development". Verified: rebuilt
       `build-release/zerolauncher_updater.exe --check-only` against the live repo → version "1.0.0", latest "1.0.0", no update.
       ✅ **Tag renamed (owner OK, 2026-11-14)**: tag `1.0.0` pushed to `zero` on the same commit `656e3a130` (via `git mktag`: the
       local `1.0.0` tag is an unrelated upstream PolyMC tag from 2021, left alone), release moved to it (still Latest, 3 assets), `v1.0.0`
       deleted on GitHub and locally. The SHIPPED updater (`dist/stage/ZEROLauncher/zerolauncher_updater.exe --check-only`) now reads
       latest "1.0.0" and offers nothing → the prompt is gone for installed 1.0.0 copies. **Future tags: plain `X.Y.Z`, no "v"**.
       (2) **Some background videos never showed** — not length (30 min / 2 h clips play fine): **10-bit video** (H.264 High 10 always;
       HEVC Main10 / VP9 / AV1 10-bit whenever the PC has no hardware decoder for them) decodes to `Format_YUV420P10`, which
       `QVideoFrame::toImage()` cannot convert; `onFrame()` dropped every null image silently. Fix (`savvy-medium`, reviewed):
       own 10→8-bit converter in `ZeroVideoBackground.cpp` (BT.601/709/2020, limited/full range, 2x decimation above 2560 px) and
       15 null frames with no picture yet → `fail()` with the pixel format in the log. Test `tenBitVideoShowsTheSamePictureAsEightBit`.
       30-clip matrix (codecs, containers, faststart, 4K60, rotation, VFR, audio, cover art) all OK otherwise. Totals **80/80 +
       FileSystem 25/25**; video test also passes with `QT_FFMPEG_DECODING_HW_DEVICE_TYPES=none`. Not done: real-window check with
       a 10-bit clip (owner QA), HDR clips look flat (no tone mapping), 12-bit formats go to the logged `fail()`. Commit `6146b1739`.
       🚀 **1.0.1 PUBLISHED 2026-11-14 (owner: "publicalo directamente")**: `797f78320` version bump (CMakeLists patch 1, README file
       name), `package.sh --release` (smoke OK), `zero-development` pushed as `develop` to `zero`, tag `1.0.1` (via `git mktag`: local
       `1.0.1` is another inherited PolyMC tag), Release "ZERO Launcher 1.0.1" (Latest) with Setup, Portable, SHA256SUMS (GitHub digests
       = local). Verified: new updater reports "1.0.1", no self-update; the INSTALLED 1.0.0 updater (`%LOCALAPPDATA%\Programs\ZEROLauncher`)
       `--check-only` → exit 100, "ZERO Launcher 1.0.1". Untested: the actual in-app download + install step (first real use), 10-bit
       video in a real window. Not done in 1.0.1: own MSA client ID (still Prism's).
       (was: owner QA of the earlier list (colour dialog, icon, sound,
       font, cart, mod roll back).)
     🔴 **Session 12 RESUME HERE** (session hit its limit while a worker ran; nothing committed — owner commits):
     - Owner decided: **Ely.by signs in with email + password** (authserver.ely.by, no app registration; OAuth code path kept only
       when `Launcher_ELY_CLIENT_ID` is set). Brief `.claude/zero-briefs/brief-ely-password.md` (self-contained, binding design)
       was DELEGATED to `savvy-careful` and was RUNNING. Check `git status` / `git diff` for `ElyPassword*` / `ElyAuthserver*` steps,
       `ely-auth` in `AccountData.cpp`, password form in `ui/zero/accounts/AddAccountDialog.*`. If missing or half done: re-delegate
       the brief as-is ("continue from the current files"). Then orchestrator review: password never saved/logged (grep accounts.json
       save code + tests), 2FA path, refresh FAILED_HARD → re-login at the Ely step, screenshots `scratchpad/account-shots/`
       (`ely-password`, `ely-2fa`, `ely-wrong`, `choose` with the Ely card, both themes).
     - Then: `scripts/zero/build.sh test` (last 78/78 + FileSystem 25/25), `scripts/zero/package.sh` (ZERO Setup v2 static,
       needs `/c/msys64/ucrt64/qt6-static`), check the final Setup with `MSYS2_ARG_CONV_EXCL='*' ./dist/…-Setup.exe /S --target <scratch>
       --no-system --plan <file>` (144 files identical to `dist/stage/ZEROLauncher`), update this file.
     - Tell the owner (Spanish, click by click) what to test: Ely.by sign-in with a real Ely account (create one free at ely.by if
       needed) incl. 2FA if they use it, then play an instance with that account (authlib-injector download + `-javaagent`, skins on
       Ely servers); Microsoft sign-in completion in the installed build (they reached login.live.com with the 127.0.0.1 redirect,
       result not reported yet); first run with no account opens Add account directly (done, verify installed); earlier list still
       open: colour dialog, icon, video sound, font, cart, mod roll back. New installer: owner confirmed it works.
     - Briefs of this session (all self-contained): `brief-ely-signin.md`, `brief-installer-v2.md`, `brief-add-account-dialog.md`,
       `brief-ely-password.md` in `.claude/zero-briefs/`.
     Verified session 12: `ctest -R ^ZeroModsPage$ --repeat until-fail:25` green (crashed on run 3 before), full suite **76/76 +
     FileSystem 25/25**, `scripts/zero/package.sh` rebuilt `dist/` (zip 95.8 MB + Setup.exe 96.5 MB + SHA256SUMS, smoke OK) with
     B1-B4 + theme (d). Owner re-tests: colour dialog, custom icon, video sound, custom font, cart, mod roll back, Microsoft sign-in
     in the INSTALLED build.
     **RESUME HERE if the session was cut:** (1) `git status` — theme work shows as changes in `zero/theme/*`,
     `ui/zero/pages/ThemeEditorPanel*`, `ZeroStyle.cpp`, `ZeroVideoBackground*` (new keys `icon`, `font`, `background.sound`);
     cart work in `zero/content/ModDependencies*` / `ui/zero/content/CartReviewDialog*`. (2) If a piece looks half done,
     re-delegate its brief as-is (briefs are self-contained) and tell the worker to continue from the current files.
     (3) `scripts/zero/build.sh test` must be green (last known 74/74 + FileSystem 25/25). (4) Review screenshots, then B2,
     then `scripts/zero/package.sh`, then tell the owner what to re-test (color picker, icon, sound, font, cart, rollback,
     Microsoft sign-in in the installed build).
     ✅ **NSIS installed** (owner OK, session 11: `mingw-w64-ucrt-x86_64-nsis` 3.13-2, `makensis` in ucrt64/bin). `package.sh`
     now builds `dist/ZEROLauncher-0.1.0-Windows-Setup.exe` (96 MB) + the portable zip + SHA256SUMS, smoke test OK. The
     installer was NOT run here on purpose (it takes over the `curseforge://` / `modrinth://` handlers from the owner's real
     Prism, HKCU; uninstall removes them only if still ZERO's). Owner tests it (RELEASE.md step 5 warns about this).
     ✅ `docs/zero/RELEASE.md` written (Spanish, owner step-by-step: NSIS `pacman -S mingw-w64-ucrt-x86_64-nsis` (3.13-2 in
     the sync DB), Azure app + Mojang approval form aka.ms/mce-reviewappid (URGENT: slow), CurseForge console key, version
     (today 0.1.0, CMakeLists.txt:191), signing optional, test the portable zip, publish only on owner OK; repo has Prism's
     `.github/workflows` (build, publish, codeql…) to decide before publishing).
- Investigation notes worth keeping (Prism internals): `ResourceDownloadDialog` only collects
  `ResourceDownloadTask`s, the instance pages run them; resource packs/shaders have no MC/loader filtering today;
  Prism never manages `options.txt` `resourcePacks` order; folder packs can't be disabled; Prism delete = trash
  with a **permanent-delete fallback** (ZERO installed managers must not fall back); `ResourceModel` and the modpack
  pages pop modal boxes on network errors; Modrinth/Flame modpack pages keep the previous pack's install task until
  new versions arrive (stale-task bug Technic already fixed); `getLogo` callbacks only fire for cached logos.

- ✅ **First-run intro** (brief-intro, savvy-careful, not committed): full-window overlay over the main window the first time ZERO opens on a data
  dir, then the Add account dialog. `ui/zero/onboarding/IntroOverlay.*` + `IntroGate.h` (pure gating), setting `ZeroIntroSeen`, hook
  `MainWindow::onboardingIntroFinished()` (the guided tour attaches there). The installer's scene moved into the shared `ui/zero/ZeroScene.*`
  (+ `ZeroMarkData.h`, generated by `installer/tools/gen_mark.py`); installer screenshots are byte-identical. New icon `arrow-right`. Dev aids
  `ZERO_FORCE_INTRO=1`, `ZERO_SCREENSHOT_INTRO=<ms>`. Details: SHELL.md "First-run intro". Tests: `ZeroIntro_test.cpp`.

- ✅ **Guided tour** (brief-tour, savvy-careful, not committed): six-step spotlight tour after the intro (and the Add account dialog).
  `ui/zero/onboarding/TourOverlay.*` + `TourSteps.*` (pure: texts, `placeCard`, palette entry), setting `ZeroTourDone` (also set silently for older data dirs),
  `MainWindow::startGuidedTour()`, palette entry "Take the tour" (`action:take-tour`), `ZeroNavRail::itemWidget`, `LibraryPage::newInstanceButton`.
  Step 3 (New instance) shows Library for that step only. Dev aids `ZERO_FORCE_TOUR=1`, `ZERO_SCREENSHOT_TOUR=<1-6>`. Details: SHELL.md "Guided tour". Tests: `ZeroTour_test.cpp`.
  Real click-through with the mouse is still open for the owner (see QA below).

- ✅ **Modpack install: first decision + Keep my game settings** (not committed): the Discover modpack install dialog starts with "What do you want to do?"
  (New instance / Update an instance cards, `InstallChoiceCard`), Update lists every instance that manages the pack (name, version, last played) and
  has "Keep my game settings" (on); the provider's second prompt ("Similar modpack was found!") is gone: `createInstallTask` calls `setConfirmUpdate(false)`
  and, for a New instance, the new `InstanceTask::setAlwaysCreateNew(true)` (read in `FlameCreationTask` / `ModrinthCreationTask::executeTask`, forwarded by
  `InstanceImportTask`) so the provider does not look for an instance to update; Technic: Update disabled. Keep = `zero/content/ModpackKeep` (options.txt,
  optionsof.txt, optionsshaders.txt, servers.dat, config/iris.properties, config/oculus.properties copied before the task, restored after a successful update
  of the same instance; `MainWindow::installModpack`). Tests: `ZeroModpackInstall_test.cpp`, `ModpackKeep_test.cpp`. Verified in the provider code: an update only
  removes the old pack's own files (old `overrides` / `client-overrides` lists and the old manifest's files); files the user added are not in those lists and stay.

- 🧩 **Session 15 (2026-11-16, owner list, /savvy-flow; nothing committed, 85/85 ctest + FileSystem 25/25)**:
  1. **Resource packs "turn off by themselves"** — root cause is vanilla Minecraft, not ZERO: when the startup resource reload throws (here old
     packs' core shaders using `#moj_import` fail on 26.3: `Couldn't parse GLSL: minecraft:core/entity`), the game logs `Caught error loading
     resourcepacks, removing all selected resourcepacks` and saves `resourcePacks:["vanilla"]` (evidence: owner's `Axas Perfect Pack(1)` logs; in
     that case the effective `core/entity` came from Refined Buckets 2.4.1 overlay `26-1/`, formats 76-99; 26.3 = format 97). Any instance can hit it.
     Fix: `zero/content/PackRollback` (log parse, suspect = top-most pack providing the failing shader at the instance's format; format read from the
     client jar's version.json `pack_version`, table fallback), marker `<instance>/.zero/pack-rollback.json`, detection on `launchEnded`
     (Application.cpp), notice + "Broke the last launch" pill on the Resource Packs page (`PackRollbackNotice`), question before Play
     (`PackRollbackPrompt::askBeforeLaunch` in `Application::launch`, once per marker). Docs: CONTENT-PACKS.md "D3 Pack rollback".
  2. **Smooth wheel scrolling** app-wide: `ui/zero/ZeroSmoothScroll` (viewport wheel filter, glide 200 ms OutCubic, touchpad/Ctrl pass through,
     edges propagate, motion off = instant, QPlainTextEdit by lines; opt-out property `zeroNoSmoothScroll` on FeaturedStrip/FilterChipBar).
  3. **Modpack install: New instance / Update an instance** first + Keep my game settings (entry above). The pre-update copy + Undo already existed.
  4. **Shader Packs page** in ZERO design: `ui/zero/instance/ShaderPacksPage` (reuses ModListView/ModDelegate, shader-mod strip, Recycle Bin remove,
     Check for updates via Prism's dialog in the overflow menu); Change version / Lock updates hidden (no real equivalent).
  5. **UI sounds**: `ui/zero/ZeroSound` (QSoundEffect pool, throttle, app-wide button filter, toasts, rail/page nav, Play), original WAVs from
     `scripts/zero/gen-ui-sounds.py` (`retro` for the Minecraft theme, `soft` for others), settings `ZeroUiSounds` (on) / `ZeroUiSoundVolume` (35)
     in Appearance, Theme Studio "Sounds" section (style + per-event WAV replace, travels in theme export).
  Owner QA still needed (real app, by ear/eye): sounds in both themes, smooth scroll feel, modpack dialog with a real update, Shader Packs page,
  rollback notice + Play question on an instance that rolled back. Owner declined "choose active shader from the launcher".

## Mission 004 — 18 owner-approved ideas (2026-11-16, session 15; owner: "hacé todas")
Checkpoint before it: `54c532039` (session 15 work). Decisions: Spanish = **neutral Spanish** ("tú"); Discord = build it hidden until the
owner sends the Application ID (public); own MSA client ID = **won't do** (owner limitation, keep Prism's). Batches (one/two workers at a time):
- **B1 quick wins**: (9) tray icon + "While playing: keep open / hide to tray (default) / close", tray menu with favorites; (15) "Now playing"
  card on Home (elapsed, game RAM when the PID is known, Screenshots/Folder/Stop); (16) end-of-session summary (duration, screenshots taken,
  backups made); (12) quick join servers from the palette + favorite servers strip on Home; (13) Storage "Largest items" + scan progress.
- **B2 play safety**: (5) pre-launch check (duplicate mods, wrong loader/version, missing deps, pack that broke the last launch) with fix
  buttons; (6) Java arguments in Boost performance (known-safe flags by RAM + Java version, explained); (18) malware scanner for mods
  (known signatures, e.g. fractureiser) before launch.
- **B3 updates & downloads**: (2) "Updates for everything" on Home across instances, update all with snapshots; (3) Downloads center
  panel (progress, speed, cancel, history); (4) smart drag & drop of any content file onto the window → detect type → choose instance.
- **B4 history & extras**: (14) playtime stats; (10) scheduled world backups + cloud folder (OneDrive/Google Drive) destination;
  (7) "Try it in a copy" (sandbox duplicate, bring changes back); (11) per-instance change timeline with undo; (17) per-instance video background (later removed by the owner).
- **B5**: (8) Discord Rich Presence (hidden until ID); (1) full Spanish translation + language picker (lifts English-only mode).
Status per batch is logged below as it lands.
- ✅ **B1 done** (89/89 + FileSystem 25/25): tray `ui/zero/ZeroTray` + setting `ZeroWhilePlaying` (tray default; supersedes/syncs Prism
  `CloseAfterLaunch`; LauncherPage radios), logic in `zero/session/` (PlayTracker, SessionSummary + `<data>/zero/last-session.json`,
  WhilePlaying, ProcessMemory); Home `NowPlayingCard`, `LastSessionCard`, `HomeServersSection` (`zero/servers/HomeServers`); palette server
  entries (`ui/zero/palette/ServerCommands`, async `ServerVaultScanner`, join = ServersPage::playOn); Storage scan progress + "Largest items"
  (StorageScanner `findLargeItems`/`aggregateLargest`). Not runtime-tested: tray with a real game launch/quit, Home Servers with real favorites.
  Old-logs clean keeps its existing 30-day rule. Now-playing Stop = same kill as the hero (no confirm, as before).
- ✅ **B2 done** (session 16; 92/92 + FileSystem 25/25). **Java arguments**: pure `zero/perf/JvmArgsAdvisor` + "JAVA ARGUMENTS" in
  `ui/zero/perf/PerformanceDialog` (per-instance override only, Undo toast in MainWindow). Session 16 fixed a real bug there: the checkbox
  listened to `toggled`, which fires before `clicked`, so a refresh put the box back to its default (only `clicked` now); 2 tests answered
  the resolver too late (fixed). **Before you play**: pure `zero/prelaunch/PreLaunchCheck` + `zero/security/` (JarScanner, ScanCache
  `<instanceRoot>/.zero/scan-cache.json`, MalwareSignatures from qrc `:/zero/security/malware-signatures.json`, SystemIndicators read-only),
  UI `ui/zero/prelaunch/PreLaunchDialog` (`checkBeforeLaunch` replaces `askBeforeLaunch` in `Application::launch`; pack rollback is a row).
  Settings `ZeroPreLaunchCheck` (global, Settings › Launcher "Before you play" group, + "Check them again" for opted-out instances) and
  `ZeroSkipPreLaunchCheck` (per instance; skips Mods/Packs, never malware). All 27 IOCs were re-checked by the orchestrator against
  fractureiser `docs/tech.md` and Check Point's Stargazers report. Cached launch with nothing found opens no dialog (~1 ms, no jar opened).
  Tests: `PreLaunchCheck_test` (44, synthetic jars), `ZeroPreLaunchDialog_test` (32; `ZERO_PRELAUNCH_SHOTS=<dir>` saves PNGs).
  Limits / open: only jars directly in `mods/` are scanned (no version subfolders); a fix's recheck runs on the UI thread (brief freeze on
  huge packs); a scan whose dialog was cancelled keeps running and the next Play of that instance waits for it; Minecraft theme look and
  the real Play path not driven by hand (owner QA below). Docs: `docs/zero/ARCHITECTURE.md` "Before you play".
- ✅ **B3 done** (session 16; 98/98 + FileSystem 25/25; dev build starts clean). Owner chose **real background downloads**.
  **Drag & drop**: `ui/zero/shell/DropOverlay` (whole-window "Drop to add"), all local files of one drop → one "Add N files" dialog
  (`ui/zero/content/DropAddDialog`, pure `zero/content/DropPlan`: kind, fit captions from jar metadata, replace caption, data packs not
  installable); modpack archives and remote links keep the upstream path; setting `ZeroDropTarget`. **Downloads center**:
  `zero/downloads/DownloadCenter` (`APPLICATION->zeroDownloads()`, history `<data>/zero/downloads.json`, 100 max), top-bar
  `DownloadsButton` (hidden until a job/history exists; ring + count) + `DownloadsPanel`, palette "Downloads", `ui/zero/shell/DownloadFlow`
  (`startFlow`: start/failed/warnings toasts). Background now: installMod, resource packs/shaders, worlds, Discover cart, Boost (memory/Java
  args written at Apply, mods in background), Mods page updates (snapshot kept for Undo), Shader Packs page updates. Still modal (by design):
  modpack install/update/rollback, `runModalTask`, upstream remote-link downloads. Guards while an instance is busy: launch, delete, folder
  rename (name only), modpack update/rollback; quit asks "Downloads are still running"; the process outlives the last window until jobs end.
  **Updates for everything**: `zero/updates/UpdateSweep` (`APPLICATION->zeroUpdates()`, sequential checks, skips running/busy/managed
  modpack instances, 24 h auto check 30 s after start while no game runs, `ZeroAutoCheckUpdates`, `<data>/zero/updates.json`), real hooks
  `ui/zero/updates/UpdateSweepSetup` (fresh check at Update, snapshot → background job → finalize), Home `HomeUpdatesSection`, palette
  "Check for updates" / "Update all", Settings › Launcher "Updates". **Mods only**: resource/shader packs have no headless checker
  (Prism's ResourceUpdateDialog is modal). Orchestrator fixes: drop dialog "To For …" label; updates' task list is moved (not copied) to the
  GUI thread so Prism tasks are never destroyed on a pool thread. Not driven by hand: real drops, real installs/updates in the background,
  Minecraft theme look (owner QA below). Tests: DropPlan, ZeroDropAddDialog, DownloadCenter, ZeroDownloadsPanel, UpdateSweep,
  ZeroHomeUpdatesSection (PNG env vars `ZERO_DROP_SHOTS`, `ZERO_DOWNLOADS_SHOTS`, `ZERO_UPDATES_SHOTS`).
- ✅ **B4 done** (session 17; 109/109 + FileSystem 25/25; one local commit for tasks 2-5). Commits: `94378325e` (B2), `8894f9a2d` (B3), `d83115e72` (B4 task 1 Playtime), then the B4 commit. Orchestrator fix in review: the test-copy banner buttons (card = full-width md buttons, instance window = one row with 28 px sm buttons). `ZeroModsPage` was seen failing once under `-j4` load and passing on rerun (flaky, cause unknown). B4 items as planned and done:
  1. ✅ **Playtime stats** (done + reviewed, checkpoint commit; 100/100 + 25/25): pure `zero/history/PlaytimeStats` + Tools › Playtime
     `ui/zero/pages/PlaytimeView` (tiles, 14-day chart, by instance), palette "Playtime", tests `PlaytimeStats_test`,
     `ZeroPlaytimeView_test` (`ZERO_PLAYTIME_SHOTS`). Notes: other-day bars = accent at 55% (no subtle-accent token); "Last played" uses
     `lastLaunch()` (Prism's `lastTimePlayed()` is a duration); "Show all" has no collapse. NEXT = task 2.
  2. ✅ **Scheduled backups + cloud folder** (session 17, done + reviewed, 103/103 + 25/25; uncommitted until the B4 commit): pure `zero/backup/BackupDestination` (cloud detection OneDrive/Google Drive/Dropbox, probe, fallback, `KnownRoots` `<data>/zero/backup-roots.json`), `zero/backup/ScheduledBackup` (`<data>/zero/backup-schedule.json`, 2 min catch-up then hourly), WorldBackupJobs `Destination` (lists/restores/deletes across every known root), Settings › Launcher "World backups" group, settings `ZeroBackupRoot` / `ZeroBackupSchedule`. Not run by hand: the Settings group, a real cloud folder, a real scheduled run. Original plan: setting for a backup root (empty = `<data>/zero/backups`; quick choices
     OneDrive / Google Drive / Dropbox when detected + "Choose folder…", writes under `<chosen>/ZERO Backups/<instanceId>/<world>`, checked
     writable, failure = warn + fall back to default, never silent); listing must show backups from the old AND new roots; schedule
     Off / Every day / Every week for worlds changed since their last backup, only while no game runs, catch-up ~2 min after start;
     UI in Settings › Launcher "World backups" group (After playing checkbox synced with `WorldBackupsSection`, schedule, destination).
     Facts: `zero/backup/WorldBackup.h` (`backupsRoot`, `backupDirFor`, `listBackups`, `applyRetention`), `AutoBackup` built in
     Application.cpp ~1157 with `ZeroAutoBackupWorlds`.
  3. ✅ **Try it in a copy** (session 17, done, 107/107 + 25/25; uncommitted until the B4 commit; the MainWindow orchestration is not covered by tests nor run in the real app yet, so click-QA it): see ARCHITECTURE.md "Test copies" (`zero/sandbox/*`, `ui/zero/sandbox/*`, `ZeroSandboxOf` key, `PackSnapshot` meta `kind`/`label`, Undo = `rollBackModpackUpdate`). Original brief:
     Prism copy of the instance marked `ZeroSandboxOf=<id>`; banner in its Library details / instance
     window "Test copy of X — Bring changes back / Discard"; bring back mods/config/resourcepacks/shaderpacks (worlds optional) after a
     full copy of the original first (reuse `saveCopyBeforeModpackUpdate` / `rollBackModpackUpdate` = Undo).
  4. ✅ **Change timeline with undo** (session 17, done; uncommitted until the B4 commit; the real-app flows are not click-tested yet, so QA them): per-instance "Timeline" page (`ui/zero/instance/TimelinePage`, after Version) over the model `zero/history/ChangeTimeline` (pure; see ARCHITECTURE.md "Change timeline"). It lists mod updates, modpack updates / "bring changes back", resource pack order changes, installs and play sessions in day groups, newest first (100, then "Show more"). Undo only on the newest active entry of each mechanism and it calls the EXISTING flows: `ModsPage::requestRollback`, `ResourcePacksPage::undo`, `MainWindow::rollBackModpackUpdate` (signal hub `TimelineUi::Requests`). Boost / Java-argument changes have no stored history, so they are not listed. Original brief: per-instance "Timeline" page aggregating existing histories (mod snapshots
     `zero/mods/ModSnapshot`, pack order history, pack snapshots, Boost/Java args, Downloads history entries) with Undo where a mechanism
     already exists.
  5. ❌ **Per-instance video background: REMOVED** (session 18, owner decision; was built in session 17). Gone: Library buttons, copy/commit helpers, `ZeroBackgroundVideo` setting, `ZeroShell::chooseVideo` / `setPageVideo` + second player, `Tokens::pageVideoMinDim`, `film.svg`, their tests. Existing `<instance>/.zero/background.*` files are left on disk, unread. The THEME video background is untouched.
- ✅ **B5 done** (session 18; 112/112 + FileSystem 25/25; one local commit). **Mission 004 is complete.**
  - **Language support**: `kEnglishOnly` gone; English / Español picker first in Settings › Launcher (restart via `zero/ZeroRestart`, the successor
    waits for the old pid); first start picks Spanish when Windows is Spanish; Prism's own es translation still downloads as upstream, ZERO's
    `launcher/resources/zero/i18n/zero_es.ts` (lrelease `-nounfinished`, qrc `:/zero/i18n/zero_es.qm`) is installed last and wins; free `tr()`
    helpers became `QCoreApplication::translate("<Context>", …)`. Rules + glossary: `docs/zero/I18N.md` (neutral Latin-American "tú",
    "Agregar" never "Añadir", "equipo" for computer). Workflow: `scripts/zero/i18n-update.sh` (lupdate + counts) and `scripts/zero/i18n-merge.py`
    (check/merge JSON chunks). State: **3,155 finished**, every `Zero::` context done (1,992); the 2,340 unfinished are upstream Prism strings that
    Prism's downloaded es file covers (a fresh offline data dir shows some in English, e.g. Settings › General "User Interface" group).
    New ZERO strings land as unfinished: translate them in the .ts (drop `type="unfinished"`), rerun the script.
  - **Discord Rich Presence** (`zero/discord/DiscordPresence`, ARCHITECTURE.md "Discord Rich Presence"), visible, App ID 1572300937387581450,
    asset key `zero` (art in `program_info/discord/`, `scripts/zero/discord-art.py`). Orchestrator review fixes: details/state kept within Discord's
    2..128 characters (else Discord rejects the whole activity; test `textStaysWithinDiscordLimits`); indent uses `Tokens::spaceXl`; the flaky
    `reconnectsAfterDiscordQuits` was a test race (the fake pipe kept listening, the 80 ms retry reconnected before the not-ready check) — the
    fake now stops listening first; 24 parallel runs pass (was 4/12 failing).
  - Spanish real-app check (screenshots of Home, Library, Tools, Settings at 1280x800, Minecraft theme): fixed the clipped Library "Agregar
    etiqueta" tag field (width now follows the placeholder) and the Home "Carpeta de instancias" caption ("Abrir en el Explorador").
  - **Not done / owner QA**: Rich Presence with the owner's real Discord open (upload `program_info/discord/zero-rich-presence-1024.png` as asset
    `zero` in the Discord developer portal first); click through the language switch + restart; the rest of the app in Spanish (dialogs, Discover,
    instance window) was not looked at screen by screen.
- ✅ **Owner QA round after B5** (session 18; 113/113 + FileSystem 25/25; commits `f33fdfc9e` + the next one). Owner results: Discord RPC,
  language switch, Now playing, pre-launch warning, drag & drop, Downloads center, scheduled backups, Playtime all OK. Fixed:
  - **Spanish everywhere**: all 5,488 strings translated (the ~2,340 upstream Prism strings too; Prism's downloaded es file did not cover them).
    `i18n-merge.py export <ts> <prefix> <n>` writes the unfinished entries as chunks. Glossary: "Browse" file picker = "Examinar".
  - **Test copy "Bring changes back" failed** ("Could not replace mods"): Windows refuses to rename a folder while anything below it is
    watched, and the launcher's own `ResourceFolderModel`s watch `mods/` + `mods/.index`. `SandboxApply` now moves only the differing
    entries inside kept folders (same all-or-nothing + rollback + Recycle Bin). Compact banner in the instance window is vertical (title,
    then two full-width 28 px buttons) and never widens the 252 px column. Shared `ui/zero/ZeroCompactButton.h` (sm buttons with
    `setFixedHeight(28)` were really 18 px because the sm qss resets min-height).
  - **Pack roll back / Undo** had the same Windows problem (whole instance root rename): `PackRollbackTask` pauses the instance's own
    watchers around the swap (`zero/backup/WatchPause`), all other guarantees unchanged. Other holders (Explorer, antivirus, classic
    Screenshots/Servers/Logs pages of an open instance window) still give the safe "could not be replaced, nothing changed" failure.
  - Tray menu: favorites only (hint when none). Before you play: "Move to Recycle Bin" next to "Disable" on mod findings. Timeline page icon.
    Home "Updates" section always visible (up to date / not checked yet + "Check now").
  - **Per-instance background video REMOVED** (owner: not useful); theme video untouched.
  - **JEI "not for 26.x"**: not a version-parsing bug (26.x audited, handled everywhere). The cart reviewed against its target instance
    (26.2) but fetched versions with the Discover section's filter (another instance on 26.3), so JEI was skipped. Cart now asks
    `ModpackSource::versionsFor(target filter)` when the filters differ. Owner has two instances both named "Axas Perfect Pack" (26.2 and
    26.3): the cart bar shows only the name, which is ambiguous (UX idea, not done).
  - **Shader packs page icons**: rows with Modrinth/CurseForge metadata show the project icon (`zero/content/ShaderPackIcons`, one batched
    API call per provider per session, `RemoteImageCache`). Checked against the real Modrinth API; CurseForge shape not checked live.
  - Flaky under parallel load, not investigated: `ZeroResourcePacksPage` segfaulted once (passes alone and on rerun).
  - Owner QA still open: Undo of a modpack update with the instance window open on Mods; bring changes back again; tray favorites; the
    two pre-launch buttons; shader icons on the real instance; the cart with JEI on the 26.2 instance.

## Build & test
- Build: `scripts/zero/build.sh` · Build + tests: `scripts/zero/build.sh test` · Run: `scripts/zero/run-dev.cmd`.
- Result at end of session 4: **45/45 ctest pass** + `FileSystem (without symlink cases): 25 passed, 0 failed`.
  Session 5: same (45/45 + 25/25), with new cases `findingsUseNaturalOrder`, `rowsAreAccessible`,
  `clickableCardIsAnAccessibleButton`, `restoreTrashesTheOldWorldUnderAReadableName`.
- Driving the real app without stealing focus (session 5): Windows UI Automation from PowerShell (`AutomationElement`,
  Invoke / SelectionItem / Value patterns; Qt exposes rail buttons, list items, the palette field and rows, cards) plus
  `PostMessage` WM_LBUTTONDOWN/UP for plain mouse clicks, and `PrintWindow` for captures. Modal dialogs (QMessageBox)
  are separate top-level windows: find them with `EnumWindows` by process id; a UIA call into the main window blocks
  while one is open. Status pings: a tiny local fake status server on 127.0.0.1 answers handshake/status/ping.
- `FileSystem` investigation: 12 of its 19 functions pass and now run in `build.sh test`. The 7 `test_link*` symlink
  cases are not run: on Windows without Developer Mode, symlinks need privileges, so `FS::create_link` launches
  `zerolauncher_filelink.exe` elevated (UAC prompt); the elevated process doesn't inherit the MSYS2 PATH (missing-DLL
  dialogs) and the test hangs. Fix options for later: enable Developer Mode on the test machine, or bundle DLLs next to
  the helper.
- After moving the repo, `build/` had to be reconfigured (CMakeCache pointed at the old path). If that happens again:
  delete `build/CMakeCache.txt build/CMakeFiles build/build.ninja build/.ninja_*` and rerun the build.
- If linking fails with "Permission denied" on `build/zerolauncher.exe`, a dev instance is still running: rename the
  exe (`build/zerolauncher.locked-<n>.exe`) instead of killing the owner's process. Old renamed exes can be deleted
  once no process uses them.
- Screenshot/evidence dev aids (`--zero-screenshot`, `ZERO_SCREENSHOT_*` incl. `_THEME`, `_TRANSITION`, `_THEME_TO`,
  `_LAUNCH_PHASE`, `_PALETTE`, `_SHORTCUTS`) are listed in `docs/zero/SHELL.md`. Always use `-d <temp dir>` and an
  absolute output path. Fake instances must have `instance.cfg` **with a `[General]` header** and a well-formed
  `mmc-pack.json`, otherwise an upstream debug `ASSERT(isRunning() == false)` in `Task.cpp:108` (a
  ComponentUpdateTask started twice) blocks the app. Debug builds only; upstream behaviour, not investigated further.

## Status
| Area | State | Verified by |
|---|---|---|
| Build baseline | ✅ MSYS2 Debug build, helper jars Java 8 | build + tests |
| Identity / data separation / Prism import | ✅ (session 1) | code review; import not run interactively |
| Credentials | ⚠️ dev builds reuse Prism's MSA client ID + CurseForge key; `-DZERO_RELEASE_BUILD=ON` refuses them | CMake guard |
| Theme engine v2 | ✅ `ThemeSpec` v2 (shape, bevel, texture, pixel roles, accent2/highlight), per-theme qss overlay, live switch, persisted via `ApplicationTheme` | `ZeroTheme`, `ZeroStyle` tests; Obsidian pixel-identical before/after |
| **Minecraft Theme** (`zero-minecraft`) | ✅ deepslate/stone/grass/diamond/gold palette, notched panels, bevelled block-press buttons, procedural stone texture, original **ZERO Pixel** font (TrueType built in memory), XP-bar launch sweep, pixel gem toasts | `ZeroPixelFont` test, screenshots `build/zero-v2-zero-minecraft-*.png`, `build/zero-v2-m3-*` |
| Motion system | ✅ `ZeroMotion` setting (system/on/off; **the owner's Windows has "Animation effects" off, so `system` = still: choose On in Appearance**), snapshot transitions (crossfade/rise/pixel dissolve), page rise, nav indicator glide, card lift, press ripple (Obsidian), toast slide/stack glide, theme transition, grid/list + details crossfades, dialog fade-in, native popup fades | `ZeroMotion` test (overlay lifetime, no leftover effects), evidence frames `build/zero-v2-motion-*-t*.png`; hover/press/toast motion **not** seen with real input |
| Play button | ✅ `ZeroPlayButton`: Idle → Launching (sweep / XP bar) → Running (hover = Stop) → failure shake; driven by `Application::launchPhaseChanged/launchEnded` (Preparing at controller start, Running when the game process starts) | `ZeroPlayButton` test, static screenshots; **no real Minecraft launch verified** |
| Appearance page | ✅ live preview of a theme before applying, theme cards, Apply (persists, transition plays), motion Follow Windows/On/Off, link to legacy appearance settings | screenshots |
| Shell v2 | ✅ `ZeroTopBar` (title/subtitle/breadcrumb, page actions, account chip, search pill), `ShellPage` chrome API, 60 px sidebar header (logo + wordmark), Appearance/More/Settings bottom group, Alt+1..7 | `ZeroShell` test, screenshots `build/zero-v2-m4-*.png` |
| Command palette | ✅ Ctrl+K / top-bar pill: instances (↵ Play, Ctrl+↵ show in Library), navigation, real QActions, settings pages, themes, motion; fuzzy ranking in `launcher/zero/palette/CommandMatcher`; shortcut sheet Ctrl+/ (also in More menu). Kill moved to **Ctrl+Shift+K**; Settings = **Ctrl+,** | `CommandMatcher`, `ZeroCommandPalette` tests, screenshots `build/zero-v2-m5-*.png` |
| **Instance Profiles / Library 2.0** | ✅ `launcher/zero/profiles/` (`InstanceProfile`, `LibraryFilter`), data in `instance.cfg` (`ZeroFavorite`, `ZeroTags`, `ZeroArtwork`) + `<instance>/.zero/artwork.png` written with `QSaveFile`; `FilterChipBar`, `ArtworkCache`, `FlowLayout`; chips filter through `InstanceProxyModel::setPredicate` (groups kept) | `InstanceProfile`, `LibraryFilter`, `ZeroLibrary` tests, screenshots `build/zero-v2-m6-*.png`; **not tried with real input** |
| **Session History** | ✅ one record per launch (played / stopped / crashed + exit code / failed) from `launchPhaseChanged` + `gameStoppedUnexpectedly` + `launchEnded`; JSON written with `QSaveFile`, cap 500, corrupt file moved to `.corrupt`; Home "Recent activity" | `SessionHistory` test, screenshots `build/zero-v2-m7-home-*.png` (hand-written history); **no real launch recorded yet** |
| **Worlds Hub** | ✅ read-only page: every world of every instance (`launcher/zero/worlds/WorldScanner`: level.dat via Prism `World` + `versionName()`/`isHardcore()`, async sizes on QThreadPool with generations, no link/junction following), search, instance filter, sort, details panel (seed copy, open folder, show instance) | `WorldScanner` test (symlink sub-case skipped: needs privileges; junction case runs), screenshots `build/zero-v2-m8-worlds-*.png`, fake worlds' files hashed unchanged after runs; **not tried with real input** |
| **Smart Backup** (worlds) | ✅ `launcher/zero/backup/` (`WorldBackup` pure functions, `WorldBackupJobs` runner): zips in `<data>/zero/backups/<instance>/<world>/` with a SHA-256 manifest, written as `.part` and verified before rename; restore = verify → safety snapshot → extract to `saves/.zero-restore-*` + verify → rename swap with rollback → old world to Recycle Bin; zip-slip rejected; retention 10 manual + 3 safety; refused while the instance runs and launch blocked during a job | `WorldBackup` test (66 rows incl. corruption, zip-slip, failed swap rollback, retention, free space), screenshots `build/zero-v2-m9-backups-*.png`; **restore/delete dialogs, toasts, Recycle Bin and the launch block never clicked through in the app** |
| **Auto backups** | ✅ `Kind::Auto` (keep 5/world): when a session where the game ran ends, worlds whose `level.dat` changed since the game started are backed up one at a time (>2 GB skipped + warned); instance launch-locked meanwhile; global toggle `ZeroAutoBackupWorlds` (default on) at the bottom of the Worlds backups section; one toast per outcome | `AutoBackup` test (real temp worlds + real jobs), `WorldBackup` test, screenshot `build/zero-v2-m10-autobackup.png`; **never triggered by a real game session** |
| **Storage Manager** | ✅ Tools › Storage: total + drive free space, 11 categories (no double counting, links/junctions not followed), per-instance rows (click → Library), Clean up: logs/crash reports > 30 days → Recycle Bin after confirmation, each file re-validated, never a permanent delete; world backups + interrupted-restore folders info only | `StorageScanner` test (28 cases), screenshots `build/zero-v2-m11-*.png`; **confirm dialog / real Recycle Bin move / toasts never clicked through** |
| **Java Doctor** | ✅ Tools › Java Doctor: pure `diagnose()` mirrors `AutoInstallJava` + `VerifyJavaInstall` (pinned → detected → download; configured path; 32-bit + >2 GB; ignored check); requirements read from `mmc-pack.json` + patches/meta files without loading or writing anything; per-instance status pill + "Use Java N" (confirmation, refused while running; writes `OverrideJavaLocation`/`JavaPath`/`AutomaticJava` like Prism's Java page); runtimes list; Crash Detective "wrong Java" links here | `JavaDoctor` test (36 cases), screenshots `build/zero-v2-m12-*.png`; **"Use Java N" never clicked through; no real launch after a change** |
| **Mod Intelligence** | ✅ own read-only parser (fabric/quilt json, mods.toml + neoforge.mods.toml, mcmod.info, manifest, nested jars from memory, depth ≤ 3); tri-state version matching (Fabric predicates, Maven ranges); findings Confirmed/Likely/Possible/Unknown: missing/mismatched deps, MC version, loader version, wrong loader (Connector / NeoForge 1.20.1-1.20.4 lower to Possible), breaks/conflicts, duplicates, unreadable jars; an unreadable jar lowers missing-dep to Possible. Tools › Mod Health with instance picker, Library "Check mods", palette "Check mods". No online/outdated checks | `ModHealth` test (159 cases incl. real zip jars), screenshots `build/zero-v2-m13-*.png`; **never run on a real modpack** |
| **Mod repair actions** | ✅ Confirmed wrong loader / MC version / loader version → "Disable mod…"; Confirmed duplicates → disable all but the unique newest (only when every version compares); preview dialog lists each `x.jar → x.jar.disabled`; never deletes, never overwrites an existing target, refused while running; Undo toast; "Fix N problems…"; Disabled mods card with Enable | `ModRepair` test (22 + 1 skipped symlink case), screenshots `build/zero-v2-m14-*.png`; **dialogs, Undo and Enable never clicked** |
| **Screenshot Gallery** | ✅ `launcher/zero/gallery/ScreenshotScanner` (read-only) + `ScreenshotsPage`: fluid 16:9 grid, thumbnails decoded on the pool at 400 px into an in-memory LRU (300), search / instance filter / sort persisted, in-window viewer (←/→, Esc, Copy, Open, Show in folder, Show instance, Delete → Recycle Bin after confirmation, never a permanent delete) | `ScreenshotScanner` test, screenshots `build/zero-v2-m15-*.png`; **viewer buttons, keys and delete never used with real input** |
| **Server Vault** | ✅ read-only `servers.dat` parser (own, on the bundled nbt lib), address normalization + grouping across instances, `ServerPinger` (McResolver/McClient, 4 in flight, 5 s timeout, session cache, motd flattening), `ServersPage` + details panel, Play on server = Prism's Join | `ServerVault` test (49 cases incl. pinger vs a fake local status server), screenshots `build/zero-v2-m16-servers-*.png`; **real quick-join launch, real servers and menu popups never exercised** |
| Crash Detective | ✅ (session 1) 13 rules; now opened under the Tools breadcrumb | `CrashDetective` test |
| **Default theme + app mark** | ✅ Minecraft default (fresh installs; Obsidian installs move once, an explicit Obsidian afterwards sticks); pixel mark crisp at whole multiples of 16 device px | screenshots, live title bar check |
| **Motion v2** | ✅ on by default; Cascade page enter; ItemReveal (Library grid/list on chip/sort, Worlds/Screenshots/Servers on first fill/filter/sort); thumbnail fade; skeletons (Screenshots, Servers, Storage, Java Doctor, Mod Health); Storage bars; rail icon pop | `ZeroMotion` test (cascade overlay, skeleton sweep, reveal), live frame captures; **feel not reviewed by the owner yet** |
| **Favorites** (worlds, servers) | ✅ star column on rows (click toggles without selecting), details "Add to favorites", favorites first in every sort, "Favorites" toggle filter (`ZeroWorldsFavoritesOnly` / `ZeroServersFavoritesOnly`) | `FavoriteStore` test, live clicks |
| **Settings page** | ✅ rail page after More (Ctrl+,): fresh pages on open, applied on leave/close/quit, emits globalSettingsAboutToOpen/Applied; modal dialog kept for the launch-without-account flow | live: change applied only after leaving; screenshots |
| **Page frame** (`PageContainer`) | ✅ ZERO side list (icon, name, two-line summary), title + explanation, option help strip (tooltip → group text), Cascade between sections; instance window header card + Play/Stop | live hover check in instance Settings |

## Manual QA checklist (owner, real input needed)
- **B4 (session 17)**: Settings › Launcher › World backups (schedule, "Save backups to" with OneDrive/Google Drive/Dropbox, a folder you then disconnect → warning + backup in the ZERO folder; old backups still listed in Worlds); Library › "Try it in a copy" → change mods in the copy → "Bring changes back" (with and without Worlds) → Undo toast; "Discard copy"; instance window › Timeline (Undo of a mod update, a pack order change, a modpack update); Library › "Set background video…" → Home plays it muted behind the hero (legibility of the 0.55 scrim), stops when leaving Home / while playing / with reduced motion.
Start `scripts/zero/run-dev.cmd`. For animations: Appearance → Motion → **On** (Windows animation effects are off here).
- [ ] **B3**: drag 2 mods + 1 resource pack from Explorer onto the window → "Drop to add" overlay → "Add 3 files" → Add → toast "Open".
      Discover: add a mod → toast "Downloading…", top-bar download button with ring → panel shows progress/speed, Cancel works; while
      it downloads, Play of that instance says "Downloads in progress"; close the launcher → "Downloads are still running". Home › Updates
      (needs an instance with outdated Modrinth mods): Check again → Update → Mods page offers Undo.
- [ ] **Before you play** (B2): put a harmless copy of a mod in an instance's `mods` twice (`a.jar`, `b.jar`) → Play shows "Before you play"
      with "Two copies of the same mod" → disable one → Play. Play again: no dialog, no delay. Tick "Don't check this instance again" → Play →
      Settings › Launcher shows "1 instance skips…" → "Check them again". Minecraft theme look. Boost performance: "Use recommended Java
      arguments" ticks/unticks, Apply → toast Undo restores the old arguments.
- [ ] **Guided tour**: on a fresh data dir (`-d <empty folder outside Temp>`): intro → Get started → close Add account → the tour starts on Home; Next / Back / Esc / Enter / Right / Left work, the spotlight glides, step 3 shows Library and
      then comes back to Home, Finish shows "You're all set", Skip shows nothing; restart: no tour. Ctrl+K → "Take the tour" starts it again from step 1. Check both themes and a 1100 x 700 window.
- [ ] Session-1 items still open: Prism import, Microsoft login/account switch, create vanilla + Fabric instance,
      rename/delete+Undo/groups/drag-and-drop/copy/export/shortcut/icon, context menu, settings dialog under Obsidian
- [ ] **Real launch**: Play shows Launching (sweep; XP bar in Minecraft) → Running when the game window appears →
      hover shows Stop → Stop kills (with confirmation). A failing launch shakes and shows "Launch failed"
- [ ] Theme switch from Appearance (preview first, then Apply): Obsidian → Minecraft dissolves in blocks, back crossfades;
      choice survives a restart; Prism's settings dialog theme combo stays in sync
- [ ] Motion On: page rise, nav pill glide, card lift on hover, ripple on press (Obsidian), toasts slide/stack, grid↔list
      crossfade, dialog fade-in — check for flicker (dialog fade can be removed in `fadeDialogIn`, `ZeroTheme.cpp`)
- [ ] Ctrl+K palette: type, arrows, ↵ launches, Ctrl+↵ shows in Library, Esc returns focus; Ctrl+/ sheet; Alt+1..7;
      Ctrl+Shift+K kills; Ctrl+, opens settings
- [ ] Top bar: Library actions (grid/list, New instance), search/sort row, account menu opens below the chip
- [ ] Crash Detective: force a crash → toast → Diagnose → breadcrumb "Tools › Crash Detective" → back via "Tools"
- [ ] Library 2.0: star on card hover toggles favorite without selecting; chips (Favorites / Running / loader / #tag) filter
      grid and list, clicking the active chip returns to All; add/remove tags (Enter, Esc, Backspace), description survives
      switching instance and restart; Set artwork… (png/jpg/webp) shows on card, details and Home hero; Remove artwork
- [ ] Session History: launch, play, quit → Home "Recent activity" shows "Played for …"; Stop from the launcher → "Stopped
      after …"; a crash → red row with exit code + Diagnose opens Crash Detective; row click opens the instance in Library
- [ ] Worlds: Alt+3 opens it; worlds of real instances listed with icon/version/size; search, instance filter, sort
      persist; Ctrl+F / Esc; Seed copy toast; Open folder; Show instance in Library; Refresh; nothing inside saves changes
- [ ] Smart Backup (use a throwaway world first!): Back up now → toast + row; Restore → confirm → world replaced, a
      Safety row appears, the game opens the restored world; Delete → Recycle Bin; buttons disabled while the game runs;
      Play blocked with a message while a backup runs (large world); leftover `saves/.zero-*` folders only after a crash
- [ ] Auto backups: play a world, quit → toast "Backed up 1 world(s) of …", an **Auto** row in Worlds; untouched worlds get
      none; Play is blocked while it runs; unticking "Back up automatically after playing" stops it
- [ ] Storage (Tools › Storage or Ctrl+K "Open Storage"): numbers look plausible vs Explorer; instance row opens Library;
      Move old logs to Recycle Bin → confirm → toast, files in the Recycle Bin, list rescans; Rescan button
- [ ] Java Doctor (Tools › Java Doctor): statuses match what really happens at launch (try an instance with a wrong Java);
      "Use Java N" → confirm → instance settings show the new path; Java settings button opens the Java page
- [ ] Mod Health on a real modpack (Fabric and Forge/NeoForge): no false Confirmed findings; try removing a dependency
      (e.g. fabric-api) → Confirmed missing; Library "Check mods" opens it for that instance
- [ ] Mod repair: "Disable mod…" → preview → file renamed to `.disabled` → Undo restores it; "Fix N problems…";
      Enable in "Disabled mods"; refused while the game runs; Prism's Mods page agrees with the new state
- [ ] Screenshots (Alt+4): thumbnails of real screenshots, filter/sort persist; viewer ←/→/Esc, Copy, Open, Delete → Recycle Bin
- [ ] Servers (Alt+5): servers of real instances grouped across instances; "Responded" / "No response" pills (and the note that a response is not availability); sort by Players / Latency; **Play on server** launches the instance and joins (with several instances: menu; disabled while the instance runs); Copy address toast; Refresh re-pings; a real quick join was never run
- [ ] Window < 1200 px collapses the rail; 1100×700 minimum
- [ ] Session 6: the Minecraft theme is on after updating; title bar/taskbar show the pixel mark, Obsidian shows the old one
- [ ] Animations feel right (page cascade, list entrances, thumbnail fades, skeletons while loading); Appearance → Motion Off stops them
- [ ] Gallery never blinks any more (the original blink was not reproduced here: watch for it with many screenshots)
- [ ] Worlds/Servers: star a row, it moves to the top; "Favorites" shows only them; the details button follows
- [ ] Settings (rail, Ctrl+,): sections explain themselves, the help strip follows the pointer, changes apply when you leave
- [ ] Instance window: header card, sections with summaries, help strip in its Settings page, Play / Stop / Close

## Known limitations / open issues
- Parallel-test flakes (session 6) fixed in session 7, both reproduced with 6 parallel copies of one test:
  `ZeroCommandPalette` checked `hasFocus()`, which needs the OS to activate the window (refused under parallel GUI
  tests); it now checks the window's focus widget, and the overlays remember the host window's focus, not the app's.
  `WorldBackup`: `QDir::mkpath` on Windows fails for the thread that loses the race to create a shared parent, so two
  first backups at once failed ("Could not create the backup folder"); `makePath` retries, with a regression test.
  QTest prints nothing to stdout here: pass `-o file,txt` to see why a case failed (the exit code = failed cases).
- The instance window's section list scrolls a little with the header card (12 sections × 60 px).
- The test data dir of session 6 (`scratchpad/data`) got a real `accounts.json` (a sign-in happened in a test window);
  the owner was asked about deleting it. Runs after that used an account-free copy.
- Prism public infrastructure still used: meta server, wiki/help URLs, translations, Prism-branded login success page.
  Release blockers in `docs/zero/CREDENTIALS.md`. `curseforge:`/`modrinth:` handlers shared with Prism.
- No updater, installer not built (NSIS template rebranded only; windeployqt not set up).
- Library details panel: its own "● Running" badge also shows while Preparing (LaunchTask sets running early).
- Languages: English and Spanish only (B5). Upstream Prism strings in Spanish depend on Prism's downloaded es file; native Windows
  dialogs (file pickers) follow the OS language.
- Without a Microsoft account the Quick Setup wizard (login page) shows on every start (upstream Prism behaviour).
- At 1100×700 the Library details panel scrolls ("Delete" below the fold); Appearance page scrolls at 1280×800.
- `ZeroCard` hover shadow is drawn inside the card (a widget can't paint outside its rect); grid cards get a real lift.
- Crash Detective regexes are validated only on self-authored fixtures (see session-1 notes in git history).
- Minecraft Theme: pixel font is used for wordmark, page titles and overlines only; legacy Prism dialogs get the
  palette/qss but no pixel details.

## Next tasks (priority order)
1. ~~Instance Profiles + Library 2.0~~ done in session 3. Left out: bulk actions (multi-select tag/favorite/move),
   "recently added" sort, favorite column in the list view; Home hero still = most recently played.
2. ~~Session History~~ done in session 3 (Home "Recent activity"). Not done: per-instance history view, stats.
3. ~~Worlds Hub~~ and ~~Smart Backup (manual + restore)~~ done in session 3, ~~automatic after playing~~ session 4.
   Smart Backup left: scheduled backups, per-instance auto opt-out, instance backup/restore, pre-update snapshots, restore preview, import/export of worlds;
   backup rows show only relative time (several "Just now" look alike; full date in the tooltip). Empty folders are not stored.
4. ~~Storage Manager~~ (session 4; left: "find large files", scan progress). ~~Java Doctor~~ (session 4). ~~Mod Intelligence~~ (session 4; left: outdated checks via Modrinth/CurseForge). Repair Center:
   first slice = Mod Health repair actions (session 4). Downloads/Update center, Repair Center, Discover, ~~Server Vault~~ (session 4; left: per-server palette "Join" entries, adding/editing servers),
   ~~Screenshot Gallery~~ (session 4) (Phases C–E in MISSION.md).
5. Release engineering: windeployqt + NSIS, own MSA client ID + CurseForge key, updater feed.

**Session 8 starts with Mission 003** (section "Mission 003" above: land the two running tasks, then Discover kit +
Modpacks, then Mods, Resource Packs, Shaders). Older plan, still valid afterwards —
**Recommended start for session 7:** run `scripts/zero/build.sh test`, skim this file and `docs/zero/SHELL.md`, then
the owner reviews session 6 (theme, icon, animations, Settings, instance window) and says what to tune; then more
Repair Center actions, Downloads/Update center, Discover, and ZERO translations (Spanish first) to lift the English-only
mode.
