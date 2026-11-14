// SPDX-License-Identifier: GPL-3.0-only
// ZERO Launcher - Create Instance dialog (see CreateInstanceDialog.h).
#include "CreateInstanceDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDirListing>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMimeDatabase>
#include <QPainter>
#include <QPushButton>
#include <QShortcut>
#include <QStackedWidget>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <filesystem>
#include <memory>

#include "Application.h"
#include "InstanceImportTask.h"
#include "InstanceList.h"
#include "icons/IconList.h"
#include "minecraft/VanillaInstanceCreationTask.h"
#include "ui/dialogs/IconPickerDialog.h"
#ifdef Q_OS_WIN
#include "StringUtils.h"
#endif

#include "CreateMetaSource.h"
#include "ImportPanel.h"
#include "InstancePreviewPanel.h"
#include "ModpackSourcesPanel.h"
#include "TypeCardRow.h"
#include "VersionPanel.h"
#include "ui/zero/ZeroIcons.h"
#include "ui/zero/ZeroMotion.h"
#include "ui/zero/ZeroPaint.h"
#include "ui/zero/ZeroStyle.h"
#include "ui/zero/ZeroThemeState.h"
#include "ui/zero/ZeroTransition.h"

namespace Zero {
namespace Create {

const char* const CreateInstanceDialog::kGeometryKey = "ZeroCreateInstanceGeometry";
const char* const CreateInstanceDialog::kKindKey = "ZeroCreateInstanceKind";
const char* const CreateInstanceDialog::kAdvancedKey = "ZeroCreateInstanceAdvanced";

namespace {
constexpr int kHeaderHeight = 64;
constexpr int kFooterHeight = 72;
constexpr int kIconButton = 40;

QString dtr(const char* text)
{
    return QCoreApplication::translate("Zero::Create::CreateInstanceDialog", text);
}

/// The footer bar: `surface` with a 1 px `border` rule on top.
class FooterBar : public QWidget {
   public:
    explicit FooterBar(QWidget* parent) : QWidget(parent)
    {
        connect(&Theme::instance(), &Theme::changed, this, qOverload<>(&QWidget::update));
    }

