// SPDX-License-Identifier: GPL-3.0-only
#include "ThemeEditorPanel.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QColorDialog>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QImageReader>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpressionValidator>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <algorithm>

#include "ui/zero/ZeroCustomFont.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroToast.h"
#include "ui/zero/content/FilterPanel.h"
#include "ui/zero/pages/ElidedLabel.h"
#include "ui/zero/pages/ThemeLibrary.h"
#include "zero/theme/ThemeArchive.h"
#include "zero/theme/ThemeFiles.h"

namespace Zero {

using Custom::MainColor;
using Custom::ThemeEditorModel;

namespace {

constexpr int kSwatchSize = 20;
constexpr int kDotSize = 18;
constexpr int kHexWidth = 84;
constexpr int kIconPreviewSize = 32;
constexpr int kFontPreviewPx = 15;

QLabel* makeLabel(const QString& text, Role role, QWidget* parent, const char* tone = nullptr)
{
    auto* l = new QLabel(text, parent);
    Style::applyRole(l, role);
    if (tone)
        Style::setProperty(l, "zeroTone", tone);
    return l;
}

/// 20 px color square: click opens the color dialog.
class ColorSwatch : public QAbstractButton {
   public:
    explicit ColorSwatch(QWidget* parent) : QAbstractButton(parent)
    {
        setFixedSize(kSwatchSize, kSwatchSize);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
    }
    void setColor(const QColor& c)
    {
        if (c == m_color)
            return;
        m_color = c;
        update();
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setBrush(m_color);
        p.setPen(QPen(underMouse() ? t.text : t.borderStrong, 1));
        p.drawPath(Paint::panelPath(r, t.radiusSm, t.corners));
        if (m_keyboardFocus) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.accent2, 2));
            p.drawPath(Paint::panelPath(QRectF(rect()).adjusted(1, 1, -1, -1), t.radiusSm, t.corners));
        }
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void focusInEvent(QFocusEvent* e) override
    {
        QAbstractButton::focusInEvent(e);
        m_keyboardFocus = e->reason() != Qt::MouseFocusReason;  // the ring is for keyboard users
        update();
    }
    void focusOutEvent(QFocusEvent* e) override
    {
        QAbstractButton::focusOutEvent(e);
        m_keyboardFocus = false;
        update();
    }

   private:
    QColor m_color;
    bool m_keyboardFocus = false;
};

/// Round accent preset; ringed while it is the current accent.
class PresetDot : public QAbstractButton {
   public:
    PresetDot(const QColor& color, QWidget* parent) : QAbstractButton(parent), m_color(color)
    {
        setFixedSize(kDotSize + 6, kDotSize + 6);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::StrongFocus);
        setCheckable(true);
        setAutoExclusive(false);
    }
    const QColor& color() const { return m_color; }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF dot = QRectF(rect()).adjusted(3, 3, -3, -3);
        p.setPen(Qt::NoPen);
        p.setBrush(m_color);
        p.drawEllipse(dot);
        if (isChecked()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(t.text, 2));
            p.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
        } else if (hasFocus() || underMouse()) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(hasFocus() ? t.accent2 : t.borderStrong, hasFocus() ? 2 : 1));
            p.drawEllipse(QRectF(rect()).adjusted(1, 1, -1, -1));
        }
    }
    void enterEvent(QEnterEvent*) override { update(); }
    void leaveEvent(QEvent*) override { update(); }
    void focusInEvent(QFocusEvent* e) override
    {
        QAbstractButton::focusInEvent(e);
        update();
    }
    void focusOutEvent(QFocusEvent* e) override
    {
        QAbstractButton::focusOutEvent(e);
        update();
    }

   private:
    QColor m_color;
};

/// A text button of a fixed height. The "sm" ghost buttons carry `min-height: 0` from the application stylesheet, which resets any
/// minimum the widget was given (setFixedHeight included) and lets a scroll area shrink its content over them; so the height is
/// also set in the button's own sheet, and the size hint and the Fixed policy agree with it.
class LinkButton : public QPushButton {
   public:
    LinkButton(const QString& text, int height, QWidget* parent) : QPushButton(text, parent), m_height(height)
    {
        setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        Style::setProperty(this, "zeroVariant", "ghost");
        Style::setProperty(this, "zeroSize", "sm");
        // The widget's own sheet wins over the application's: the minimum height becomes the real one (border included).
        setStyleSheet(QStringLiteral("QPushButton { min-height: %1px; }").arg(height - 2));
        setCursor(Qt::PointingHandCursor);
    }
    QSize sizeHint() const override { return { QPushButton::sizeHint().width(), m_height }; }
    QSize minimumSizeHint() const override { return { QPushButton::minimumSizeHint().width(), m_height }; }

   private:
    int m_height;
};

/// A strip that paints a 1 px rule on one edge (the footer's top, in the theme's border color).
class RuleWidget : public QWidget {
   public:
    explicit RuleWidget(QWidget* parent) : QWidget(parent) {}

   protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.fillRect(QRect(0, 0, width(), 1), Theme::current().border);
    }
};

