#include "DefaultLayout.hpp"

namespace digitalforge::app::defaults {

namespace {
// Capturado con QMainWindow::saveState() de una sesion real y pegado aqui en
// base64 (ver DefaultLayout.hpp). Cadena vacia = todavia no se capturo
// ninguno, y entonces vale el layout que arma setupDocks() en codigo.
constexpr const char* kWindowStateBase64 = "";
} // namespace

QByteArray windowState() { return QByteArray::fromBase64(QByteArray(kWindowStateBase64)); }

QStringList autoHiddenDocks() {
    // Paneles del lado derecho colapsados de entrada: el lienzo arranca con
    // todo el ancho util y cada panel se despliega al pulsar su pestana.
    return {QStringLiteral("truthTableDock"), QStringLiteral("waveformDock"), QStringLiteral("diagnosticsDock")};
}

} // namespace digitalforge::app::defaults
