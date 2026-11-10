// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Tools page: a grid of tool cards; each tool opens inside the page. See docs/zero/SHELL.md.
#pragma once

#include <QWidget>
#include <optional>

class QStackedLayout;

namespace Zero {

class CrashDetectiveView;

class ToolsPage : public QWidget {
    Q_OBJECT
   public:
    explicit ToolsPage(QWidget* parent = nullptr);

    /// Opens Crash Detective for `instanceId`; analyzes right away when `analyzeNow` is set.
    void openCrashDetective(const QString& instanceId, bool analyzeNow, std::optional<int> exitCode = std::nullopt);
    /// Default instance for Crash Detective (the Library selection). Ignored while a result is showing.
    void setPreferredInstance(const QString& instanceId);
    /// Back to the grid of tools.
    void showOverview();

    CrashDetectiveView* crashDetective() const { return m_detective; }

   signals:
    void instanceSettingsRequested(const QString& instanceId);

   protected:
    void showEvent(QShowEvent* event) override;

   private:
    QStackedLayout* m_stack;
    CrashDetectiveView* m_detective;
};

}  // namespace Zero