   protected:
    void paintEvent(QPaintEvent*) override
    {
        const auto& t = Theme::current();
        QPainter p(this);
        p.fillRect(rect(), t.surface);
        p.fillRect(QRect(0, 0, width(), 1), t.border);
    }
};

QLabel* overline(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    Style::applyRole(label, Role::Overline);
    return label;
}

/// A suggested name that fits kMaxNameLength (cut on a character boundary, trimmed). Names the user types keep their error.
QString fitSuggestedName(const QString& name)
{
    QString out = name.trimmed();
    if (out.size() > kMaxNameLength) {
        int length = kMaxNameLength;
        if (out.at(length - 1).isHighSurrogate())
            --length;
        out.truncate(length);
        out = out.trimmed();
    }
    return out;
}

int kindIndex(CreateInstanceDialog::Kind kind)
{
    return static_cast<int>(kind);
}
}  // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Environment
// ---------------------------------------------------------------------------------------------------------------------

CreateInstanceEnvironment CreateInstanceEnvironment::fromApplication()
{
    CreateInstanceEnvironment env;
    auto settings = APPLICATION->settings();
    // ZERO settings of this dialog (registered here so the dialog stays self-contained).
    if (!settings->contains(CreateInstanceDialog::kGeometryKey))
        settings->registerSetting(CreateInstanceDialog::kGeometryKey, QString());
    if (!settings->contains(CreateInstanceDialog::kKindKey))
        settings->registerSetting(CreateInstanceDialog::kKindKey, QString());
    if (!settings->contains(CreateInstanceDialog::kAdvancedKey))
        settings->registerSetting(CreateInstanceDialog::kAdvancedKey, false);

    env.groups = APPLICATION->instances()->getGroups();
    // Same folders as NewInstanceDialog::refreshInstDirBox(): the default folder and the additional ones, canonical.
    env.instanceDirs = []() {
        QList<QPair<QString, QString>> dirs;
        auto add = [&dirs](const QString& dir, const QString& label) {
            if (dir.isEmpty())
                return;
            const QString canonical = QDir(dir).canonicalPath();
            if (canonical.isEmpty())
                return;
            dirs.append({ label.isEmpty() ? canonical : label, canonical });
        };
        const QString instDir = APPLICATION->settings()->get("InstanceDir").toString();
        add(instDir, dtr("Default (%1)").arg(instDir));
        for (const QString& dir : APPLICATION->settings()->get("AdditionalInstanceDirs").toStringList())
            add(dir, {});
        return dirs;
    };
    env.lastUsedDir = settings->get("LastUsedInstDirForNewInstance").toString();

    const QString templateDir = settings->get("TemplateDir").toString();
    env.templateAvailable = !templateDir.isEmpty() && QDir(templateDir).exists();
#ifdef Q_OS_WIN
    if (env.templateAvailable) {
        // Like NewInstanceDialog: mention the symlink permission only when the template really contains symlinks.
        const auto flags = QDirListing::IteratorFlag::Recursive | QDirListing::IteratorFlag::IncludeHidden;
        for (const auto& entry : QDirListing(templateDir, flags)) {
            if (std::filesystem::is_symlink(StringUtils::toStdString(entry.absoluteFilePath()))) {
                env.templateToolTip = dtr("On Windows, symbolic links may require admin permission to create.");
                break;
            }
        }
    }
#endif
    env.curseForge = APPLICATION->capabilities() & Application::SupportsFlame;
    env.readSetting = [](const QString& key) { return APPLICATION->settings()->get(key); };
    env.writeSetting = [](const QString& key, const QVariant& value) { APPLICATION->settings()->set(key, value); };
    env.instanceIcon = [](const QString& key) { return APPLICATION->icons()->getIcon(key); };
    env.pickIcon = [](QWidget* parent, const QString& current) {
        IconPickerDialog dialog(parent);
        dialog.execWithSelection(current);
        return dialog.result() == QDialog::Accepted ? dialog.selectedIconKey : QString();
    };
    env.chooseImportFile = [](QWidget* parent) {
        // ImportPage's filter: zip by MIME glob patterns, plus .mrpack.
        const QMimeType zip = QMimeDatabase().mimeTypeForName(QStringLiteral("application/zip"));
        QString filter = dtr("Supported files") + QStringLiteral(" (%1 *.mrpack)").arg(zip.globPatterns().join(QLatin1Char(' ')));
        filter += QStringLiteral(";;") + zip.filterString();
        filter += QStringLiteral(";;") + dtr("Modrinth pack") + QStringLiteral(" (*.mrpack)");
        return QFileDialog::getOpenFileName(parent, dtr("Choose modpack"), QString(), filter);
    };
    env.makeMetaSource = [](QObject* parent) -> MetaSource* { return new PrismMetaSource(parent); };
    return env;
}

// ---------------------------------------------------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------------------------------------------------

CreateInstanceDialog::CreateInstanceDialog(const QString& initialGroup, Kind initialKind, QWidget* parent)
    : CreateInstanceDialog(CreateInstanceEnvironment::fromApplication(), initialGroup, initialKind, parent)
{}

CreateInstanceDialog::CreateInstanceDialog(CreateInstanceEnvironment env, const QString& initialGroup, Kind initialKind, QWidget* parent)
    : QDialog(parent), m_env(std::move(env))
{
    setObjectName(QStringLiteral("ZeroCreateInstanceDialog"));
    setWindowTitle(tr("Create instance"));
    setMinimumSize(kMinWidth, kMinHeight);
    m_meta = m_env.makeMetaSource(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    buildHeader(root);

    // type cards
    auto* cardsWrap = new QWidget(this);
    auto* cardsLayout = new QHBoxLayout(cardsWrap);
    cardsLayout->setContentsMargins(Tokens::spaceXl, 0, Tokens::spaceXl, Tokens::spaceLg);
    m_cards = new TypeCardRow(cardsWrap);
    m_cards->addCard(QStringLiteral("cuboid"), tr("Vanilla"), tr("The game as Mojang made it"));
    m_cards->addCard(QStringLiteral("blocks"), tr("Modded"), tr("Fabric, Forge, NeoForge or Quilt"));
    m_cards->addCard(QStringLiteral("package"), tr("Modpack"), tr("A ready-made pack from Modrinth, CurseForge or Technic"));
    m_cards->addCard(QStringLiteral("file-archive"), tr("Import"), tr("A .zip or .mrpack you already have"));
    cardsLayout->addWidget(m_cards);
    root->addWidget(cardsWrap);

    // body: configuration | preview
    m_body = new QWidget(this);
    auto* body = new QHBoxLayout(m_body);
    body->setContentsMargins(Tokens::spaceXl, 0, Tokens::spaceXl, Tokens::spaceLg);
    body->setSpacing(Tokens::spaceXl);
    m_config = new QStackedWidget(m_body);
    m_versions = new VersionPanel(m_meta, m_config);
    m_modpacks = new ModpackSourcesPanel(m_env.curseForge, m_config);
    m_import = new ImportPanel(m_env.chooseImportFile, m_config);
    m_config->addWidget(m_versions);
    m_config->addWidget(m_modpacks);
    m_config->addWidget(m_import);
    m_preview = new InstancePreviewPanel(m_body);
    body->addWidget(m_config, 1);
    body->addWidget(m_preview);
    root->addWidget(m_body, 1);

    root->addWidget(buildFooter());

    // group (NewInstanceDialog's rule: "" first, the initial group inserted when it is not a known one)
    QStringList groups = m_env.groups;
    groups.prepend(QString());
    int groupIndex = groups.indexOf(initialGroup);
    if (groupIndex == -1) {
        groupIndex = 1;
        groups.insert(groupIndex, initialGroup);
    }
    m_group->addItems(groups);
    m_group->setCurrentIndex(groupIndex);
    refreshLocations();

    connect(m_cards, &TypeCardRow::chosen, this, [this](int index) { chooseKind(static_cast<Kind>(index)); });
    // 1-4 choose a kind only while a card has focus: elsewhere the digits belong to the field or list (type-ahead, names).
    for (int i = 0; i < 4; ++i) {
        auto* shortcut = new QShortcut(QKeySequence(Qt::Key_1 + i), m_cards);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, this, [this, i]() { chooseKind(static_cast<Kind>(i)); });
    }
    connect(m_versions, &VersionPanel::selectionChanged, this, &CreateInstanceDialog::refreshState);
    connect(m_import, &ImportPanel::sourceChanged, this, &CreateInstanceDialog::refreshState);
    connect(m_import, &ImportPanel::classicImportRequested, this, &CreateInstanceDialog::classicImportRequested);
    connect(m_modpacks, &ModpackSourcesPanel::pageRequested, this, &CreateInstanceDialog::classicPageRequested);
    connect(m_modpacks, &ModpackSourcesPanel::browseAllRequested, this, &CreateInstanceDialog::browseModpacksRequested);
    connect(m_name, &QLineEdit::textEdited, this, [this]() {
        m_nameEdited = true;
        refreshState();
    });
    connect(m_name, &QLineEdit::textChanged, this, &CreateInstanceDialog::refreshState);
    connect(m_location, &QComboBox::currentIndexChanged, this, [this]() {
        m_footerError->hide();
        refreshState();
    });
    connect(&Theme::instance(), &Theme::changed, this, [this]() {
        refreshIcons();
        update();
    });

    // Tab order: cards -> configuration -> footer (icon, name, group, Advanced, Cancel, Create).
    setTabOrder(m_cards->card(3), m_versions->firstFocusWidget());
    setTabOrder(m_iconButton, m_name);
    setTabOrder(m_name, m_group);
    setTabOrder(m_group, m_advanced);
    setTabOrder(m_advanced, m_location);
    setTabOrder(m_location, m_template);
    setTabOrder(m_template, m_cancel);
    setTabOrder(m_cancel, m_create);

    if (const QVariant geometry = m_env.readSetting ? m_env.readSetting(kGeometryKey) : QVariant();
        geometry.isValid() && !geometry.toString().isEmpty())
        restoreGeometry(QByteArray::fromBase64(geometry.toString().toUtf8()));
    else
        resize(kDefaultWidth, kDefaultHeight);
    setAdvancedExpanded(m_env.readSetting && m_env.readSetting(kAdvancedKey).toBool());

    refreshIcons();
    m_kind = initialKind;
    m_cards->setCurrent(kindIndex(m_kind));
    applyKind(false);
    m_cards->card(kindIndex(m_kind))->setFocus(Qt::OtherFocusReason);
}

CreateInstanceDialog::~CreateInstanceDialog() = default;

void CreateInstanceDialog::buildHeader(QLayout* root)
{
    auto* header = new QWidget(this);
    header->setFixedHeight(kHeaderHeight);
    auto* column = new QVBoxLayout(header);
    column->setContentsMargins(Tokens::spaceXl, Tokens::spaceSm, Tokens::spaceXl, Tokens::spaceSm);
    column->setSpacing(2);
    column->addStretch(1);
    auto* title = new QLabel(tr("Create instance"), header);
    Style::applyRole(title, Role::Title);
    auto* caption = new QLabel(tr("Choose what to start from"), header);
    Style::applyRole(caption, Role::Caption);
    column->addWidget(title);
    column->addWidget(caption);
    column->addStretch(1);
    root->addWidget(header);
}

QWidget* CreateInstanceDialog::buildFooter()
{
    m_footer = new FooterBar(this);
    auto* column = new QVBoxLayout(m_footer);
    column->setContentsMargins(Tokens::spaceXl, 1, Tokens::spaceXl, 0);
    column->setSpacing(0);

    // Advanced (above the main row when expanded): location, template
    m_advancedStrip = new QWidget(m_footer);
    auto* strip = new QHBoxLayout(m_advancedStrip);
    strip->setContentsMargins(0, Tokens::spaceMd, 0, 0);
    strip->setSpacing(Tokens::spaceXl);
    m_locationColumn = new QWidget(m_advancedStrip);
    auto* location = new QVBoxLayout(m_locationColumn);
    location->setContentsMargins(0, 0, 0, 0);
    location->setSpacing(2);
    auto* locationLabel = overline(tr("Location"), m_locationColumn);
    m_location = new QComboBox(m_locationColumn);
    m_location->setAccessibleName(tr("Instance location"));
    m_location->setMinimumWidth(320);
    m_location->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    locationLabel->setBuddy(m_location);
    location->addWidget(locationLabel);
    location->addWidget(m_location);
    m_template = new QCheckBox(tr("Use instance template"), m_advancedStrip);
    m_template->setAccessibleName(tr("Use instance template"));
    m_template->setChecked(m_env.templateAvailable);  // NewInstanceDialog: on whenever a template exists
    if (!m_env.templateToolTip.isEmpty())
        m_template->setToolTip(m_env.templateToolTip);
    strip->addWidget(m_locationColumn);
    strip->addWidget(m_template, 0, Qt::AlignBottom);
    strip->addStretch(1);
    column->addWidget(m_advancedStrip);

    // main row (72 px): icon, name, group, Advanced | error, Cancel, Create
    auto* mainRow = new QWidget(m_footer);
    mainRow->setFixedHeight(kFooterHeight - 1);
    auto* row = new QHBoxLayout(mainRow);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(Tokens::spaceLg);

    m_iconButton = new QToolButton(mainRow);
    m_iconButton->setFixedSize(kIconButton, kIconButton);
    m_iconButton->setIconSize(QSize(32, 32));
    m_iconButton->setAccessibleName(tr("Instance icon"));
    m_iconButton->setToolTip(tr("Change the icon"));
    Style::setProperty(m_iconButton, "zeroVariant", "secondary");
    connect(m_iconButton, &QToolButton::clicked, this, &CreateInstanceDialog::pickIcon);
    row->addWidget(m_iconButton, 0, Qt::AlignVCenter);

    // Name / Group columns: label, field, inline error line (reserved, so nothing jumps)
    auto makeColumn = [mainRow](const QString& label, QWidget* field, QLabel** error) {
        auto* w = new QWidget(mainRow);
        auto* c = new QVBoxLayout(w);
        c->setContentsMargins(0, Tokens::spaceXs, 0, Tokens::spaceXs / 2);
        c->setSpacing(Tokens::spaceXs / 2);
        auto* l = overline(label, w);
        l->setBuddy(field);
        field->setParent(w);
        c->addWidget(l);
        c->addWidget(field);
        auto* e = new QLabel(w);
        Style::applyRole(e, Role::Caption);
        e->setFixedHeight(QFontMetrics(Style::font(Role::Caption)).height());
        c->addWidget(e);
        if (error) {
            Style::setProperty(e, "zeroTone", "danger");
            *error = e;
        }
        return w;
    };
    m_name = new QLineEdit;
    m_name->setAccessibleName(tr("Instance name"));
    m_name->setPlaceholderText(tr("Instance name"));
    m_name->setMaxLength(kMaxNameLength + 64);  // longer than allowed so the inline error can explain, but bounded
    m_name->installEventFilter(this);
    m_nameColumn = makeColumn(tr("Name"), m_name, &m_nameError);
    m_nameColumn->setMinimumWidth(200);
    m_nameColumn->setMaximumWidth(380);
    row->addWidget(m_nameColumn, 2);

    m_group = new QComboBox;
    m_group->setEditable(true);
    m_group->setAccessibleName(tr("Group"));
    m_group->lineEdit()->setPlaceholderText(tr("No group"));
    m_group->setInsertPolicy(QComboBox::NoInsert);
    m_groupColumn = makeColumn(tr("Group"), m_group, nullptr);
    m_groupColumn->setMinimumWidth(150);
    m_groupColumn->setMaximumWidth(240);
    row->addWidget(m_groupColumn, 1);

    m_advanced = new QToolButton(mainRow);
    m_advanced->setText(tr("Advanced"));
    m_advanced->setAccessibleName(tr("Advanced options"));
    m_advanced->setCheckable(true);
    m_advanced->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advanced->setLayoutDirection(Qt::RightToLeft);  // "Advanced ▸": the chevron after the text
    Style::setProperty(m_advanced, "zeroVariant", "ghost");
    connect(m_advanced, &QToolButton::toggled, this, [this](bool on) { setAdvancedExpanded(on); });
    row->addWidget(m_advanced, 0, Qt::AlignVCenter);

    row->addStretch(1);
    m_footerError = new QLabel(mainRow);
    Style::applyRole(m_footerError, Role::Caption);
    Style::setProperty(m_footerError, "zeroTone", "danger");
    m_footerError->setWordWrap(true);
    m_footerError->setMaximumWidth(260);
    m_footerError->hide();
    row->addWidget(m_footerError, 0, Qt::AlignVCenter);

    m_cancel = new QPushButton(tr("Cancel"), mainRow);
    Style::setProperty(m_cancel, "zeroVariant", "ghost");
    m_cancel->setAutoDefault(false);
    connect(m_cancel, &QPushButton::clicked, this, &CreateInstanceDialog::reject);
    row->addWidget(m_cancel, 0, Qt::AlignVCenter);

    m_create = new QPushButton(tr("Create"), mainRow);
    Style::setProperty(m_create, "zeroVariant", "primary");
    Style::setProperty(m_create, "zeroSize", "lg");
    m_create->setDefault(true);
    m_create->setAutoDefault(true);
    m_create->setEnabled(false);
    connect(m_create, &QPushButton::clicked, this, &CreateInstanceDialog::accept);
    row->addWidget(m_create, 0, Qt::AlignVCenter);

    column->addWidget(mainRow);
    return m_footer;
}

void CreateInstanceDialog::refreshIcons()
{
    const auto& t = Theme::current();
    if (m_env.instanceIcon)
        m_iconButton->setIcon(m_env.instanceIcon(m_iconKey));
    m_advanced->setIcon(Icons::icon(m_advanced->isChecked() ? QStringLiteral("chevron-down") : QStringLiteral("chevron-right"), t.textSecondary));
}

void CreateInstanceDialog::refreshLocations()
{
    const QString previous = m_location->currentData().toString();
    const QList<QPair<QString, QString>> dirs = m_env.instanceDirs ? m_env.instanceDirs() : QList<QPair<QString, QString>>{};
    {
        const QSignalBlocker block(m_location);
        m_location->clear();
        for (const auto& d : dirs)
            m_location->addItem(d.first, d.second);
        int index = m_location->findData(previous.isEmpty() ? m_env.lastUsedDir : previous);
        m_location->setCurrentIndex(index >= 0 ? index : 0);
    }
    // The combo is only worth showing when there is a choice.
    m_locationColumn->setVisible(m_location->count() > 1);
    const bool hasAdvanced = m_location->count() > 1 || m_env.templateAvailable;
    m_template->setVisible(m_env.templateAvailable);
    m_advanced->setVisible(hasAdvanced && m_kind != Kind::Modpack);
    m_advancedStrip->setVisible(hasAdvanced && m_kind != Kind::Modpack && m_advanced->isChecked());
}

void CreateInstanceDialog::setAdvancedExpanded(bool expanded)
{
    if (m_advanced->isChecked() != expanded) {
        const QSignalBlocker block(m_advanced);
        m_advanced->setChecked(expanded);
    }
    const bool hasAdvanced = m_location->count() > 1 || m_env.templateAvailable;
    m_advancedStrip->setVisible(expanded && hasAdvanced && m_kind != Kind::Modpack);
    m_advanced->setAccessibleDescription(expanded ? tr("Expanded") : tr("Collapsed"));
    refreshIcons();
}

void CreateInstanceDialog::setKind(Kind kind)
{
    if (kind == m_kind)
        return;
    m_kind = kind;
    m_cards->setCurrent(kindIndex(kind));
    applyKind(true);
}

void CreateInstanceDialog::chooseKind(Kind kind)
{
    if (kind == m_kind)
        return;
    m_kindChosen = true;
    m_kind = kind;
    m_cards->setCurrent(kindIndex(kind));
    applyKind(true);
}

void CreateInstanceDialog::applyKind(bool animate)
{
    auto change = [this]() {
        switch (m_kind) {
            case Kind::Vanilla:
            case Kind::Modded:
                m_versions->setModded(m_kind == Kind::Modded);
                m_config->setCurrentWidget(m_versions);
                break;
            case Kind::Modpack:
                m_config->setCurrentWidget(m_modpacks);
                break;
            case Kind::Import:
                m_config->setCurrentWidget(m_import);
                break;
        }
        // Modpack hands over to a provider browser: nothing is created here, so the preview and the instance fields go away.
        const bool creates = m_kind != Kind::Modpack;
        m_preview->setVisible(creates);
        m_iconButton->setVisible(creates);
        m_nameColumn->setVisible(creates);
        m_groupColumn->setVisible(creates);
        m_create->setVisible(creates);
        m_footerError->hide();
        refreshLocations();
        refreshState();
    };
    if (animate && isVisible())
        Transition::run(m_body, Transition::Kind::Crossfade, Tokens::durFast, change);
    else
        change();
    // The card row precedes the configuration in the tab order.
    QWidget* first = m_kind == Kind::Modpack ? m_modpacks->tile(m_modpacks->tileIds().value(0))
                     : m_kind == Kind::Import ? static_cast<QWidget*>(m_import->dropZone()->browseButton())
                                              : m_versions->firstFocusWidget();
    if (first)
        setTabOrder(m_cards->card(3), first);
}

void CreateInstanceDialog::refreshState()
{
    bool hasTask = false;
    QString suggestion;
    QList<QPair<QString, QString>> rows;
    switch (m_kind) {
        case Kind::Vanilla:
            hasTask = m_versions->hasSelection();
            suggestion = suggestName(m_versions->minecraftVersion());
            m_preview->setArtwork(QStringLiteral("cuboid"));
            rows = { { tr("Type"), tr("Vanilla") }, { tr("Minecraft"), m_versions->minecraftVersion() } };
            break;
        case Kind::Modded: {
            hasTask = m_versions->hasSelection();
            const auto loader = m_versions->loader();
            suggestion = suggestName(m_versions->minecraftVersion(), loader);
            m_preview->setArtwork(QStringLiteral("blocks"), loader ? loaderName(*loader) : QString());
            QString loaderText;
            if (loader && !m_versions->loaderVersion().isEmpty())
                loaderText = QStringLiteral("%1 %2").arg(loaderName(*loader), m_versions->loaderVersion());
            rows = { { tr("Type"), tr("Modded") }, { tr("Minecraft"), m_versions->minecraftVersion() }, { tr("Loader"), loaderText } };
            break;
        }
        case Kind::Import: {
            const ImportSource& s = m_import->source();
            hasTask = s.isImportable();
            suggestion = hasTask ? s.suggestedName : QString();
            m_preview->setArtwork(QStringLiteral("file-archive"));
            QString file;
            if (hasTask)
                file = s.fileName.isEmpty() ? s.url.toString() : s.fileName;
            rows = { { tr("Type"), tr("Import") }, { tr("Source file"), file } };
            break;
        }
        case Kind::Modpack:
            break;
    }

    if (suggestion != m_suggestedName) {
        m_suggestedName = suggestion;
        if (!m_nameEdited) {
            const QSignalBlocker block(m_name);
            m_name->setText(fitSuggestedName(suggestion));
            m_nameSelectedOnce = false;
        }
    }

    const QString error = validateName(m_name->text());
    const bool showError = m_kind != Kind::Modpack && !error.isEmpty() && (m_nameEdited || hasTask);
    m_nameError->setText(showError ? error : QString());
    m_name->setToolTip(showError ? error : QString());
    m_name->setAccessibleDescription(showError ? error : QString());
    m_create->setEnabled(m_kind != Kind::Modpack && hasTask && error.isEmpty() && !instDir().isEmpty());

    m_preview->setName(m_name->text());
    m_preview->setRows(rows);
}

bool CreateInstanceDialog::canCreate() const
{
    return m_create->isEnabled();
}

QString CreateInstanceDialog::nameError() const
{
    return m_nameError->text();
}

QString CreateInstanceDialog::footerError() const
{
    return m_footerError->isVisibleTo(this) ? m_footerError->text() : QString();
}

QString CreateInstanceDialog::instName() const
{
    return m_name->text().trimmed();
}

QString CreateInstanceDialog::instGroup() const
{
    return m_group->currentText();
}

QString CreateInstanceDialog::instDir() const
{
    return m_location->currentData().toString();
}

bool CreateInstanceDialog::copyTemplateDir() const
{
    return m_template->isChecked();
}

BaseVersion::Ptr CreateInstanceDialog::findVersion(const QString& uid, const QString& id) const
{
    if (id.isEmpty())
        return {};
    for (const Meta::Version::Ptr& v : m_meta->versions(uid))
        if (v && v->version() == id)
            return v;
    return {};
}

InstanceTask* CreateInstanceDialog::extractTask()
{
    InstanceTask* task = nullptr;
    switch (m_kind) {
        case Kind::Vanilla:
        case Kind::Modded: {
            if (!m_versions->hasSelection())
                return nullptr;
            const BaseVersion::Ptr minecraft = findVersion(kMinecraftUid, m_versions->minecraftVersion());
            if (!minecraft)
                return nullptr;
            if (m_kind == Kind::Vanilla) {
                task = new VanillaCreationTask(minecraft);
            } else {
                const QString uid = loaderUid(*m_versions->loader());
                const BaseVersion::Ptr loaderVersion = findVersion(uid, m_versions->loaderVersion());
                if (!loaderVersion)
                    return nullptr;
                task = new VanillaCreationTask(minecraft, uid, loaderVersion);
            }
            break;
        }
        case Kind::Import: {
            const ImportSource& s = m_import->source();
            if (!s.isImportable())
                return nullptr;
            // ImportPage parents the task to its page; ours lives as long as this dialog, which outlives the task run.
            task = new InstanceImportTask(s.url, false, this, {});
            break;
        }
        case Kind::Modpack:
            return nullptr;
    }
    // Exactly what NewInstanceDialog::extractTask() sets.
    task->setName(m_name->text().trimmed());
    task->setOriginalName(m_suggestedName.trimmed(), QString());
    task->setGroup(instGroup());
    task->setIcon(iconKey());
    task->setTargetDir(instDir());
    task->setCopyTemplateDir(copyTemplateDir());
    return task;
}

void CreateInstanceDialog::pickIcon()
{
    if (!m_env.pickIcon)
        return;
    const QString key = m_env.pickIcon(this, m_iconKey);
    if (key.isEmpty())
        return;
    m_iconKey = key;
    refreshIcons();
}

void CreateInstanceDialog::accept()
{
    if (!canCreate())
        return;
    const QString dir = instDir();
    if (!QDir(dir).exists()) {
        // NewInstanceDialog shows a message box here; ZERO says it inline and offers the remaining folders.
        m_footerError->setText(tr("The folder \"%1\" is no longer available. Choose another location.").arg(dir));
        m_footerError->show();
        refreshLocations();
        if (m_location->count() > 1)
            setAdvancedExpanded(true);
        refreshState();
        return;
    }
    saveState();
    QDialog::accept();
}

void CreateInstanceDialog::reject()
{
    saveState();
    QDialog::reject();
}

void CreateInstanceDialog::saveState()
{
    if (!m_env.writeSetting)
        return;
    m_env.writeSetting(kGeometryKey, QString::fromUtf8(saveGeometry().toBase64()));
    // Only a Vanilla / Modded choice the user made: Import (opened from "Import") and Modpack create nothing here.
    if (m_kindChosen && (m_kind == Kind::Vanilla || m_kind == Kind::Modded))
        m_env.writeSetting(kKindKey, kindKey(m_kind));
    m_env.writeSetting(kAdvancedKey, m_advanced->isChecked());
}

CreateInstanceDialog::Kind CreateInstanceDialog::rememberedKind(const CreateInstanceEnvironment& env)
{
    if (!env.readSetting)
        return Kind::Vanilla;
    return kindFromKey(env.readSetting(kKindKey).toString()).value_or(Kind::Vanilla);
}

QString CreateInstanceDialog::kindKey(Kind kind)
{
    switch (kind) {
        case Kind::Vanilla:
            return QStringLiteral("vanilla");
        case Kind::Modded:
            return QStringLiteral("modded");
        case Kind::Modpack:
            return QStringLiteral("modpack");
        case Kind::Import:
            return QStringLiteral("import");
    }
    return {};
}

std::optional<CreateInstanceDialog::Kind> CreateInstanceDialog::kindFromKey(const QString& key)
{
    for (Kind k : { Kind::Vanilla, Kind::Modded, Kind::Modpack, Kind::Import })
        if (kindKey(k) == key.trimmed().toLower())
            return k;
    return std::nullopt;
}

void CreateInstanceDialog::callWhenSettled(std::function<void()> done, int timeoutMs)
{
    const bool needsLists = m_kind == Kind::Vanilla || m_kind == Kind::Modded;
    if (!needsLists || m_versions->isSettled()) {
        QTimer::singleShot(0, this, done);
        return;
    }
    auto fired = std::make_shared<bool>(false);
    auto fire = [fired, done]() {
        if (*fired)
            return;
        *fired = true;
        done();
    };
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(m_versions, &VersionPanel::settledChanged, this, [this, fire, connection]() {
        if (!m_versions->isSettled())
            return;
        disconnect(*connection);
        fire();
    });
    QTimer::singleShot(timeoutMs, this, fire);
}

void CreateInstanceDialog::preselect(const QString& minecraft, std::optional<Loader> loader)
{
    m_versions->preselect(minecraft, loader);
}

void CreateInstanceDialog::paintEvent(QPaintEvent*)
{
    // `bg` with the theme's stone texture (Minecraft); the lists, preview and footer are flat surfaces on top.
    QPainter p(this);
    Paint::paintBackground(p, rect(), Theme::current().bg);
}

void CreateInstanceDialog::keyPressEvent(QKeyEvent* event)
{
    // Enter creates only from the name field (and on the Create button itself, which handles its own key): QDialog would
    // otherwise click the default button from any field (version search, group, link).
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (focusWidget() == m_name && canCreate())
            accept();
        event->accept();
        return;
    }
    QDialog::keyPressEvent(event);
}

bool CreateInstanceDialog::eventFilter(QObject* watched, QEvent* event)
{
    // NewInstanceDialog: the first time the name field gets focus its (suggested) text is selected, ready to overwrite.
    if (watched == m_name && event->type() == QEvent::FocusIn && !m_nameSelectedOnce) {
        m_nameSelectedOnce = true;
        QTimer::singleShot(0, m_name, &QLineEdit::selectAll);
    }
    return QDialog::eventFilter(watched, event);
}

}  // namespace Create
}  // namespace Zero
