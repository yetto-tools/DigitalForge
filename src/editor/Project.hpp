#pragma once

#include <QObject>
#include <QString>
#include <QUndoGroup>

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "components/ComponentRegistry.hpp"

class QUndoStack;

namespace digitalforge::editor {

class CircuitDocument;

// Agrupa uno o mas CircuitDocument bajo un mismo proyecto (analogo a una
// "solucion" de Visual Studio con varios documentos/circuitos), cada uno con
// su propio QUndoStack coordinado por un unico QUndoGroup, de modo que las
// acciones Deshacer/Rehacer siempre operan sobre el documento activo sin
// necesidad de reconstruirlas al cambiar de documento.
//
// Deliberadamente no sabe nada de CircuitScene/QGraphicsScene ni de ningun
// otro tipo de QtWidgets: quien lo use (hoy MainWindow) es responsable de
// mantener su propia estructura paralela de escenas graficas indexada por
// los mismos ids estables que expone esta clase (ver documentAdded/
// documentAboutToBeRemoved), para que Project se pueda seguir probando con
// solo Qt6::Core (ver tests_qt/CMakeLists.txt).
//
// Siempre tiene al menos un documento: el constructor arranca con uno vacio
// sin ruta en disco (un "proyecto anonimo" de un solo documento), que es
// tambien el resultado de abrir un .dfproj del schema viejo de un solo
// circuito (ver loadFromFile). Cada CircuitDocument sigue construyendo su
// propio ComponentRegistry internamente, exactamente igual que hoy; el
// registry_ de esta clase es una copia aparte, dedicada solo a que
// ui::ComponentPalette tenga una referencia estable que sobreviva a que se
// cierren/agreguen documentos individuales.
class Project : public QObject {
    Q_OBJECT

public:
    explicit Project(QObject* parent = nullptr);
    ~Project() override;

    [[nodiscard]] const components::ComponentRegistry& registry() const noexcept { return registry_; }

    // Descarta todos los documentos actuales (emitiendo documentAboutToBeRemoved
    // por cada uno, para que quien los escuche pueda liberar lo que tenga
    // asociado a ellos - p. ej. una CircuitScene) y vuelve a un proyecto
    // anonimo de un solo documento vacio, igual que un Project recien
    // construido.
    void newProject();

    // Crea un circuito vacio, lo agrega y lo activa. Devuelve su id estable.
    uint32_t addDocument(const QString& name);
    // Copia externalPath a la carpeta del proyecto si el proyecto ya se
    // guardo alguna vez (si no, simplemente referencia el archivo donde
    // esta); lo agrega y lo activa. Lanza std::runtime_error si
    // externalPath no existe, o cualquier excepcion que lance
    // formats::loadProjectFromFile si el contenido es invalido.
    uint32_t importDocument(const QString& externalPath);
    // Importa un archivo .circ de Logisim-evolution como un documento nuevo
    // (ver formats::importLogisimCircFile para el alcance soportado). A
    // diferencia de importDocument(), no preserva un enlace al archivo
    // original -- el documento resultante queda "nunca guardado", como uno
    // creado con addDocument(). Lanza std::runtime_error si circPath no
    // existe, o std::invalid_argument si el circuito usa componentes de
    // Logisim sin equivalente todavia en DigitalForge.
    uint32_t importLogisimDocument(const QString& circPath);
    // Guarda una copia independiente del documento `id` en destPath, sin
    // afectar la copia propia del proyecto. Lanza std::invalid_argument si
    // id es desconocido.
    void exportDocument(uint32_t id, const QString& destPath) const;
    // Lanza std::invalid_argument si id es desconocido, o si es el unico
    // documento del proyecto (un proyecto siempre tiene al menos uno).
    void removeDocument(uint32_t id);
    void renameDocument(uint32_t id, const QString& newName);

    // true si algun documento del proyecto (salvo `excludeId`, usado por
    // renameDocument() para no chocar contra si mismo) ya tiene ese nombre,
    // sin distinguir mayusculas/minusculas (los nombres se usan como
    // nombre de archivo en disco - Windows no distingue).
    [[nodiscard]] bool hasDocumentNamed(const QString& name, std::optional<uint32_t> excludeId = std::nullopt) const;
    // Devuelve `base` tal cual si ningun documento del proyecto ya lo usa;
    // si no, `base` + "2", "3", etc. hasta encontrar uno libre. Pensado
    // para sugerir un nombre por defecto en dialogos de "agregar
    // documento" que no choque de entrada con el `addDocument()`/
    // `renameDocument()` que rechazan un nombre duplicado (ver
    // ui::ProjectTree::addNewDocument()).
    [[nodiscard]] QString suggestUniqueDocumentName(const QString& base) const;

