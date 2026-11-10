# Third-party assets used by the ZERO design system

## Lucide icons

- Source: https://lucide.dev (npm package `lucide-static`, pinned to **1.71.0**, fetched from
  `https://unpkg.com/lucide-static@1.71.0/icons/<name>.svg`)
- License: ISC (some icons derived from Feather are additionally MIT, Copyright (c) 2013-present Cole Bemis).
  The full text, unmodified, is in `launcher/resources/zero/icons/LICENSE`.
- Location in the repo: `launcher/resources/zero/icons/*.svg`, compiled in through `launcher/resources/zero/zero.qrc`
  (`:/zero/icons/<name>.svg`). The files are unmodified; `currentColor` is replaced at runtime by `Zero::Icons`
  and by the generated stylesheet to tint them with design tokens.

Icons shipped: house, library-big, compass, earth, wrench, download, palette, settings, search, play, square, plus,
star, folder-open, ellipsis, pencil, trash-2, copy, x, check, triangle-alert, info, circle-check, circle-x,
layout-grid, list, list-filter, clock, user, chevron-down, chevron-right, chevron-up, chevron-left, minus,
refresh-cw, archive, hard-drive, cpu, stethoscope, terminal, command, package, box.

To update: change the pinned version above and in this file, re-download the same names, and replace `LICENSE`.

## Fonts

ZERO does not bundle fonts. It uses `Segoe UI Variable Text`, falling back to `Segoe UI` (both ship with Windows 10/11).
