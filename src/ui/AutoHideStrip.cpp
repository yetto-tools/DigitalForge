#include "AutoHideStrip.hpp"

#include <QDockWidget>
#include <QFontMetrics>
#include <QPainter>
#include <QToolButton>
#include <QVBoxLayout>

namespace digitalforge::ui {

namespace {

// Pestana individual de la franja: angosta y alta, con el texto rotado -90
// grados (se lee de abajo hacia arriba, igual que el auto-hide de Visual
// Studio). Se pinta a mano en vez de apoyarse en QStyle porque el control
// nativo de QToolButton no sabe rotar su contenido.
//
// Deliberadamente NO checkable: dejar que QToolButton alterne su propio
// "checked" en cada click competia con setActive() (llamado desde
// MainWindow al abrir/cerrar el flyout) - un segundo click alternaba el
// checked interno de Qt a destiempo de lo que en verdad estaba mostrado,
// dejando la pestana con el resaltado equivocado. active_ es la unica
// fuente de verdad para el estado visual.
class VerticalTabButton : public QToolButton {
public:
    explicit VerticalTabButton(const QString& text, QWidget* parent = nullptr) : QToolButton(parent) {
        setText(text);
        setAutoRaise(true);
        setCursor(Qt::PointingHandCursor);
    }

    [[nodiscard]] QSize sizeHint() const override {
        const QFontMetrics metrics(font());
        return QSize(26, metrics.horizontalAdvance(text()) + 24);
    }

    void setActive(bool active) {
        active_ = active;
        update();
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
        QPainter painter(this);
        if (active_) {
            painter.fillRect(rect(), palette().color(QPalette::Highlight));
            painter.setPen(palette().color(QPalette::HighlightedText));
        } else {
            if (underMouse()) {
                painter.fillRect(rect(), palette().color(QPalette::Midlight));
            }
            painter.setPen(palette().color(QPalette::WindowText));
        }
        painter.translate(0, height());
        painter.rotate(-90);
        painter.drawText(QRect(0, 0, height(), width()), Qt::AlignCenter, text());
    }

private:
    bool active_ = false;
};

} // namespace

AutoHideStrip::AutoHideStrip(QWidget* parent) : QWidget(parent) {
    layout_ = new QVBoxLayout(this);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    layout_->addStretch(1);
}

void AutoHideStrip::addPanel(QDockWidget* dock) {
    if (buttons_.contains(dock)) {
        return;
    }
    auto* button = new VerticalTabButton(dock->windowTitle(), this);
    connect(button, &QToolButton::clicked, this, [this, dock] { emit panelActivated(dock); });
    // Insertar antes del stretch final para que las pestanas se apilen desde
    // arriba y el espacio sobrante quede abajo.
    layout_->insertWidget(static_cast<int>(buttons_.size()), button);
    buttons_.emplace(dock, button);
}

void AutoHideStrip::removePanel(QDockWidget* dock) {
    const auto it = buttons_.find(dock);
    if (it == buttons_.end()) {
        return;
    }
    it->second->deleteLater();
    buttons_.erase(it);
}

void AutoHideStrip::setPanelActive(QDockWidget* dock, bool active) {
    const auto it = buttons_.find(dock);
    if (it == buttons_.end()) {
        return;
    }
    // buttons_ se declara como QToolButton* en el header (VerticalTabButton
    // es un detalle de implementacion, en el namespace anonimo de este
    // .cpp) pero addPanel() solo crea VerticalTabButton, asi que el cast es
    // seguro.
    static_cast<VerticalTabButton*>(it->second)->setActive(active);
}

bool AutoHideStrip::isEmpty() const { return buttons_.empty(); }

} // namespace digitalforge::ui
