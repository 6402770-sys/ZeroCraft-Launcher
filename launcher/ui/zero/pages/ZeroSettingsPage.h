// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Settings as a page of the main window (rail item "Settings", Ctrl+,). Hosts the launcher's global
// settings pages (the same ones Prism's settings dialog shows) in a PageContainer with the ZERO side list, a plain-
// language explanation per section and the help strip that explains the option under the pointer.
//
// The pages are created fresh every time Settings opens (so they show what is saved now, also after the modal settings
// dialog was used elsewhere) and everything is applied when you leave Settings: navigating to another page, closing the
// window or quitting. Like the dialog, opening emits Application::globalSettingsAboutToOpen and saving emits
// Application::globalSettingsApplied (instance settings pages and the main window refresh on those).
#pragma once

#include <QPointer>
#include "ui/zero/shell/ShellPage.h"

class PageContainer;
class BasePageProvider;
class QVBoxLayout;

namespace Zero {

class ZeroSettingsPage : public ShellPage {
    Q_OBJECT
   public:
    explicit ZeroSettingsPage(BasePageProvider* provider, QWidget* parent = nullptr);
    ~ZeroSettingsPage() override;

    QString title() const override;
    QString subtitle() const override;

    /// Selects a section ("launcher-settings", "accounts", ...); empty = the last one shown. Works before the page is
    /// open too (the section is selected when it opens).
    void selectSection(const QString& pageId);
    /// Applies every section (no-op while closed). False when a section refused (it stays open then).
    bool applyAll();
    bool isOpen() const { return m_container != nullptr; }

   protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

   private:
    void open();
    void close();

    BasePageProvider* m_provider;
    QVBoxLayout* m_layout;
    QPointer<PageContainer> m_container;
    QString m_section;
};

}  // namespace Zero
