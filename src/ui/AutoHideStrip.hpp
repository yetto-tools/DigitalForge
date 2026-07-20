#pragma once

#include <QWidget>

#include <map>

class QDockWidget;
class QToolButton;
class QVBoxLayout;

namespace digitalforge::ui {

// Franja angosta (auto-hide, estilo Visual Studio) que se acopla junto al
// borde izquierdo o derecho de la ventana y muestra una pestana vertical por
// cada panel que el usuario despineo (ver DockTitleBar::pinnedChanged en
// MainWindow.cpp). Un click en una pestana pide que su panel se despliegue
// por encima del lienzo (ver MainWindow::showFlyout); la franja misma no
// sabe nada de flotar/posicionar, solo emite panelActivated().
class AutoHideStrip : public QWidget {
    Q_OBJECT

public:
    explicit AutoHideStrip(QWidget* parent = nullptr);

    // Agrega (o, si ya estaba, simplemente deja) la pestana de `dock`.
    void addPanel(QDockWidget* dock);
    // Quita la pestana de `dock` - usado al re-pinearlo.
    void removePanel(QDockWidget* dock);
    // Sincroniza el estado visual "presionado" de la pestana con si su panel
    // esta desplegado como flyout ahora mismo (ver MainWindow::showFlyout/
    // collapseFlyout) - no emite panelActivated.
    void setPanelActive(QDockWidget* dock, bool active);
    [[nodiscard]] bool isEmpty() const;

signals:
    void panelActivated(QDockWidget* dock);

private:
    QVBoxLayout* layout_;
    std::map<QDockWidget*, QToolButton*> buttons_;
};

} // namespace digitalforge::ui