QString presetName(const Tokens::AccentPreset& preset)
{
    return QCoreApplication::translate("Zero::ThemeEditor", preset.name);
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------------

ThemeEditorPanel::ThemeEditorPanel(ThemeLibrary* library, QWidget* parent) : QWidget(parent), m_library(library)
{
    m_model = new ThemeEditorModel(this);
    setMinimumWidth(kMinWidth);
    setMaximumWidth(kMaxWidth);
    setFixedWidth(kDefaultWidth);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setFocusPolicy(Qt::NoFocus);
    setAccessibleName(tr("Theme editor"));
    hide();

    build();

    m_live.setSingleShot(true);
    m_live.setInterval(kLiveMs);
    connect(&m_live, &QTimer::timeout, this, [this]() {
        if (m_open)
            applyLive();
    });
    connect(m_model, &ThemeEditorModel::changed, this, &ThemeEditorPanel::syncFromModel);
    connect(m_model, &ThemeEditorModel::lookChanged, this, [this]() {
        if (m_open)
            scheduleLive();
    });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcons();
        update();
    });
    connect(m_library, &ThemeLibrary::themeDeleted, this, [this](const QString& id) {
        if (m_open && editingThemeId() == id)
            discard();
    });

    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this, nullptr, nullptr, Qt::WidgetWithChildrenShortcut);
    connect(esc, &QShortcut::activated, this, &ThemeEditorPanel::cancel);

    syncFromModel();
    refreshIcons();
}

QSize ThemeEditorPanel::sizeHint() const
{
    return { width(), 600 };
}

QString ThemeEditorPanel::editingThemeId() const
{
    return m_open && !m_model->isNew() ? m_model->themeId() : QString();
}

QStringList ThemeEditorPanel::allColorsKeys() const
{
    QStringList keys = Custom::overrideKeys();
    for (const char* main : { "accent", "bg", "text" })
        keys.removeAll(QLatin1String(main));  // the rows above are these
    return keys;
}

QLineEdit* ThemeEditorPanel::hexEdit(const QString& key, bool allColors) const
{
    const auto& map = allColors ? m_tokens : m_main;
    const auto it = map.constFind(key);
    return it == map.cend() ? nullptr : it->hex;
}

QPushButton* ThemeEditorPanel::resetButton(const QString& key) const
{
    const auto it = m_tokens.constFind(key);
    return it == m_tokens.cend() ? nullptr : it->reset;
}

// ---------------------------------------------------------------------------------------------------------------------
// Construction

ThemeEditorPanel::Row ThemeEditorPanel::makeRow(const QString& key, const QString& label, bool withReset, bool isMain, QWidget* parent)
{
    Row row;
    row.key = key;
    row.widget = new QWidget(parent);
    row.layout = new QHBoxLayout(row.widget);
    row.layout->setContentsMargins(0, 0, 0, 0);
    row.layout->setSpacing(Tokens::spaceSm);

    auto* swatch = new ColorSwatch(row.widget);
    swatch->setAccessibleName(tr("%1 color").arg(label));
    swatch->setToolTip(tr("Pick %1").arg(label.toLower()));
    row.swatch = swatch;
    connect(swatch, &QAbstractButton::clicked, this, [this, key]() { pickColor(key); });
    row.layout->addWidget(swatch);

    row.label = makeLabel(label, Role::Body, row.widget);
    row.layout->addWidget(row.label, 1);

    if (withReset) {
        row.reset = new LinkButton(tr("Reset"), 22, row.widget);
        row.reset->setAccessibleName(tr("Reset %1").arg(label));
        row.reset->hide();
        connect(row.reset, &QPushButton::clicked, this, [this, key]() { m_model->resetToken(key); });
        row.layout->addWidget(row.reset);
    }

    row.hex = new QLineEdit(row.widget);
    row.hex->setFixedWidth(kHexWidth);
    row.hex->setMaxLength(7);
    row.hex->setValidator(new QRegularExpressionValidator(QRegularExpression(QStringLiteral("^#?[0-9A-Fa-f]{0,6}$")), row.hex));
    row.hex->setAccessibleName(tr("%1 hex value").arg(label));
    row.hex->setPlaceholderText(QStringLiteral("#RRGGBB"));
    connect(row.hex, &QLineEdit::textEdited, this, [this, key, isMain]() { hexEdited(key, isMain); });
    connect(row.hex, &QLineEdit::editingFinished, this, [this, key, isMain]() { hexFinished(key, isMain); });
    row.layout->addWidget(row.hex);
    return row;
}

