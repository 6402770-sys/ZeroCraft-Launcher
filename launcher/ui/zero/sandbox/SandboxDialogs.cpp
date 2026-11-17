// SPDX-License-Identifier: GPL-3.0-only
#include "SandboxDialogs.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/content/InstallDialogParts.h"

namespace Zero {
namespace SandboxUi {

namespace {
constexpr int kDialogWidth = 480;

QString tr(const char* text)
{
    return QCoreApplication::translate("Zero::SandboxUi", text);
}

const char* variantOf(ModsUi::ChoiceButton::Kind kind)
{
    switch (kind) {
        case ModsUi::ChoiceButton::Kind::Primary:
            return "primary";
        case ModsUi::ChoiceButton::Kind::Danger:
            return "danger";
        case ModsUi::ChoiceButton::Kind::Secondary:
            return "secondary";
        case ModsUi::ChoiceButton::Kind::Ghost:
            break;
    }
    return "ghost";
}

QString partLabel(Sandbox::Part part)
{
    switch (part) {
        case Sandbox::Part::Mods:
            return tr("Mods");
        case Sandbox::Part::Config:
            return tr("Config");
        case Sandbox::Part::ResourcePacks:
            return tr("Resource packs");
        case Sandbox::Part::ShaderPacks:
            return tr("Shader packs");
        case Sandbox::Part::GameVersion:
            return tr("Game version and loader");
        case Sandbox::Part::Worlds:
            break;
    }
    return tr("Worlds");
}
}  // namespace

CheckDialog::CheckDialog(const CheckPrompt& prompt, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("ZeroSandboxDialog"));
    setWindowTitle(prompt.title);
    setModal(true);
    setFixedWidth(kDialogWidth);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* body = new QWidget(this);
    auto* column = new QVBoxLayout(body);
    column->setContentsMargins(Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceXl, Tokens::spaceLg);
    column->setSpacing(Tokens::spaceMd);

    auto* title = new QLabel(prompt.title, body);
    title->setObjectName(QStringLiteral("zeroSandboxDialogTitle"));
    Style::applyRole(title, Role::Heading);
    title->setWordWrap(true);
    title->setTextFormat(Qt::PlainText);
    column->addWidget(title);

    if (!prompt.body.isEmpty()) {
        auto* text = new QLabel(prompt.body, body);
        text->setObjectName(QStringLiteral("zeroSandboxDialogBody"));
        Style::applyRole(text, Role::Body);
        Style::setProperty(text, "zeroTone", "secondary");
        text->setWordWrap(true);
        text->setTextFormat(Qt::PlainText);
        column->addWidget(text);
    }

    for (const CheckOption& option : prompt.options) {
        if (option.gapBefore)
            column->addSpacing(Tokens::spaceSm);
        auto* box = new QVBoxLayout;
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(Tokens::spaceXs);
        auto* check = new QCheckBox(option.label, body);
        check->setObjectName(QStringLiteral("zeroSandboxOption-") + option.id);
        check->setChecked(option.checked);
        box->addWidget(check);
        if (!option.caption.isEmpty()) {
            auto* caption = new QLabel(option.caption, body);
            caption->setObjectName(QStringLiteral("zeroSandboxCaption-") + option.id);
            Style::applyRole(caption, Role::Caption);
            Style::setProperty(caption, "zeroTone", "secondary");
            caption->setWordWrap(true);
            caption->setTextFormat(Qt::PlainText);
            caption->setContentsMargins(Tokens::spaceXl, 0, 0, 0);
            box->addWidget(caption);
        }
        column->addLayout(box);
        m_checks.append({ option.id, check });
    }

    if (!prompt.note.isEmpty()) {
        auto* note = new QLabel(prompt.note, body);
        note->setObjectName(QStringLiteral("zeroSandboxDialogNote"));
        Style::applyRole(note, Role::Caption);
        Style::setProperty(note, "zeroTone", "secondary");
        note->setWordWrap(true);
        note->setTextFormat(Qt::PlainText);
        column->addWidget(note);
    }
    root->addWidget(body);

