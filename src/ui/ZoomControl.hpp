#pragma once

#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;
class QWheelEvent;

namespace digitalforge::editor {
class CircuitView;
}

namespace digitalforge::ui {

// Control de zoom de la barra de estado, con la misma disposicion que el de
// Excel: boton Alejar, deslizador con el 100% en el centro, boton Acercar y el
// porcentaje actual (al hacer clic vuelve al 100%). Es una vista sobre el zoom
// de CircuitView: no guarda estado propio, se sincroniza con la senal
// zoomChanged() y escribe via setZoomFactor().
class ZoomControl : public QWidget {
    Q_OBJECT

public:
    explicit ZoomControl(editor::CircuitView* view, QWidget* parent = nullptr);

    // Reengancha el control a otra vista (o a ninguna, con nullptr).
    void setView(editor::CircuitView* view);

protected:
    // La rueda sobre el control hace zoom, como sobre el lienzo.
    void wheelEvent(QWheelEvent* event) override;
    // El porcentaje es un QLabel (no un boton, para no meter relieve en la
    // barra de estado), asi que su clic se recoge por aqui.
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    // Vuelve a pedir los iconos a IconFactory al cambiar el tema claro/oscuro
    // del sistema, igual que SimulationToolbar y ComponentPalette.
    void refreshIcons();

private:
    // Refleja `factor` en el deslizador y la etiqueta sin reemitir cambios.
    void syncFrom(qreal factor);

    // El deslizador es lineal en pasos, pero el zoom es multiplicativo: 100%
    // debe caer en el centro aunque el rango sea 10%..400%. Estas dos
    // funciones convierten entre ambos usando dos tramos lineales (10->100 en
    // la mitad izquierda, 100->400 en la derecha), igual que Excel.
    [[nodiscard]] static int factorToSlider(qreal factor);
    [[nodiscard]] static qreal sliderToFactor(int value);

    editor::CircuitView* view_ = nullptr;
    QToolButton* zoomOutButton_ = nullptr;
    QToolButton* zoomInButton_ = nullptr;
    QSlider* slider_ = nullptr;
    QLabel* percentLabel_ = nullptr;
    // Evita el lazo deslizador -> vista -> deslizador mientras se arrastra.
    bool syncing_ = false;
};

} // namespace digitalforge::ui
