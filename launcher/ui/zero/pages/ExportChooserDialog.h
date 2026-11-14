// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - "Export" chooser: pick Full copy / Modrinth pack / CurseForge pack, then Prism's own export dialog
// (file tree, names, save path) opens for that format. The dialog knows nothing about Prism's dialogs: the caller reads selected().
#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;

namespace Zero {

class ZeroCard;

enum class ExportFormat { Zip, MrPack, CurseForge };

/// Setting value of a format ("zip", "mrpack", "curseforge") and back (unknown text = Zip).
QString exportFormatKey(ExportFormat format);
ExportFormat exportFormatFromKey(const QString& key);

class ExportChooserDialog : public QDialog {
    Q_OBJECT
   public:
    /// `snapshot`: the instance runs a Minecraft snapshot, which CurseForge packs cannot hold (that card is disabled).
    /// `initial`: the remembered choice; falls back to Zip when it is not available.
    ExportChooserDialog(const QString& instanceName, bool snapshot, ExportFormat initial, QWidget* parent = nullptr);

    ExportFormat selected() const { return m_selected; }
    void setSelected(ExportFormat format);
    ZeroCard* card(ExportFormat format) const { return m_cards[int(format)]; }
    bool isAvailable(ExportFormat format) const;
    QPushButton* continueButton() const { return m_continue; }
    QPushButton* cancelButton() const { return m_cancel; }
    QLabel* subtitleLabel() const { return m_subtitle; }

   protected:
    void keyPressEvent(QKeyEvent* event) override;

   private:
    void move(int step);

    ZeroCard* m_cards[3] = { nullptr, nullptr, nullptr };
    bool m_snapshot;
    ExportFormat m_selected = ExportFormat::Zip;
    QLabel* m_subtitle = nullptr;
    QPushButton* m_continue = nullptr;
    QPushButton* m_cancel = nullptr;
};

}  // namespace Zero