void ThemeEditorPanel::build()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(1, 0, 0, 0);  // the left edge is the panel's border
    root->setSpacing(0);

    // ---- header
    auto* header = new QWidget(this);
    auto* head = new QHBoxLayout(header);
    head->setContentsMargins(Tokens::spaceLg, Tokens::spaceMd, Tokens::spaceMd, Tokens::spaceSm);
    head->setSpacing(Tokens::spaceSm);
    m_title = makeLabel(tr("New theme"), Role::Heading, header);
    head->addWidget(m_title, 1);
    m_menuButton = new QToolButton(header);
    Style::setProperty(m_menuButton, "zeroVariant", "ghost");
    Style::setProperty(m_menuButton, "zeroSize", "icon");
    m_menuButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_menuButton->setIconSize(QSize(16, 16));
    m_menuButton->setFixedSize(28, 28);
    m_menuButton->setCursor(Qt::PointingHandCursor);
    m_menuButton->setToolTip(tr("Theme options"));
    m_menuButton->setAccessibleName(tr("Theme options"));
    connect(m_menuButton, &QToolButton::clicked, this, &ThemeEditorPanel::showMenu);
    head->addWidget(m_menuButton);
    root->addWidget(header);

    // ---- scrolling body
    auto* scroll = new QScrollArea(this);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; border: none; }"));
    scroll->viewport()->setAutoFillBackground(false);
    auto* body = new QWidget;
    scroll->setWidget(body);
    body->setAutoFillBackground(false);  // after setWidget(): the scroll area turns it on, and the panel paints its own surface
    auto* col = new QVBoxLayout(body);
    col->setContentsMargins(Tokens::spaceLg, Tokens::spaceSm, Tokens::spaceLg, Tokens::spaceLg);
    col->setSpacing(0);

    const auto section = [&](const QString& title) {
        col->addSpacing(Tokens::spaceLg + Tokens::spaceXs);
        col->addWidget(makeLabel(title, Role::Overline, body));
        col->addSpacing(Tokens::spaceSm);
    };

    // Name
    m_name = new QLineEdit(body);
    m_name->setPlaceholderText(tr("Theme name"));
    m_name->setAccessibleName(tr("Theme name"));
    m_name->setMaxLength(60);
    connect(m_name, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (!m_syncing)
            m_model->setName(text);
    });
    col->addWidget(m_name);

    // Start from
    section(tr("Start from"));
    m_base = new Content::SegmentedControl({ { QStringLiteral("obsidian"), tr("Obsidian"), {} }, { QStringLiteral("minecraft"), tr("Minecraft"), {} } }, body);
    m_base->setAccessibleName(tr("Start from"));
    m_base->onActivated = [this](const QString& v) { m_model->setBase(Custom::baseFromString(v)); };
    col->addWidget(m_base);

    // Colors
    section(tr("Colors"));
    const struct {
        const char* key;
        QString label;
    } mains[] = { { "accent", tr("Accent") }, { "bg", tr("Background") }, { "text", tr("Text") } };
    for (const auto& m : mains) {
        Row row = makeRow(QString::fromLatin1(m.key), m.label, false, true, body);
        m_main.insert(row.key, row);
        col->addWidget(row.widget);
        if (row.key == QLatin1String("accent")) {
            auto* dots = new QWidget(body);
            auto* dl = new QHBoxLayout(dots);
            dl->setContentsMargins(kSwatchSize + Tokens::spaceSm - 3, 0, 0, 0);
            dl->setSpacing(Tokens::spaceXs);
            for (const auto& preset : Tokens::accentPresets) {
                auto* dot = new PresetDot(QColor::fromRgba(preset.rgb), dots);
                dot->setToolTip(presetName(preset));
                dot->setAccessibleName(tr("%1 accent preset").arg(presetName(preset)));
                connect(dot, &QAbstractButton::clicked, this, [this, c = dot->color()]() { applyColor(QStringLiteral("accent"), c); });
                dl->addWidget(dot);
                m_dots << dot;
            }
            dl->addStretch(1);
            col->addWidget(dots);
        }
        if (row.key == QLatin1String("text")) {
            m_readability = new QLabel(row.widget);
            Style::applyRole(m_readability, Role::Caption);
            m_readability->setAccessibleName(tr("Readability"));
            row.layout->insertWidget(row.layout->indexOf(row.hex), m_readability);
        }
        col->addSpacing(Tokens::spaceSm);
    }

    m_allToggle = new LinkButton(tr("All colors"), 24, body);
    m_allToggle->setCheckable(true);
    m_allToggle->setAccessibleName(tr("All colors"));
    m_allToggle->setIconSize(QSize(14, 14));
    connect(m_allToggle, &QPushButton::toggled, this, [this](bool on) {
        m_allBlock->setVisible(on);
        refreshIcons();
    });
    auto* toggleRow = new QHBoxLayout;
    toggleRow->setContentsMargins(0, 0, 0, 0);
    toggleRow->addWidget(m_allToggle);
    toggleRow->addStretch(1);
    col->addLayout(toggleRow);

    m_allBlock = new QWidget(body);
    auto* all = new QVBoxLayout(m_allBlock);
    all->setContentsMargins(0, Tokens::spaceXs, 0, 0);
    all->setSpacing(Tokens::spaceSm);
    for (const QString& key : allColorsKeys()) {
        Row row = makeRow(key, ThemeEditorModel::tokenLabel(key), true, false, m_allBlock);
        m_tokens.insert(key, row);
        all->addWidget(row.widget);
    }
    m_allBlock->hide();
    col->addWidget(m_allBlock);

    // App icon
    section(tr("App icon"));
    const QString iconTip = tr("Window, taskbar and sidebar. The desktop shortcut keeps the ZERO icon.");
    m_iconControl = new Content::SegmentedControl({ { QStringLiteral("default"), tr("Default"), {} }, { QStringLiteral("custom"), tr("Custom"), {} } }, body);
    m_iconControl->setAccessibleName(tr("App icon"));
    m_iconControl->setToolTip(iconTip);
    m_iconControl->onActivated = [this](const QString& v) {
        if (v == QLatin1String("default")) {
            m_model->selectDefaultIcon();
        } else if (!m_model->selectCustomIcon()) {
            chooseIcon();
            syncFromModel();  // a cancelled dialog leaves the segment where it was
        }
    };
    col->addWidget(m_iconControl);

    m_iconBlock = new QWidget(body);
    auto* iconRow = new QHBoxLayout(m_iconBlock);
    iconRow->setContentsMargins(0, Tokens::spaceMd, 0, 0);
    iconRow->setSpacing(Tokens::spaceSm);
    m_iconPreview = new QLabel(m_iconBlock);
    m_iconPreview->setFixedSize(kIconPreviewSize, kIconPreviewSize);
    m_iconPreview->setAlignment(Qt::AlignCenter);
    m_iconPreview->setToolTip(iconTip);
    m_iconPreview->setAccessibleName(tr("App icon preview"));
    iconRow->addWidget(m_iconPreview);
    m_iconName = new ElidedLabel(m_iconBlock);
    Style::applyRole(m_iconName, Role::Body);
    m_iconName->setAccessibleName(tr("App icon file"));
    iconRow->addWidget(m_iconName, 1);
    auto* iconChange = new LinkButton(tr("Change"), 22, m_iconBlock);
    iconChange->setAccessibleName(tr("Change app icon"));
    connect(iconChange, &QPushButton::clicked, this, [this]() { chooseIcon(); });
    iconRow->addWidget(iconChange);
    m_iconBlock->hide();
    col->addWidget(m_iconBlock);

    // Background
    section(tr("Background"));
    m_background = new Content::SegmentedControl({ { QStringLiteral("none"), tr("None"), {} }, { QStringLiteral("image"), tr("Image"), {} }, { QStringLiteral("video"), tr("Video"), {} } }, body);
    m_background->setAccessibleName(tr("Background"));
    m_background->onActivated = [this](const QString& v) {
        if (v == QLatin1String("none")) {
            m_model->selectNoBackground();
        } else if (v == QLatin1String("video")) {
            if (!m_model->selectVideoBackground()) {
                chooseVideo();
                syncFromModel();  // a cancelled dialog leaves the segment where it was
            }
        } else if (!m_model->selectImageBackground()) {
            chooseImage();
            syncFromModel();  // a cancelled dialog leaves the segment where it was
        }
    };
    col->addWidget(m_background);

    m_imageBlock = new QWidget(body);
    auto* image = new QVBoxLayout(m_imageBlock);
    image->setContentsMargins(0, Tokens::spaceMd, 0, 0);
    image->setSpacing(Tokens::spaceMd);
    auto* fileRow = new QHBoxLayout;
    fileRow->setContentsMargins(0, 0, 0, 0);
    fileRow->setSpacing(Tokens::spaceSm);
    m_imageName = new ElidedLabel(m_imageBlock);
    Style::applyRole(m_imageName, Role::Body);
    m_imageName->setAccessibleName(tr("Background picture"));
    fileRow->addWidget(m_imageName, 1);
    m_imageChange = new LinkButton(tr("Change"), 22, m_imageBlock);
    m_imageChange->setAccessibleName(tr("Change background picture"));
    connect(m_imageChange, &QPushButton::clicked, this, [this]() {
        if (m_model->backgroundChoice() == ThemeEditorModel::BackgroundChoice::Video)
            chooseVideo();
        else
            chooseImage();
    });
    fileRow->addWidget(m_imageChange);
    image->addLayout(fileRow);

    // The video's sound (video only): off by default; the volume shows only while it is on.
    m_soundBlock = new QWidget(m_imageBlock);
    auto* soundBox = new QVBoxLayout(m_soundBlock);
    soundBox->setContentsMargins(0, 0, 0, 0);
    soundBox->setSpacing(Tokens::spaceMd);
    m_sound = new QCheckBox(tr("Sound"), m_soundBlock);
    m_sound->setAccessibleName(tr("Video sound"));
    m_sound->setToolTip(tr("Play the video's sound. It stops whenever the video pauses."));
    connect(m_sound, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_syncing)
            m_model->setSound(on);
    });
    soundBox->addWidget(m_sound);
    m_volumeRow = new QWidget(m_soundBlock);
    auto* volumeBox = new QVBoxLayout(m_volumeRow);
    volumeBox->setContentsMargins(0, 0, 0, 0);
    volumeBox->setSpacing(Tokens::spaceXs);
    auto* volumeLine = new QHBoxLayout;
    volumeLine->setContentsMargins(0, 0, 0, 0);
    volumeLine->addWidget(makeLabel(tr("Volume"), Role::Body, m_volumeRow), 1);
    m_volumeValue = makeLabel(QString(), Role::Caption, m_volumeRow);
    volumeLine->addWidget(m_volumeValue);
    volumeBox->addLayout(volumeLine);
    m_volume = new QSlider(Qt::Horizontal, m_volumeRow);
    m_volume->setRange(0, 100);
    m_volume->setAccessibleName(tr("Video volume"));
    volumeBox->addWidget(m_volume);
    connect(m_volume, &QSlider::valueChanged, this, [this](int v) {
        if (!m_syncing)
            m_model->setVolume(v / 100.0);
    });
    soundBox->addWidget(m_volumeRow);
    m_soundBlock->hide();
    image->addWidget(m_soundBlock);

    const auto sliderRow = [&](const QString& title, const QString& accessible, int minimum, int maximum, QSlider** sliderOut, QLabel** valueOut) {
        auto* box = new QVBoxLayout;
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(Tokens::spaceXs);
        auto* line = new QHBoxLayout;
        line->setContentsMargins(0, 0, 0, 0);
        line->addWidget(makeLabel(title, Role::Body, m_imageBlock), 1);
        *valueOut = makeLabel(QString(), Role::Caption, m_imageBlock);
        line->addWidget(*valueOut);
        box->addLayout(line);
        *sliderOut = new QSlider(Qt::Horizontal, m_imageBlock);
        (*sliderOut)->setRange(minimum, maximum);
        (*sliderOut)->setAccessibleName(accessible);
        box->addWidget(*sliderOut);
        image->addLayout(box);
    };
    sliderRow(tr("Dim"), tr("Background dim"), 0, 90, &m_dim, &m_dimValue);
    sliderRow(tr("Cards"), tr("Card opacity"), int(Custom::kMinCardOpacity * 100.0 + 0.5), 100, &m_cards, &m_cardsValue);
    connect(m_dim, &QSlider::valueChanged, this, [this](int v) {
        if (!m_syncing)
            m_model->setDim(v / 100.0);
    });
    connect(m_cards, &QSlider::valueChanged, this, [this](int v) {
        if (!m_syncing)
            m_model->setCardOpacity(v / 100.0);
    });
    m_imageBlock->hide();
    col->addWidget(m_imageBlock);

    // Font
    section(tr("Font"));
    m_fontControl = new Content::SegmentedControl({ { QStringLiteral("default"), tr("Default"), {} }, { QStringLiteral("custom"), tr("Custom"), {} } }, body);
    m_fontControl->setAccessibleName(tr("Font"));
    m_fontControl->onActivated = [this](const QString& v) {
        if (v == QLatin1String("default")) {
            m_model->selectDefaultFont();
        } else if (!m_model->selectCustomFont()) {
            chooseFont();
            syncFromModel();  // a cancelled dialog leaves the segment where it was
        }
    };
    col->addWidget(m_fontControl);

    m_fontBlock = new QWidget(body);
    auto* fontBox = new QVBoxLayout(m_fontBlock);
    fontBox->setContentsMargins(0, Tokens::spaceMd, 0, 0);
    fontBox->setSpacing(Tokens::spaceSm);
    auto* fontRow = new QHBoxLayout;
    fontRow->setContentsMargins(0, 0, 0, 0);
    fontRow->setSpacing(Tokens::spaceSm);
    m_fontName = new ElidedLabel(m_fontBlock);
    Style::applyRole(m_fontName, Role::Body);
    m_fontName->setAccessibleName(tr("Font file"));
    fontRow->addWidget(m_fontName, 1);
    auto* fontChange = new LinkButton(tr("Change"), 22, m_fontBlock);
    fontChange->setAccessibleName(tr("Change font"));
    connect(fontChange, &QPushButton::clicked, this, [this]() { chooseFont(); });
    fontRow->addWidget(fontChange);
    fontBox->addLayout(fontRow);
    m_fontPreview = new QLabel(tr("The quick brown fox jumps over the lazy dog"), m_fontBlock);
    m_fontPreview->setWordWrap(true);
    m_fontPreview->setAccessibleName(tr("Font preview"));
    fontBox->addWidget(m_fontPreview);
    m_fontBlock->hide();
    col->addWidget(m_fontBlock);

    // Corners
    section(tr("Corners"));
    m_corners = new Content::SegmentedControl({ { QStringLiteral("rounded"), tr("Rounded"), {} }, { QStringLiteral("square"), tr("Square"), {} } }, body);
    m_corners->setAccessibleName(tr("Corners"));
    m_corners->onActivated = [this](const QString& v) {
        m_model->setCorners(v == QLatin1String("square") ? Custom::CornerStyle::Square : Custom::CornerStyle::Rounded);
    };
    col->addWidget(m_corners);
    col->addStretch(1);
    root->addWidget(scroll, 1);

    // ---- footer
    root->addWidget(new RuleWidget(this));
    auto* footer = new QWidget(this);
    auto* foot = new QHBoxLayout(footer);
    foot->setContentsMargins(Tokens::spaceLg, Tokens::spaceMd, Tokens::spaceLg, Tokens::spaceMd);
    foot->setSpacing(Tokens::spaceSm);
    foot->addStretch(1);
    m_cancel = new QPushButton(tr("Cancel"), footer);
    Style::setProperty(m_cancel, "zeroVariant", "secondary");
    connect(m_cancel, &QPushButton::clicked, this, &ThemeEditorPanel::cancel);
    foot->addWidget(m_cancel);
    m_save = new QPushButton(tr("Save theme"), footer);
    Style::setProperty(m_save, "zeroVariant", "primary");
    connect(m_save, &QPushButton::clicked, this, [this]() { save(); });
    foot->addWidget(m_save);
    root->addWidget(footer);

    setTabOrder(m_menuButton, m_name);
}

