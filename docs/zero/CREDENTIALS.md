# External credentials and services

ZERO Launcher is a fork of Prism Launcher. During development it still talks to a few services that Prism runs or
has registered. This file lists them, what ZERO does today, and what must change before a public release.

## Credentials (compile-time, in the root `CMakeLists.txt`)

| Variable | What it is | Today | Before a public release |
|---|---|---|---|
| `Launcher_MSA_CLIENT_ID` | Microsoft Entra (Azure) app registration used for the Microsoft/Xbox/Minecraft sign-in | Prism's client ID, local development only | Register an own Azure app, then request Minecraft API approval from Mojang/Microsoft for it. Tokens from one client ID do not work with another, so users re-login. |
| `Launcher_CURSEFORGE_API_KEY` | CurseForge Core API key (mod/modpack search and downloads) | Prism's key, local development only | Apply for an own key at the CurseForge developer console. CurseForge requires derivative works to use their own key. |
| `Launcher_IMGUR_CLIENT_ID` | Imgur API client for screenshot upload | Empty (upload action hidden) | Optional. Register an Imgur app to enable it. |

Guard: configure with `-DZERO_RELEASE_BUILD=ON` to make CMake fail while either of the first two still equals the
Prism default. Pass your own values with `-DLauncher_MSA_CLIENT_ID=...` and `-DLauncher_CURSEFORGE_API_KEY=...`.
Never commit real values or print them in logs.

## Services reused from Prism (public infrastructure)

| Variable | Service | Why reused | Release plan |
|---|---|---|---|
| `Launcher_META_URL` | `meta.prismlauncher.org` version metadata (Minecraft, Forge, Fabric, Java manifests) | The launcher cannot list or launch versions without it | Keep using it with Prism's goodwill, or host an own meta mirror. Do not hammer it; keep the user agent honest. |
| `Launcher_LEGACY_FMLLIBS_BASE_URL` | Forge libraries for Minecraft 1.5.2 and older | Only hosted by Prism | Keep, or mirror. |
| `Launcher_TRANSLATIONS_URL`, `Launcher_TRANSLATION_FILES_URL` | Weblate project and translation file host | Gives translations without own infrastructure | Keep, or move to an own Weblate project. |
| `Launcher_WIKI_URL`, `Launcher_HELP_URL` | Prism wiki and help pages | No ZERO docs yet | Replace once ZERO has own docs. |
| `Launcher_LOGIN_CALLBACK_URL` | Page the browser is redirected to after a successful Microsoft login | Only a "you may close this tab" page shown after the local loopback redirect. It is not part of the MSA registration (the registered redirect is the loopback address), so it can be replaced freely | Point it at an own page, shows Prism branding today. |

## Disabled until ZERO has its own (set empty, the matching UI is hidden)

`Launcher_NEWS_RSS_URL`, `Launcher_NEWS_OPEN_URL`, `Launcher_BUG_TRACKER_URL`, `Launcher_MATRIX_URL`,
`Launcher_DISCORD_URL`, `Launcher_SUBREDDIT_URL`, `Launcher_UPDATER_GITHUB_REPO`, `Launcher_Git`,
`MACOSX_SPARKLE_UPDATE_FEED_URL`.

## Owner checklist before a public release

1. Azure app registration plus Minecraft API approval, then set `Launcher_MSA_CLIENT_ID`.
2. CurseForge API key, then set `Launcher_CURSEFORGE_API_KEY`.
3. Decide on the meta server (keep Prism's or run an own mirror).
4. Public repository URL (`Launcher_Git`), bug tracker, community links, updater repo and artifact name.
5. Configure with `-DZERO_RELEASE_BUILD=ON` for release builds.