    [[nodiscard]] std::vector<uint32_t> documentIds() const; // orden de insercion/visualizacion
    [[nodiscard]] QString documentName(uint32_t id) const;
    [[nodiscard]] CircuitDocument* document(uint32_t id) const;
    [[nodiscard]] QUndoStack* undoStack(uint32_t id) const;

    void setActiveDocument(uint32_t id);
    [[nodiscard]] uint32_t activeDocumentId() const noexcept { return activeId_; }
    [[nodiscard]] CircuitDocument* activeDocument() const;
    [[nodiscard]] QUndoStack* activeUndoStack() const;

    // Las acciones Deshacer/Rehacer de la UI se crean a partir de este grupo
    // (QUndoGroup::createUndoAction/createRedoAction) en vez de un unico
    // QUndoStack, para que seguir automaticamente al documento activo sin
    // tener que reconstruirlas en cada cambio.
    [[nodiscard]] QUndoGroup& undoGroup() noexcept { return undoGroup_; }

    // true si algun documento tiene cambios sin guardar, o si la lista de
    // documentos del proyecto (agregar/quitar/renombrar) cambio desde el
    // ultimo saveToFile/loadFromFile.
    [[nodiscard]] bool hasUnsavedChanges() const;

    [[nodiscard]] const QString& filePath() const noexcept { return projectFilePath_; }
    [[nodiscard]] QString projectName() const { return projectName_; }

    // Reemplaza todo el estado actual del proyecto. Soporta tanto el schema
    // de proyecto nuevo (con clave "documents") como un .dfproj de un solo
    // circuito del schema viejo, que se envuelve como proyecto anonimo de un
    // documento (filePath() queda vacio en ese caso). Lanza
    // std::runtime_error/std::invalid_argument igual que
    // formats::loadProjectFromFile/loadProject/parseProjectManifest.
    void loadFromFile(const QString& path);
    // Escribe cada documento junto a `path`, como archivos .dfc separados -
    // siempre completo, no solo si tiene cambios pendientes (son "los
    // importantes": deben contener el circuito completo y actualizado, sin
    // depender de una senal de "sucio" que no captura ediciones hechas
    // fuera de un QUndoCommand). El manifiesto (.dfproj) en si es mas
    // barato de recalcular pero solo se reescribe si la lista de
    // documentos cambio desde el ultimo guardado
    // (agregar/quitar/renombrar/importar) o si `path` es una ubicacion
    // distinta de la actual (primer guardado o "Guardar como").
    void saveToFile(const QString& path);

signals:
    void documentAdded(uint32_t id);
    void documentAboutToBeRemoved(uint32_t id);
    void documentRenamed(uint32_t id);
    void activeDocumentChanged(uint32_t id);
    // filePath()/projectName() acaban de cambiar (justo despues de
    // saveToFile() o loadFromFile()) - a diferencia de los cuatro de
    // arriba, esto no tiene que ver con que documento cambio sino con la
    // identidad del proyecto en si (nombre, si tiene o no un .dfproj propio
    // en disco). ui::ProjectTree lo usa para refrescar la etiqueta de su
    // raiz, que de otro modo se queda mostrando "Proyecto sin guardar" para
    // siempre despues de un primer Guardar/Guardar como.
    void projectMetadataChanged();

private:
    struct Entry {
        QString name;
        QString absolutePath; // vacio si el documento nunca se guardo en disco
        std::unique_ptr<CircuitDocument> document;
        std::unique_ptr<QUndoStack> undoStack;
    };

    uint32_t addEntry(const QString& name, const QString& absolutePath);
    // Emite documentAboutToBeRemoved por cada documento actual y los borra
    // todos (order_ queda vacio). Usado por newProject() y loadFromFile()
    // antes de reemplazar el contenido del proyecto.
    void clearAllDocuments();
    // Reinstala en cada CircuitDocument una lambda que resuelve una ruta
    // relativa a su documento hermano correspondiente (ver
    // CircuitDocument::setSiblingResolver) - se llama cada vez que cambia
    // la lista de documentos (addEntry/removeDocument).
    void refreshSiblingResolvers();

    components::ComponentRegistry registry_;
    QUndoGroup undoGroup_;
    std::map<uint32_t, Entry> documents_;
    std::vector<uint32_t> order_;
    uint32_t nextId_ = 0;
    uint32_t activeId_ = 0;
    QString projectFilePath_;
    QString projectName_;
    bool manifestDirty_ = false;
};

} // namespace digitalforge::editor