// ---------------------------------------------------------------------------------------------------------------------
// Sessions

void ThemeEditorPanel::begin(const std::function<void()>& loadModel)
{
    const bool wasOpen = m_open;
    if (!wasOpen) {
        const auto& active = m_library->hooks().activeThemeId;
        m_activeAtOpen = active ? active() : QString();
    }
    m_live.stop();
    loadModel();
    m_live.stop();  // loading the model asked for a live apply; the one below replaces it
    m_open = true;
    syncFromModel();
    setAllColorsExpanded(false);
    if (!wasOpen) {
        show();
        Motion::fadeIn(this, Tokens::durBase);
    }
    // The app shows exactly what the panel shows from the first moment.
    applyLive();
    m_name->setFocus();
    m_name->selectAll();
    emit openChanged(true);
}

void ThemeEditorPanel::openNew()
{
    begin([this]() {
        const QString name = Custom::uniqueName(tr("My theme"), Custom::existingThemeNames(m_library->themesRoot()));
        m_model->beginNew(name);
    });
}

bool ThemeEditorPanel::openEdit(const QString& themeId)
{
    const QString folder = m_library->folderOf(themeId);
    if (folder.isEmpty())
        return false;
    auto def = Custom::loadDefinition(folder);
    if (!def) {
        ZeroToast::show(this, def.error(), ZeroToast::Kind::Error);
        return false;
    }
    begin([&]() { m_model->beginEdit(*def, folder, themeId); });
    return true;
}

