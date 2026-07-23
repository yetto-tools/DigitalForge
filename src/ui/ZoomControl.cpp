#include "ZoomControl.hpp"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QSlider>
#include <QStyleHints>
#include <QToolButton>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "IconFactory.hpp"
#include "Theme.hpp"
#include "editor/CircuitView.hpp"

namespace digitalforge::ui {

namespace {
// Posicion del 100% en el deslizador: el centro exacto, como en Excel.
constexpr int kSliderMin = 0;
constexpr int kSliderMax = 100;
constexpr int kSliderMid = 50;

// Zona iman alrededor del centro: al arrastrar cerca del 100% engancha en el
// valor exacto, que es el que el usuario busca la mayoria de las veces y a
// mano se pasa siempre por uno o dos pasos.
constexpr int kSnapRadius = 3;

// Altura util del control. Fijarla evita que la barra de estado crezca de alto
// al agregarle botones e iconos. Con el deslizador mostrando la muesca del
// 100% debajo del surco, por debajo de 20px el surco se comprime y queda
// visualmente mas fino que los iconos que lo flanquean.
constexpr int kControlHeight = 20;
} // namespace

ZoomControl::ZoomControl(editor::CircuitView* view, QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    // Sin margen vertical y con separacion minima: el control tiene que leerse
    // como una unidad compacta en el extremo de la barra, no como tres
    // controles sueltos.
    layout->setContentsMargins(6, 0, 2, 0);
    layout->setSpacing(2);

    // Botones planos y cuadrados con los mismos iconos vectoriales que el menu
    // Ver y la barra de herramientas -antes eran los caracteres "+" y "-", que
    // ni coincidian con el resto de la interfaz ni quedaban alineados con el
    // deslizador.
    const auto makeIconButton = [this](const QIcon& icon, const QString& tooltip) {
        auto* button = new QToolButton(this);
        button->setIcon(icon);
        button->setIconSize(QSize(14, 14));
        button->setFixedSize(kControlHeight, kControlHeight);
        button->setToolTip(tooltip);
        button->setAutoRaise(true);
        button->setAutoRepeat(true); // mantener pulsado sigue alejando/acercando
        button->setFocusPolicy(Qt::NoFocus);
        button->setCursor(Qt::ArrowCursor);
        return button;
    };

    zoomOutButton_ = makeIconButton(icons::zoomOut(), tr("Alejar"));
    zoomInButton_ = makeIconButton(icons::zoomIn(), tr("Acercar"));

    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(kSliderMin, kSliderMax);
    slider_->setValue(kSliderMid);
    slider_->setFixedWidth(96);
    slider_->setFixedHeight(kControlHeight);
    slider_->setFocusPolicy(Qt::NoFocus);
    // Una sola muesca, la del centro: marca el 100% sin llenar de rayas el
    // recorrido.
    slider_->setTickPosition(QSlider::TicksBelow);
    slider_->setTickInterval(kSliderMid);
    slider_->setSingleStep(1);
    slider_->setPageStep(10);
    slider_->setCursor(Qt::ArrowCursor);

    // El porcentaje es texto, no un boton: en la barra de estado un boton con
    // relieve compite con el resto de la informacion. Sigue siendo clickeable
    // (vuelve al 100%), lo que se indica con el cursor de mano.
    percentLabel_ = new QLabel(QStringLiteral("100%"), this);
    percentLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // Ancho FIJO al caso mas largo ("400%"): asi el texto no se desplaza
    // mientras se arrastra el deslizador y, sobre todo, la etiqueta no absorbe
    // el espacio sobrante que la barra de estado cede al control -que era lo
    // que dejaba el porcentaje flotando lejos del boton de acercar.
    percentLabel_->setFixedWidth(percentLabel_->fontMetrics().horizontalAdvance(QStringLiteral("400%")) + 8);
    percentLabel_->setToolTip(tr("Restablecer zoom al 100%"));
    percentLabel_->setCursor(Qt::PointingHandCursor);
    percentLabel_->installEventFilter(this);

    // Alineacion vertical explicita en cada pieza: los tres widgets tienen
    // alturas naturales distintas (boton, surco del deslizador, linea de
    // texto) y sin esto cada uno se apoya donde su sizeHint manda, que es lo
    // que los dejaba a distinta altura dentro de la barra.
    layout->addWidget(zoomOutButton_, 0, Qt::AlignVCenter);
    layout->addWidget(slider_, 0, Qt::AlignVCenter);
    layout->addWidget(zoomInButton_, 0, Qt::AlignVCenter);
    layout->addWidget(percentLabel_, 0, Qt::AlignVCenter);

    // El control ocupa exactamente su contenido: sin esto la barra de estado
    // lo estira hasta el borde y las piezas se separan entre si.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedHeight(kControlHeight);

    connect(slider_, &QSlider::valueChanged, this, [this](int value) {
        if (syncing_ || view_ == nullptr) {
            return;
        }
        if (std::abs(value - kSliderMid) <= kSnapRadius && value != kSliderMid) {
            slider_->setValue(kSliderMid); // reentra por este mismo slot con el valor exacto
            return;
        }
        view_->setZoomFactor(sliderToFactor(value));
    });

    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) { refreshIcons(); });
    // Forzar un tema cambia la paleta a mano, sin emitir colorSchemeChanged.
    connect(&ThemeManager::instance(), &ThemeManager::changed, this, [this] { refreshIcons(); });

    setView(view);
}

