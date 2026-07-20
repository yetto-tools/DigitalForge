#include "ProjectManifestSerializer.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

namespace digitalforge::formats {

bool isProjectManifest(const nlohmann::json& json) { return json.contains("documents"); }

nlohmann::json serializeProjectManifest(const ProjectManifest& manifest) {
    nlohmann::json json;
    json["schemaVersion"] = 1;
    json["projectName"] = manifest.projectName.toStdString();

    nlohmann::json documents = nlohmann::json::array();
    for (const ProjectManifestEntry& entry : manifest.documents) {
        documents.push_back({
            {"name", entry.name.toStdString()},
            {"path", entry.relativePath.toStdString()},
        });
    }
    json["documents"] = std::move(documents);
    json["activeDocument"] = manifest.activeDocumentIndex;
    return json;
}

ProjectManifest parseProjectManifest(const nlohmann::json& json) {
    if (!json.contains("schemaVersion") || json.at("schemaVersion").get<int>() != 1) {
        throw std::invalid_argument("parseProjectManifest: unsupported or missing schemaVersion");
    }

    ProjectManifest manifest;
    manifest.projectName = QString::fromStdString(json.value("projectName", std::string()));

    for (const nlohmann::json& docJson : json.at("documents")) {
        ProjectManifestEntry entry;
        entry.name = QString::fromStdString(docJson.at("name").get<std::string>());
        entry.relativePath = QString::fromStdString(docJson.at("path").get<std::string>());
        manifest.documents.push_back(std::move(entry));
    }
    if (manifest.documents.empty()) {
        throw std::invalid_argument("parseProjectManifest: project has no documents");
    }

    manifest.activeDocumentIndex = json.value("activeDocument", 0);
    return manifest;
}

} // namespace digitalforge::formats