void ThemeEditorPanel::closePanel()
{
    m_live.stop();
    if (!m_open)
        return;
    m_open = false;
    hide();
    emit openChanged(false);
}

void ThemeEditorPanel::cancel()
{
    if (!m_open)
        return;
    const QString id = m_activeAtOpen;
    closePanel();
    if (const auto& reapply = m_library->hooks().reapplyTheme)
        reapply(id);  // even when it is the id that is on screen: the live edits are not on disk
}

void ThemeEditorPanel::discard()
{
    if (!m_open)
        return;
    closePanel();
    const auto& hooks = m_library->hooks();
    if (hooks.reapplyTheme)
        hooks.reapplyTheme(hooks.activeThemeId ? hooks.activeThemeId() : QString());
}

bool ThemeEditorPanel::save()
{
    if (!m_open || !m_model->nameIsValid())
        return false;
    m_live.stop();
    QString id;
    if (!m_library->saveTheme(m_model->folder(), m_model->definition(), m_model->pendingBackground(), nullptr, &id,
                              { m_model->pendingIcon(), m_model->pendingFont() }))
        return false;  // the library showed the error; the panel stays as the user left it
    closePanel();
    emit saved(id);
    return true;
}

void ThemeEditorPanel::flushPreview()
{
    if (!m_live.isActive())
        return;
    m_live.stop();
    if (m_open)
        applyLive();
}

