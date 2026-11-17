// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the glue between an install flow (a page or the main window) and the Downloads center: starts a job and shows the
// toasts every flow shares ("Downloading X…", "Couldn't download X", "Downloaded with warnings", each with the action "Downloads" that
// opens the panel). The flow's own follow-up (re-read the folder, its success toast) stays in `job.onFinished`. See
// docs/zero/ARCHITECTURE.md ("Downloads center").
#pragma once

#include <QString>
#include "zero/downloads/DownloadText.h"

class QWidget;

namespace Zero {
namespace Downloads {

/// Shows the main window and opens the Downloads panel under its top bar button (does nothing while the button is hidden).
void showPanel();

/// Starts `job` in `APPLICATION->zeroDownloads()` and returns its id ("" = nothing started: no center or no task).
/// * A toast "Downloading <title>…" with the action "Downloads" appears on `host`'s window at once.
/// * When the job ends, `job.onFinished` runs first (the flow's follow-up and success toast), then: Failed -> Error toast "Couldn't
///   download <title>", Done with warnings -> Warning toast "Downloaded with warnings" (both with the action "Downloads"); Cancelled shows
///   nothing. The toasts need `host`: they are skipped when it is gone (the history keeps the error and the warnings).
/// `host` may be null (no toasts at all).
QString startFlow(QWidget* host, DownloadJob job);

}  // namespace Downloads
}  // namespace Zero
