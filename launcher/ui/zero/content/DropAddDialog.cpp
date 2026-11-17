// SPDX-License-Identifier: GPL-3.0-only
#include "DropAddDialog.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>

#include "ui/zero/PillTag.h"
#include "ui/zero/ZeroEmptyState.h"
#include "ui/zero/ZeroFormat.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/content/InstallDialogParts.h"

namespace Zero::Content {

namespace {

constexpr int kWidth = 560;
constexpr int kMaxBodyHeight = 340;
constexpr int kEmptyHeight = 220;

/// A click anywhere on the row flips its box (when it can be ticked).
class RowWidget : public QWidget {
   public:
    using QWidget::QWidget;
    QCheckBox* box = nullptr;

   protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (box && box->isEnabled() && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())) {
            box->toggle();
        }
        QWidget::mouseReleaseEvent(event);
    }
};

QLabel* makeLabel(const QString& text, Role role, const char* tone, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, role);
    if (tone) {
        Style::setProperty(label, "zeroTone", tone);
    }
    return label;
}

}  // namespace

DropAddDialog::DropAddDialog(DropAddEnvironment env, QList<Drop::DropFile> files, QWidget* parent)
    : QDialog(parent), m_env(std::move(env)), m_files(std::move(files))
{
    setObjectName(QStringLiteral("ZeroDropAddDialog"));
    setWindowTitle(Drop::titleText(m_files.size()));
    for (const auto& file : std::as_const(m_files)) {
        if (file.kind == Drop::Kind::Mod) {
            m_anyMod = true;
        }
    }
    buildUi(m_files);
    refresh();
}

