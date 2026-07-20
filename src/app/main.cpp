#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QSplashScreen>

#include "MainWindow.hpp"

namespace {

// Splash dibujado a mano (no hay ningun arte de splash dedicado en
// resources/) a partir del icono de la app ya empaquetado - evita agregar
// un asset nuevo solo para esto.
QPixmap buildSplashPixmap() {
    constexpr int kWidth = 420;
    constexpr int kHeight = 260;
    QPixmap pixmap(kWidth, kHeight);
    pixmap.fill(QColor(30, 30, 34));

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPixmap logo(":/icons/app_128.png");
    if (!logo.isNull()) {
        constexpr int kLogoSize = 96;
        const QPixmap scaledLogo =
            logo.scaled(kLogoSize, kLogoSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        painter.drawPixmap((kWidth - kLogoSize) / 2, 28, scaledLogo);
    }

    QFont titleFont = painter.font();
    titleFont.setPointSizeF(20.0);
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(Qt::white);
    painter.drawText(QRect(0, 136, kWidth, 36), Qt::AlignCenter, QStringLiteral("DigitalForge"));

    QFont versionFont = painter.font();
    versionFont.setPointSizeF(9.0);
    versionFont.setBold(false);
    painter.setFont(versionFont);
    painter.setPen(QColor(170, 170, 170));
    painter.drawText(QRect(0, 172, kWidth, 20), Qt::AlignCenter, QString::fromUtf8(digitalforge::app::kAppVersion));

    return pixmap;
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("DigitalForge");
    QApplication::setOrganizationName("DigitalForge");

    QIcon appIcon;
    appIcon.addFile(":/icons/app_16.png");
    appIcon.addFile(":/icons/app_32.png");
    appIcon.addFile(":/icons/app_48.png");
    appIcon.addFile(":/icons/app_128.png");
    appIcon.addFile(":/icons/app_256.png");
    QApplication::setWindowIcon(appIcon);

    // MainWindow arma todos sus docks/paneles/menus en el constructor -
    // suficiente trabajo sincronico en la primera ejecucion (sin cache de
    // shaders/fuentes todavia) como para que la ventana tarde en aparecer
    // sin ningun indicio visual mientras tanto. splash.finish(&window) lo
    // cierra automaticamente en cuanto la ventana principal se muestra.
    QSplashScreen splash(buildSplashPixmap());
    splash.show();
    splash.showMessage(QObject::tr("Iniciando..."), Qt::AlignBottom | Qt::AlignHCenter, Qt::lightGray);
    QApplication::processEvents();

    digitalforge::app::MainWindow window;
    window.show();
    splash.finish(&window);

    return QApplication::exec();
}
