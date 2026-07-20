#pragma once

namespace digitalforge::app {

// Preferencias persistidas entre sesiones via QSettings (organizacion/app ya
// configuradas en main.cpp). No incluye layout de ventana/paneles - eso lo
// maneja MainWindow directamente (saveGeometry()/restoreGeometry() y el
// conjunto de paneles auto-ocultos, ver setDockAutoHidden()) porque necesita
// acceso directo a los widgets en vivo.
struct AppSettings {
    int autosaveIntervalSec = 60;
    bool defaultGridVisible = true;
    bool defaultSnapToGrid = true;

    // Devuelve los valores guardados, o estos mismos defaults si todavia no
    // se guardo nada (primera ejecucion).
    [[nodiscard]] static AppSettings load();
    void save() const;
};

} // namespace digitalforge::app
