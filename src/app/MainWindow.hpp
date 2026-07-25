#pragma once

#include <QMainWindow>
#include <QMetaObject>
#include <QPoint>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <map>
#include <memory>
#include <set>

#include "AppSettings.hpp"

class QUndoStack;
class QTimer;
class QLabel;
class QCloseEvent;
class QResizeEvent;
class QEvent;
class QAction;
class QDockWidget;
class QMenu;
class QTabBar;
class QToolBar;

namespace digitalforge::editor {
class CircuitDocument;
class CircuitScene;
class CircuitView;
class Project;
} // namespace digitalforge::editor

namespace digitalforge::ui {
class AutoHideStrip;
class ComponentPalette;
class MiniMapView;
class ProjectTree;
class PropertyInspector;
class SimulationToolbar;
class TruthTablePanel;
class WaveformPanel;
class ZoomControl;
} // namespace digitalforge::ui

namespace digitalforge::app {

class UpdateChecker;

// Compartido entre MainWindow::onAbout() y main.cpp (splash de inicio) -
// unico lugar donde cambiar la version mostrada al usuario.
inline constexpr const char* kAppVersion = "0.1.2 PRE-ALPHA";

// Ventana principal: menus (Archivo/Editar/Simulacion/Ver/Bibliotecas),
// barra de herramientas, paleta de componentes a la izquierda, lienzo del
// circuito en el centro, inspector de propiedades a la derecha, barra de
// estado abajo - todo conectado a un unico editor::Project, que puede
// agrupar varios CircuitDocument (uno activo a la vez). Cada documento tiene
// su propia CircuitScene (ver scenes_); vista/inspector/barra de simulacion
// se re-vinculan al documento/escena activos cada vez que cambian (ver
// onActiveDocumentChanged).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Abre un .dfproj o .dfc indicado al arrancar: doble clic sobre el archivo
    // en el Explorador (ver las asociaciones que registra el instalador) o
    // ruta pasada por linea de comandos. Devuelve false si no se pudo leer.
    bool openFileAtStartup(const QString& path);

private slots:
    void onSceneSelectionChanged();
    void onNewProject();
    void onOpen();
    void onSave();
    void onSaveAs();
    void onAutosave();
    void updateStatusBar();
    // CircuitScene es puramente presentacional y no la conoce editor::Project
    // (para que este se pueda seguir probando con solo Qt6::Core) - estos
    // tres slots son los que mantienen scenes_ sincronizado con los
    // documentos reales del proyecto.
    void onDocumentAdded(uint32_t id);
    void onDocumentAboutToBeRemoved(uint32_t id);
    void onActiveDocumentChanged(uint32_t id);
    void onDocumentRenamed(uint32_t id);
    void onAbout();
    // Chequeo manual de actualizaciones (menu Ayuda): a diferencia del
    // automatico al arrancar, este si avisa cuando ya estas al dia o cuando
    // falla la comprobacion.
    void onCheckForUpdatesManually();
    // Aviso (comun al chequeo manual y al automatico) de que hay una version
    // mas nueva publicada, con opcion de abrir la pagina de descargas.
    void onUpdateAvailable(const QString& latestVersion, const QString& downloadUrl);
    // Pestanas de documento (estilo Visual Studio) arriba del lienzo - ver
    // documentTabBar_. Cerrar una pestana no quita el documento del
    // proyecto (eso lo sigue haciendo unicamente ui::ProjectTree): solo dejo
    // de mostrarlo en una pestana, igual que cerrar un archivo abierto en VS
    // sin sacarlo de la solucion.
    void onDocumentTabChanged(int index);
    void onDocumentTabCloseRequested(int index);
    // Via explicita para ver/editar propiedades durante la simulacion, ya
    // que onSceneSelectionChanged() deja de actualizar el inspector
    // automaticamente en ese estado (ver el comentario ahi).
    void onComponentContextMenuRequested(uint32_t componentId, QPoint screenPos);
    // Abre ui::PreferencesDialog (menu Archivo). Su boton "Restablecer
    // valores predeterminados" re-pinea de inmediato cualquier panel
    // auto-oculto (ver panelsResetRequested en PreferencesDialog.hpp); si el
    // usuario acepta el dialogo, settings_ se actualiza y se persiste.
    void onPreferences();

protected:
    void closeEvent(QCloseEvent* event) override;
    // Redimensionar con un flyout de auto-hide desplegado (ver showFlyout())
    // dejaria su geometria vieja - mas simple colapsarlo que recalcularla.
    void resizeEvent(QResizeEvent* event) override;
    // Detecta clicks fuera del flyout de auto-hide actualmente desplegado
    // para colapsarlo (ver showFlyout()/collapseFlyout()) - instalado sobre
    // qApp solo mientras hay un flyout abierto.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setupCentralWidgets();
    void setupDocks();
    // Tamano de fabrica, centrado en la pantalla disponible. Se usa cuando no
    // hay geometria guardada (primera ejecucion) o la guardada es ilegible.
    void applyDefaultWindowGeometry();
    // Aplica la disposicion de paneles/barras de la sesion anterior, o la de
    // fabrica (ver DefaultLayout.hpp) si no hay ninguna. Debe llamarse una vez
    // creados TODOS los docks y toolbars, y siempre antes de auto-ocultar
    // paneles: setDockAutoHidden() los saca del layout y restoreState() no
    // puede colocar un dock que ya no esta en la ventana.
    void restoreWindowLayout();
    // Devuelve la ventana a la disposicion de fabrica, descartando la actual.
    void resetWindowLayout();
    // Huella de la estructura actual de docks/toolbars (sus objectName(),
    // ordenados) - ver restoreWindowLayout() y closeEvent(). Un
    // "MainWindow/state" guardado por una version anterior de la app (con
    // otro conjunto de paneles/barras) puede diferir de la huella grabada
    // junto a el; restoreState() puede devolver true igual (no valida tan
    // estricto) pero deja el QDockAreaLayout interno de Qt corrupto de forma
    // que crashea en cuanto se saca un dock del layout (auto-hide) mas
    // adelante - reproducido de forma 100% consistente con un blob viejo,
    // y confirmado ausente al generar un blob nuevo con este mismo build.
    [[nodiscard]] QStringList currentDockLayoutFingerprint() const;
    // Arma las dos franjas de auto-hide (izquierda/derecha) que alojan las
    // pestanas verticales de los paneles despineados - ver setupDocks() y
    // makeAutoHideable().
    void setupAutoHideStrips();
    // Registra `dock` como despineable: `area` es su area de origen (donde
    // vive cuando esta pineado) y `tabifySibling` (si no es null) el vecino
    // con el que estaba tabificado, para poder re-tabificarlo con el al
    // re-pinear (ver setDockAutoHidden()). Conecta su DockTitleBar::
    // pinnedChanged a setDockAutoHidden().
    void makeAutoHideable(QDockWidget* dock, Qt::DockWidgetArea area, QDockWidget* tabifySibling = nullptr);
    // Saca `dock` del layout de docks y lo reemplaza por una pestana en su
    // franja de auto-hide (autoHidden = true), o lo devuelve a su posicion
    // original (autoHidden = false).
    void setDockAutoHidden(QDockWidget* dock, bool autoHidden);
    // Despliega `dock` (ya auto-oculto) por encima del lienzo, anclado al
    // borde de su franja. Colapsa cualquier flyout previo primero - solo uno
    // puede estar abierto a la vez.
    void showFlyout(QDockWidget* dock);
    // Oculta el flyout actualmente desplegado (si hay uno) y deja de
    // escuchar clicks afuera.
    void collapseFlyout();
    // Punto unico para "traer al frente" un panel desde codigo (usado por
    // onComponentContextMenuRequested): si esta auto-oculto lo despliega
    // como flyout, si no hace el show()/raise() de siempre.
    void revealDock(QDockWidget* dock);
    [[nodiscard]] bool isAutoHidden(QDockWidget* dock) const;
    void setupMenusAndToolbars();
    void setupAutosave();
    bool saveToPath(const QString& path);
    bool loadFromPath(const QString& path);
    // Reconstruye las acciones de recentFilesMenu_ a partir de lo persistido
    // en QSettings ("RecentFiles/paths") - poda de paso cualquier ruta que
    // ya no exista en disco (self-healing: no hace falta una accion
    // separada de "limpiar" para que la lista no se llene de entradas
    // muertas). Se llama una vez al construir la ventana y cada vez que
    // addToRecentFiles() cambia la lista.
    void rebuildRecentFilesMenu();
    // Mueve `path` al frente de la lista de recientes (o lo agrega si no
    // estaba), recorta a kMaxRecentFiles y persiste + reconstruye el menu.
    // Llamado despues de abrir/guardar con exito - nunca para el
    // autoguardado interno (autosavePath_), que no es un archivo que el
    // usuario haya elegido abrir.
    void addToRecentFiles(const QString& path);
    // Le pide al usuario nombre+ubicacion con el mismo dialogo de "Guardar
    // como" de siempre, pero interpreta el nombre elegido como nombre de
    // carpeta (estilo Visual Studio: una carpeta por proyecto, con el
    // .dfproj adentro) en vez de un archivo suelto - crea esa carpeta y
    // devuelve la ruta final del .dfproj dentro de ella. Usado tanto por
    // onNewProject() como por onSaveAs(). Devuelve QString() si el usuario
    // cancelo o hubo un error (ya avisado con QMessageBox::warning).
    QString promptForNewProjectPath();
    void updateWindowTitle();
    void refreshWindowModified();
    // Si el proyecto tiene cambios sin guardar, pregunta Guardar/Descartar/
    // Cancelar. Devuelve true si esta bien continuar con la operacion que lo
    // llamo (cerrar la ventana, abrir otro proyecto, empezar uno nuevo);
    // false si el usuario cancelo o el guardado fallo/se cancelo.
    // `showProgressWhileSaving` envuelve el guardado real (si el usuario
    // elige "Guardar" en el dialogo de confirmacion) en un QProgressDialog
    // indeterminado - pensado para closeEvent(), donde un disco lento o un
    // proyecto con muchos documentos podia sentirse como que la app se
    // colgo al cerrar, sin ningun indicio visual de que seguia guardando.
    // El resto de los llamadores (onOpen()/onNewProject()/etc.) no lo
    // necesitan: ahi "cerrar el documento actual" no es la operacion
    // visible, así que se deja en false por defecto.
    bool maybeSaveChanges(bool showProgressWhileSaving = false);
    // Reconecta simulationRebuilt/simulationStepped/liveSimulationChanged/
    // editBlocked (los que alimentan la barra de estado) al documento activo
    // actual, desconectandolos del anterior.
    void bindActiveDocumentSignals();
    // Resincroniza gridAction_/snapAction_ (estado marcado + a que
    // CircuitScene apunta su senal toggled) con la escena activa actual.
    void bindActiveSceneViewActions();
    // Agrega una pestana nueva para `id` (si todavia no tiene una) y la
    // selecciona - usado por onDocumentAdded() y por
    // onActiveDocumentChanged() cuando el documento recien activado no
    // tenia pestana (p. ej. se lo selecciono desde ui::ProjectTree estando
    // "cerrado").
    void addDocumentTab(uint32_t id);
    // -1 si `id` no tiene ninguna pestana abierta actualmente.
    [[nodiscard]] int tabIndexForDocument(uint32_t id) const;

