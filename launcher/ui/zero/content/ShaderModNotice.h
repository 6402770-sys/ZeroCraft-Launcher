// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - the info strip above the shader results ("Fabric Fun has no shader mod. Shaders need Iris." + "Find Iris"). Same
// family as the running banner of the instance pages (a tinted bar with an icon), in the `info` tone. Shown only while the target has
// no shader mod; the decision is ShaderNotice (zero/content/ShaderMod.h).
#pragma once

#include <QWidget>

#include "zero/content/ShaderMod.h"

class QLabel;
class QPushButton;

namespace Zero::Content {

class ShaderModNotice : public QWidget {
    Q_OBJECT
   public:
    explicit ShaderModNotice(QWidget* parent = nullptr);

    /// Shows or hides the strip for `notice`.
    void setNotice(const ShaderNotice& notice);
    ShaderNotice notice() const { return m_notice; }
    QLabel* label() const { return m_label; }
    QPushButton* button() const { return m_button; }

   signals:
    /// "Find Iris" / "Find Oculus": the text to search for in Discover > Mods.
    void findRequested(const QString& query);

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    ShaderNotice m_notice;
    QLabel* m_label;
    QPushButton* m_button;
};

}  // namespace Zero::Content
