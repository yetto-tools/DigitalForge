#pragma once

#include <QDialog>

#include "app/AppSettings.hpp"

class QCheckBox;
class QSpinBox;

namespace digitalforge::ui {

// Dialogo de Preferencias (menu Archivo de MainWindow). Los campos son el
// default para documentos *nuevos* (grid/snap ya son estado por documento,
// ver CircuitScene::setGridVisible/setSnapToGridEnabled) y el intervalo de
// autoguardado - no incluye layout de ventana/paneles, que MainWindow
// persiste por su cuenta (ver AppSettings.hpp).
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(const app::AppSettings& current, QWidget* parent = nullptr);

    [[nodiscard]] app::AppSettings values() const;

signals:
    // El boton "Restablecer valores predeterminados" ademas de resetear los
    // campos de este dialogo (sin cerrarlo), pide que MainWindow re-pinee
    // de inmediato cualquier panel auto-oculto en ese momento - "reset"
    // cubre tanto preferencias como el layout de paneles en una sola accion.
    void panelsResetRequested();

private:
    void resetToDefaults();

    QSpinBox* autosaveIntervalSpin_;
    QCheckBox* defaultGridCheck_;
    QCheckBox* defaultSnapCheck_;
};

} // namespace digitalforge::ui
