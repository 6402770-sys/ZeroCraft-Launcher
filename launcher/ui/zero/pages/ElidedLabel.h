// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - a QLabel that elides its text to the available width instead of forcing the layout wider.
#pragma once

#include <QLabel>

namespace Zero {

class ElidedLabel : public QLabel {
    Q_OBJECT
   public:
    explicit ElidedLabel(QWidget* parent = nullptr) : QLabel(parent)
    {
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setMinimumWidth(0);
    }

    void setFullText(const QString& text)
    {
        m_full = text;
        update_();
    }
    const QString& fullText() const { return m_full; }

    QSize minimumSizeHint() const override { return { 0, QLabel::minimumSizeHint().height() }; }

   protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        update_();
    }
    void changeEvent(QEvent* event) override
    {
        QLabel::changeEvent(event);
        if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
            update_();
    }

   private:
    void update_()
    {
        const QString elided = fontMetrics().elidedText(m_full, Qt::ElideRight, qMax(0, width()));
        if (elided != text())
            setText(elided);
        setToolTip(elided == m_full ? QString() : m_full);
    }

    QString m_full;
};

}  // namespace Zero
