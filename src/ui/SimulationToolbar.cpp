#include "SimulationToolbar.hpp"

#include <QAction>
#include <QApplication>
#include <QLabel>
#include <QSignalBlocker>
#include <QStyleHints>

#include "IconFactory.hpp"
#include "Theme.hpp"
#include "editor/CircuitDocument.hpp"

namespace digitalforge::ui {

using editor::CircuitDocument;

SimulationToolbar::SimulationToolbar(editor::CircuitDocument* document, QWidget* parent)
    : QToolBar(tr("Simulacion"), parent), document_(nullptr) {
    runAction_ = addAction(icons::run(), tr("Ejecutar"), this, &SimulationToolbar::onRun);
    pauseAction_ = addAction(icons::pause(), tr("Pausar"), this, &SimulationToolbar::onPause);
    stepAction_ = addAction(icons::step(), tr("Paso a paso"), this, &SimulationToolbar::onStep);
    resetAction_ = addAction(icons::reset(), tr("Reiniciar"), this, &SimulationToolbar::onReset);

    addSeparator();
    polarityAction_ = addAction(icons::polarity(true), QString(), this, &SimulationToolbar::onTogglePolarity);
    polarityAction_->setCheckable(true);
    polarityAction_->setChecked(true);

    statusLabel_ = new QLabel(this);
    addSeparator();
    addWidget(statusLabel_);

    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) { refreshIcons(); });
    // Forzar un tema cambia la paleta a mano, sin emitir colorSchemeChanged.
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, [this] { refreshIcons(); });

    setDocument(document);
}

void SimulationToolbar::setDocument(editor::CircuitDocument* document) {
    disconnect(simulationRebuiltConnection_);
    disconnect(simulationSteppedConnection_);
    disconnect(positiveLogicPolarityChangedConnection_);
    document_ = document;
    simulationRebuiltConnection_ = connect(document_, &CircuitDocument::simulationRebuilt, this,
                                            &SimulationToolbar::updateStatus);
    simulationSteppedConnection_ = connect(document_, &CircuitDocument::simulationStepped, this,
                                            &SimulationToolbar::updateStatus);
    positiveLogicPolarityChangedConnection_ =
        connect(document_, &CircuitDocument::positiveLogicPolarityChanged, this,
                &SimulationToolbar::updatePolarityActionAppearance);
    {
        const QSignalBlocker blocker(polarityAction_);
        polarityAction_->setChecked(document_->positiveLogicPolarity());
    }
    updatePolarityActionAppearance(document_->positiveLogicPolarity());
    updateStatus();
}

void SimulationToolbar::onRun() {
    document_->setLiveSimulation(true);
    updateStatus();
}

void SimulationToolbar::onPause() {
    document_->setLiveSimulation(false);
    updateStatus();
}

void SimulationToolbar::onReset() {
    document_->rebuildSimulation();
    updateStatus();
}

void SimulationToolbar::onStep() { document_->step(); }

void SimulationToolbar::onTogglePolarity(bool positive) {
    document_->setPositiveLogicPolarity(positive);
    updatePolarityActionAppearance(positive);
}

void SimulationToolbar::updatePolarityActionAppearance(bool positive) {
    polarityAction_->setIcon(icons::polarity(positive));
    polarityAction_->setText(positive ? tr("Logica positiva") : tr("Logica negativa"));
    polarityAction_->setToolTip(
        positive
            ? tr("Logica positiva: voltaje alto = 1, voltaje bajo = 0. "
                 "Una entrada flotante (Z) se resuelve a 0 al iniciar la simulacion.")
            : tr("Logica negativa: voltaje alto = 0, voltaje bajo = 1. "
                 "Una entrada flotante (Z) se resuelve a 1 al iniciar la simulacion."));
}

void SimulationToolbar::refreshIcons() {
    runAction_->setIcon(icons::run());
    pauseAction_->setIcon(icons::pause());
    stepAction_->setIcon(icons::step());
    resetAction_->setIcon(icons::reset());
    polarityAction_->setIcon(icons::polarity(polarityAction_->isChecked()));
}

void SimulationToolbar::updateStatus() {
    runAction_->setEnabled(!document_->isLiveSimulation());
    pauseAction_->setEnabled(document_->isLiveSimulation());
    stepAction_->setEnabled(!document_->isLiveSimulation());

    if (document_->oscillationDetected()) {
        statusLabel_->setText(tr("Oscilacion detectada - algunas redes quedaron en X"));
        statusLabel_->setStyleSheet("color: #b00000; font-weight: bold;");
    } else {
        statusLabel_->setText(document_->isLiveSimulation() ? tr("Ejecutando") : tr("Pausado"));
        statusLabel_->setStyleSheet("");
    }
}

} // namespace digitalforge::ui
