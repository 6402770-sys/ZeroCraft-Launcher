// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the main window shell: [ZeroNavRail | page stack]. See docs/zero/SHELL.md.
#pragma once

#include <QHash>
#include <QPixmap>
#include <QWidget>

class QStackedWidget;

namespace Zero {

class AccountChip;
class ZeroNavRail;

class ZeroShell : public QWidget {
    Q_OBJECT
   public:
    /// Window width below which the rail collapses to icons only.
    static constexpr int kCollapseBelow = 1200;

    explicit ZeroShell(QWidget* parent = nullptr);

    /// Adds a page and its rail item. Pages are appended in order; the first one added becomes current.
    void addPage(const QString& id, const QString& iconName, const QString& label, QWidget* page);
    void setCurrentPage(const QString& id);
    QString currentPage() const { return m_current; }
    QWidget* page(const QString& id) const { return m_pages.value(id); }

    /// Adds the rail footer (account chip, More, Settings). Call once, after all pages have been added.
    void finishRail();

    /// Account chip content (profile name, "Microsoft account" / "Offline" / "No account", face or null).
    void setAccount(const QString& name, const QString& caption, const QPixmap& face);

    ZeroNavRail* rail() const { return m_rail; }

   signals:
    void pageChanged(const QString& id);
    /// `anchor` is a global position at the right edge of the clicked rail control, bottom aligned: a popup menu
    /// should place its bottom-left corner there.
    void accountMenuRequested(const QPoint& anchor);
    void moreMenuRequested(const QPoint& anchor);
    void settingsRequested();

   protected:
    void resizeEvent(QResizeEvent* event) override;

   private:
    void showPage(const QString& id);
    void updateCollapsed();

    ZeroNavRail* m_rail;
    AccountChip* m_chip;
    QStackedWidget* m_stack;
    QHash<QString, QWidget*> m_pages;
    QString m_current;
    bool m_railFinished = false;
};

}  // namespace Zero
