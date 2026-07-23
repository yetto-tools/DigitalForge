#pragma once

#include <QString>

namespace digitalforge::app {

// Preferencias persistidas entre sesiones via QSettings (organizacion/app ya
// configuradas en main.cpp). No incluye layout de ventana/paneles - eso lo
// maneja MainWindow directamente (saveGeometry()/restoreGeometry() y el
// conjunto de paneles auto-ocultos, ver setDockAutoHidden()) porque necesita
// acceso directo a los widgets en vivo.
struct AppSettings {
    // 30 s: medio minuto entre autoguardados, el valor con el que se venia
    // trabajando en la practica (antes el default de fabrica eran 60 s).
    int autosaveIntervalSec = 30;
    bool defaultGridVisible = true;
    bool defaultSnapToGrid = true;
    // Carpeta donde arrancan los dialogos de abrir/guardar y donde se crean
    // los proyectos nuevos. Vacia = usar defaultWorkspacePath(); se guarda
    // vacia mientras el usuario no la cambie, para que la carpeta siga a la de
    // Documentos del sistema si esta se mueve (perfil movil, OneDrive, otro
    // idioma de Windows) en vez de quedar congelada en la ruta de hoy.
    QString workspacePath;
    // Tema visual: "system" (sigue al sistema operativo), "light" u "dark".
    // Se guarda como texto y no como indice para que agregar temas mas
    // adelante no reinterprete lo ya guardado - ver ui::ThemeManager.
    QString themeMode = QStringLiteral("system");

    // Carpeta de trabajo de fabrica: "<Documentos del usuario>/DigitalForge".
    // La ubicacion de Documentos la resuelve QStandardPaths segun el sistema
    // (en Windows la carpeta conocida del perfil, en Linux XDG_DOCUMENTS_DIR
    // con ~/Documents de reserva, en macOS ~/Documents), asi que no se leen
    // variables de entorno a mano ni se asume ningun idioma de carpeta.
    [[nodiscard]] static QString defaultWorkspacePath();

    // La ruta configurada, o la de fabrica si no hay ninguna. No toca el disco.
    [[nodiscard]] QString effectiveWorkspacePath() const;

    // Igual que effectiveWorkspacePath() pero creando la carpeta si falta.
    // Devuelve la ruta de Documentos como reserva si no se pudo crear (disco
    // de solo lectura, permisos), para que los dialogos siempre abran en algun
    // lugar valido.
    [[nodiscard]] QString ensureWorkspacePath() const;

    // Devuelve los valores guardados, o estos mismos defaults si todavia no
    // se guardo nada (primera ejecucion).
    [[nodiscard]] static AppSettings load();
    void save() const;
};

} // namespace digitalforge::app