void ThemeEditorPanel::applyLive()
{
    const ThemeSpec spec = m_model->spec();
    m_library->hooks().previewSpec(spec);
    emit previewed(spec);
}

void ThemeEditorPanel::scheduleLive()
{
    // Throttle, not debounce: the first change starts the clock and whatever the state is when it rings gets applied, so a long drag
    // keeps updating (about 8 times a second) instead of waiting for the pointer to stop.
    if (!m_live.isActive())
        m_live.start();
}

void ThemeEditorPanel::fitTo(int pageWidth)
{
    setFixedWidth(std::clamp(int(pageWidth * 0.28), kMinWidth, kMaxWidth));
}

// ---------------------------------------------------------------------------------------------------------------------
// Editing

void ThemeEditorPanel::applyColor(const QString& key, const QColor& color)
{
    if (!color.isValid() || color.rgb() == m_model->tokenColor(key).rgb())
        return;
    m_model->setTokenColor(key, color);
}

void ThemeEditorPanel::hexEdited(const QString& key, bool isMain)
{
    if (m_syncing)
        return;
    const Row& row = isMain ? m_main[key] : m_tokens[key];
    QString text = row.hex->text().trimmed();
    if (text.remove(QLatin1Char('#')).size() == 6)  // a complete color applies while typing
        applyColor(key, ThemeEditorModel::parseHex(row.hex->text()));
}

void ThemeEditorPanel::hexFinished(const QString& key, bool isMain)
{
    if (m_syncing)
        return;
    const Row& row = isMain ? m_main[key] : m_tokens[key];
    const QColor c = ThemeEditorModel::parseHex(row.hex->text());
    if (c.isValid())
        applyColor(key, c);
    // Canonical text again; an invalid entry reverts to the current value.
    m_syncing = true;
    row.hex->setText(ThemeEditorModel::toHex(m_model->tokenColor(key)));
    m_syncing = false;
}

