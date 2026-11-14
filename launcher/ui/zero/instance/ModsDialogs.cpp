// SPDX-License-Identifier: GPL-3.0-only
#include "ModsDialogs.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroTokens.h"
#include "ui/zero/content/InstallDialogParts.h"

namespace Zero {
namespace ModsUi {

namespace {
constexpr int kDialogWidth = 480;

const char* variantOf(ChoiceButton::Kind kind)
{
    switch (kind) {
        case ChoiceButton::Kind::Primary:
            return "primary";
        case ChoiceButton::Kind::Danger:
            return "danger";
        case ChoiceButton::Kind::Secondary:
            return "secondary";
        case ChoiceButton::Kind::Ghost:
            break;
    }
    return "ghost";
}
}  // namespace

ChoiceDialog::ChoiceDialog(const ChoicePrompt& prompt, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("ZeroModsChoiceDialog"));
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
    Style::applyRole(title, Role::Heading);
    title->setWordWrap(true);
    title->setTextFormat(Qt::PlainText);
    column->addWidget(title);

    if (!prompt.body.isEmpty()) {
        auto* text = new QLabel(prompt.body, body);
        Style::applyRole(text, Role::Body);
        Style::setProperty(text, "zeroTone", "secondary");
        text->setWordWrap(true);
        text->setTextFormat(Qt::PlainText);
        column->addWidget(text);
    }
    if (!prompt.list.isEmpty()) {
        QStringList lines = prompt.list;
        if (!prompt.more.isEmpty()) {
            lines.append(prompt.more);
        }
        auto* list = new QLabel(lines.join(QLatin1Char('\n')), body);
        Style::applyRole(list, Role::Body);
        list->setTextFormat(Qt::PlainText);
        list->setWordWrap(true);
        column->addWidget(list);
    }
    if (!prompt.note.isEmpty()) {
        auto* note = new QLabel(prompt.note, body);
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
    for (int i = 0; i < prompt.buttons.size(); ++i) {
        auto* button = new QPushButton(prompt.buttons.at(i).label, footer);
        Style::setProperty(button, "zeroVariant", variantOf(prompt.buttons.at(i).kind));
        Style::setProperty(button, "zeroSize", "lg");
        button->setAutoDefault(false);
        const bool isDefault = i == (prompt.defaultButton >= 0 ? prompt.defaultButton : int(prompt.buttons.size()) - 1);
        button->setDefault(isDefault);
        connect(button, &QPushButton::clicked, this, [this, i]() {
            m_chosen = i;
            accept();
        });
        row->addWidget(button);
        m_buttons.append(button);
    }
    root->addWidget(footer);
    if (!m_buttons.isEmpty()) {
        m_buttons.at(prompt.defaultButton >= 0 && prompt.defaultButton < m_buttons.size() ? prompt.defaultButton : int(m_buttons.size()) - 1)->setFocus();
    }
}

int ChoiceDialog::choose()
{
    m_chosen = -1;
    exec();
    return m_chosen;
}

int askWithDialog(QWidget* parent, const ChoicePrompt& prompt)
{
    ChoiceDialog dialog(prompt, parent ? parent->window() : nullptr);
    return dialog.choose();
}

}  // namespace ModsUi
}  // namespace Zero
