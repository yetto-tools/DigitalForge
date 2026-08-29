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
class KarnaughDocument;
class TruthTableDocument;
class ExcitationTableDocument;

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
    // Que tipo de documento es un id dado - ver documentKind(). Un
    // KarnaughDocument vive en una coleccion totalmente aparte de los
    // CircuitDocument (ver el comentario junto a karnaughDocuments_ mas
    // abajo); esto es la unica pieza de esta clase que necesita saber
    // "de que tipo" es un id sin adivinar por en cual mapa aparece.
    enum class DocumentKind : uint8_t { Circuit, Karnaugh, TruthTable, ExcitationTable };

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

    // Mapas de Karnaugh: una coleccion separada de los CircuitDocument de
    // arriba (ver el comentario junto a karnaughDocuments_), pero que
    // comparte el mismo espacio de ids (nextId_) para que un tabId/treeId
    // nunca sea ambiguo entre los dos tipos - ver documentKind(). A
    // diferencia de removeDocument(), no hace falta que quede al menos uno
    // (cero mapas de Karnaugh es un estado valido de proyecto). Ninguno de
    // los dos participa en activeDocumentId()/activeDocument()/
    // undoGroup(): "cual pestana esta al frente" es un estado de la UI
    // (MainWindow), no de este modelo - ver el comentario de clase de
    // arriba sobre por que Project no sabe nada de QtWidgets.
    uint32_t addKarnaughDocument(const QString& name, int variableCount = 4);
    void removeKarnaughDocument(uint32_t id);
    void renameKarnaughDocument(uint32_t id, const QString& newName);
    [[nodiscard]] KarnaughDocument* karnaughDocument(uint32_t id) const;
    [[nodiscard]] QString karnaughDocumentName(uint32_t id) const;
    [[nodiscard]] std::vector<uint32_t> karnaughDocumentIds() const; // orden de insercion/visualizacion

    // Tablas de verdad multi-salida: mismo patron que los mapas de Karnaugh
    // de arriba (coleccion totalmente aparte, comparte nextId_, cero es un
    // estado valido de proyecto) -- ver el comentario de
    // truthTableDocuments_ mas abajo.
    uint32_t addTruthTableDocument(const QString& name, int variableCount = 4);
    void removeTruthTableDocument(uint32_t id);
    void renameTruthTableDocument(uint32_t id, const QString& newName);
    [[nodiscard]] TruthTableDocument* truthTableDocument(uint32_t id) const;
    [[nodiscard]] QString truthTableDocumentName(uint32_t id) const;
    [[nodiscard]] std::vector<uint32_t> truthTableDocumentIds() const; // orden de insercion/visualizacion

    // Tablas de excitacion de flip-flops: mismo patron que los mapas de
    // Karnaugh/tablas de verdad de arriba -- ver el comentario de
    // excitationTableDocuments_ mas abajo.
    uint32_t addExcitationTableDocument(const QString& name, int stateBitCount = 2);
    void removeExcitationTableDocument(uint32_t id);
    void renameExcitationTableDocument(uint32_t id, const QString& newName);
    [[nodiscard]] ExcitationTableDocument* excitationTableDocument(uint32_t id) const;
    [[nodiscard]] QString excitationTableDocumentName(uint32_t id) const;
    [[nodiscard]] std::vector<uint32_t> excitationTableDocumentIds() const; // orden de insercion/visualizacion

    // Lanza std::invalid_argument si `id` no aparece en ninguna de las
    // cuatro colecciones (documents_, karnaughDocuments_,
    // truthTableDocuments_ ni excitationTableDocuments_).
    [[nodiscard]] DocumentKind documentKind(uint32_t id) const;

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
    // Mirror de los tres de arriba, para karnaughDocuments_ - no hay un
    // "karnaughActiveDocumentChanged" analogo, ver el comentario junto a
    // addKarnaughDocument() sobre por que este modelo no tiene nocion de
    // "cual mapa de Karnaugh esta activo".
    void karnaughDocumentAdded(uint32_t id);
    void karnaughDocumentAboutToBeRemoved(uint32_t id);
    void karnaughDocumentRenamed(uint32_t id);
    // Mirror de los tres de arriba, para truthTableDocuments_.
    void truthTableDocumentAdded(uint32_t id);
    void truthTableDocumentAboutToBeRemoved(uint32_t id);
    void truthTableDocumentRenamed(uint32_t id);
    // Mirror de los tres de arriba, para excitationTableDocuments_.
    void excitationTableDocumentAdded(uint32_t id);
    void excitationTableDocumentAboutToBeRemoved(uint32_t id);
    void excitationTableDocumentRenamed(uint32_t id);
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

    // Coleccion TOTALMENTE APARTE de documents_/Entry (no una jerarquia
    // polimorfica comun): un KarnaughDocument no tiene simulacion, ni
    // ComponentRegistry, ni QUndoStack, asi que forzarlo a compartir Entry
    // no ahorraria nada y obligaria a todo el resto de esta clase (y a los
    // ~22 lugares fuera de ella que llaman document(id)) a lidiar con un
    // puntero que podria ser de cualquiera de los dos tipos. Comparte
    // nextId_ con documents_ (nunca su propio contador) para que un mismo
    // id jamas pueda referirse a un CircuitDocument y a un KarnaughDocument
    // a la vez.
    struct KarnaughEntry {
        QString name;
        QString absolutePath; // vacio si el mapa nunca se guardo en disco
        std::unique_ptr<KarnaughDocument> document;
    };
    uint32_t addKarnaughEntry(const QString& name, const QString& absolutePath, int variableCount);
    // Emite karnaughDocumentAboutToBeRemoved por cada mapa actual y los
    // borra todos (karnaughOrder_ queda vacio). Mirror de
    // clearAllDocuments(), usado en los mismos dos lugares.
    void clearAllKarnaughDocuments();

    // Coleccion TOTALMENTE APARTE de documents_/karnaughDocuments_, mismo
    // criterio que estos ultimos (ver su comentario): una tabla de verdad no
    // tiene ComponentRegistry, simulacion ni QUndoStack propio. Comparte
    // nextId_ con las otras dos.
    struct TruthTableEntry {
        QString name;
        QString absolutePath; // vacio si la tabla nunca se guardo en disco
        std::unique_ptr<TruthTableDocument> document;
    };
    uint32_t addTruthTableEntry(const QString& name, const QString& absolutePath, int variableCount);
    // Emite truthTableDocumentAboutToBeRemoved por cada tabla actual y las
    // borra todas (truthTableOrder_ queda vacio). Mirror de
    // clearAllKarnaughDocuments().
    void clearAllTruthTableDocuments();

    // Coleccion TOTALMENTE APARTE de las tres de arriba, mismo criterio
    // (ver el comentario de KarnaughEntry): una tabla de excitacion no tiene
    // ComponentRegistry, simulacion ni QUndoStack propio. Comparte nextId_
    // con las otras tres.
    struct ExcitationTableEntry {
        QString name;
        QString absolutePath; // vacio si la tabla nunca se guardo en disco
        std::unique_ptr<ExcitationTableDocument> document;
    };
    uint32_t addExcitationTableEntry(const QString& name, const QString& absolutePath, int stateBitCount);
    // Emite excitationTableDocumentAboutToBeRemoved por cada tabla actual y
    // las borra todas (excitationTableOrder_ queda vacio). Mirror de
    // clearAllTruthTableDocuments().
    void clearAllExcitationTableDocuments();

    components::ComponentRegistry registry_;
    QUndoGroup undoGroup_;
    std::map<uint32_t, Entry> documents_;
    std::vector<uint32_t> order_;
    std::map<uint32_t, KarnaughEntry> karnaughDocuments_;
    std::vector<uint32_t> karnaughOrder_;
    std::map<uint32_t, TruthTableEntry> truthTableDocuments_;
    std::vector<uint32_t> truthTableOrder_;
    std::map<uint32_t, ExcitationTableEntry> excitationTableDocuments_;
    std::vector<uint32_t> excitationTableOrder_;
    uint32_t nextId_ = 0;
    uint32_t activeId_ = 0;
    QString projectFilePath_;
    QString projectName_;
    bool manifestDirty_ = false;
};

} // namespace digitalforge::editor
