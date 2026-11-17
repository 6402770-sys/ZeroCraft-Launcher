// SPDX-License-Identifier: GPL-3.0-only
#include "DownloadFlow.h"

#include <QPointer>
#include <QWidget>
#include <utility>

#include "Application.h"
#include "ui/MainWindow.h"
#include "ui/zero/ZeroToast.h"
#include "zero/downloads/DownloadCenter.h"

namespace Zero {
namespace Downloads {

void showPanel()
{
    if (auto* window = APPLICATION->showMainWindow()) {
        window->showDownloadsPanel();
    }
}

QString startFlow(QWidget* host, DownloadJob job)
{
    auto* center = APPLICATION ? APPLICATION->zeroDownloads() : nullptr;
    if (!center || !job.task) {
        return {};
    }
    const QPointer<QWidget> guard(host);
    const QString title = job.title;
    auto followUp = std::move(job.onFinished);
    job.onFinished = [guard, title, followUp](const DownloadResult& result) {
        if (followUp) {
            followUp(result);
        }
        if (!guard) {
            return;
        }
        if (result.state == DownloadResult::State::Failed) {
            ZeroToast::show(guard, failedToastText(title), ZeroToast::Kind::Error, downloadsActionText(), []() { showPanel(); });
        } else if (result.state == DownloadResult::State::Done && !result.warnings.isEmpty()) {
            ZeroToast::show(guard, warningsToastText(), ZeroToast::Kind::Warning, downloadsActionText(), []() { showPanel(); });
        }
    };
    if (guard) {
        ZeroToast::show(guard, startedToastText(title), ZeroToast::Kind::Info, downloadsActionText(), []() { showPanel(); });
    }
    return center->start(std::move(job));
}

}  // namespace Downloads
}  // namespace Zero
