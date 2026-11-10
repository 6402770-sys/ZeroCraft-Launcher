# ZERO Launcher — Owner's mission (condensed, binding)

Condensed from the owner's original brief (2026-11-10). Future sessions should treat this as the product brief.

## Product
ZERO is a radical transformation of Prism Launcher into an independent, premium Minecraft Java launcher for
**Windows 10/11**. It is not a rebrand or a theme: it should feel like a new application with its own
navigation, information architecture, visual language, workflows and distinctive features, while preserving Prism's
engine. Philosophy: **high risk, high reward**. The owner authorizes big refactors, new subsystems, new dependencies,
CMake changes and UI replacement, and asks for autonomous engineering decisions without asking which approach to take.
Ambition must never mean careless work.

## Working rules from the owner
- Work cycle: investigate → decide → implement → compile → test → fix regressions → next subsystem. Don't stop at plans.
- Branch `zero-development`. Make **local commits** as checkpoints. **Never push** without explicit authorization.
- Use `/savvy-flow`: Opus orchestrates, `savvy-medium` for ordinary work, `savvy-careful` for delicate, security,
  migration and critical work. Review delegated work before integrating it.
- No mock functionality: buttons must work, loading indicators must reflect real operations, and unfinished things are reported, not faked.
- Never destroy user data, expose credentials, bypass platform security or silently overwrite installations.
- Keep HANDOFF.md current so a fresh session can continue without re-investigating.

## Design (Obsidian), detailed in `DESIGN.md`
Palette: bg #101010, surface #191919, elevated #222222, higher #292929, accent #D4D4D4, text #F5F5F5,
secondary text #929292, borders #303030. The look is a premium, minimal desktop app with strong hierarchy, subtle motion,
good empty and loading states, and native feel. No gaming aesthetics, gradients or RGB. Everything is built from a
reusable design system (colors, type, spacing, radius, shadows, buttons, inputs, cards, dialogs, navigation, menus,
notifications, progress, loading).

## Navigation (may be adjusted if a better architecture emerges)
- **Home**: recent instances, favorite, prominent Play, recent activity, download/update status, shortcuts, Minecraft news.
- **Library**: instance cards, grid/list, search and advanced filters, categories/tags, favorites, metadata, per-instance and bulk actions, context menus.
- **Discover**: Modrinth + CurseForge mods, modpacks, resource packs, shaders and more. Reuse the existing integrations.
- **Worlds**: worlds across instances with metadata, size and dates; backup/restore, import/export, open folder.
- **Tools**: Crash Detective, Java Doctor, Storage Manager, Mod Compatibility Inspector, Backup Manager, diagnostics.
- **Downloads**: active downloads, queue, progress, updates, batch operations, errors, history.
- **Theme Studio**: themes, accent colors, backgrounds, density, animation preferences, preview before applying.
- **Settings**: redesigned, searchable sections with contextual help.

## Signature features (requirements)
- **Smart Backup**: automatic/scheduled world backups, instance backup/restore, retention policies, storage-efficient,
  restore previews, pre-update snapshots, manual backups. Never silently overwrite user data.
- **Crash Detective**: local analysis of logs, crash reports, loader/mod loading, dependencies, Java exceptions,
  memory, config mistakes; plain-language causes and fixes; never present uncertain diagnoses as confirmed. Works
  without any API key; **optional local AI via Ollama** (Ollama is installed on the owner's PC).
- **Mod Health Inspector**: dependencies, missing deps, conflicts, duplicates, loader and Minecraft compatibility, outdated
  versions (only with reliable metadata), suspicious metadata. Separate definite conflicts from possible ones.
- **Instance Profiles**: names, descriptions, categories, tags, favorites, artwork, notes, per-instance config.
  Extend Prism's abstractions; no duplicate profile store.
- **Storage Manager**: usage across instances, mods, worlds, logs, screenshots, packs, caches, backups; find large or
  disposable files; never delete without clear consent.
- **Java Doctor**: detect runtimes and versions, check compatibility per Minecraft version, recommend, configure per
  instance, diagnose Java launch failures.
- **Command Palette**: global quick actions (instances, pages, actions, content, settings), keyboard shortcuts and
  a discoverable shortcut reference.

## Core functionality that must never regress
Microsoft auth and accounts, Minecraft install, versions, Fabric/Forge/NeoForge/Quilt, modpack install,
Modrinth/CurseForge, instance isolation, launching, Java config, JVM args, process monitoring, downloads.

## Windows product independence
Own name, exe metadata, icons, install/config/log dirs, shortcuts, **installer, uninstaller, update mechanism**,
versioning. Safe Prism import that never assumes replacing Prism. Keep upstream cross-platform abstractions where useful.

## Licensing and security
Respect GPL-3.0-only and asset licenses, and keep attribution. Don't blindly reuse upstream credentials. Keep MS auth secure.
Never log or commit tokens or keys. No auth bypasses.

## Testing priorities
Compilation, unit, integration and regression tests, auth safety, instance isolation, launch reliability, backup integrity,
data migration, UI consistency, Windows-specific behavior.

## Priorities
- **P0 Foundation**: baseline, identity, shell, design system, architecture. *(done)*
- **P1 UX**: Home, Library, navigation, cards, Settings, core interactions. *(Home/Library/nav done; Settings redesign pending)*
- **P2 Management**: Worlds, Smart Backup, Downloads, Storage Manager, Instance Profiles.
- **P3 Intelligence**: Crash Detective *(done, Ollama pending)*, Mod Health, Java Doctor, Command Palette, local AI.
- **P4 Completion**: Theme Studio, polish, performance, installer, updater, documentation.