    [[nodiscard]] editor::CircuitScene* activeScene() const;

    std::unique_ptr<editor::Project> project_;
    std::map<uint32_t, editor::CircuitScene*> scenes_; // Qt-parented (this); ver onDocumentAdded/onDocumentAboutToBeRemoved
    // QObject::disconnect(QMetaObject::Connection) es seguro incluso si el
    // documento del que colgaban ya se destruyo (a diferencia de
    // disconnect(sender, ...) con un puntero colgante, que es un
    // use-after-free si ese documento ya no existe - el crash/freeze
    // reportado al recargar/reabrir un proyecto).
    QMetaObject::Connection simulationRebuiltConnection_;
    QMetaObject::Connection simulationSteppedConnection_;
    QMetaObject::Connection liveSimulationChangedConnection_;
    QMetaObject::Connection editBlockedConnection_;

    editor::CircuitView* view_ = nullptr;
    // Pestanas estilo Visual Studio arriba de view_ (ver
    // setupCentralWidgets()). El id de documento de cada pestana se guarda
    // via QTabBar::setTabData(); no todo documento del proyecto tiene
    // necesariamente una pestana (cerrarla no lo saca del proyecto).
    QTabBar* documentTabBar_ = nullptr;
    ui::ComponentPalette* palette_ = nullptr;
    // Guardado para poder ofrecer "Nuevo documento" desde el menu Archivo
    // (ver setupMenusAndToolbars()), ademas del menu contextual propio de
    // este arbol.
    ui::ProjectTree* projectTree_ = nullptr;
    // Miniatura del lienzo estilo Proteus (ver setupDocks()) - se
    // reapunta a la escena/vista activas en onActiveDocumentChanged(),
    // mismo patron que inspector_/truthTablePanel_/waveformPanel_.
    ui::MiniMapView* miniMap_ = nullptr;
    ui::PropertyInspector* inspector_ = nullptr;
    ui::SimulationToolbar* simulationToolbar_ = nullptr;
    ui::TruthTablePanel* truthTablePanel_ = nullptr;
    ui::WaveformPanel* waveformPanel_ = nullptr;
    // Guardados para poder traer al frente la pestana "Propiedades" desde
    // onComponentContextMenuRequested(), y para poder ocultarlos/mostrarlos
    // en conjunto desde el boton "Panel derecho" (ver setupDocks()/
    // setupMenusAndToolbars()) - los tres comparten una misma pestana.
    QDockWidget* inspectorDock_ = nullptr;
    QDockWidget* truthTableDock_ = nullptr;
    QDockWidget* waveformDock_ = nullptr;