QColorDialog* ThemeEditorPanel::pickColor(const QString& key)
{
    // While the dialog is open the app's style sheet is NOT re-applied: the picked color only shows in this row's swatch and hex, and the
    // theme (model, live preview, style sheet) is updated once, on OK. Re-applying the style sheet under an open dialog restyles it while
    // its layout is already fixed (clipped content) and gives the next dialog a different look than the one on screen. A preview that was
    // still waiting for its turn is applied now, so none lands while the dialog is up.
    flushPreview();
    const QColor before = m_model->tokenColor(key);

    auto* dialog = new QColorDialog(before, this);
    dialog->setOption(QColorDialog::DontUseNativeDialog, true);
    dialog->setOption(QColorDialog::ShowAlphaChannel, false);
    dialog->setWindowTitle(tr("Pick a color"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QColorDialog::currentColorChanged, this, [this, key](const QColor& c) { previewSwatch(key, c); });
    connect(dialog, &QColorDialog::colorSelected, this, [this, key](const QColor& c) { applyColor(key, c); });
    connect(dialog, &QDialog::finished, this, [this]() { syncFromModel(); });  // Cancel: the row shows the model's color again
    dialog->open();
    return dialog;
}

void ThemeEditorPanel::previewSwatch(const QString& key, const QColor& color)
{
    if (!color.isValid())
        return;
    const Row* row = nullptr;
    if (const auto it = m_main.constFind(key); it != m_main.cend())
        row = &*it;
    else if (const auto it2 = m_tokens.constFind(key); it2 != m_tokens.cend())
        row = &*it2;
    if (!row)
        return;
    m_syncing = true;
    static_cast<ColorSwatch*>(row->swatch)->setColor(ThemeSpec::opaque(color));
    row->hex->setText(ThemeEditorModel::toHex(color));
    m_syncing = false;
}

void ThemeEditorPanel::chooseImage()
{
    const QStringList patterns = [] {
        QStringList p;
        for (const QString& e : Custom::imageExtensions())
            p << QStringLiteral("*.") + e;
        return p;
    }();
    const QString start = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Choose a background picture"), start, tr("Pictures (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (path.isEmpty())
        return;
    if (Custom::imageExtension(path).isEmpty() || !QImageReader(path).canRead()) {
        ZeroToast::show(this, tr("ZERO could not read this picture."), ZeroToast::Kind::Error);
        return;
    }
    m_model->setImage(path);
}

void ThemeEditorPanel::chooseVideo()
{
    QStringList patterns;
    for (const QString& e : Custom::videoExtensions())
        patterns << QStringLiteral("*.") + e;
    const QString start = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Choose a background video"), start, tr("Videos (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (path.isEmpty())
        return;
    if (Custom::videoExtension(path).isEmpty()) {
        ZeroToast::show(this, tr("ZERO can use mp4, webm, mkv or mov videos as a background."), ZeroToast::Kind::Error);
        return;
    }
    if (QFileInfo(path).size() > Custom::kMaxBackgroundBytes) {
        ZeroToast::show(this, tr("This video is too big for a background. The limit is %1 MB.").arg(Custom::kMaxBackgroundBytes / (1024 * 1024)),
                        ZeroToast::Kind::Error);
        return;
    }
    m_model->setVideo(path);
}

void ThemeEditorPanel::chooseIcon()
{
    const QString start = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose an app icon"), start, tr("Icons (*.png *.svg *.ico)"));
    if (path.isEmpty())
        return;
    if (auto ok = Custom::validateIconSource(path); !ok) {
        ZeroToast::show(this, ok.error(), ZeroToast::Kind::Error);
        return;
    }
    m_model->setIcon(path);
}

void ThemeEditorPanel::chooseFont()
{
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose a font"), start, tr("Fonts (*.ttf *.otf)"));
    if (path.isEmpty())
        return;
    if (auto ok = Custom::validateFontSource(path); !ok) {
        ZeroToast::show(this, ok.error(), ZeroToast::Kind::Error);
        return;
    }
    if (CustomFont::load(path).isEmpty()) {  // Qt has to be able to load it, or the theme would silently keep the default font
        ZeroToast::show(this, tr("ZERO could not load this font."), ZeroToast::Kind::Error);
        return;
    }
    m_model->setFont(path);
}

void ThemeEditorPanel::showMenu()
{
    if (m_model->isNew() || !m_open)
        return;
    const QString id = m_model->themeId();
    const auto& t = Theme::current();
    QMenu menu(this);
    auto* duplicate = menu.addAction(Icons::icon(QStringLiteral("copy"), t.text), tr("Duplicate"));
    auto* exportAction = menu.addAction(Icons::icon(QStringLiteral("upload"), t.text), tr("Export…"));
    menu.addSeparator();
    auto* remove = menu.addAction(Icons::icon(QStringLiteral("trash-2"), t.danger), tr("Delete"));
    const QAction* chosen = menu.exec(m_menuButton->mapToGlobal(QPoint(m_menuButton->width() - menu.sizeHint().width(), m_menuButton->height() + 4)));
    if (chosen == duplicate)
        m_library->duplicateTheme(id);
    else if (chosen == exportAction)
        m_library->exportThemeWithDialog(id);
    else if (chosen == remove)
        m_library->deleteTheme(id);  // themeDeleted closes the panel
}

void ThemeEditorPanel::setAllColorsExpanded(bool expanded)
{
    m_allToggle->setChecked(expanded);
    m_allBlock->setVisible(expanded);
    refreshIcons();
}

// ---------------------------------------------------------------------------------------------------------------------
// Model -> widgets

void ThemeEditorPanel::syncFromModel()
{
    if (m_syncing)
        return;
    m_syncing = true;
    const ThemeSpec spec = m_model->spec();

    m_title->setText(m_model->isNew() ? tr("New theme") : tr("Edit theme"));
    m_menuButton->setVisible(!m_model->isNew());
    if (m_name->text() != m_model->name())
        m_name->setText(m_model->name());
    m_save->setEnabled(m_model->nameIsValid());

    m_base->setSelected(Custom::baseToString(m_model->base()));
    m_corners->setSelected(m_model->corners() == Custom::CornerStyle::Square ? QStringLiteral("square") : QStringLiteral("rounded"));

    const auto fill = [&](Row& row) {
        const QColor c = ThemeEditorModel::specColor(spec, row.key);
        static_cast<ColorSwatch*>(row.swatch)->setColor(c);
        if (!row.hex->hasFocus())
            row.hex->setText(ThemeEditorModel::toHex(c));
        if (row.reset)
            row.reset->setVisible(m_model->isOverridden(row.key));
    };
    for (auto it = m_main.begin(); it != m_main.end(); ++it)
        fill(it.value());
    for (auto it = m_tokens.begin(); it != m_tokens.end(); ++it)
        fill(it.value());

    for (QAbstractButton* dot : std::as_const(m_dots))
        dot->setChecked(static_cast<PresetDot*>(dot)->color().rgb() == spec.accent.rgb());

    const Custom::ReadabilityReport report = Custom::readability(spec);
    const bool readable = report.level == Custom::Readability::Readable;
    m_readability->setText(readable ? tr("Readable") : tr("Hard to read"));
    Style::setProperty(m_readability, "zeroTone", readable ? "success" : "warning");
    m_readability->setToolTip(readable ? tr("Text and buttons have enough contrast.") : report.summary);

    const auto choice = m_model->backgroundChoice();
    const bool video = choice == ThemeEditorModel::BackgroundChoice::Video;
    m_background->setSelected(video ? QStringLiteral("video")
                                    : (choice == ThemeEditorModel::BackgroundChoice::Image ? QStringLiteral("image") : QStringLiteral("none")));
    m_imageBlock->setVisible(choice != ThemeEditorModel::BackgroundChoice::None);
    m_imageName->setFullText(video ? m_model->videoName() : m_model->imageName());
    m_imageName->setAccessibleName(video ? tr("Background video") : tr("Background picture"));
    m_imageChange->setAccessibleName(video ? tr("Change background video") : tr("Change background picture"));
    m_soundBlock->setVisible(video);
    m_sound->setChecked(m_model->sound());
    m_volumeRow->setVisible(m_model->sound());
    m_volume->setValue(int(m_model->volume() * 100.0 + 0.5));
    m_volumeValue->setText(tr("%1%").arg(m_volume->value()));
    m_dim->setValue(int(m_model->dim() * 100.0 + 0.5));
    m_cards->setValue(int(m_model->cardOpacity() * 100.0 + 0.5));
    m_dimValue->setText(tr("%1%").arg(m_dim->value()));
    m_cardsValue->setText(tr("%1%").arg(m_cards->value()));

    // App icon
    const bool customIcon = m_model->iconChoice() == ThemeEditorModel::AssetChoice::Custom;
    m_iconControl->setSelected(customIcon ? QStringLiteral("custom") : QStringLiteral("default"));
    m_iconBlock->setVisible(customIcon);
    m_iconName->setFullText(m_model->iconName());
    {
        const QString iconPath = m_model->iconPath();
        QPixmap pm;
        if (!iconPath.isEmpty()) {
            const qreal dpr = devicePixelRatioF();
            pm = QIcon(iconPath).pixmap(QSize(kIconPreviewSize, kIconPreviewSize), dpr);
            pm.setDevicePixelRatio(dpr);
        }
        m_iconPreview->setPixmap(pm);
    }

    // Font
    const bool customFont = m_model->fontChoice() == ThemeEditorModel::AssetChoice::Custom;
    m_fontControl->setSelected(customFont ? QStringLiteral("custom") : QStringLiteral("default"));
    m_fontBlock->setVisible(customFont);
    m_fontName->setFullText(m_model->fontName());
    {
        QFont previewFont = m_fontPreview->font();
        const QString family = customFont && !m_model->fontPath().isEmpty() ? CustomFont::load(m_model->fontPath()) : QString();
        if (!family.isEmpty())
            previewFont.setFamilies({ family, QLatin1String(Tokens::fontFamilyPrimary), QLatin1String(Tokens::fontFamilyFallback) });
        else
            previewFont = Style::font(Role::Body);
        previewFont.setPixelSize(kFontPreviewPx);
        m_fontPreview->setFont(previewFont);
    }
    m_syncing = false;
}

void ThemeEditorPanel::refreshIcons()

{
    const auto& t = Theme::current();
    m_menuButton->setIcon(Icons::icon(QStringLiteral("ellipsis"), t.textSecondary));
    m_allToggle->setIcon(Icons::icon(m_allToggle->isChecked() ? QStringLiteral("chevron-up") : QStringLiteral("chevron-down"), t.textSecondary));
}

void ThemeEditorPanel::paintEvent(QPaintEvent*)
{
    const auto& t = Theme::current();
    QPainter p(this);
    p.fillRect(rect(), t.elevated);
    p.fillRect(QRect(0, 0, 1, height()), t.border);
}

}  // namespace Zero
