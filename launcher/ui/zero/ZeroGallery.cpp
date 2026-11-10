// SPDX-License-Identifier: GPL-3.0-only
#include "ZeroGallery.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include "ZeroCard.h"
#include "ZeroEmptyState.h"
#include "ZeroIcons.h"
#include "ZeroNavRail.h"
#include "ZeroPageHeader.h"
#include "ZeroSpinner.h"
#include "ZeroStyle.h"
#include "ZeroThemeState.h"
#include "ZeroToast.h"

namespace Zero {

namespace {

QLabel* label(const QString& text, Role role, QWidget* parent, const char* tone = nullptr)
{
    auto* l = new QLabel(text, parent);
    Style::applyRole(l, role);
    if (tone)
        Style::setProperty(l, "zeroTone", tone);
    return l;
}

/// A card with an overline title and a vertical body layout.
ZeroCard* section(const QString& title, QWidget* parent, QVBoxLayout** body)
{
    auto* card = new ZeroCard(parent);
    card->setHoverEnabled(false);
    card->setContentsMargins(Tokens::spaceMd, Tokens::spaceMd, Tokens::spaceMd, Tokens::spaceMd);
    auto* lay = new QVBoxLayout(card);
    lay->setSpacing(Tokens::spaceSm);
    lay->addWidget(label(title, Role::Overline, card));
    *body = lay;
    return card;
}

QPushButton* button(const QString& text, const char* variant, QWidget* parent, const char* size = nullptr)
{
    auto* b = new QPushButton(text, parent);
    Style::setProperty(b, "zeroVariant", variant);
    if (size)
        Style::setProperty(b, "zeroSize", size);
    return b;
}

/// Forces a sample into its hover or pressed visual state (gallery only).

}  // namespace

ZeroGallery::ZeroGallery(QWidget* parent) : QWidget(parent), m_rail(new ZeroNavRail(this))
{
    setWindowTitle(tr("ZERO component gallery"));
    setAutoFillBackground(true);

    // ---- nav rail ----------------------------------------------------------------------------------------------
    m_rail->addItem("home", "house", tr("Home"));
    m_rail->addItem("library", "library-big", tr("Library"));
    m_rail->addItem("discover", "compass", tr("Discover"));
    m_rail->addItem("servers", "earth", tr("Servers"));
    m_rail->addItem("tools", "wrench", tr("Tools"));
    m_rail->addItem("downloads", "download", tr("Downloads"));
    m_rail->addItem("appearance", "palette", tr("Appearance"));
    m_rail->addStretch();
    {
        auto* chip = new ZeroCard;
        chip->setClickable(true);
        chip->setContentsMargins(Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm, Tokens::spaceSm);
        auto* row = new QHBoxLayout(chip);
        row->setSpacing(Tokens::spaceSm);
        auto* avatar = new QLabel(chip);
        avatar->setFixedSize(24, 24);
        avatar->setPixmap(Icons::pixmap("user", Theme::current().textSecondary, 24, devicePixelRatioF()));
        row->addWidget(avatar);
        row->addWidget(label(tr("Player"), Role::Label, chip), 1);
        auto* chev = new QLabel(chip);
        chev->setPixmap(Icons::pixmap("chevron-down", Theme::current().textTertiary, 14, devicePixelRatioF()));
        row->addWidget(chev);
        m_rail->setBottomWidget(chip);
    }
    m_rail->addItem("settings", "settings", tr("Settings"));
    m_rail->setCurrent("library");

    // ---- page --------------------------------------------------------------------------------------------------
    auto* page = new QWidget(this);
    auto* pageLay = new QVBoxLayout(page);
    pageLay->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceMd);
    pageLay->setSpacing(0);

    auto* header = new ZeroPageHeader(tr("Component gallery"), tr("Every ZERO component and styled Qt control, on one page"), page);
    {
        auto* collapse = button(tr("Collapse rail"), "ghost", header);
        connect(collapse, &QPushButton::clicked, this, [this]() { m_rail->setCollapsed(!m_rail->isCollapsed()); });
        header->addAction(collapse);
        auto* motion = new QCheckBox(tr("Reduce motion"), header);
        motion->setChecked(Theme::reduceMotion());
        connect(motion, &QCheckBox::toggled, this, [](bool on) { Theme::setReduceMotion(on); });
        header->addAction(motion);
        auto* create = button(tr("New instance"), "primary", header);
        create->setIcon(Icons::icon("plus", Theme::current().onAccent));
        header->addAction(create);
    }
    pageLay->addWidget(header);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(Tokens::spaceLg);
    grid->setVerticalSpacing(Tokens::spaceMd);
    pageLay->addLayout(grid, 1);

