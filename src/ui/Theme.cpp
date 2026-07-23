#include "Theme.hpp"

#include <QApplication>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>

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
    }

    const bool sameMode = mode == mode_ && mode != ThemeMode::System;
    mode_ = mode;

    switch (mode) {
        case ThemeMode::System:
            // Volver al estilo nativo y soltar la paleta propia: a partir de
            // aca manda de nuevo la configuracion del sistema, que Qt aplica
            // sola (y notifica por QStyleHints::colorSchemeChanged).
            if (!nativeStyleName_.isEmpty()) {
                QApplication::setStyle(QStyleFactory::create(nativeStyleName_));
            }
            QApplication::setPalette(QApplication::style()->standardPalette());
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
