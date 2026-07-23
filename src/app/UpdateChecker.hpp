#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

namespace digitalforge::app {

// Comprueba, de forma asincronica y sin bloquear la interfaz, si hay una
// version mas nueva de DigitalForge publicada en los releases de GitHub, y lo
// informa por senal. NO descarga ni instala nada: solo compara la version y,
// si corresponde, avisa con el enlace a la pagina de descargas (el usuario
// decide). Es best-effort: sin conexion o con un error de red no molesta al
// usuario cuando el chequeo fue automatico (ver `silent`).
//
// Se consulta el endpoint publico de la API (sin token): la cuota anonima
// alcanza de sobra para un chequeo ocasional al arrancar.
class UpdateChecker : public QObject {
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // Lanza la consulta. `silent` = chequeo automatico al arrancar: solo emite
    // updateAvailable() si de verdad hay novedad; los casos "ya estas al dia"
    // o "fallo la red" se callan. Con `silent` = false (chequeo manual desde
    // el menu) tambien emite upToDate()/checkFailed() para dar respuesta.
    void checkForUpdates(bool silent);

signals:
    void updateAvailable(const QString& latestVersion, const QString& downloadUrl);
    void upToDate();
    void checkFailed(const QString& reason);

private:
    void handleReply(QNetworkReply* reply);

    QNetworkAccessManager* manager_;
    bool silent_ = true;
};

} // namespace digitalforge::app
