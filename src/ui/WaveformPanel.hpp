#pragma once

#include <QSize>
#include <QWidget>

#include <vector>

#include "editor/CircuitDocument.hpp"

class QLabel;
class QPaintEvent;

namespace digitalforge::editor {
class WaveformRecorder;
}

namespace digitalforge::ui {

// Widget de dibujo puro (sin Q_OBJECT propio: no necesita senales/slots)
// que pinta una linea de tiempo escalonada por cada red vigilada de un
// editor::WaveformRecorder - tres niveles (alto/medio/bajo) segun el valor
// sea One/ambiguo(X,Z,Error)/Zero, con color distinto por estado, adaptable
// a tema claro/oscuro igual que ui::IconFactory. Sin zoom/scroll en v1: se
// ajusta al ancho disponible del panel.
class WaveformCanvas : public QWidget {
public:
    explicit WaveformCanvas(const editor::WaveformRecorder* recorder, QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const editor::WaveformRecorder* recorder_;
};

// Panel dock del analizador de senales. Boton "Agregar seleccion": emite
// addRequested(), que MainWindow conecta a la seleccion actual de la
// escena activa (ver MainWindow::setupDocks()) - este panel no conoce
// CircuitScene/ComponentItem directamente. v1 solo admite vigilar
// componentes de un solo pin (wiring.input/output, debug.probe, etc.); un
// selector de pin especifico dentro de un componente multi-pin queda para
// una fase futura.
class WaveformPanel : public QWidget {
    Q_OBJECT

public:
    explicit WaveformPanel(editor::CircuitDocument* document, QWidget* parent = nullptr);

    // Mismo patron que PropertyInspector::setDocument().
    void setDocument(editor::CircuitDocument* document);

    // Agrega cada endpoint (con su etiqueta) a la grabadora, si hay lugar -
    // llamado por MainWindow en respuesta a addRequested().
    void addWatches(const std::vector<editor::WireEndpoint>& endpoints, const std::vector<QString>& labels);

signals:
    void addRequested();

private slots:
    void onClearClicked();
    void onSamplesChanged();

private:
    editor::WaveformRecorder* recorder_;
    QLabel* statusLabel_;
    WaveformCanvas* canvas_;
};

} // namespace digitalforge::ui
