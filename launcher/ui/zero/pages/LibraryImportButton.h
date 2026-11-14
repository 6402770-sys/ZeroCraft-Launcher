// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Library top bar "Import" button (secondary, file-archive icon). Collapses to icon-only when the bar is narrow.
#pragma once

#include <QPushButton>

namespace Zero {

class LibraryImportButton : public QPushButton {
    Q_OBJECT
   public:
    explicit LibraryImportButton(QWidget* parent = nullptr);

    /// Icon-only (same tooltip, accessible name "Import") or icon + "Import".
    void setCompact(bool compact);
    bool isCompact() const { return m_compact; }

   private:
    void refreshIcon();
    bool m_compact = false;
};

}  // namespace Zero
