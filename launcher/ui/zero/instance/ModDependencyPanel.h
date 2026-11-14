// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the Dependency Inspector panel of the instance window's Mods page (docs/zero/CONTENT-MODS.md, "Dependency Inspector"):
// 320 px, a `surface` card to the right of the list. Header (icon, name, "version · file name"), then the sections Needs / Game /
// Optional / Needed by / Conflicts. A dumb view: the page hands it `PanelData` and reacts to the signals (Enable goes through the page's
// own enable flow, Find opens Discover, a Needed by row selects that mod). It changes nothing by itself.
#pragma once

#include <QFrame>
#include <QList>
#include <QPixmap>
#include <QString>

#include <functional>

#include "zero/mods/DependencyInspector.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

namespace Zero {

class PillTag;
class ZeroSkeleton;

namespace ModsUi {

/// A mod that needs the shown one (the scan's answer joined with Prism's own relations).
struct PanelNeededBy {
    QString path;  ///< the file of that mod: the page selects the row with this path
    QString name;
    bool enabled = true;
};

struct PanelData {
    QString key;       ///< identifies the mod: the same key = a refresh, another = a different mod
    QString name;
    QString version;
    QString fileName;
    QString path;
    QString iconId;    ///< what the icon provider is asked for ("" = no logo)
    bool enabled = true;
    bool scanning = true;   ///< the scan has not told about this file yet: skeleton lines
    bool canChange = true;  ///< false while the game runs or a check reads the mods: Enable is dimmed
    QString mcVersion;
    Mods::ModInspection inspection;  ///< valid when !scanning
    QList<PanelNeededBy> neededBy;
};

class ModDependencyPanel : public QFrame {
    Q_OBJECT
   public:
    static constexpr int kWidth = 320;
    static constexpr int kPad = 16;
    static constexpr int kIcon = 40;

    using IconProvider = std::function<QPixmap(const QString& id, int px, qreal dpr)>;

    explicit ModDependencyPanel(QWidget* parent = nullptr);

    void setIconProvider(IconProvider provider) { m_icons = std::move(provider); }
    /// Shows `data` (rebuilds the sections; the scroll position stays when the key is the same).
    void setData(const PanelData& data);
    const PanelData& data() const { return m_data; }
    QString key() const { return m_data.key; }

    // Parts, for the tests and the dev aids.
    QLabel* nameLabel() const { return m_name; }
    QLabel* captionLabel() const { return m_caption; }
    QToolButton* closeButton() const { return m_close; }
    ZeroSkeleton* skeleton() const;
    QStringList sectionTitles() const;
    QList<QPushButton*> enableButtons() const;
    QList<QPushButton*> findButtons() const;
    QList<QPushButton*> neededByButtons() const;
    /// The text of every pill, top to bottom.
    QStringList pillTexts() const;
    /// The captions of the rows of a section ("Needs" ...), top to bottom: "<name> | <caption>".
    QStringList rowTexts(const QString& sectionTitle) const;
    QStringList emptyTexts() const;  ///< "Needs no other mods." ...

   signals:
    void closeRequested();
    void enableRequested(const QString& path);  ///< the file of the disabled mod
    void findRequested(const QString& id);      ///< the dependency id
    void neededByClicked(const QString& path);

   protected:
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

   private:
    void rebuild();
    void refreshIcon();
    QWidget* addSection(const QString& title);

    IconProvider m_icons;
    PanelData m_data;
    QWidget* m_iconBox = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_caption = nullptr;
    QToolButton* m_close = nullptr;
    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QVBoxLayout* m_sections = nullptr;
};

}  // namespace ModsUi
}  // namespace Zero