    QVBoxLayout* body = nullptr;

    // ---- column 0: typography, buttons, badges ---------------------------------------------------------------
    auto* col0 = new QVBoxLayout;
    col0->setSpacing(Tokens::spaceMd);
    {
        auto* c = section(tr("TYPOGRAPHY"), page, &body);
        body->addWidget(label(tr("Display 28"), Role::Display, c));
        body->addWidget(label(tr("Title 20"), Role::Title, c));
        body->addWidget(label(tr("Heading 15"), Role::Heading, c));
        body->addWidget(label(tr("Body 13 regular text"), Role::Body, c));
        body->addWidget(label(tr("Label 13 medium"), Role::Label, c));
        body->addWidget(label(tr("Caption 11 secondary"), Role::Caption, c));
        body->addWidget(label(tr("Overline 10 section"), Role::Overline, c));
        body->addWidget(label(tr("Secondary tone text"), Role::Body, c, "secondary"));
        body->addWidget(label(tr("Tertiary tone text"), Role::Body, c, "tertiary"));
        col0->addWidget(c);
    }
    {
        auto* c = section(tr("BUTTONS  (rest / hover / pressed / disabled)"), page, &body);
        const struct {
            const char* variant;
            const char* text;
        } variants[] = { { "primary", "Primary" }, { "secondary", "Secondary" }, { "ghost", "Ghost" }, { "danger", "Danger" } };
        for (const auto& v : variants) {
            auto* row = new QHBoxLayout;
            row->setSpacing(Tokens::spaceSm);
            auto* rest = button(v.text, v.variant, c);
            auto* hover = button(tr("Hover"), v.variant, c);
            m_hoverSamples << hover;
            auto* pressed = button(tr("Press"), v.variant, c);
            pressed->setDown(true);  // pressed look is driven by the button state, so it can be shown statically
            auto* disabled = button(tr("Off"), v.variant, c);
            disabled->setEnabled(false);
            row->addWidget(rest);
            row->addWidget(hover);
            row->addWidget(pressed);
            row->addWidget(disabled);
            row->addStretch(1);
            body->addLayout(row);
        }
        auto* lg = new QHBoxLayout;
        auto* play = button(tr("Play"), "primary", c, "lg");
        play->setIcon(Icons::icon("play", Theme::current().onAccent));
        auto* lgSec = button(tr("Large secondary"), "secondary", c, "lg");
        lg->addWidget(play);
        lg->addWidget(lgSec);
        lg->addStretch(1);
        body->addLayout(lg);

        auto* tools = new QHBoxLayout;
        tools->setSpacing(Tokens::spaceSm);
        for (const char* ic : { "star", "folder-open", "pencil", "copy", "trash-2" }) {
            auto* tb = new QToolButton(c);
            tb->setIcon(Icons::icon(ic, Theme::current().text));
            tb->setIconSize(QSize(16, 16));
            Style::setProperty(tb, "zeroVariant", QString(ic) == "trash-2" ? "danger" : "ghost");
            tools->addWidget(tb);
        }
        auto* menuBtn = new QToolButton(c);
        menuBtn->setText(tr("Menu"));
        menuBtn->setIcon(Icons::icon("ellipsis", Theme::current().text));
        menuBtn->setPopupMode(QToolButton::InstantPopup);
        auto* menu = new QMenu(menuBtn);
        menu->addAction(Icons::icon("play", Theme::current().text), tr("Launch"));
        menu->addAction(Icons::icon("pencil", Theme::current().text), tr("Edit"));
        menu->addSeparator();
        menu->addAction(Icons::icon("trash-2", Theme::current().danger), tr("Delete"));
        menuBtn->setMenu(menu);
        tools->addWidget(menuBtn);
        tools->addStretch(1);
        body->addLayout(tools);
        col0->addWidget(c);
    }
    {
        auto* c = section(tr("BADGES"), page, &body);
        auto* row = new QHBoxLayout;
        row->setSpacing(Tokens::spaceSm);
        for (const char* kind : { "neutral", "success", "warning", "danger", "info" }) {
            auto* b = new QLabel(QString(kind).replace(0, 1, QString(kind).left(1).toUpper()), c);
            Style::setProperty(b, "zeroBadge", kind);
            row->addWidget(b);
        }
        row->addStretch(1);
        body->addLayout(row);
        auto* rule = new QFrame(c);
        rule->setFrameShape(QFrame::HLine);
        body->addWidget(rule);
        col0->addWidget(c);
    }
    col0->addStretch(1);
    grid->addLayout(col0, 0, 0);