void DropAddDialog::buildUi(const QList<Drop::DropFile>& files)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* body = new QWidget(this);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceLg);
    column->setSpacing(Tokens::spaceMd);

    m_title = makeLabel(Drop::titleText(files.size()), Role::Title, nullptr, body);
    m_title->setObjectName(QStringLiteral("zeroDropTitle"));
    column->addWidget(m_title);

    const bool noInstances = m_env.targets.isEmpty();
    if (noInstances) {
        m_empty = new ZeroEmptyState(QStringLiteral("blocks"), tr("Create an instance first"),
                                     tr("Mods, packs and worlds need an instance to go into."), QString(), body);
        m_empty->setObjectName(QStringLiteral("zeroDropEmpty"));
        m_empty->setMinimumHeight(kEmptyHeight);
        column->addWidget(m_empty, 1);
    } else {
        m_targetRow = new QWidget(body);
        auto* row = new QHBoxLayout(m_targetRow);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(Tokens::spaceSm);
        // No "To" label: the button already reads "For <instance> · <version>".
        m_button = new TargetInstanceButton(m_targetRow, m_env.icons);
        m_button->setNeedsLoader(m_anyMod);
        m_button->setTargets(m_env.targets);
        m_targetId = chooseTarget(m_env.targets, m_env.librarySelectedId, m_env.savedTargetId, m_anyMod);
        m_button->setCurrentId(m_targetId);
        row->addWidget(m_button);
        row->addStretch(1);
        column->addWidget(m_targetRow);
        connect(m_button, &TargetInstanceButton::targetChosen, this, [this](const QString& id) { setTarget(id); });

        m_scroll = new QScrollArea(body);
        m_scroll->setWidgetResizable(true);
        m_scroll->setFrameShape(QFrame::NoFrame);
        m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_content = new QWidget;
        m_scroll->setWidget(m_content);
        m_content->setAutoFillBackground(false);
        m_scroll->viewport()->setAutoFillBackground(false);
        auto* rows = new QVBoxLayout(m_content);
        rows->setContentsMargins(0, 0, 0, 0);
        rows->setSpacing(0);

        for (const Drop::DropFile& file : files) {
            RowUi ui;
            ui.file = file;
            auto* widget = new RowWidget(m_content);
            ui.widget = widget;
            widget->setObjectName(QStringLiteral("zeroDropRow"));
            auto* layout = new QHBoxLayout(widget);
            layout->setContentsMargins(0, Tokens::spaceSm, 0, Tokens::spaceSm);
            layout->setSpacing(Tokens::spaceMd);

            ui.check = new QCheckBox(widget);
            widget->box = ui.check;
            ui.check->setAccessibleName(tr("Add %1").arg(file.name));
            const bool can = Drop::installable(file.kind);
            ui.check->setEnabled(can);
            ui.check->setChecked(can);
            layout->addWidget(ui.check, 0, Qt::AlignTop);

            ui.pill = new PillTag(Drop::kindLabel(file.kind), PillTag::Tone::Plain, widget);
            layout->addWidget(ui.pill, 0, Qt::AlignTop);

            auto* texts = new QVBoxLayout;
            texts->setContentsMargins(0, 0, 0, 0);
            texts->setSpacing(0);
            ui.name = makeLabel(file.name, Role::Label, can ? nullptr : "secondary", widget);
            ui.name->setObjectName(QStringLiteral("zeroDropName"));
            ui.name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            texts->addWidget(ui.name);
            if (file.name != file.fileName) {
                auto* fileName = makeLabel(file.fileName, Role::Caption, "secondary", widget);
                fileName->setObjectName(QStringLiteral("zeroDropFileName"));
                fileName->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
                texts->addWidget(fileName);
            }
            ui.fit = makeLabel(QString(), Role::Caption, "warning", widget);
            ui.fit->setObjectName(QStringLiteral("zeroDropFit"));
            ui.fit->setWordWrap(true);
            texts->addWidget(ui.fit);
            ui.replace = makeLabel(QString(), Role::Caption, "secondary", widget);
            ui.replace->setObjectName(QStringLiteral("zeroDropReplace"));
            ui.replace->setWordWrap(true);
            texts->addWidget(ui.replace);
            ui.note = makeLabel(Drop::notInstallableCaption(file), Role::Caption, "secondary", widget);
            ui.note->setObjectName(QStringLiteral("zeroDropNote"));
            ui.note->setWordWrap(true);
            ui.note->setVisible(!ui.note->text().isEmpty());
            texts->addWidget(ui.note);
            layout->addLayout(texts, 1);

            if (file.size >= 0) {
                auto* size = makeLabel(Format::bytes(file.size), Role::Caption, "secondary", widget);
                size->setObjectName(QStringLiteral("zeroDropSize"));
                layout->addWidget(size, 0, Qt::AlignTop);
            }
            widget->setCursor(can ? Qt::PointingHandCursor : Qt::ArrowCursor);
            connect(ui.check, &QCheckBox::toggled, this, [this]() { refresh(); });
            rows->addWidget(widget);
            m_rows.push_back(ui);
        }
        rows->addStretch(1);
        column->addWidget(m_scroll, 1);
    }
    root->addWidget(body, 1);

    const Parts::FooterParts footer = Parts::buildFooter(this, noInstances ? tr("Create instance") : tr("Add"));
    m_cancel = footer.cancel;
    m_primary = footer.primary;
    auto* footerLayout = qobject_cast<QHBoxLayout*>(footer.bar->layout());
    if (!noInstances) {
        delete footerLayout->takeAt(0);  // the stretch: the summary takes its place
        m_summary = makeLabel(QString(), Role::Body, "secondary", footer.bar);
        m_summary->setObjectName(QStringLiteral("zeroDropSummary"));
        footerLayout->insertWidget(0, m_summary, 1);
    }
    root->addWidget(footer.bar);

    connect(m_cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_primary, &QPushButton::clicked, this, &DropAddDialog::onPrimary);
    connect(&Theme::instance(), &Theme::changed, this, [this]() { update(); });
    setTabOrder(m_cancel, m_primary);
    setFixedWidth(kWidth);
}

const TargetInstance* DropAddDialog::currentTarget() const
{
    for (const auto& target : m_env.targets) {
        if (target.id == m_targetId) {
            return &target;
        }
    }
    return nullptr;
}

