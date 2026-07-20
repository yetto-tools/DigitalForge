#pragma once

#include <QMetaObject>
#include <QObject>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <vector>

#include "CircuitDocument.hpp"
#include "core/LogicValue.hpp"

namespace digitalforge::editor {

struct WaveformSample {
    uint64_t index;
    core::LogicValue value;
};

struct WatchedNet {
    WireEndpoint endpoint;
    QString label;
    // Solo se agrega una muestra nueva cuando el valor cambia respecto de
    // la ultima registrada (compresion "run-length" implicita) - por eso
    // cada entrada carga su propio indice en vez de asumir un paso fijo.
    std::vector<WaveformSample> samples;
};

// Graba el valor a lo largo del tiempo de un conjunto chico de redes
// vigiladas, muestreando en cada CircuitDocument::simulationStepped()/
// simulationRebuilt() del documento que observa.
//
// No hay un reloj/timer de simulacion continuo en este proyecto (ver
// CircuitDocument::step()/setInputValue(): "Ejecutar" solo activa
// isLiveSimulation(), la propagacion real ocurre de forma sincronica
// dentro de cada interaccion) - por eso el eje horizontal resultante es un
// contador de muestra local (nextSampleIndex_), no el core::Simulator::clock_
// real. Exponer ese tick real como accessor se pospone a una fase futura:
// pueden ocurrir varios eventos internos del simulador por cada
// step()/setInputValue(), asi que hoy no seria un eje de tiempo
// significativo.
class WaveformRecorder : public QObject {
    Q_OBJECT

public:
    static constexpr std::size_t kMaxWatchedNets = 8;

    explicit WaveformRecorder(CircuitDocument* document, QObject* parent = nullptr);

    // Mismo patron que PropertyInspector::setDocument() - usado por
    // MainWindow al cambiar de documento activo. Descarta todas las redes
    // vigiladas (pertenecian al documento anterior).
    void setDocument(CircuitDocument* document);

    // Devuelve false si ya hay kMaxWatchedNets redes vigiladas o si
    // `endpoint` ya esta siendo vigilado (sin agregar un duplicado).
    // Captura el valor actual como primera muestra de inmediato.
    bool addWatch(WireEndpoint endpoint, QString label);
    void removeWatch(std::size_t index);
    void clearWatches();

    [[nodiscard]] const std::vector<WatchedNet>& watches() const noexcept { return watches_; }
    [[nodiscard]] uint64_t currentSampleIndex() const noexcept { return nextSampleIndex_; }

signals:
    // Emitida despues de cualquier cambio a watches()/sus muestras -
    // ui::WaveformCanvas se conecta para saber cuando repintar.
    void samplesChanged();

private slots:
    void sample();
    void reset();

private:
    CircuitDocument* document_;
    std::vector<WatchedNet> watches_;
    uint64_t nextSampleIndex_ = 0;
    // Ver el comentario sobre QMetaObject::Connection::disconnect() en
    // PropertyInspector.hpp: seguro incluso si el documento ya se destruyo.
    QMetaObject::Connection steppedConnection_;
    QMetaObject::Connection rebuiltConnection_;
};

} // namespace digitalforge::editor
