#include "AppSettings.hpp"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>

namespace digitalforge::app {

QString AppSettings::defaultWorkspacePath() {
    QString documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) {
        // Sistema sin carpeta de Documentos configurada: el home sirve igual.
        documents = QDir::homePath();
    }
    return QDir(documents).filePath(QStringLiteral("DigitalForge"));
}

QString AppSettings::effectiveWorkspacePath() const {
    return workspacePath.isEmpty() ? defaultWorkspacePath() : workspacePath;
}

QString AppSettings::ensureWorkspacePath() const {
    const QString path = effectiveWorkspacePath();
    QDir dir(path);
    if (dir.exists() || dir.mkpath(QStringLiteral("."))) {
        return path;
    }
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return fallback.isEmpty() ? QDir::homePath() : fallback;
}

AppSettings AppSettings::load() {
    QSettings settings;
    AppSettings result;
    settings.beginGroup("Preferences");
    result.autosaveIntervalSec = settings.value("autosaveIntervalSec", result.autosaveIntervalSec).toInt();
    result.defaultGridVisible = settings.value("defaultGridVisible", result.defaultGridVisible).toBool();
    result.defaultSnapToGrid = settings.value("defaultSnapToGrid", result.defaultSnapToGrid).toBool();
    result.workspacePath = settings.value("workspacePath", result.workspacePath).toString();
    result.themeMode = settings.value("themeMode", result.themeMode).toString();
    settings.endGroup();
    return result;
}

void AppSettings::save() const {
    QSettings settings;
    settings.beginGroup("Preferences");
    settings.setValue("autosaveIntervalSec", autosaveIntervalSec);
    settings.setValue("defaultGridVisible", defaultGridVisible);
    settings.setValue("defaultSnapToGrid", defaultSnapToGrid);
    settings.setValue("workspacePath", workspacePath);
    settings.setValue("themeMode", themeMode);
    settings.endGroup();
}

} // namespace digitalforge::app
