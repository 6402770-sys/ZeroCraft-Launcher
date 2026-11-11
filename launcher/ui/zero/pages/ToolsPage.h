// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Tools page: a grid of tool cards; each tool opens inside the page. See docs/zero/SHELL.md.
#pragma once

#include <QWidget>
#include <optional>
#include "ui/zero/shell/ShellPage.h"

class QStackedLayout;

namespace Zero {

class CrashDetectiveView;
class StorageView;

class ToolsPage : public ShellPage {
    Q_OBJECT
   public:
    explicit ToolsPage(QWidget* parent = nullptr);

    /// "Tools", or the open tool's name ("Crash Detective").
    QString title() const override;
    QString subtitle() const override;
    /// "Tools" while a tool is open (clicking it goes back to the overview), otherwise none.
    QString breadcrumbParent() const override;
    void breadcrumbActivated() override { showOverview(); }
    QWidget* topBarActions() const override;

    /// Opens Crash Detective for `instanceId`; analyzes right away when `analyzeNow` is set.
    void openCrashDetective(const QString& instanceId, bool analyzeNow, std::optional<int> exitCode = std::nullopt);
    /// Opens Storage and starts a scan.
    void openStorage();
    /// Default instance for Crash Detective (the Library selection). Ignored while a result is showing.
    void setPreferredInstance(const QString& instanceId);
    /// Back to the grid of tools.
    void showOverview();
    /// True while a tool (Crash Detective or Storage) is open.
    bool isToolOpen() const;

    CrashDetectiveView* crashDetective() const { return m_detective; }
    StorageView* storage() const { return m_storage; }

   signals:
    void instanceSettingsRequested(const QString& instanceId);
    void showInstanceRequested(const QString& instanceId);

   protected:
    void showEvent(QShowEvent* event) override;

   private:
    QStackedLayout* m_stack;
    CrashDetectiveView* m_detective;
    StorageView* m_storage;
};

}  // namespace Zero
