#pragma once

#include <QIcon>

#include "components/ComponentDefinition.hpp"

namespace digitalforge::ui {

// Cada icono aqui se dibuja de forma procedural con primitivas de QPainter
// en el momento de uso; no se carga nada desde un recurso externo ni se
// copia de ningun conjunto de iconos de terceros.
namespace icons {

[[nodiscard]] QIcon newDocument();
// Carpeta (no una hoja suelta como newDocument()) con una pequena marca
// "+" en la esquina - usado por "Nuevo proyecto" para distinguirlo
// visualmente de "Nuevo documento" (antes ambos compartian el mismo
// icono de hoja, sin ninguna diferencia visible entre "crear un proyecto"
// y "agregar un documento a uno ya existente").
[[nodiscard]] QIcon newProject();
[[nodiscard]] QIcon open();
[[nodiscard]] QIcon save();
[[nodiscard]] QIcon undo();
[[nodiscard]] QIcon redo();
[[nodiscard]] QIcon run();
[[nodiscard]] QIcon pause();
[[nodiscard]] QIcon step();
[[nodiscard]] QIcon reset();
// `positiveLogic` true dibuja un "0" (logica positiva: una Z flotante se
// resuelve a 0 al iniciar la simulacion), false dibuja un "1" (logica
// negativa: se resuelve a 1). Ver SimulationToolbar::polarityAction_.
[[nodiscard]] QIcon polarity(bool positiveLogic);
[[nodiscard]] QIcon zoomIn();
[[nodiscard]] QIcon zoomOut();
[[nodiscard]] QIcon deleteItem();
[[nodiscard]] QIcon rotate();
[[nodiscard]] QIcon dockFloat();
[[nodiscard]] QIcon dockClose();
// `pinned` true dibuja el alfiler recto (panel anclado normalmente); false
// lo dibuja inclinado (panel en auto-hide) - misma convencion visual que
// Visual Studio para el boton de pin de un tool window.
[[nodiscard]] QIcon dockPin(bool pinned);

} // namespace icons

// Un pequeno icono que representa un tipo de componente para el arbol de la
// paleta: el mismo contorno de puerta ANSI usado en el lienzo para gates.*,
// y un glifo original simple para cada una de las demas categorias.
[[nodiscard]] QIcon componentIcon(const components::ComponentDefinition& definition);

} // namespace digitalforge::ui
