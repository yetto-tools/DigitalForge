#pragma once

#include <QObject>
#include <QString>

namespace digitalforge::ui {

// Tema visual de la aplicacion.
enum class ThemeMode {
    System, // sigue el modo claro/oscuro configurado en el sistema operativo
    Light,
    Dark,
};

// Aplica el tema y avisa a quien tenga que redibujarse.
//
// Qt 6.7 todavia no permite forzar el esquema de color (QStyleHints::
// setColorScheme llega en 6.8), asi que los modos Claro/Oscuro se consiguen
// cambiando al estilo Fusion con una paleta propia; el modo Sistema restaura
// el estilo y la paleta nativos, que en Windows ya siguen la configuracion de
// color del sistema.
//
// Todo lo que hoy se reconstruye al recibir QStyleHints::colorSchemeChanged
// (iconos procedurales, colores del lienzo) debe conectarse ademas a
// changed(): cambiar la paleta a mano no emite aquella senal.
class ThemeManager : public QObject {
    Q_OBJECT

public:
    [[nodiscard]] static ThemeManager& instance();

    [[nodiscard]] ThemeMode mode() const noexcept { return mode_; }
    void setMode(ThemeMode mode);

    // Conversion a/desde el valor persistido en AppSettings.
    [[nodiscard]] static QString toSettingsValue(ThemeMode mode);
    [[nodiscard]] static ThemeMode fromSettingsValue(const QString& value);

    // Nombre visible para menus.
    [[nodiscard]] static QString displayName(ThemeMode mode);

signals:
    // Emitida despues de aplicar un tema distinto al vigente.
    void changed();

private:
    ThemeManager() = default;

    ThemeMode mode_ = ThemeMode::System;
    // Estilo y paleta con los que arranco la aplicacion, para poder volver
    // exactamente a ellos en modo Sistema.
    QString nativeStyleName_;
    bool nativeCaptured_ = false;
};

} // namespace digitalforge::ui
