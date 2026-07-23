#include "LockFile.hpp"

#include <algorithm>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>

#include "FormatVersions.hpp"
#include "components/ComponentFingerprints.hpp"
#include "components/ComponentInstance.hpp"
#include "components/ComponentRegistry.hpp"
#include "core/Sha256.hpp"
#include "editor/CircuitDocument.hpp"
#include "editor/Project.hpp"

namespace digitalforge::formats {

namespace {

// Identificador de la unica biblioteca que existe hoy: la integrada en el
// binario. Cuando haya bibliotecas externas, cada componente dira de cual
// viene y esto se derivara de ahi.
constexpr const char* kBuiltinLibraryId = "org.digitalforge.builtin";

} // namespace

LockFile buildLockFile(const editor::Project& project, const std::string& digitalForgeVersion) {
    LockFile lock;
    lock.lockFormatVersion = versions::kLockSchema;
    lock.digitalForgeVersion = digitalForgeVersion;

    // Un typeId puede aparecer en muchas instancias y en varios documentos;
    // el lock lista cada tipo UNA vez, con la huella de su definicion (no de
    // una instancia concreta), asi que se recorre por tipo. std::map para que
    // el lock salga en orden estable y sea comparable/diffeable.
    std::map<std::string, components::ComponentFingerprints> usedTypes;

    for (const uint32_t documentId : project.documentIds()) {
        const editor::CircuitDocument* document = project.document(documentId);
        if (document == nullptr) {
            continue;
        }
        for (const uint32_t componentId : document->componentIds()) {
            const components::ComponentInstance* instance = document->component(componentId);
            if (instance == nullptr || usedTypes.count(instance->typeId()) != 0) {
                continue;
            }
            usedTypes.emplace(instance->typeId(), components::computeFingerprints(*instance));
        }
    }

    // contentHash de la biblioteca: huella acumulada de las interfaces
    // publicas de todos sus tipos en uso, en orden estable. Detecta que la
    // biblioteca integrada cambio entre versiones del binario.
    core::Sha256Builder libraryBuilder;
    libraryBuilder.field(std::string_view("library.v1")).field(kBuiltinLibraryId);

    for (const auto& [typeId, fingerprints] : usedTypes) {
        const components::ComponentDefinition& definition = project.registry().definition(typeId);
        LockedComponent locked;
        locked.typeId = typeId;
        locked.definitionVersion = definition.definitionVersion;
        locked.publicInterfaceHash = fingerprints.publicInterface.toHex();
        locked.simulationHash = fingerprints.simulation.toHex();
        lock.components.push_back(std::move(locked));

        libraryBuilder.field(typeId).field(fingerprints.publicInterface.toHex());
    }

    if (!usedTypes.empty()) {
        LockedLibrary library;
        library.libraryId = kBuiltinLibraryId;
        library.version = digitalForgeVersion; // la integrada versiona con la app
        library.contentHash = libraryBuilder.finish().toHex();
        lock.libraries.push_back(std::move(library));
    }

    return lock;
}

nlohmann::json serializeLockFile(const LockFile& lock) {
    nlohmann::json libraries = nlohmann::json::array();
    for (const LockedLibrary& library : lock.libraries) {
        libraries.push_back({
            {"libraryId", library.libraryId},
            {"version", library.version},
            {"contentHash", library.contentHash},
        });
    }

    nlohmann::json components = nlohmann::json::array();
    for (const LockedComponent& component : lock.components) {
        components.push_back({
            {"typeId", component.typeId},
            {"definitionVersion", component.definitionVersion},
            {"publicInterfaceHash", component.publicInterfaceHash},
            {"simulationHash", component.simulationHash},
        });
    }

    return nlohmann::json{
        {"lockFormatVersion", lock.lockFormatVersion},
        {"digitalForgeVersion", lock.digitalForgeVersion},
        {"libraries", std::move(libraries)},
        {"components", std::move(components)},
    };
}

LockFile parseLockFile(const nlohmann::json& json) {
    LockFile lock;
    lock.lockFormatVersion = json.value("lockFormatVersion", 0u);
    lock.digitalForgeVersion = json.value("digitalForgeVersion", std::string{});

    if (json.contains("libraries")) {
        for (const nlohmann::json& l : json.at("libraries")) {
            LockedLibrary library;
            library.libraryId = l.value("libraryId", std::string{});
            library.version = l.value("version", std::string{});
            library.contentHash = l.value("contentHash", std::string{});
            lock.libraries.push_back(std::move(library));
        }
    }
    if (json.contains("components")) {
        for (const nlohmann::json& c : json.at("components")) {
            LockedComponent component;
            component.typeId = c.value("typeId", std::string{});
            component.definitionVersion = c.value("definitionVersion", 0u);
            component.publicInterfaceHash = c.value("publicInterfaceHash", std::string{});
            component.simulationHash = c.value("simulationHash", std::string{});
            lock.components.push_back(std::move(component));
        }
    }
    return lock;
}

void writeLockFile(const editor::Project& project, const std::string& digitalForgeVersion,
                   const std::string& projectDir) {
    const LockFile lock = buildLockFile(project, digitalForgeVersion);
    const std::string path = projectDir.empty() ? std::string(kLockFileName)
                                                 : projectDir + "/" + kLockFileName;
    std::ofstream out(path, std::ios::trunc);
    if (out.is_open()) {
        out << serializeLockFile(lock).dump(2);
    }
}

bool readLockFile(const std::string& projectDir, LockFile& out) {
    const std::string path = projectDir.empty() ? std::string(kLockFileName)
                                                 : projectDir + "/" + kLockFileName;
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }
    nlohmann::json json;
    in >> json;
    out = parseLockFile(json);
    return true;
}

std::vector<LockDifference> compareLockFiles(const LockFile& stored, const LockFile& current) {
    std::map<std::string, const LockedComponent*> storedByType;
    std::map<std::string, const LockedComponent*> currentByType;
    for (const LockedComponent& c : stored.components) {
        storedByType[c.typeId] = &c;
    }
    for (const LockedComponent& c : current.components) {
        currentByType[c.typeId] = &c;
    }

    std::vector<LockDifference> differences;
    for (const auto& [typeId, storedComponent] : storedByType) {
        const auto it = currentByType.find(typeId);
        if (it == currentByType.end()) {
            differences.push_back({LockComparisonKind::Removed, typeId});
            continue;
        }
        const LockedComponent& currentComponent = *it->second;
        // Cambio silencioso: mismo typeId, distinta huella o version. Es
        // exactamente lo que el lock existe para no dejar pasar.
        if (storedComponent->publicInterfaceHash != currentComponent.publicInterfaceHash ||
            storedComponent->simulationHash != currentComponent.simulationHash ||
            storedComponent->definitionVersion != currentComponent.definitionVersion) {
            differences.push_back({LockComparisonKind::Changed, typeId});
        }
    }
    for (const auto& [typeId, currentComponent] : currentByType) {
        if (storedByType.count(typeId) == 0) {
            differences.push_back({LockComparisonKind::Added, typeId});
        }
    }
    return differences;
}

} // namespace digitalforge::formats
