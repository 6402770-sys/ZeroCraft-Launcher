// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the keyboard shortcut reference (Ctrl+/ or the "Keyboard shortcuts" command), same overlay style as
// the command palette. MainWindow supplies the groups, built from the real QActions' shortcuts.
#pragma once

#include <functional>

#include "PaletteOverlay.h"
#include "PaletteTypes.h"

namespace Zero {

class SheetContent;

class ZeroShortcutSheet : public PaletteOverlay {
    Q_OBJECT
   public:
    explicit ZeroShortcutSheet(QWidget* host);

    using Provider = std::function<QList<ShortcutGroup>()>;
    void setGroupsProvider(Provider provider) { m_provider = std::move(provider); }
    void setGroups(const QList<ShortcutGroup>& groups);
    QList<ShortcutGroup> groups() const { return m_groups; }

   protected:
    void aboutToOpen() override;
    QWidget* initialFocus() override;
    int preferredWidth() const override { return 720; }
    int panelHeight() const override;

   private:
    Provider m_provider;
    QList<ShortcutGroup> m_groups;
    SheetContent* m_content;
};

}  // namespace Zero
