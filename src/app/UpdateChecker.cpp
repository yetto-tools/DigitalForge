#include "UpdateChecker.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <array>

#include "MainWindow.hpp"  // kAppVersion
#include "core/Version.hpp"

namespace digitalforge::app {

namespace {

constexpr const char* kReleasesApiUrl =
    "https://api.github.com/repos/yetto-tools/DigitalForge/releases?per_page=10";
constexpr const char* kReleasesPageUrl = "https://github.com/yetto-tools/DigitalForge/releases";

// Extrae el nucleo semantico (major, minor, patch) de una etiqueta como
// "v0.2.0-alpha" -> {0,2,0}. Ignora la 'v' inicial y cualquier sufijo de
// prelanzamiento tras el primer '-'. Los componentes ausentes valen 0.
std::array<int, 3> parseVersionCore(const QString& tag) {
    QString core = tag.trimmed();
    if (core.startsWith('v') || core.startsWith('V')) {
        core.remove(0, 1);
    }
    const qsizetype dash = core.indexOf('-');
    if (dash >= 0) {
        core = core.left(dash);
    }
    const qsizetype plus = core.indexOf('+'); // metadata de build, por si acaso
    if (plus >= 0) {
        core = core.left(plus);
    }
    std::array<int, 3> out{0, 0, 0};
    const QStringList parts = core.split('.');
    for (qsizetype i = 0; i < parts.size() && i < 3; ++i) {
        out[static_cast<std::size_t>(i)] = parts[i].toInt();
    }
    return out;
}

// True si `candidate` es estrictamente mayor que `current` comparando solo el
// nucleo semantico. Deliberadamente NO distingue prelanzamientos del mismo
// nucleo (0.1.0-alpha vs 0.1.0): asi el primer release no se anuncia a si
// mismo como "actualizacion" a un usuario que ya tiene esa version.
bool isNewer(const std::array<int, 3>& candidate, const std::array<int, 3>& current) {
    for (std::size_t i = 0; i < 3; ++i) {
        if (candidate[i] != current[i]) {
            return candidate[i] > current[i];
        }
    }
    return false;
}

} // namespace

UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent), manager_(new QNetworkAccessManager(this)) {}

void UpdateChecker::checkForUpdates(bool silent) {
    silent_ = silent;
    QNetworkRequest request((QUrl(QString::fromUtf8(kReleasesApiUrl))));
    request.setRawHeader("Accept", "application/vnd.github+json");
    // GitHub rechaza las peticiones sin User-Agent.
    request.setRawHeader("User-Agent", "DigitalForge-UpdateChecker");
    QNetworkReply* reply = manager_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { handleReply(reply); });
}

void UpdateChecker::handleReply(QNetworkReply* reply) {
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError) {
        if (!silent_) {
            emit checkFailed(reply->errorString());
        }
        return;
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
    if (!document.isArray()) {
        if (!silent_) {
            emit checkFailed(tr("respuesta inesperada de GitHub"));
        }
        return;
    }

    const QJsonArray releases = document.array();
    const std::array<int, 3> current = parseVersionCore(QString::fromUtf8(core::kDigitalForgeVersion));

    // El endpoint devuelve los releases del mas nuevo al mas viejo; se toma el
    // primero que traiga una etiqueta utilizable.
    for (const QJsonValue& value : releases) {
        const QJsonObject release = value.toObject();
        if (release.value("draft").toBool()) {
            continue; // por las dudas (los borradores no deberian llegar sin token)
        }
        const QString tag = release.value("tag_name").toString();
        if (tag.isEmpty()) {
            continue;
        }
        const QString url =
            release.value("html_url").toString(QString::fromUtf8(kReleasesPageUrl));
        if (isNewer(parseVersionCore(tag), current)) {
            emit updateAvailable(tag, url);
        } else if (!silent_) {
            emit upToDate();
        }
        return;
    }

    // No hay ningun release publicado todavia.
    if (!silent_) {
        emit upToDate();
    }
}

} // namespace digitalforge::app
