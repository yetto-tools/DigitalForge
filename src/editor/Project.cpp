#include "Project.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUndoStack>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numeric>
#include <stdexcept>

#include "CircuitDocument.hpp"
#include "components/BasicComponentLibrary.hpp"
#include "formats/LogisimImporter.hpp"
#include "formats/ProjectManifestSerializer.hpp"
#include "formats/ProjectSerializer.hpp"

namespace digitalforge::editor {

namespace {

// Espia el JSON crudo de `path` (sin construir ningun CircuitDocument
// todavia) para saber si contiene algun structural.subcircuit - usado por
// loadFromFile para decidir el orden de carga: como el anidamiento esta
// limitado a un nivel, alcanza con cargar primero los documentos "hoja"
// (sin subcircuitos) y recien despues los que si tienen uno, para que su
// documento referenciado ya exista *con contenido* cuando el subcircuito
// intente resolver sus pines (si no, resolveria al documento correcto pero
// todavia vacio, perdiendo los cables que apuntaban a sus pines).
bool documentHasSubcircuit(const QString& path) {
    std::ifstream file(path.toStdString());
    if (!file.is_open()) {
        return false;
    }
    nlohmann::json json;
    file >> json;
    if (!json.contains("components")) {
        return false;
    }
    for (const nlohmann::json& c : json.at("components")) {
        if (c.contains("typeId") && c.at("typeId").get<std::string>() == "structural.subcircuit") {
            return true;
        }
    }
    return false;
}

} // namespace

Project::Project(QObject* parent) : QObject(parent) {
    components::registerBasicComponentLibrary(registry_);
    newProject();
}

Project::~Project() = default;

void Project::clearAllDocuments() {
    for (auto it = documents_.begin(); it != documents_.end();) {
        emit documentAboutToBeRemoved(it->first);
        undoGroup_.removeStack(it->second.undoStack.get());
        it = documents_.erase(it);
    }
    order_.clear();
}

void Project::newProject() {
    clearAllDocuments();
    nextId_ = 0;
    projectFilePath_.clear();
    projectName_.clear();
    manifestDirty_ = false;
    activeId_ = addEntry(QStringLiteral("Documento"), QString());
    // addEntry() solo agrega el stack al grupo (QUndoGroup::addStack no lo
    // activa solo) - a diferencia de setActiveDocument()/loadFromFile(),
    // sin esto el grupo se queda sin stack activo y Deshacer/Rehacer
    // quedan deshabilitados para siempre en el documento anonimo inicial.
    undoGroup_.setActiveStack(documents_.at(activeId_).undoStack.get());
    emit activeDocumentChanged(activeId_);
}

uint32_t Project::addEntry(const QString& name, const QString& absolutePath) {
    Entry entry;
    entry.name = name;
    entry.absolutePath = absolutePath;
    entry.document = std::make_unique<CircuitDocument>();
    entry.undoStack = std::make_unique<QUndoStack>();
    undoGroup_.addStack(entry.undoStack.get());

    const uint32_t id = nextId_++;
    documents_[id] = std::move(entry);
    order_.push_back(id);
    refreshSiblingResolvers();
    emit documentAdded(id);
    return id;
}

void Project::refreshSiblingResolvers() {
    // Cada CircuitDocument recibe una lambda que busca en documents_ *en el
    // momento en que se la invoca* (no una foto de ahora) - por eso no
    // importa que esto se llame antes de que todos los documentos de una
    // carga multi-documento existan todavia (ver loadFromFile): para cuando
    // algun structural.subcircuit efectivamente la invoque, ya van a estar
    // todos.
    for (const auto& [id, entry] : documents_) {
        entry.document->setSiblingResolver([this](const QString& relativePath) -> const CircuitDocument* {
            if (projectFilePath_.isEmpty()) {
                return nullptr; // el proyecto nunca se guardo - ninguna ruta relativa es resoluble todavia
            }
            const QDir projectDir = QFileInfo(projectFilePath_).absoluteDir();
            const QString wanted = QFileInfo(projectDir.filePath(relativePath)).absoluteFilePath();
            for (const auto& [otherId, otherEntry] : documents_) {
                if (otherEntry.absolutePath.isEmpty()) {
                    continue; // ese documento hermano tampoco se guardo todavia
                }
                if (QFileInfo(otherEntry.absolutePath).absoluteFilePath() == wanted) {
                    return otherEntry.document.get();
                }
            }
            return nullptr;
        });
    }
}

bool Project::hasDocumentNamed(const QString& name, std::optional<uint32_t> excludeId) const {
    for (const auto& [id, entry] : documents_) {
        if (excludeId.has_value() && id == *excludeId) {
            continue;
        }
        if (entry.name.compare(name, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

QString Project::suggestUniqueDocumentName(const QString& base) const {
    if (!hasDocumentNamed(base)) {
        return base;
    }
    int suffix = 2;
    QString candidate;
    do {
        candidate = base + QString::number(suffix++);
    } while (hasDocumentNamed(candidate));
    return candidate;
}

uint32_t Project::addDocument(const QString& name) {
    if (hasDocumentNamed(name)) {
        throw std::invalid_argument("addDocument: ya existe un documento llamado '" + name.toStdString() + "'");
    }
    const uint32_t id = addEntry(name, QString());
    manifestDirty_ = true;
    setActiveDocument(id);
    return id;
}

uint32_t Project::importDocument(const QString& externalPath) {
    const QFileInfo info(externalPath);
    if (!info.exists()) {
        throw std::runtime_error("importDocument: no existe '" + externalPath.toStdString() + "'");
    }
    const QString name = info.completeBaseName();
    if (hasDocumentNamed(name)) {
        throw std::invalid_argument("importDocument: ya existe un documento llamado '" + name.toStdString() + "'");
    }

    QString targetPath = info.absoluteFilePath();
    if (!projectFilePath_.isEmpty()) {
        const QDir projectDir = QFileInfo(projectFilePath_).absoluteDir();
        const QString candidate = projectDir.filePath(info.fileName());
        if (QFileInfo(candidate).absoluteFilePath() != info.absoluteFilePath()) {
            // El proyecto ya tiene una carpeta propia: un documento
            // importado tiene que vivir ahi adentro, nunca quedar
            // referenciando un archivo externo (la fuga silenciosa
            // reportada) - si la copia falla, se rechaza en vez de caer de
            // vuelta a la ruta original.
            if (!QFile::copy(externalPath, candidate)) {
                throw std::runtime_error("importDocument: no se pudo copiar '" + externalPath.toStdString() +
                                          "' a la carpeta del proyecto");
            }
            targetPath = candidate;
        }
    }

    const uint32_t id = addEntry(name, targetPath);
    formats::loadProjectFromFile(*documents_.at(id).document, targetPath.toStdString());
    manifestDirty_ = true;
    setActiveDocument(id);
    return id;
}

uint32_t Project::importLogisimDocument(const QString& circPath) {
    const QFileInfo info(circPath);
    if (!info.exists()) {
        throw std::runtime_error("importLogisimDocument: no existe '" + circPath.toStdString() + "'");
    }

    // A diferencia de importDocument() (que preserva un enlace al archivo
    // .dfc original), un .circ de Logisim se convierte de una vez al
    // formato propio de DigitalForge: el documento resultante queda "nunca
    // guardado" (sin absolutePath), igual que addDocument().
    const uint32_t id = addEntry(info.completeBaseName(), QString());
    formats::importLogisimCircFile(*documents_.at(id).document, circPath.toStdString());
    manifestDirty_ = true;
    setActiveDocument(id);
    return id;
}

void Project::exportDocument(uint32_t id, const QString& destPath) const {
    const auto it = documents_.find(id);
    if (it == documents_.end()) {
        throw std::invalid_argument("exportDocument: documento desconocido");
    }
    formats::saveProjectToFile(*it->second.document, destPath.toStdString());
}

void Project::removeDocument(uint32_t id) {
    if (order_.size() <= 1) {
        throw std::invalid_argument("removeDocument: el proyecto debe tener al menos un documento");
    }
    const auto it = documents_.find(id);
    if (it == documents_.end()) {
        throw std::invalid_argument("removeDocument: documento desconocido");
    }

    emit documentAboutToBeRemoved(id);
    undoGroup_.removeStack(it->second.undoStack.get());
    documents_.erase(it);
    order_.erase(std::find(order_.begin(), order_.end(), id));
    manifestDirty_ = true;
    refreshSiblingResolvers();

    if (activeId_ == id) {
        setActiveDocument(order_.front());
    }
}

void Project::renameDocument(uint32_t id, const QString& newName) {
    if (hasDocumentNamed(newName, id)) {
        throw std::invalid_argument("renameDocument: ya existe un documento llamado '" + newName.toStdString() + "'");
    }
    documents_.at(id).name = newName;
    manifestDirty_ = true;
    emit documentRenamed(id);
}

std::vector<uint32_t> Project::documentIds() const { return order_; }

QString Project::documentName(uint32_t id) const { return documents_.at(id).name; }

CircuitDocument* Project::document(uint32_t id) const { return documents_.at(id).document.get(); }

QUndoStack* Project::undoStack(uint32_t id) const { return documents_.at(id).undoStack.get(); }

void Project::setActiveDocument(uint32_t id) {
    if (id == activeId_ || !documents_.contains(id)) {
        return;
    }
    activeId_ = id;
    undoGroup_.setActiveStack(documents_.at(id).undoStack.get());
    emit activeDocumentChanged(id);
}

CircuitDocument* Project::activeDocument() const { return document(activeId_); }

QUndoStack* Project::activeUndoStack() const { return undoStack(activeId_); }

bool Project::hasUnsavedChanges() const {
    if (manifestDirty_) {
        return true;
    }
    for (const auto& [id, entry] : documents_) {
        if (!entry.undoStack->isClean()) {
            return true;
        }
    }
    return false;
}

void Project::loadFromFile(const QString& path) {
    std::ifstream file(path.toStdString());
    if (!file.is_open()) {
        throw std::runtime_error("Project::loadFromFile: no se pudo abrir '" + path.toStdString() + "'");
    }
    nlohmann::json json;
    file >> json;

    clearAllDocuments();
    nextId_ = 0;

    const QDir baseDir = QFileInfo(path).absoluteDir();

    if (formats::isProjectManifest(json)) {
        const formats::ProjectManifest manifest = formats::parseProjectManifest(json);
        projectName_ = manifest.projectName;
        // Asignado *antes* de cargar contenido (no al final, como antes de
        // Fase 4): el resolver de structural.subcircuit que
        // refreshSiblingResolvers() instala en cada documento depende de
        // projectFilePath_ para resolver rutas relativas, y ese resolver ya
        // se invoca durante la carga de componentes de abajo.
        projectFilePath_ = path;

        // Primera pasada: crear *todas* las entradas de documento (sin
        // cargar su contenido todavia), para que un structural.subcircuit
        // pueda referenciar a cualquier documento hermano sin importar el
        // orden en que aparecen en el manifiesto.
        std::vector<uint32_t> newIds;
        std::vector<QString> absPaths;
        newIds.reserve(manifest.documents.size());
        absPaths.reserve(manifest.documents.size());
        for (const formats::ProjectManifestEntry& docEntry : manifest.documents) {
            const QString absPath = baseDir.filePath(docEntry.relativePath);
            newIds.push_back(addEntry(docEntry.name, absPath));
            absPaths.push_back(absPath);
        }

        // Segunda pasada: recien ahora cargar el contenido de cada uno, en
        // un orden que garantice que cualquier documento referenciado por
        // un structural.subcircuit ya tenga su propio contenido cargado
        // (no solo la entrada creada en la primera pasada) - ver
        // documentHasSubcircuit() arriba.
        std::vector<std::size_t> loadOrder(newIds.size());
        std::iota(loadOrder.begin(), loadOrder.end(), std::size_t{0});
        std::stable_partition(loadOrder.begin(), loadOrder.end(),
                               [&absPaths](std::size_t i) { return !documentHasSubcircuit(absPaths[i]); });
        for (const std::size_t i : loadOrder) {
            formats::loadProjectFromFile(*documents_.at(newIds[i]).document, absPaths[i].toStdString());
        }

        const int clampedIndex = std::clamp(manifest.activeDocumentIndex, 0, static_cast<int>(newIds.size()) - 1);
        activeId_ = newIds[static_cast<std::size_t>(clampedIndex)];
        undoGroup_.setActiveStack(documents_.at(activeId_).undoStack.get());
    } else {
        projectName_.clear();
        const uint32_t id = addEntry(QFileInfo(path).completeBaseName(), path);
        formats::loadProject(*documents_.at(id).document, json);
        activeId_ = id;
        undoGroup_.setActiveStack(documents_.at(id).undoStack.get());
        // Sigue siendo "anonimo": este .dfproj es el circuito en si (schema
        // viejo), no un manifiesto de proyecto propio.
        projectFilePath_.clear();
    }

    manifestDirty_ = false;
    emit activeDocumentChanged(activeId_);
    emit projectMetadataChanged();
}

void Project::saveToFile(const QString& path) {
    const QDir projectDir = QFileInfo(path).absoluteDir();
    // Cada documento siempre se reescribe completo (son "los importantes":
    // deben contener el circuito completo y actualizado en todo momento,
    // sin depender de una senal de "sucio" que solo captura ediciones
    // hechas via QUndoCommand - CircuitDocument tambien se puede mutar
    // directamente). El .dfproj en cambio es barato de recalcular pero no
    // deberia reescribirse en cada Guardar sin necesidad: solo se toca si
    // la lista de documentos cambio desde el ultimo guardado
    // (manifestDirty_) o si `path` es una ubicacion nueva (primer guardado
    // o "Guardar como" - ahi si hace falta, para que el manifiesto exista
    // en la carpeta destino).
    const bool isNewLocation = path != projectFilePath_;

    for (const uint32_t id : order_) {
        Entry& entry = documents_.at(id);
        if (entry.absolutePath.isEmpty() || isNewLocation) {
            entry.absolutePath = projectDir.filePath(entry.name + ".dfc");
        }
        formats::saveProjectToFile(*entry.document, entry.absolutePath.toStdString());
        entry.undoStack->setClean();
    }

    if (manifestDirty_ || isNewLocation) {
        formats::ProjectManifest manifest;
        manifest.projectName = projectName_.isEmpty() ? QFileInfo(path).completeBaseName() : projectName_;
        for (const uint32_t id : order_) {
            const Entry& entry = documents_.at(id);
            formats::ProjectManifestEntry manifestEntry;
            manifestEntry.name = entry.name;
            manifestEntry.relativePath = projectDir.relativeFilePath(entry.absolutePath);
            manifest.documents.push_back(std::move(manifestEntry));
        }

        const auto activeIt = std::find(order_.begin(), order_.end(), activeId_);
        manifest.activeDocumentIndex =
            activeIt != order_.end() ? static_cast<int>(std::distance(order_.begin(), activeIt)) : 0;

        const nlohmann::json json = formats::serializeProjectManifest(manifest);
        std::ofstream out(path.toStdString(), std::ios::trunc);
        if (!out.is_open()) {
            throw std::runtime_error("Project::saveToFile: no se pudo escribir '" + path.toStdString() + "'");
        }
        out << json.dump(2);
        if (!out.good()) {
            throw std::runtime_error("Project::saveToFile: fallo la escritura de '" + path.toStdString() + "'");
        }
        projectName_ = manifest.projectName;
    }

    projectFilePath_ = path;
    manifestDirty_ = false;
    emit projectMetadataChanged();
}

} // namespace digitalforge::editor
