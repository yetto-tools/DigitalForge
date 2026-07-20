#pragma once

#include <QMetaObject>
#include <QToolBar>

class QAction;
class QLabel;

namespace digitalforge::editor {
class CircuitDocument;
}

namespace digitalforge::ui {

// Controles de Ejecutar / Pausar / Reiniciar / Paso a paso conectados
// directamente a las primitivas de simulacion de CircuitDocument
// (setLiveSimulation/rebuildSimulation/step), ademas de una etiqueta de
// estado que reporta advertencias de oscilacion.
class SimulationToolbar : public QToolBar {
    Q_OBJECT

public:
    explicit SimulationToolbar(editor::CircuitDocument* document, QWidget* parent = nullptr);

    // Rebinds this toolbar to a different document - usado por MainWindow al
    // cambiar de documento activo dentro de un mismo Project.
    void setDocument(editor::CircuitDocument* document);

private slots:
    void onRun();
    void onPause();
    void onReset();
    void onStep();
    void onTogglePolarity(bool positive);
    void updateStatus();
    // Vuelve a pedir cada icono a IconFactory (que relee el palette activo)
    // cuando el usuario cambia el tema claro/oscuro del sistema en caliente.
    void refreshIcons();

private:
    // Actualiza icono, texto y tooltip de polarityAction_ para reflejar
    // `positive`, sin cambiar su estado checked (llamarlo tras sincronizar
    // ese estado por separado, para no disparar toggled() de vuelta).
    void updatePolarityActionAppearance(bool positive);

    editor::CircuitDocument* document_;
    // QObject::disconnect(QMetaObject::Connection) es seguro incluso si el
    // documento del que colgaban ya se destruyo (a diferencia de
    // disconnect(document_, ...) con un puntero colgante).
    QMetaObject::Connection simulationRebuiltConnection_;
    QMetaObject::Connection simulationSteppedConnection_;
    QMetaObject::Connection positiveLogicPolarityChangedConnection_;
    QAction* runAction_;
    QAction* pauseAction_;
    QAction* stepAction_;
    QAction* resetAction_;
    QAction* polarityAction_;
    QLabel* statusLabel_;
};

} // namespace digitalforge::ui