void DropAddDialog::setTarget(const QString& id)
{
    if (!m_button) {
        return;
    }
    m_targetId = id;
    m_button->setCurrentId(id);
    refresh();
}

QList<Drop::DropFile> DropAddDialog::chosenFiles() const
{
    QList<Drop::DropFile> out;
    for (const auto& row : m_rows) {
        if (row.check->isEnabled() && row.check->isChecked()) {
            out << row.file;
        }
    }
    return out;
}

void DropAddDialog::refresh()
{
    if (m_empty) {
        m_primary->setEnabled(true);
        return;
    }
    const TargetInstance* target = currentTarget();
    int ticked = 0;
    for (auto& row : m_rows) {
        const QString fit = target ? Drop::fitCaption(row.file, *target) : QString();
        const QString replace =
            target && m_env.folderFor ? Drop::replaceCaption(row.file, m_env.folderFor(target->id, row.file.kind)) : QString();
        row.fit->setText(fit);
        row.fit->setVisible(!fit.isEmpty());
        row.replace->setText(replace);
        row.replace->setVisible(!replace.isEmpty());
        if (row.check->isEnabled() && row.check->isChecked()) {
            ++ticked;
        }
    }
    m_summary->setText(Drop::summaryText(ticked));
    m_primary->setText(target ? Drop::primaryText(target->name) : tr("Add"));
    m_primary->setEnabled(target && ticked > 0);
    fitBody();
}

void DropAddDialog::fitBody()
{
    if (!m_content || !m_content->layout()) {
        return;
    }
    QLayout* layout = m_content->layout();
    layout->activate();
    const int width = kWidth - 2 * Tokens::spaceXl;
    const int wanted = layout->hasHeightForWidth() ? layout->totalHeightForWidth(width) : layout->sizeHint().height();
    const int height = std::min(std::max(wanted, 48), kMaxBodyHeight);
    if (m_scroll->height() != height || m_scroll->minimumHeight() != height) {
        m_scroll->setFixedHeight(height);
        if (isVisible()) {
            adjustSize();
        }
    }
}

void DropAddDialog::onPrimary()
{
    if (m_empty) {
        m_createRequested = true;
        reject();
        if (m_env.createInstance) {
            m_env.createInstance();
        }
        return;
    }
    if (!m_primary->isEnabled()) {
        return;
    }
    m_added.clear();
    m_failures.clear();
    for (const Drop::DropFile& file : chosenFiles()) {
        const QString error = m_env.install ? m_env.install(m_targetId, file) : tr("Nothing can install this file.");
        if (error.isEmpty()) {
            m_added << file;
        } else {
            m_failures.append({ file, error });
        }
    }
    accept();
}

void DropAddDialog::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    Paint::paintBackground(p, rect(), Theme::current().bg);
}

void DropAddDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    fitBody();
    adjustSize();
}

QCheckBox* DropAddDialog::rowBox(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).check : nullptr;
}
QString DropAddDialog::rowKind(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).pill->text() : QString();
}
QString DropAddDialog::rowName(int row) const
{
    return row >= 0 && row < m_rows.size() ? m_rows.at(row).name->text() : QString();
}
QString DropAddDialog::rowFit(int row) const
{
    return row >= 0 && row < m_rows.size() && !m_rows.at(row).fit->isHidden() ? m_rows.at(row).fit->text() : QString();
}
QString DropAddDialog::rowReplace(int row) const
{
    return row >= 0 && row < m_rows.size() && !m_rows.at(row).replace->isHidden() ? m_rows.at(row).replace->text() : QString();
}
QString DropAddDialog::rowNote(int row) const
{
    return row >= 0 && row < m_rows.size() && !m_rows.at(row).note->isHidden() ? m_rows.at(row).note->text() : QString();
}
QString DropAddDialog::summaryText() const
{
    return m_summary ? m_summary->text() : QString();
}
QString DropAddDialog::titleText() const
{
    return m_title->text();
}

}  // namespace Zero::Content
