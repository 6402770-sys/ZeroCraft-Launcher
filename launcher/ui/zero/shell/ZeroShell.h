// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the main window shell: [ZeroNavRail | ZeroTopBar over the page stack]. See docs/zero/SHELL.md.
#pragma once

#include <QHash>
#include <QKeySequence>
#include <QPixmap>
#include <QWidget>

class QStackedWidget;

namespace Zero {

class ShellPage;
class ZeroNavRail;
class ZeroTopBar;

class ZeroShell : public QWidget {
    Q_OBJECT
   public:
    /// Window width below which the rail collapses to icons only.
    static constexpr int kCollapseBelow = 1200;

    /// Where a page's rail item goes: the top group, or the bottom group (after the stretch).
    enum class Placement { Top, Bottom };

    explicit ZeroShell(QWidget* parent = nullptr);

    /// Adds a page and its rail item. Pages are appended in order; the first one added becomes current. The n-th page
    /// added (n <= 9) gets the window-wide shortcut Alt+n, shown in its rail tooltip.
    void addPage(const QString& id, const QString& iconName, const QString& label, ShellPage* page, Placement placement = Placement::Top);
    /// The "settings" page: listed last in the bottom group, after "More" (call before finishRail()). Without it the
    /// Settings rail item only emits settingsRequested().
    void setSettingsPage(ShellPage* page);
    void setCurrentPage(const QString& id);
    QString currentPage() const { return m_current; }
    ShellPage* page(const QString& id) const { return m_pages.value(id); }
    /// The Alt+n shortcut of a page (empty if it has none).
    QKeySequence pageShortcut(const QString& id) const { return m_shortcuts.value(id); }

    /// Adds More and Settings at the bottom of the rail. Call once, after all pages have been added.
    void finishRail();

    /// Account chip content in the top bar (profile name, "Microsoft account" / "Offline" / "No account", face or null).
    void setAccount(const QString& name, const QString& caption, const QPixmap& face);

    ZeroNavRail* rail() const { return m_rail; }
    ZeroTopBar* topBar() const { return m_topBar; }

   signals:
    void pageChanged(const QString& id);
    /// Account menu: `anchor` is the global bottom-right corner of the top bar chip; place the menu's top-right corner
    /// just below it. More menu: `anchor` is at the right edge of the clicked rail control, bottom aligned; place the
    /// menu's bottom-left corner there.
    void accountMenuRequested(const QPoint& anchor);
    void moreMenuRequested(const QPoint& anchor);
    void settingsRequested();

   protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

   private:
    void showPage(const QString& id);
    void updateCollapsed();

    ZeroNavRail* m_rail;
    ZeroTopBar* m_topBar;
    QStackedWidget* m_stack;
    QHash<QString, ShellPage*> m_pages;
    QHash<QString, QKeySequence> m_shortcuts;
    QString m_current;
    bool m_railFinished = false;
    ShellPage* m_settingsPage = nullptr;
    bool m_hasStretch = false;
};

}  // namespace Zero