    // ---- column 1: inputs, check/radio, progress, tabs --------------------------------------------------------
    auto* col1 = new QVBoxLayout;
    col1->setSpacing(Tokens::spaceMd);
    {
        auto* c = section(tr("INPUTS"), page, &body);
        auto* le = new QLineEdit(tr("Instance name"), c);
        auto* ph = new QLineEdit(c);
        ph->setPlaceholderText(tr("Search instances"));
        auto* dis = new QLineEdit(tr("Disabled field"), c);
        dis->setEnabled(false);
        auto* combo = new QComboBox(c);
        combo->addItems({ tr("Vanilla 1.21"), tr("Fabric 1.21"), tr("NeoForge 1.21") });
        auto* spin = new QSpinBox(c);
        spin->setRange(1, 64);
        spin->setValue(8);
        spin->setSuffix(" GiB");
        auto* text = new QPlainTextEdit(c);
        text->setPlainText(tr("Multi-line text area\nSecond line of text"));
        text->setFixedHeight(64);
        body->addWidget(le);
        body->addWidget(ph);
        body->addWidget(dis);
        body->addWidget(combo);
        body->addWidget(spin);
        body->addWidget(text);
        col1->addWidget(c);
    }
    {
        auto* c = section(tr("CHECK / RADIO / SLIDER"), page, &body);
        auto* row1 = new QHBoxLayout;
        auto* chk1 = new QCheckBox(tr("Checked"), c);
        chk1->setChecked(true);
        auto* chk2 = new QCheckBox(tr("Unchecked"), c);
        auto* chk3 = new QCheckBox(tr("Partial"), c);
        chk3->setTristate(true);
        chk3->setCheckState(Qt::PartiallyChecked);
        row1->addWidget(chk1);
        row1->addWidget(chk2);
        row1->addWidget(chk3);
        row1->addStretch(1);
        body->addLayout(row1);
        auto* row2 = new QHBoxLayout;
        auto* r1 = new QRadioButton(tr("Selected"), c);
        r1->setChecked(true);
        auto* r2 = new QRadioButton(tr("Other"), c);
        auto* r3 = new QRadioButton(tr("Disabled"), c);
        r3->setEnabled(false);
        auto* chk4 = new QCheckBox(tr("Disabled"), c);
        chk4->setChecked(true);
        chk4->setEnabled(false);
        row2->addWidget(r1);
        row2->addWidget(r2);
        row2->addWidget(r3);
        row2->addStretch(1);
        body->addLayout(row2);
        body->addWidget(chk4);
        auto* slider = new QSlider(Qt::Horizontal, c);
        slider->setValue(40);
        body->addWidget(slider);
        col1->addWidget(c);
    }
    {
        auto* c = section(tr("PROGRESS"), page, &body);
        auto* p1 = new QProgressBar(c);
        p1->setValue(65);
        auto* p2 = new QProgressBar(c);
        p2->setValue(20);
        auto* row = new QHBoxLayout;
        row->setSpacing(Tokens::spaceMd);
        row->addWidget(new ZeroSpinner(ZeroSpinner::Small, c));
        row->addWidget(new ZeroSpinner(ZeroSpinner::Medium, c));
        row->addWidget(label(tr("Downloading..."), Role::Caption, c));
        row->addStretch(1);
        body->addWidget(p1);
        body->addWidget(p2);
        body->addLayout(row);
        col1->addWidget(c);
    }
    {
        auto* c = section(tr("TABS"), page, &body);
        auto* tabs = new QTabWidget(c);
        for (const char* name : { "Overview", "Mods", "Settings" }) {
            auto* pg = new QWidget;
            auto* l = new QVBoxLayout(pg);
            l->addWidget(label(tr("%1 page content").arg(name), Role::Body, pg, "secondary"));
            tabs->addTab(pg, tr(name));
        }
        tabs->setFixedHeight(96);
        body->addWidget(tabs);
        col1->addWidget(c);
    }
    col1->addStretch(1);
    grid->addLayout(col1, 0, 1);

