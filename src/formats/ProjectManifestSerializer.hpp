#pragma once

#include <QString>
#include <nlohmann/json_fwd.hpp>
#include <vector>

namespace digitalforge::formats {

// Una entrada del manifiesto: un documento (.dfc o .dfk) del proyecto,
// referenciado por una ruta relativa al directorio del propio archivo
// .dfproj (al estilo .sln + .csproj de Visual Studio) - nunca embebido en
// el manifiesto.
struct ProjectManifestEntry {
    QString name;
    QString relativePath;
    // "circuit" (.dfc, editor::CircuitDocument) o "karnaugh" (.dfk,
    // editor::KarnaughDocument). Ausente en el JSON == "circuit" (ver
    // parseProjectManifest) - todo proyecto guardado antes de que
    // existiera esta distincion sigue cargando exactamente igual.
    QString kind = QStringLiteral("circuit");
};

// Datos planos del manifiesto de un proyecto multi-documento (.dfproj,
// schemaVersion 1 con clave "documents"). No sabe nada de rutas absolutas ni
// del sistema de archivos - eso lo resuelve editor::Project, que conoce el
// directorio donde vive el propio .dfproj.
struct ProjectManifest {
    QString projectName;
    std::vector<ProjectManifestEntry> documents;
    int activeDocumentIndex = 0;
};

// true si json tiene la clave "documents" (schema de proyecto multi-documento),
// a diferencia del schema plano de un solo circuito de ProjectSerializer (que
// no la tiene). Se usa para decidir, al abrir un .dfproj, si es un proyecto o
// un circuito suelto del schema viejo.
[[nodiscard]] bool isProjectManifest(const nlohmann::json& json);

[[nodiscard]] nlohmann::json serializeProjectManifest(const ProjectManifest& manifest);

// Lanza std::invalid_argument ante un schemaVersion no soportado/ausente o una
// lista de documentos vacia.
[[nodiscard]] ProjectManifest parseProjectManifest(const nlohmann::json& json);

} // namespace digitalforge::formats
