#include "DiagnosticsPanel.hpp"

#include <QApplication>
#include <QLabel>
#include <QListWidget>
#include <QStyle>
#include <QVBoxLayout>

namespace digitalforge::ui {

using editor::CircuitDocument;

DiagnosticsPanel::DiagnosticsPanel(editor::CircuitDocument* document, QWidget* parent)
    : QWidget(parent), document_(nullptr) {
    auto* layout = new QVBoxLayout(this);

    list_ = new QListWidget(this);
    list_->setAlternatingRowColors(true);
    connect(list_, &QListWidget::itemActivated, this, &DiagnosticsPanel::onItemActivated);
    layout->addWidget(list_);

    setDocument(document);
}

void DiagnosticsPanel::setDocument(editor::CircuitDocument* document) {
    disconnect(simulationRebuiltConnection_);
    disconnect(simulationSteppedConnection_);
    disconnect(liveSimulationChangedConnection_);
    document_ = document;
    simulationRebuiltConnection_ = connect(document_, &CircuitDocument::simulationRebuilt, this,
                                            &DiagnosticsPanel::refresh);
    simulationSteppedConnection_ = connect(document_, &CircuitDocument::simulationStepped, this,
                                            &DiagnosticsPanel::refresh);
    liveSimulationChangedConnection_ = connect(document_, &CircuitDocument::liveSimulationChanged, this,
                                                &DiagnosticsPanel::refresh);
    refresh();
}

void DiagnosticsPanel::refresh() {
    list_->clear();
    rowEndpoints_.clear();
    if (document_ == nullptr) {
        return;
    }

    const std::vector<CircuitDocument::CircuitDiagnostic> diagnostics = document_->runDiagnostics();
    const QStyle* style = QApplication::style();
    for (const CircuitDocument::CircuitDiagnostic& diagnostic : diagnostics) {
        auto* item = new QListWidgetItem(diagnostic.message);
        const QStyle::StandardPixmap icon = diagnostic.severity == CircuitDocument::CircuitDiagnostic::Severity::Error
                                                 ? QStyle::SP_MessageBoxCritical
                                                 : QStyle::SP_MessageBoxWarning;
        item->setIcon(style->standardIcon(icon));
        list_->addItem(item);
        rowEndpoints_.push_back(diagnostic.relatedEndpoints);
    }

    if (diagnostics.empty()) {
        auto* item = new QListWidgetItem(tr("Sin problemas detectados."));
        item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        list_->addItem(item);
        rowEndpoints_.emplace_back();
    }
}

void DiagnosticsPanel::onItemActivated(QListWidgetItem* item) {
    const int row = list_->row(item);
    if (row < 0 || static_cast<std::size_t>(row) >= rowEndpoints_.size()) {
        return;
    }
    const std::vector<editor::WireEndpoint>& endpoints = rowEndpoints_[static_cast<std::size_t>(row)];
    if (endpoints.empty()) {
        return;
    }
    emit diagnosticActivated(endpoints);
}

} // namespace digitalforge::ui
