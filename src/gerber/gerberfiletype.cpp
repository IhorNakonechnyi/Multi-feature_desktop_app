#include "gerberfiletype.h"

#include <QFileInfo>
#include <QSet>
#include <QSettings>

namespace {

// Sane built-in default, merging two Gerber naming conventions (not
// confidential per research.md - just file-naming style, no spec/supplier
// data): the common Altium/KiCad-style extensions, and the EAGLE-CAM/Protel
// -style extensions confirmed against a real sample archive (cmp/sol top-
// /bottom copper, stc/sts top/bottom soldermask, plc top silkscreen).
// Deliberately excludes drd/dri/gpi seen in that same sample - drd is a
// real Excellon drill file (different format entirely, handled separately
// by the future ExcellonReader, not this Gerber parser), dri/gpi are plain-
// text CAM tool reports, not geometry at all.
const QSet<QString> &defaultExtensions()
{
    static const QSet<QString> extensions = {
        QStringLiteral("gtl"), QStringLiteral("gbl"), QStringLiteral("gts"), QStringLiteral("gbs"),
        QStringLiteral("gto"), QStringLiteral("gbo"), QStringLiteral("gtp"), QStringLiteral("gbp"),
        QStringLiteral("gko"), QStringLiteral("gml"), QStringLiteral("gbr"), QStringLiteral("drl"),
        QStringLiteral("xln"), QStringLiteral("cmp"), QStringLiteral("sol"), QStringLiteral("stc"),
        QStringLiteral("sts"), QStringLiteral("plc"),
    };
    return extensions;
}

QSettings makeSettings()
{
    return QSettings(QSettings::IniFormat, QSettings::UserScope, "PcbSupplierWorkflow", "PcbSupplierWorkflow");
}

constexpr auto kUserExtensionsKey = "gerber/userRecognizedExtensions";

} // namespace

bool GerberFileType::isRecognized(const QString &fileName)
{
    const QString extension = QFileInfo(fileName).suffix().toLower();
    if (extension.isEmpty()) {
        return false;
    }

    if (defaultExtensions().contains(extension)) {
        return true;
    }

    const QStringList userExtensions = makeSettings().value(kUserExtensionsKey).toStringList();
    for (const QString &userExtension : userExtensions) {
        if (userExtension.toLower() == extension) {
            return true;
        }
    }
    return false;
}

void GerberFileType::addUserExtension(const QString &extension)
{
    QSettings settings = makeSettings();
    QStringList userExtensions = settings.value(kUserExtensionsKey).toStringList();
    const QString normalized = extension.toLower();
    if (!userExtensions.contains(normalized, Qt::CaseInsensitive)) {
        userExtensions.append(normalized);
        settings.setValue(kUserExtensionsKey, userExtensions);
    }
}
