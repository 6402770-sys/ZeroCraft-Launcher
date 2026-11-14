# External credentials and services

ZERO Launcher is a fork of Prism Launcher. During development it still talks to a few services that Prism runs or
has registered. This file lists them, what ZERO does today, and what must change before a public release.

## Credentials (compile-time, in the root `CMakeLists.txt`)

| Variable | What it is | Today | Before a public release |
|---|---|---|---|
| `Launcher_MSA_CLIENT_ID` | Microsoft Entra (Azure) app registration used for the Microsoft/Xbox/Minecraft sign-in | Prism's client ID | **Owner decision 2026-11-13: releases keep Prism's client ID** (the owner cannot open an Azure account; ElyPrismLauncher ships Prism's ID the same way). Consequences: sign-in uses the `http://localhost` redirect (`MSAStep.cpp`), Microsoft's consent page names Prism Launcher, and Prism/Microsoft could revoke it. Later, optionally: own Azure app + Minecraft API approval (users then re-login, tokens do not carry over between client IDs). |
| `Launcher_ELY_CLIENT_ID` / `Launcher_ELY_CLIENT_SECRET` | OAuth application of ZERO at Ely.by (account.ely.by > Applications), used for the Ely.by device code sign-in and token refresh | **Set: `zerocraft-launcher`** (ZERO's own app, registered by the owner on 2026-11-13; Ely.by answered the device code request with HTTP 200). Email + password sign-in needs no ID; an empty ID hides only the code sign-in. Never use another launcher's registration. A developer tests with `-DLauncher_ELY_CLIENT_ID=<id>` (and `-DLauncher_ELY_CLIENT_SECRET=<secret>` only if the app is confidential; the secret is sent only when set and never logged). | Register ZERO's application at Ely.by, enable the device code grant, put the ID (and secret if Ely.by issues one) into the release configure. Accounts remember the client ID they signed in with: changing it makes Ely.by accounts ask to sign in again. |
| `Launcher_CURSEFORGE_API_KEY` | CurseForge Core API key (mod/modpack search and downloads) | Prism's key, local development only | Apply for an own key at the CurseForge developer console. CurseForge requires derivative works to use their own key. |
| `Launcher_IMGUR_CLIENT_ID` | Imgur API client for screenshot upload | Empty (upload action hidden) | Optional. Register an Imgur app to enable it. |

Guard: configure with `-DZERO_RELEASE_BUILD=ON` to make CMake fail while the CurseForge key still equals the Prism default
(the MSA client ID may stay Prism's, see above; CMake prints a status line when it does). Pass your own values with `-DLauncher_MSA_CLIENT_ID=...` and `-DLauncher_CURSEFORGE_API_KEY=...`.
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

Exception: `scripts/zero/package.sh` configures its release tree with `Launcher_UPDATER_GITHUB_REPO` set to the owner's GitHub
repo and the artifact name `Windows` (see "Packaging" in `ARCHITECTURE.md`); the dev build keeps the updater off.

## Owner checklist before a public release

1. (Optional, decided against for now) Azure app registration plus Minecraft API approval, then set `Launcher_MSA_CLIENT_ID`.
2. ✅ CurseForge API key (own, verified 2026-11-13): stored outside the repo in `%USERPROFILE%\.zero\curseforge-api-key`; `package.sh --release` reads it when `ZERO_CURSEFORGE_API_KEY` is unset (`ZERO_CURSEFORGE_API_KEY_FILE` overrides the path).
3. Decide on the meta server (keep Prism's or run an own mirror).
4. Public repository URL (`Launcher_Git`), bug tracker, community links, updater repo and artifact name.
5. Configure with `-DZERO_RELEASE_BUILD=ON` for release builds.
