// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - small bordered label ("Safety", "Damaged", "Ready", "Will fail"), shared by the Worlds backups and Java Doctor.
#pragma once

#include <QWidget>

class QPainter;

namespace Zero {

class PillTag : public QWidget {
    Q_OBJECT
   public:
    /// Plain / PlainDanger: the original badge look (`surface` fill, `border` outline, secondary or danger text).
    /// Success / Warning / Danger / Info: status tones, a 12% tint of the status color with a stronger outline and colored text.
    enum class Tone { Plain, PlainDanger, Success, Warning, Danger, Info };

    PillTag(const QString& text, Tone tone, QWidget* parent = nullptr);
    /// Original two-state form: `danger` = PlainDanger, otherwise Plain.
    PillTag(const QString& text, bool danger, QWidget* parent = nullptr);

    /// Width a pill with this text needs, and the pill itself painted into `rect` (for delegates that paint rows without widgets).
    static int widthForText(const QString& text);
    static void paintPill(QPainter* painter, const QRectF& rect, const QString& text, Tone tone);

    void setText(const QString& text);
    void setTone(Tone tone);
    QString text() const { return m_text; }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }

   protected:
    void paintEvent(QPaintEvent*) override;

   private:
    QString m_text;
    Tone m_tone;
};

}  // namespace Zero
