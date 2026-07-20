#include "WaveformPanel.hpp"

#include <QApplication>
#include <QColor>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QRectF>
#include <QVBoxLayout>

#include <algorithm>

#include "core/LogicValue.hpp"
#include "editor/WaveformRecorder.hpp"

namespace digitalforge::ui {

using editor::WaveformRecorder;

namespace {

constexpr int kRowHeight = 28;
constexpr int kLabelWidth = 90;
constexpr int kSampleWidth = 14;

// Mismo criterio que ui::IconFactory: nunca se cachea, cada pintado vuelve
// a leer el palette activo para que el canvas se actualice solo al cambiar
// de tema claro/oscuro.
bool isDarkPalette() { return QApplication::palette().color(QPalette::Base).lightness() < 128; }

QColor colorForValue(core::LogicValue value) {
    switch (value) {
        case core::LogicValue::Zero:
        case core::LogicValue::One:
            return isDarkPalette() ? QColor(90, 200, 120) : QColor(30, 140, 60);
        case core::LogicValue::HighImpedance:
            return QColor(150, 150, 150);
        case core::LogicValue::Unknown:
            return QColor(220, 160, 30);
        case core::LogicValue::Error:
            return QColor(200, 60, 60);
    }
    return QColor(150, 150, 150);
}

} // namespace

WaveformCanvas::WaveformCanvas(const editor::WaveformRecorder* recorder, QWidget* parent)
    : QWidget(parent), recorder_(recorder) {}

QSize WaveformCanvas::sizeHint() const {
    const int rows = std::max<int>(1, static_cast<int>(recorder_->watches().size()));
    return QSize(400, rows * kRowHeight + 10);
}

void WaveformCanvas::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    const QColor textColor = isDarkPalette() ? QColor(225, 225, 225) : QColor(40, 40, 40);
    const uint64_t nowIndex = recorder_->currentSampleIndex();

    int y = 5;
    for (const editor::WatchedNet& watch : recorder_->watches()) {
        painter.setPen(textColor);
        painter.drawText(QRectF(2, y, kLabelWidth - 4, kRowHeight), Qt::AlignVCenter | Qt::AlignLeft, watch.label);

        const int trackTop = y + 4;
        const int trackBottom = y + kRowHeight - 4;
        const int trackMid = (trackTop + trackBottom) / 2;
        const auto yFor = [&](core::LogicValue v) {
            if (v == core::LogicValue::One) {
                return trackTop;
            }
            if (v == core::LogicValue::Zero) {
                return trackBottom;
            }
            return trackMid;
        };

        int previousY = -1;
        for (std::size_t i = 0; i < watch.samples.size(); ++i) {
            const editor::WaveformSample& sample = watch.samples[i];
            const uint64_t endIndex =
                (i + 1 < watch.samples.size()) ? watch.samples[i + 1].index : std::max(nowIndex, sample.index + 1);
            const int startX = kLabelWidth + static_cast<int>(sample.index) * kSampleWidth;
            const int endX = kLabelWidth + static_cast<int>(endIndex) * kSampleWidth;
            const int lineY = yFor(sample.value);

            painter.setPen(QPen(colorForValue(sample.value), 2));
            if (previousY >= 0 && previousY != lineY) {
                painter.drawLine(startX, previousY, startX, lineY);
            }
            painter.drawLine(startX, lineY, endX, lineY);
            previousY = lineY;
        }
        y += kRowHeight;
    }
}

WaveformPanel::WaveformPanel(editor::CircuitDocument* document, QWidget* parent) : QWidget(parent) {
    recorder_ = new editor::WaveformRecorder(document, this);

    auto* layout = new QVBoxLayout(this);

    auto* buttonRow = new QHBoxLayout();
    auto* addButton = new QPushButton(tr("Agregar seleccion"), this);
    connect(addButton, &QPushButton::clicked, this, &WaveformPanel::addRequested);
    buttonRow->addWidget(addButton);
    auto* clearButton = new QPushButton(tr("Limpiar"), this);
    connect(clearButton, &QPushButton::clicked, this, &WaveformPanel::onClearClicked);
    buttonRow->addWidget(clearButton);
    buttonRow->addStretch();
    layout->addLayout(buttonRow);

    statusLabel_ = new QLabel(
        tr("Seleccione un componente de un solo pin (entrada, salida, sonda) en el lienzo y presione "
           "'Agregar seleccion'."),
        this);
    statusLabel_->setWordWrap(true);
    layout->addWidget(statusLabel_);

    canvas_ = new WaveformCanvas(recorder_, this);
    layout->addWidget(canvas_);
    layout->addStretch();

    connect(recorder_, &WaveformRecorder::samplesChanged, this, &WaveformPanel::onSamplesChanged);
}

void WaveformPanel::setDocument(editor::CircuitDocument* document) { recorder_->setDocument(document); }

void WaveformPanel::addWatches(const std::vector<editor::WireEndpoint>& endpoints,
                                const std::vector<QString>& labels) {
    std::size_t added = 0;
    for (std::size_t i = 0; i < endpoints.size(); ++i) {
        if (recorder_->addWatch(endpoints[i], labels[i])) {
            ++added;
        }
    }
    if (endpoints.empty()) {
        statusLabel_->setText(tr("Seleccione al menos un componente de un solo pin en el lienzo."));
    } else if (added == 0) {
        statusLabel_->setText(tr("No se agrego ninguna red nueva (limite de %1 alcanzado, o ya estaban vigiladas).")
                                   .arg(WaveformRecorder::kMaxWatchedNets));
    } else {
        statusLabel_->setText(tr("%1 red(es) agregada(s).").arg(added));
    }
}

void WaveformPanel::onClearClicked() { recorder_->clearWatches(); }

void WaveformPanel::onSamplesChanged() {
    canvas_->updateGeometry();
    canvas_->update();
}

} // namespace digitalforge::ui