void ZoomControl::setView(editor::CircuitView* view) {
    if (view_ != nullptr) {
        disconnect(view_, nullptr, this, nullptr);
        disconnect(zoomOutButton_, nullptr, view_, nullptr);
        disconnect(zoomInButton_, nullptr, view_, nullptr);
    }
    view_ = view;
    setEnabled(view_ != nullptr);
    if (view_ == nullptr) {
        return;
    }

    connect(zoomOutButton_, &QToolButton::clicked, view_, &editor::CircuitView::zoomOut);
    connect(zoomInButton_, &QToolButton::clicked, view_, &editor::CircuitView::zoomIn);
    connect(view_, &editor::CircuitView::zoomChanged, this, &ZoomControl::syncFrom);

    syncFrom(view_->zoomFactor());
}

void ZoomControl::wheelEvent(QWheelEvent* event) {
    if (view_ == nullptr || event->angleDelta().y() == 0) {
        QWidget::wheelEvent(event);
        return;
    }
    if (event->angleDelta().y() > 0) {
        view_->zoomIn();
    } else {
        view_->zoomOut();
    }
    event->accept();
}

bool ZoomControl::eventFilter(QObject* watched, QEvent* event) {
    if (watched == percentLabel_ && event->type() == QEvent::MouseButtonPress && view_ != nullptr) {
        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            view_->resetZoom();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ZoomControl::refreshIcons() {
    zoomOutButton_->setIcon(icons::zoomOut());
    zoomInButton_->setIcon(icons::zoomIn());
}

void ZoomControl::syncFrom(qreal factor) {
    syncing_ = true;
    slider_->setValue(factorToSlider(factor));
    const QString percent = QStringLiteral("%1%").arg(std::lround(factor * 100.0));
    percentLabel_->setText(percent);
    // El tooltip del deslizador acompana al valor: al arrastrar se ve el
    // porcentaje sin tener que mirar al otro extremo del control.
    slider_->setToolTip(tr("Zoom: %1").arg(percent));
    syncing_ = false;
}

int ZoomControl::factorToSlider(qreal factor) {
    const qreal clamped = std::clamp(factor, editor::CircuitView::kMinZoom, editor::CircuitView::kMaxZoom);
    if (clamped <= 1.0) {
        const qreal span = 1.0 - editor::CircuitView::kMinZoom;
        return kSliderMin +
               static_cast<int>(std::lround((clamped - editor::CircuitView::kMinZoom) / span * kSliderMid));
    }
    const qreal span = editor::CircuitView::kMaxZoom - 1.0;
    return kSliderMid + static_cast<int>(std::lround((clamped - 1.0) / span * (kSliderMax - kSliderMid)));
}

qreal ZoomControl::sliderToFactor(int value) {
    if (value <= kSliderMid) {
        const qreal span = 1.0 - editor::CircuitView::kMinZoom;
        return editor::CircuitView::kMinZoom + span * static_cast<qreal>(value - kSliderMin) / kSliderMid;
    }
    const qreal span = editor::CircuitView::kMaxZoom - 1.0;
    return 1.0 + span * static_cast<qreal>(value - kSliderMid) / (kSliderMax - kSliderMid);
}

} // namespace digitalforge::ui
