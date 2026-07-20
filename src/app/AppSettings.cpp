#include "AppSettings.hpp"

#include <QSettings>

namespace digitalforge::app {

AppSettings AppSettings::load() {
    QSettings settings;
    AppSettings result;
    settings.beginGroup("Preferences");
    result.autosaveIntervalSec = settings.value("autosaveIntervalSec", result.autosaveIntervalSec).toInt();
    result.defaultGridVisible = settings.value("defaultGridVisible", result.defaultGridVisible).toBool();
    result.defaultSnapToGrid = settings.value("defaultSnapToGrid", result.defaultSnapToGrid).toBool();
    settings.endGroup();
    return result;
}

void AppSettings::save() const {
    QSettings settings;
    settings.beginGroup("Preferences");
    settings.setValue("autosaveIntervalSec", autosaveIntervalSec);
    settings.setValue("defaultGridVisible", defaultGridVisible);
    settings.setValue("defaultSnapToGrid", defaultSnapToGrid);
    settings.endGroup();
}

} // namespace digitalforge::app
