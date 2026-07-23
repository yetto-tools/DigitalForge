#include "Theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QWidget>

namespace digitalforge::ui {

namespace {

// Paletas explicitas (no derivadas del sistema) para que "Claro" y "Oscuro"
// se vean igual en cualquier maquina, que es justamente el sentido de forzar
// el tema en vez de dejarlo en Sistema.
QPalette buildDarkPalette() {
    QPalette palette;
    const QColor window(45, 45, 48);
    const QColor base(30, 30, 30);
    const QColor alternate(38, 38, 40);
    const QColor text(220, 220, 220);
    const QColor disabled(120, 120, 120);
    const QColor highlight(42, 130, 218);

    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternate);
    palette.setColor(QPalette::ToolTipBase, window);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, window);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, highlight);
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    return palette;
}

QPalette buildLightPalette() {
    QPalette palette;
    const QColor window(240, 240, 240);
    const QColor base(255, 255, 255);
    const QColor alternate(247, 247, 247);
    const QColor text(30, 30, 30);
    const QColor disabled(150, 150, 150);
    const QColor highlight(42, 130, 218);

    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternate);
    palette.setColor(QPalette::ToolTipBase, base);
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, window);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::red);
    palette.setColor(QPalette::Link, highlight);
    palette.setColor(QPalette::Highlight, highlight);
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, disabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
    return palette;
}

// style()->standardPalette() no distingue claro/oscuro (es la paleta
// generica del estilo, no la del SO), y una vez que se fuerza una paleta
// propia Qt ya no vuelve a sincronizarla sola con el tema del sistema. Por
// eso el modo Sistema arma su propia paleta a partir de lo que reporta
// QStyleHints::colorScheme(), que si sigue el registro de Windows en vivo.
void applySystemPalette() {
    const bool dark = QApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    QApplication::setPalette(dark ? buildDarkPalette() : buildLightPalette());
}

} // namespace

ThemeManager& ThemeManager::instance() {
    static ThemeManager manager;
    return manager;
}

void ThemeManager::setMode(ThemeMode mode) {
    if (!nativeCaptured_) {
        // Se captura en la primera aplicacion, no en el constructor: este
        // singleton puede crearse antes que QApplication.
        nativeStyleName_ = QApplication::style() != nullptr ? QApplication::style()->objectName() : QString();
        nativeCaptured_ = true;

        // Si el modo es Sistema y el usuario cambia el tema del SO mientras
        // la app esta abierta, hay que rearmar la paleta: colorSchemeChanged
        // avisa el cambio pero no reaplica nada por si solo.
        connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this](Qt::ColorScheme) {
            if (mode_ == ThemeMode::System) {
                setMode(ThemeMode::System);
            }
        });
    }

    const bool sameMode = mode == mode_ && mode != ThemeMode::System;
    mode_ = mode;

    switch (mode) {
        case ThemeMode::System:
            // Estilo nativo, pero con paleta propia calculada segun el
            // esquema de color real del SO (ver applySystemPalette()).
            if (!nativeStyleName_.isEmpty()) {
                QApplication::setStyle(QStyleFactory::create(nativeStyleName_));
            }
            applySystemPalette();
            break;
        case ThemeMode::Light:
            QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
            QApplication::setPalette(buildLightPalette());
            break;
        case ThemeMode::Dark:
            QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
            QApplication::setPalette(buildDarkPalette());
            break;
    }

    // setStyle()/setPalette() no repintan por si solos las ventanas ya
    // abiertas (p. ej. al volver de Fusion al estilo nativo en Windows los
    // fondos de botones quedan con los colores viejos hasta el proximo
    // resize). Se fuerza un repolish + repintado de todo lo que ya existe.
    QStyle* style = QApplication::style();
    for (QWidget* widget : QApplication::allWidgets()) {
        style->unpolish(widget);
        style->polish(widget);
        widget->update();
    }

    if (!sameMode) {
        emit changed();
    }
}

QString ThemeManager::toSettingsValue(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light:
            return QStringLiteral("light");
        case ThemeMode::Dark:
            return QStringLiteral("dark");
        case ThemeMode::System:
            break;
    }
    return QStringLiteral("system");
}

ThemeMode ThemeManager::fromSettingsValue(const QString& value) {
    if (value == QStringLiteral("light")) {
        return ThemeMode::Light;
    }
    if (value == QStringLiteral("dark")) {
        return ThemeMode::Dark;
    }
    return ThemeMode::System;
}

QString ThemeManager::displayName(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light:
            return QObject::tr("Claro");
        case ThemeMode::Dark:
            return QObject::tr("Oscuro");
        case ThemeMode::System:
            break;
    }
    return QObject::tr("Del sistema");
}

} // namespace digitalforge::ui