    auto* footer = new Content::Parts::FooterBar(this);
    auto* row = new QHBoxLayout(footer);
    row->setContentsMargins(Tokens::spaceXl, Tokens::spaceMd, Tokens::spaceXl, Tokens::spaceMd);
    row->setSpacing(Tokens::spaceSm);
    row->addStretch(1);
    const int defaultIndex = prompt.defaultButton >= 0 ? prompt.defaultButton : int(prompt.buttons.size()) - 1;
    for (int i = 0; i < prompt.buttons.size(); ++i) {
        auto* button = new QPushButton(prompt.buttons.at(i).label, footer);
        Style::setProperty(button, "zeroVariant", variantOf(prompt.buttons.at(i).kind));
        Style::setProperty(button, "zeroSize", "lg");
        button->setAutoDefault(false);
        button->setDefault(i == defaultIndex);
        connect(button, &QPushButton::clicked, this, [this, i]() {
            m_chosen = i;
            accept();
        });
        row->addWidget(button);
        m_buttons.append(button);
    }
    root->addWidget(footer);
    if (defaultIndex >= 0 && defaultIndex < m_buttons.size())
        m_buttons.at(defaultIndex)->setFocus();
}

CheckAnswer CheckDialog::answer() const
{
    CheckAnswer a;
    a.button = m_chosen;
    for (const auto& [id, box] : m_checks) {
        if (box->isChecked())
            a.checked << id;
    }
    return a;
}

CheckAnswer CheckDialog::choose()
{
    m_chosen = -1;
    exec();
    return answer();
}

QCheckBox* CheckDialog::checkBox(const QString& id) const
{
    for (const auto& [optionId, box] : m_checks) {
        if (optionId == id)
            return box;
    }
    return nullptr;
}

QStringList CheckDialog::optionIds() const
{
    QStringList ids;
    for (const auto& [id, box] : m_checks)
        ids << id;
    return ids;
}

CheckAnswer askChecksWithDialog(QWidget* parent, const CheckPrompt& prompt)
{
    CheckDialog dialog(prompt, parent ? parent->window() : nullptr);
    return dialog.choose();
}

CheckPrompt startPrompt(const QString& originalName)
{
    CheckPrompt prompt;
    prompt.title = tr("Try it in a copy");
    prompt.body = tr("Make a test copy of %1. Change mods, versions or settings there, then bring the changes back to %1 or discard the copy. %1 stays as it is.")
                      .arg(originalName);
    prompt.options = { { QString::fromLatin1(kOptionWorlds), tr("Copy worlds too"), {}, true, false } };
    prompt.buttons = { { tr("Cancel"), ModsUi::ChoiceButton::Kind::Ghost }, { tr("Make test copy"), ModsUi::ChoiceButton::Kind::Primary } };
    prompt.defaultButton = 1;
    return prompt;
}

CheckPrompt bringBackPrompt(const QString& originalName, const QList<Sandbox::PartChange>& plan)
{
    CheckPrompt prompt;
    for (const Sandbox::PartChange& change : plan) {
        if (!change.differs)
            continue;
        CheckOption option;
        option.id = Sandbox::partKey(change.part);
        option.label = partLabel(change.part);
        if (change.part == Sandbox::Part::Worlds) {
            option.checked = false;
            option.caption = tr("Replaces %1's worlds with the copy's. The old ones are kept as backups.").arg(originalName);
        }
        prompt.options.append(option);
    }
    if (prompt.options.isEmpty()) {
        prompt.title = tr("No changes to bring back");
        prompt.body = tr("The test copy and %1 are the same.").arg(originalName);
        prompt.buttons = { { tr("Close"), ModsUi::ChoiceButton::Kind::Ghost }, { tr("Discard copy"), ModsUi::ChoiceButton::Kind::Secondary } };
        prompt.defaultButton = 0;
        return prompt;
    }
    prompt.title = tr("Bring changes back to %1?").arg(originalName);
    prompt.body = tr("Choose what to bring back from the test copy.");
    prompt.options.append({ QString::fromLatin1(kOptionDiscard), tr("Discard the test copy afterwards"), {}, true, true });
    prompt.note = tr("A copy of %1 is saved first, so you can undo this.").arg(originalName);
    prompt.buttons = { { tr("Cancel"), ModsUi::ChoiceButton::Kind::Ghost }, { tr("Bring changes back"), ModsUi::ChoiceButton::Kind::Primary } };
    prompt.defaultButton = 0;  // Enter cancels
    return prompt;
}

QList<Sandbox::Part> chosenParts(const CheckAnswer& answer)
{
    QList<Sandbox::Part> out;
    for (const Sandbox::Part part : Sandbox::allParts()) {
        if (answer.isChecked(Sandbox::partKey(part)))
            out << part;
    }
    return out;
}

}  // namespace SandboxUi
}  // namespace Zero
