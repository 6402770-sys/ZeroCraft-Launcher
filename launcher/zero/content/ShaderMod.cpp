// SPDX-License-Identifier: GPL-3.0-only
#include "ShaderMod.h"

#include <QCoreApplication>
#include <QDir>

namespace Zero::Content {

namespace {
QString tr(const char* text)
{
    return QCoreApplication::translate("Zero::Content::ShaderMod", text);
}
}  // namespace

ShaderModFacts shaderModFacts(const QStringList& modFileNames)
{
    ShaderModFacts facts;
    bool iris = false;
    bool optifine = false;
    for (const QString& name : modFileNames) {
        if (name.endsWith(QStringLiteral(".disabled"), Qt::CaseInsensitive)) {
            continue;
        }
        const bool isIris = name.contains(QStringLiteral("iris"), Qt::CaseInsensitive) || name.contains(QStringLiteral("oculus"), Qt::CaseInsensitive);
        const bool isOptiFine = name.contains(QStringLiteral("optifine"), Qt::CaseInsensitive);
        iris = iris || isIris;
        optifine = optifine || isOptiFine;
    }
    if (iris) {
        facts.platforms.append(QStringLiteral("iris"));
    }
    if (optifine) {
        facts.platforms.append(QStringLiteral("optifine"));
    }
    facts.hasShaderMod = iris || optifine;
    return facts;
}

QStringList modFileNamesIn(const QString& modsDir)
{
    return modsDir.isEmpty() ? QStringList() : QDir(modsDir).entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
}

ShaderNotice shaderNoticeFor(const QString& instanceName, ModPlatform::ModLoaderTypes loaders, const ShaderModFacts& facts)
{
    ShaderNotice notice;
    if (facts.hasShaderMod) {
        return notice;
    }
    if (loaders.testFlag(ModPlatform::Fabric) || loaders.testFlag(ModPlatform::Quilt) || loaders.testFlag(ModPlatform::NeoForge)) {
        notice.show = true;
        notice.text = tr("%1 has no shader mod. Shaders need Iris.").arg(instanceName);
        notice.buttonText = tr("Find Iris");
        notice.searchQuery = QStringLiteral("Iris");
    } else if (loaders.testFlag(ModPlatform::Forge)) {
        notice.show = true;
        notice.text = tr("%1 has no shader mod. Shaders need Oculus.").arg(instanceName);
        notice.buttonText = tr("Find Oculus");
        notice.searchQuery = QStringLiteral("Oculus");
    } else if (loaders == ModPlatform::ModLoaderTypes()) {
        notice.show = true;
        notice.text = tr("Shaders need Iris or OptiFine, which need a modded instance.");
    }
    return notice;
}

}  // namespace Zero::Content
