// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the "For <instance> · 1.20.1 Fabric" control of Discover > Mods (docs/zero/CONTENT-MODS.md).
#pragma once

#include <QIcon>
#include <QList>
#include <QPushButton>
#include <QString>
#include <functional>

#include "zero/content/ModTarget.h"

class QMenu;

namespace Zero {
namespace Content {

class TargetInstanceButton : public QPushButton {
    Q_OBJECT
   public:
    using IconProvider = std::function<QIcon(const QString& iconKey)>;

    /// `icons` defaults to the launcher's icon list (APPLICATION->icons()->getIcon).
    explicit TargetInstanceButton(QWidget* parent = nullptr, IconProvider icons = {});

    void setTargets(const QList<TargetInstance>& targets);
    void setCurrentId(const QString& id);
    QString currentId() const { return m_currentId; }
    /// Compact: only the icon, the instance name and the chevron ("For" and "1.20.1 Fabric" move into the tooltip). The section uses it
    /// while the details panel narrows the toolbar (which may take even the name away: the button then shrinks to icon and chevron).
    void setCompact(bool compact);
    bool isCompact() const { return m_compact; }
    /// false (Resource Packs): every instance with a Minecraft version is listed, no loader needed (default true = mods).
    void setNeedsLoader(bool needsLoader);
    bool needsLoader() const { return m_needsLoader; }

    QSize sizeHint() const override;
    /// Icon and chevron only: a toolbar that is short of room takes the rest of the button away (the name and "1.20.1 Fabric" elide).
    QSize minimumSizeHint() const override;

    /// The menu a click opens (mod-ready instances, then the "without a mod loader" caption). The caller owns it.
    QMenu* buildMenu(QWidget* parent);

   signals:
    void targetChosen(const QString& id);

   protected:
    void paintEvent(QPaintEvent* e) override;

   private:
    const TargetInstance* current() const;
    void refreshAccessibility();
    int hintWidth(bool compact) const;
    void openMenu();
    QIcon iconFor(const QString& key) const;

    static constexpr int kPad = 14;
    static constexpr int kIcon = 20;
    static constexpr int kChevron = 16;
    static constexpr int kNameMax = 180;
    static constexpr int kCompactNameMax = 140;

    QList<TargetInstance> m_targets;
    QString m_currentId;
    bool m_compact = false;
    bool m_needsLoader = true;
    IconProvider m_icons;
};

}  // namespace Content
}  // namespace Zero