    // ---- column 2: views, cards, empty state ------------------------------------------------------------------
    auto* col2 = new QVBoxLayout;
    col2->setSpacing(Tokens::spaceMd);
    {
        auto* c = section(tr("LIST / TABLE / TREE"), page, &body);
        auto* list = new QListWidget(c);
        for (const char* n : { "Survival World", "Creative Build", "Modded Pack", "Skyblock" })
            list->addItem(tr(n));
        list->setCurrentRow(1);
        list->setFixedHeight(100);
        body->addWidget(list);

        auto* table = new QTableWidget(3, 3, c);
        table->setHorizontalHeaderLabels({ tr("Name"), tr("Version"), tr("Played") });
        table->verticalHeader()->hide();
        table->horizontalHeader()->setStretchLastSection(true);
        const char* rows[3][3] = { { "Survival", "1.21.4", "2h ago" }, { "Creative", "1.20.1", "Yesterday" }, { "Skyblock", "1.21", "Last week" } };
        for (int r = 0; r < 3; ++r)
            for (int col = 0; col < 3; ++col)
                table->setItem(r, col, new QTableWidgetItem(tr(rows[r][col])));
        table->selectRow(0);
        table->setFixedHeight(124);
        table->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        body->addWidget(table);

        auto* tree = new QTreeWidget(c);
        tree->setHeaderHidden(true);
        auto* root = new QTreeWidgetItem(tree, { tr("Instances") });
        new QTreeWidgetItem(root, { tr("Vanilla") });
        new QTreeWidgetItem(root, { tr("Modded") });
        tree->expandAll();
        tree->setFixedHeight(80);
        tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        body->addWidget(tree);
        col2->addWidget(c);
    }
    {
        auto* c = section(tr("CARDS / EMPTY STATE / TOASTS"), page, &body);
        auto* cards = new QHBoxLayout;
        cards->setSpacing(Tokens::spaceMd);
        for (const char* n : { "Hover me", "Clickable" }) {
            auto* card = new ZeroCard(c);
            card->setClickable(QString(n) == "Clickable");
            auto* l = new QVBoxLayout(card);
            l->setSpacing(2);
            l->addWidget(label(tr(n), Role::Heading, card));
            l->addWidget(label(tr("Card with hover"), Role::Caption, card));
            cards->addWidget(card);
        }
        body->addLayout(cards);
        auto* empty = new ZeroEmptyState("package", tr("No instances yet"), tr("Create your first instance to start playing."),
                                         tr("Create instance"), c);
        empty->setFixedHeight(190);
        body->addWidget(empty);
        auto* toasts = new QGridLayout;
        toasts->setSpacing(Tokens::spaceSm);
        int toastIndex = 0;
        const struct {
            const char* text;
            ZeroToast::Kind kind;
        } kinds[] = { { "Info", ZeroToast::Kind::Info }, { "Success", ZeroToast::Kind::Success }, { "Warning", ZeroToast::Kind::Warning }, { "Error", ZeroToast::Kind::Error } };
        for (const auto& k : kinds) {
            auto* b = button(tr(k.text), "secondary", c);
            const auto kind = k.kind;
            const QString text = tr(k.text);
            connect(b, &QPushButton::clicked, this, [this, kind, text]() { ZeroToast::show(this, tr("%1 toast message").arg(text), kind, tr("Undo"), [] {}); });
            toasts->addWidget(b, toastIndex / 2, toastIndex % 2);
            toastIndex++;
        }
        body->addLayout(toasts);
        col2->addWidget(c);
    }
    col2->addStretch(1);
    grid->addLayout(col2, 0, 2);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_rail);
    root->addWidget(page, 1);
}

QList<QAbstractButton*> ZeroGallery::hoverSamples() const
{
    QList<QAbstractButton*> out;
    for (const auto& b : m_hoverSamples)
        if (b)
            out << b;
    return out;
}

}  // namespace Zero
