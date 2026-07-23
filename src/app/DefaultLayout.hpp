#pragma once

#include <QByteArray>
#include <QSize>
#include <QStringList>

namespace digitalforge::app {

// Disposicion de fabrica de la ventana principal: la que se aplica en la
// primera ejecucion, cuando lo guardado no se puede leer, y cuando el usuario
// pide "Restablecer disposicion predeterminada".
//
// El estado de los paneles (que area ocupa cada uno, con quien esta agrupado
// en pestanas, sus tamanos) no se escribe a mano: se captura de una sesion
// real con QMainWindow::saveState() y se pega aqui en base64. Ver
// MainWindow::closeEvent(), que es quien produce ese blob.
namespace defaults {

// Tamano inicial de la ventana cuando no hay geometria guardada. La posicion
// NO se hornea: la ventana se centra en la pantalla disponible, para que en un
// monitor mas chico que aquel donde se capturo no arranque fuera de los
// limites visibles.
inline constexpr QSize kWindowSize{1024, 768};

// Estado de paneles y barras (QMainWindow::saveState). Vacio = usar el layout
// que construye setupDocks() en codigo.
[[nodiscard]] QByteArray windowState();

// Paneles que arrancan auto-ocultos (colapsados en la franja lateral), por
// objectName. Se aplica *encima* de windowState().
[[nodiscard]] QStringList autoHiddenDocks();

} // namespace defaults

} // namespace digitalforge::app