    // Auto-hide estilo Visual Studio (ver setupAutoHideStrips()/
    // makeAutoHideable()/setDockAutoHidden()). leftAutoHideStrip_/
    // rightAutoHideStrip_ son QToolBar verticales (no QDockWidget) que
    // alojan a leftStripWidget_/rightStripWidget_ (las franjas de pestanas
    // verticales en si) - un QToolBar vive en su propia banda de
    // QMainWindowLayout, separada del splitter de los QDockWidget reales,
    // asi que su ancho no se ve afectado por cuanto midan (o dejen de
    // medir) projectTreeDock/inspectorDock_/etc. Version anterior los
    // hacia QDockWidget compartiendo splitDockWidget() con esos paneles, y
    // heredaban anchos incorrectos (mitad del ancho del panel vecino, o un
    // hueco vacio que no se colapsaba al sacar ese vecino) cada vez que un
    // panel real se auto-ocultaba o volvia.
    QToolBar* leftAutoHideStrip_ = nullptr;
    QToolBar* rightAutoHideStrip_ = nullptr;
    ui::AutoHideStrip* leftStripWidget_ = nullptr;
    ui::AutoHideStrip* rightStripWidget_ = nullptr;
    // El panel actualmente desplegado como flyout (ver showFlyout()), o
    // nullptr si no hay ninguno. Solo uno puede estar abierto a la vez.
    QDockWidget* flyoutDock_ = nullptr;
    // Contenedor liviano (QWidget comun, sin ningun vinculo con
    // QMainWindowLayout) que aloja temporalmente el titleBarWidget()/
    // widget() de flyoutDock_ mientras esta desplegado - ver el comentario
    // en showFlyout() sobre por que no se reutiliza el QDockWidget mismo
    // como overlay.
    QWidget* flyoutHost_ = nullptr;

