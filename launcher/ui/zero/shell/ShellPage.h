// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - base class of every page hosted by ZeroShell: it describes the page's top bar chrome.
#pragma once

#include <QPointer>
#include <QWidget>

namespace Zero {

/// A page of the ZeroShell. The page owns its identity in the top bar: title, subtitle, an optional breadcrumb parent
/// (a clickable segment before the title) and an action widget. The top bar shows only the current page's chrome and
/// re-reads it whenever the page emits chromeChanged().
class ShellPage : public QWidget {
    Q_OBJECT
   public:
    explicit ShellPage(QWidget* parent = nullptr);
    ~ShellPage() override;

    virtual QString title() const = 0;
    virtual QString subtitle() const { return {}; }
    /// Label of the clickable parent segment shown before the title ("Tools" in "Tools > Crash Detective"); empty = none.
    virtual QString breadcrumbParent() const { return {}; }
    /// The parent segment was clicked (default: nothing). The page usually navigates back.
    virtual void breadcrumbActivated() {}
    /// Widget shown at the right of the top bar while this page is current; null = none. The page owns it: the top bar
    /// borrows it (reparents it) and gives it back, hidden, when another page becomes current.
    virtual QWidget* topBarActions() const { return m_actions; }

   signals:
    /// Title, subtitle, breadcrumb or actions changed: the top bar refreshes if this page is current.
    void chromeChanged();

   protected:
    /// Sets the default action widget (returned by topBarActions()). Reparented to the page and hidden until the top bar uses it.
    void setTopBarActions(QWidget* actions);

   private:
    QPointer<QWidget> m_actions;
};

}  // namespace Zero