    struct AutoHideOrigin {
        Qt::DockWidgetArea area;
        QDockWidget* tabifySibling; // nullptr si no estaba tabificado con nadie
    };
    // Area/vecino "de origen" (donde vive pineado) de cada panel
    // despineable - se completa una unica vez en makeAutoHideable().
    std::map<QDockWidget*, AutoHideOrigin> autoHideOrigin_;
    // Cuales de los paneles registrados estan actualmente colapsados a una
    // pestana en su franja (en vez de ocupar su lugar normal en el layout).
    std::set<QDockWidget*> autoHidden_;
    // objectName() -> QDockWidget*, poblado en makeAutoHideable() - permite
    // guardar/restaurar que paneles estaban auto-ocultos entre sesiones por
    // nombre estable (ver closeEvent()/setupDocks()), ya que los punteros en
    // si no sirven de una ejecucion a la siguiente.
    std::map<QString, QDockWidget*> autoHideDocksByName_;

    // Preferencias persistidas (ver AppSettings.hpp) - cargadas una vez al
    // construir la ventana y reescritas en closeEvent()/onPreferences().
    AppSettings settings_;

    UpdateChecker* updateChecker_ = nullptr;

    QLabel* simulationStateLabel_ = nullptr;
    ui::ZoomControl* zoomControl_ = nullptr;
    QAction* gridAction_ = nullptr;
    QAction* snapAction_ = nullptr;

    // Submenu "Abrir recientes" (ver rebuildRecentFilesMenu()) - creado una
    // vez en setupMenusAndToolbars(), sus acciones se reconstruyen enteras
    // cada vez que cambia la lista en vez de diffearlas (a lo sumo 20
    // entradas, no vale la pena la complejidad extra).
    QMenu* recentFilesMenu_ = nullptr;

    QString autosavePath_;
    QTimer* autosaveTimer_ = nullptr;
    // QUndoStack::clear() siempre deja el stack "limpio" (ver
    // setupAutosave()), asi que restaurar un autoguardado de una sesion
    // anterior no alcanza para que maybeSaveChanges() lo detecte como
    // cambios sin guardar; esta bandera cubre exactamente ese caso, ya que
    // ese contenido recuperado no corresponde a ningun archivo en disco
    // hasta que el usuario lo guarde explicitamente.
    bool recoveredUnsavedContent_ = false;
};

} // namespace digitalforge::app
