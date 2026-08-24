#include "MainWindow.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QSettings>
#include <QStringList>
#include <QTabBar>
#include <QVBoxLayout>
#include <QFrame>
#include <QLabel>
#include <QDesktopServices>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleHints>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUndoGroup>
#include <QScreen>
#include <QUndoStack>
#include <algorithm>
#include <stdexcept>

#include "DefaultLayout.hpp"
#include "UpdateChecker.hpp"
#include "editor/CircuitDocument.hpp"
#include "editor/CircuitScene.hpp"
#include "editor/CircuitView.hpp"
#include "editor/ComponentItem.hpp"
#include "editor/KarnaughDocument.hpp"
#include "editor/Project.hpp"
#include "editor/TruthTableDocument.hpp"
#include "formats/ProjectSerializer.hpp"
#include "ui/AutoHideStrip.hpp"
#include "ui/ComponentPalette.hpp"
#include "ui/IconFactory.hpp"
#include "ui/KarnaughMapView.hpp"
#include "ui/MiniMapView.hpp"
#include "ui/PreferencesDialog.hpp"
#include "ui/ProjectTree.hpp"
#include "ui/PropertyInspector.hpp"
#include "ui/SimulationToolbar.hpp"
#include "ui/Theme.hpp"
#include "ui/TruthTablePanel.hpp"
#include "ui/TruthTableView.hpp"
#include "ui/WaveformPanel.hpp"
#include "ui/ZoomControl.hpp"

namespace digitalforge::app {

using editor::CircuitDocument;
using editor::CircuitScene;
using editor::CircuitView;
using editor::ComponentItem;
namespace icons = ui::icons;

namespace {

// Reemplaza la barra de titulo nativa de un QDockWidget: en el estilo de
// Windows los botones nativos de flotar/cerrar no siempre se distinguen
// sobre un tema oscuro (el problema original reportado con los "controles
// internos de ventana"), asi que se dibujan aqui con la misma tinta
// adaptable al tema que el resto del chrome (ver icons::dockFloat/dockClose
// y IconFactory::inkColor). El area del QLabel del titulo no intercepta el
// evento de mouse, asi que arrastrar desde ahi sigue re-anclando el panel
// exactamente como lo haria la barra de titulo nativa.
//
// El boton de pin (izquierda de flotar/cerrar) es lo que habilita el
// auto-hide estilo Visual Studio: MainWindow escucha pinnedChanged() para
// sacar/devolver el panel del layout de docks (ver
// MainWindow::setDockAutoHidden). Necesita Q_OBJECT (y por lo tanto
// #include "MainWindow.moc" al final de este archivo) porque es la unica
// clase de aca que declara una senal propia - mismo patron que
// FocusOutPlainTextEdit en src/ui/PropertyInspector.cpp.
class DockTitleBar : public QWidget {
    Q_OBJECT

public:
    explicit DockTitleBar(QDockWidget* dock) : QWidget(dock), dock_(dock) {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(6, 2, 2, 2);
        layout->setSpacing(2);

        auto* label = new QLabel(dock->windowTitle(), this);
        layout->addWidget(label);
        layout->addStretch(1);

        pinButton_ = new QToolButton(this);
        pinButton_->setAutoRaise(true);
        pinButton_->setCheckable(true);
        pinButton_->setChecked(true);
        pinButton_->setCursor(Qt::PointingHandCursor);
        pinButton_->setToolTip(tr("Anclar/auto-ocultar panel"));
        connect(pinButton_, &QToolButton::toggled, this, [this](bool pinned) {
            pinButton_->setIcon(icons::dockPin(pinned));
            emit pinnedChanged(pinned);
        });
        layout->addWidget(pinButton_);

        floatButton_ = new QToolButton(this);
        floatButton_->setAutoRaise(true);
        floatButton_->setToolTip(tr("Flotar/anclar panel"));
        connect(floatButton_, &QToolButton::clicked, dock_, [this] { dock_->setFloating(!dock_->isFloating()); });
        layout->addWidget(floatButton_);

        closeButton_ = new QToolButton(this);
        closeButton_->setAutoRaise(true);
        closeButton_->setToolTip(tr("Ocultar panel"));
        connect(closeButton_, &QToolButton::clicked, dock_, &QDockWidget::close);
        layout->addWidget(closeButton_);

        refreshIcons();
        connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
                [this](Qt::ColorScheme) { refreshIcons(); });
        // Forzar un tema cambia la paleta a mano, lo que NO emite
        // colorSchemeChanged - ver ui::ThemeManager.
        connect(&ui::ThemeManager::instance(), &ui::ThemeManager::changed, this, [this] { refreshIcons(); });

        // El QLabel no acepta el evento de mouse por si mismo, pero al ser
        // el widget mas al frente bajo el cursor igual se lo queda (Qt
        // entrega el evento al hijo mas profundo, sin "burbujear" al padre
        // solo porque el hijo no hizo nada con el) - eso dejaba sin efecto
        // un press/drag que arrancara justo sobre el texto del titulo, sin
        // afectar el resto de la franja (el espacio vacio entre el texto y
        // los botones), que si llega a mousePressEvent() de mas abajo. Con
        // esto, un click sobre el texto pasa de largo y cae en `this` igual
        // que si hubiera sido sobre el espacio vacio.
        label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

signals:
    void pinnedChanged(bool pinned);

public:
    // Sincroniza el boton de pin con un cambio de estado que no vino de un
    // click del usuario (p. ej. restaurar auto-hide guardado de una sesion
    // anterior, ver MainWindow::setDockAutoHidden()) sin re-emitir
    // pinnedChanged (evita un ciclo infinito con quien la llama).
    void setPinned(bool pinned) {
        const QSignalBlocker blocker(pinButton_);
        pinButton_->setChecked(pinned);
        pinButton_->setIcon(icons::dockPin(pinned));
    }

protected:
    // setTitleBarWidget() reemplaza la barra de titulo NATIVA de QDockWidget
    // por este QWidget comun - a partir de ahi, un click/arrastre sobre esta
    // franja llega primero a `this` (o a sus hijos), nunca al
    // mousePressEvent()/mouseMoveEvent() PRIVADOS de QDockWidget que
    // implementan "arrastrar para reacomodar" (redockear en otro borde,
    // tabificar con otro panel, soltar afuera para flotar) - ese mecanismo
    // interno simplemente nunca se entera de que el mouse se apreto (el
    // defecto reportado: arrastrar el titulo no hacia nada, a diferencia de
    // Visual Studio). Se resuelve reenviando el mismo evento, tal cual,
    // directamente a dock_ (traducido a sus propias coordenadas): eso
    // dispara el mismo camino interno que si el click hubiera caido sobre
    // la barra nativa. QDockWidget hace un grabMouse() propio al arrancar
    // el arrastre, asi que alcanza con reenviar el press -- se reenvia
    // tambien el move/release por las dudas (un widget que ya tiene el
    // mouse grab ignora sin problema los eventos de mas).
    void mousePressEvent(QMouseEvent* event) override {
        relayToDock(QEvent::MouseButtonPress, event);
        QWidget::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        relayToDock(QEvent::MouseMove, event);
        QWidget::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        relayToDock(QEvent::MouseButtonRelease, event);
        QWidget::mouseReleaseEvent(event);
    }

private:
    void relayToDock(QEvent::Type type, QMouseEvent* event) {
        QMouseEvent forwarded(type, QPointF(dock_->mapFromGlobal(event->globalPosition().toPoint())),
                               event->globalPosition(), event->button(), event->buttons(), event->modifiers());
        QApplication::sendEvent(dock_, &forwarded);
    }

    void refreshIcons() {
        pinButton_->setIcon(icons::dockPin(pinButton_->isChecked()));
        floatButton_->setIcon(icons::dockFloat());
        closeButton_->setIcon(icons::dockClose());
    }

    QDockWidget* dock_;
    QToolButton* pinButton_;
    QToolButton* floatButton_;
    QToolButton* closeButton_;
};

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), project_(std::make_unique<editor::Project>(this)) {
    // Antes del loop de onDocumentAdded() de abajo: el documento anonimo
    // inicial tambien debe recibir el grid/snap por defecto configurados.
    settings_ = AppSettings::load();

    connect(project_.get(), &editor::Project::documentAdded, this, &MainWindow::onDocumentAdded);
    connect(project_.get(), &editor::Project::documentAboutToBeRemoved, this, &MainWindow::onDocumentAboutToBeRemoved);
    connect(project_.get(), &editor::Project::activeDocumentChanged, this, &MainWindow::onActiveDocumentChanged);
    connect(project_.get(), &editor::Project::documentRenamed, this, &MainWindow::onDocumentRenamed);
    // Mirror de las cuatro de arriba, para karnaughViews_ - a diferencia
    // del documento anonimo inicial (un CircuitDocument, sincronizado a
    // mano dos lineas mas abajo porque Project ya lo creo en su propio
    // constructor antes de que estas conexiones existieran), un proyecto
    // recien construido nunca tiene ningun mapa de Karnaugh todavia, asi
    // que no hace falta un sincronizado equivalente aca.
    connect(project_.get(), &editor::Project::karnaughDocumentAdded, this, &MainWindow::onKarnaughDocumentAdded);
    connect(project_.get(), &editor::Project::karnaughDocumentAboutToBeRemoved, this,
            &MainWindow::onKarnaughDocumentAboutToBeRemoved);
    connect(project_.get(), &editor::Project::karnaughDocumentRenamed, this, &MainWindow::onKarnaughDocumentRenamed);
    // Mirror de las tres de arriba, para truthTableViews_.
    connect(project_.get(), &editor::Project::truthTableDocumentAdded, this, &MainWindow::onTruthTableDocumentAdded);
    connect(project_.get(), &editor::Project::truthTableDocumentAboutToBeRemoved, this,
            &MainWindow::onTruthTableDocumentAboutToBeRemoved);
    connect(project_.get(), &editor::Project::truthTableDocumentRenamed, this,
            &MainWindow::onTruthTableDocumentRenamed);
    // El documento anonimo inicial se creo dentro del constructor de Project,
    // antes de que las conexiones de arriba existieran - se sincroniza a mano
    // aca para que scenes_ tenga una entrada para el desde el principio.
    for (const uint32_t id : project_->documentIds()) {
        onDocumentAdded(id);
    }

    // Recordar tamano/posicion de ventana entre sesiones (ver closeEvent());
    // si todavia no hay nada guardado (primera ejecucion) o restoreGeometry()
    // rechaza los datos, se usa el tamano de fabrica centrado en la pantalla.
    const QByteArray savedGeometry = QSettings().value("MainWindow/geometry").toByteArray();
    if (savedGeometry.isEmpty() || !restoreGeometry(savedGeometry)) {
        applyDefaultWindowGeometry();
    }

    setupCentralWidgets();
    setupDocks();
    setupMenusAndToolbars();

    // Despues de que docks Y barras existen: sin ellas creadas, restoreState()
    // no tiene a quien aplicarle el estado guardado.
    restoreWindowLayout();

    // Un widget permanente sobrevive a las llamadas transitorias a
    // showMessage() usadas en otras partes para confirmar guardado/apertura
    // (esas reemplazan el texto temporal de la barra de estado, no ningun
    // widget permanente), asi que el estado de ejecucion/edicion permanece
    // visible en lugar de desaparecer cuando expira un mensaje de confirmacion.
    // Tiene que existir *antes* de setupAutosave(): si el usuario acepta
    // recuperar un autoguardado, eso reemplaza el documento activo y dispara
    // onActiveDocumentChanged() -> updateStatusBar(), que escribe en este
    // label - crasheaba con un puntero nulo cuando el label todavia no
    // existia (el crash al aceptar la recuperacion sin llegar a mostrar
    // ninguna ventana).
    simulationStateLabel_ = new QLabel(this);
    simulationStateLabel_->setStyleSheet("font-weight: bold; padding: 0 6px;");
    // Por defecto QStatusBar enmarca cada widget permanente con un relieve
    // hundido, que con varios widgets seguidos deja la barra llena de cajas.
    statusBar()->setStyleSheet("QStatusBar::item { border: none; }");
    statusBar()->addPermanentWidget(simulationStateLabel_);

    // Separador fino entre el estado de simulacion y el zoom: son dos bloques
    // de informacion distintos, igual que las secciones de la barra de Excel.
    auto* statusSeparator = new QFrame(this);
    statusSeparator->setFrameShape(QFrame::VLine);
    statusSeparator->setFrameShadow(QFrame::Sunken);
    statusSeparator->setFixedHeight(14);
    statusBar()->addPermanentWidget(statusSeparator);

    // Control de zoom al extremo derecho de la barra de estado, como en Excel.
    zoomControl_ = new ui::ZoomControl(view_, this);
    statusBar()->addPermanentWidget(zoomControl_);

    setupAutosave();

    bindActiveDocumentSignals();
    // QUndoGroup::cleanChanged sigue automaticamente al stack activo -
    // alcanza con esta unica conexion sin importar cual documento este
    // activo en cada momento.
    connect(&project_->undoGroup(), &QUndoGroup::cleanChanged, this, [this](bool) { refreshWindowModified(); });

    updateWindowTitle();
    refreshWindowModified();
    updateStatusBar();
}

MainWindow::~MainWindow() = default;

void MainWindow::setupCentralWidgets() {
    documentTabBar_ = new QTabBar(this);
    documentTabBar_->setTabsClosable(true);
    documentTabBar_->setExpanding(false);
    documentTabBar_->setDocumentMode(true);
    connect(documentTabBar_, &QTabBar::currentChanged, this, &MainWindow::onDocumentTabChanged);
    connect(documentTabBar_, &QTabBar::tabCloseRequested, this, &MainWindow::onDocumentTabCloseRequested);

    view_ = new CircuitView(activeScene(), this);

    // Pila entre pestanas de circuito (view_, pagina 0, siempre presente) y
    // las de mapa de Karnaugh (una pagina de ui::KarnaughMapView por
    // documento abierto, ver karnaughViews_) - onDocumentTabChanged()
    // decide cual mostrar segun editor::Project::documentKind().
    centralStack_ = new QStackedWidget(this);
    centralStack_->addWidget(view_);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(documentTabBar_);
    layout->addWidget(centralStack_);
    setCentralWidget(container);

    // El documento anonimo inicial (agregado via onDocumentAdded() en el
    // constructor, antes de que documentTabBar_ existiera) no tiene pestana
    // todavia - se agrega a mano aca, una unica vez.
    addDocumentTab(project_->activeDocumentId());
}

void MainWindow::setupDocks() {
    // Pestanas abajo del grupo tabificado (Propiedades/Tabla de verdad/
    // Analizador de senales), estilo Visual Studio, en vez del default de
    // Qt (arriba).
    setTabPosition(Qt::AllDockWidgetAreas, QTabWidget::South);

    setupAutoHideStrips();

    projectTree_ = new ui::ProjectTree(project_.get(), this);
    connect(projectTree_, &ui::ProjectTree::karnaughDocumentActivationRequested, this,
            &MainWindow::onKarnaughDocumentActivationRequested);
    connect(projectTree_, &ui::ProjectTree::truthTableDocumentActivationRequested, this,
            &MainWindow::onTruthTableDocumentActivationRequested);
    auto* projectTreeDock = new QDockWidget(tr("Proyecto"), this);
    projectTreeDock->setObjectName("projectTreeDock");
    projectTreeDock->setWidget(projectTree_);
    projectTreeDock->setTitleBarWidget(new DockTitleBar(projectTreeDock));
    addDockWidget(Qt::LeftDockWidgetArea, projectTreeDock);

    palette_ = new ui::ComponentPalette(project_->registry(), this);

    // Miniatura del lienzo estilo Proteus - fija arriba de la lista de
    // Componentes (no como pestana propia: compartir el mismo dock le pone
    // un techo natural a su alto, ver MiniMapView::setMaximumHeight()),
    // siempre visible mientras esa pestana este activa.
    miniMap_ = new ui::MiniMapView(this);
    auto* paletteContainer = new QWidget(this);
    auto* paletteLayout = new QVBoxLayout(paletteContainer);
    paletteLayout->setContentsMargins(0, 0, 0, 0);
    paletteLayout->setSpacing(0);
    paletteLayout->addWidget(miniMap_);
    paletteLayout->addWidget(palette_, 1);

    auto* paletteDock = new QDockWidget(tr("Componentes"), this);
    paletteDock->setObjectName("paletteDock");
    paletteDock->setWidget(paletteContainer);
    paletteDock->setTitleBarWidget(new DockTitleBar(paletteDock));
    // Tabificado (no partido en franjas separadas) con projectTreeDock -
    // mismo tratamiento que ya tienen Propiedades/Tabla de verdad/
    // Analizador del lado derecho, a pedido explicito: "pestanas como las
    // que [tienen del] lado izquierdo" refiriendose a como ya se ve la
    // franja derecha.
    tabifyDockWidget(projectTreeDock, paletteDock);
    connect(palette_, &ui::ComponentPalette::placementRequested, this,
            [this](const std::string& typeId) { activeScene()->beginPlacement(typeId); });
    // El documento activo inicial ya tiene su CircuitScene (creada en
    // onDocumentAdded(), replayada a mano en el constructor) y view_ ya
    // apunta a ella (ver setupCentralWidgets()) - pero eso paso *antes* de
    // que miniMap_ existiera, asi que no hay ningun activeDocumentChanged()
    // que lo vaya a sincronizar solo; se hace una vez aca, a mano.
    miniMap_->setTarget(activeScene(), view_);

    inspector_ = new ui::PropertyInspector(project_->activeDocument(), project_->activeUndoStack(), this);
    inspectorDock_ = new QDockWidget(tr("Propiedades"), this);
    inspectorDock_->setObjectName("inspectorDock");
    inspectorDock_->setWidget(inspector_);
    inspectorDock_->setTitleBarWidget(new DockTitleBar(inspectorDock_));
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock_);

    truthTablePanel_ = new ui::TruthTablePanel(project_.get(), project_->activeDocument(), this);
    truthTableDock_ = new QDockWidget(tr("Tabla de verdad"), this);
    truthTableDock_->setObjectName("truthTableDock");
    truthTableDock_->setWidget(truthTablePanel_);
    truthTableDock_->setTitleBarWidget(new DockTitleBar(truthTableDock_));
    tabifyDockWidget(inspectorDock_, truthTableDock_);

    waveformPanel_ = new ui::WaveformPanel(project_->activeDocument(), this);
    waveformDock_ = new QDockWidget(tr("Analizador de senales"), this);
    waveformDock_->setObjectName("waveformDock");
    waveformDock_->setWidget(waveformPanel_);
    waveformDock_->setTitleBarWidget(new DockTitleBar(waveformDock_));
    tabifyDockWidget(inspectorDock_, waveformDock_);
    // Visibles de entrada, igual que projectTreeDock/paletteDock del lado
    // izquierdo - el unico control de visibilidad es el pin de auto-hide de
    // cada uno (ver DockTitleBar/makeAutoHideable), sin ningun boton grupal
    // aparte ni ocultamiento especial para este lado.
    // Las franjas de auto-hide arrancan vacias (nada esta despineado
    // todavia) - se muestran solas apenas tengan al menos una pestana, ver
    // setDockAutoHidden().
    leftAutoHideStrip_->hide();
    rightAutoHideStrip_->hide();

    makeAutoHideable(projectTreeDock, Qt::LeftDockWidgetArea, paletteDock);
    makeAutoHideable(paletteDock, Qt::LeftDockWidgetArea, projectTreeDock);
    makeAutoHideable(inspectorDock_, Qt::RightDockWidgetArea, truthTableDock_);
    makeAutoHideable(truthTableDock_, Qt::RightDockWidgetArea, inspectorDock_);
    makeAutoHideable(waveformDock_, Qt::RightDockWidgetArea, inspectorDock_);

    // El auto-hide guardado ya no se aplica aca sino en restoreWindowLayout(),
    // que corre una vez creadas tambien las barras de herramientas: hay que
    // llamar a restoreState() antes de sacar paneles del layout, o el estado
    // restaurado los volveria a anclar.

    connect(waveformPanel_, &ui::WaveformPanel::addRequested, this, [this] {
        std::vector<editor::WireEndpoint> endpoints;
        std::vector<QString> labels;
        for (QGraphicsItem* item : activeScene()->selectedItems()) {
            auto* component = dynamic_cast<ComponentItem*>(item);
            if (component == nullptr) {
                continue;
            }
            const auto* instance = project_->activeDocument()->component(component->componentId());
            if (instance == nullptr || instance->pins().size() != 1) {
                continue;
            }
            endpoints.push_back(editor::PinRef{component->componentId(), 0});
            const std::string& label = std::get<std::string>(instance->property("label"));
            labels.push_back(label.empty() ? QString::fromStdString(instance->typeId()) +
                                                  QString::number(component->componentId())
                                            : QString::fromStdString(label));
        }
        waveformPanel_->addWatches(endpoints, labels);
    });
}

void MainWindow::setupAutoHideStrips() {
    // QToolBar en vez de QDockWidget: un dock widget comparte splitter con
    // los demas docks reales de su area, y QMainWindowLayout reparte ese
    // espacio segun el widget YA presente en la celda al agregar uno nuevo
    // (splitDockWidget), no segun el sizeHint del nuevo - la franja derecha
    // (agregada partiendo el ancho, ya grande, de inspectorDock_) terminaba
    // con la mitad de ese ancho en vez de sus ~26px reales, y sacar despues
    // ese vecino no colapsaba el hueco (el bug reportado: contenido/hueco
    // que no se ocultaba bien). Un QToolBar vive en su propia banda de
    // QMainWindowLayout, completamente ajena al splitter de los docks, asi
    // que ninguno de esos dos problemas puede volver a pasar.
    constexpr int kAutoHideStripWidth = 28;
    auto makeStrip = [this](ui::AutoHideStrip*& stripWidget, Qt::ToolBarArea area) {
        auto* bar = new QToolBar(this);
        // saveState()/restoreState() identifican cada barra y dock por
        // objectName; sin el, Qt avisa por consola y esa barra no se restaura.
        bar->setObjectName(area == Qt::LeftToolBarArea ? "leftAutoHideStrip" : "rightAutoHideStrip");
        bar->setMovable(false);
        bar->setFloatable(false);
        bar->setOrientation(Qt::Vertical);
        bar->setContentsMargins(0, 0, 0, 0);
        stripWidget = new ui::AutoHideStrip(bar);
        stripWidget->setFixedWidth(kAutoHideStripWidth);
        bar->addWidget(stripWidget);
        connect(stripWidget, &ui::AutoHideStrip::panelActivated, this, &MainWindow::showFlyout);
        addToolBar(area, bar);
        return bar;
    };
    leftAutoHideStrip_ = makeStrip(leftStripWidget_, Qt::LeftToolBarArea);
    rightAutoHideStrip_ = makeStrip(rightStripWidget_, Qt::RightToolBarArea);
}

void MainWindow::makeAutoHideable(QDockWidget* dock, Qt::DockWidgetArea area, QDockWidget* tabifySibling) {
    autoHideOrigin_[dock] = {area, tabifySibling};
    autoHideDocksByName_[dock->objectName()] = dock;
    auto* titleBar = qobject_cast<DockTitleBar*>(dock->titleBarWidget());
    connect(titleBar, &DockTitleBar::pinnedChanged, this,
            [this, dock](bool pinned) { setDockAutoHidden(dock, !pinned); });
}

void MainWindow::applyDefaultWindowGeometry() {
    resize(defaults::kWindowSize);
    // La posicion no se hornea junto con el tamano: depende del monitor donde
    // se haya capturado y en una pantalla mas chica dejaria la ventana fuera
    // de los limites visibles. Centrarla es estable en cualquier resolucion.
    if (const QScreen* screen = QApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        move(available.center() - QPoint(width() / 2, height() / 2));
    }
}

void MainWindow::restoreWindowLayout() {
    // Disposicion de la sesion anterior; si no hay ninguna guardada o el blob
    // es ilegible (formato viejo, instalacion nueva), la de fabrica.
    //
    // Nunca se auto-oculta ningun panel a partir de un "MainWindow/state"
    // restaurado (solo en la disposicion de fabrica, mas abajo). v0.1.2 y
    // v0.1.3 intentaron primero comparar una huella de docks/toolbars y
    // despues tambien la version exacta que guardo el estado antes de confiar
    // en el, pero un caso real (mismo binario, mismo instalador, huella Y
    // version identicas) igual reprodujo el mismo crash "aparece el splash y
    // se cierra": restoreState() puede devolver true con un blob asi (no
    // valida tan estricto), pero el primer removeDockWidget() posterior
    // (auto-hide) encuentra el QDockAreaLayout interno de Qt en un estado que
    // a veces esta corrupto y a veces no, por una razon que no depende solo
    // de version/huella (probablemente la geometria exacta capturada en la
    // sesion que guardo el blob). No hay forma fiable de detectar por
    // adelantado si un blob puntual es seguro, asi que se elimina la unica
    // operacion que alguna vez crasheo (auto-ocultar un panel restaurado) en
    // vez de seguir intentando adivinar cuando es seguro hacerlo. El tamano
    // y posicion de ventana (restoreGeometry(), en el constructor) y el
    // resto del estado de docks/toolbars siguen restaurandose normalmente -
    // solo el auto-hide de sesiones anteriores queda sin restaurar; el
    // usuario puede volver a auto-ocultar sus paneles con un click.
    const QSettings settings;
    const QByteArray savedState = settings.value("MainWindow/state").toByteArray();
    QStringList autoHiddenNames;
    if (!savedState.isEmpty() && restoreState(savedState)) {
        autoHiddenNames.clear();
    } else {
        const QByteArray defaultState = defaults::windowState();
        if (!defaultState.isEmpty()) {
            restoreState(defaultState);
        }
        autoHiddenNames = defaults::autoHiddenDocks();
    }

    // Siempre al final: estos paneles salen del layout, asi que aplicarlos
    // antes de restoreState() haria que este los volviera a anclar.
    for (const QString& name : autoHiddenNames) {
        const auto it = autoHideDocksByName_.find(name);
        if (it != autoHideDocksByName_.end() && !isAutoHidden(it->second)) {
            setDockAutoHidden(it->second, true);
        }
    }
}

void MainWindow::resetWindowLayout() {
    // Devuelve al layout lo que este colapsado antes de restaurar: un dock
    // removido de la ventana no puede ser reubicado por restoreState(). Se
    // copia a un vector porque setDockAutoHidden() modifica autoHidden_.
    const std::vector<QDockWidget*> toRepin(autoHidden_.begin(), autoHidden_.end());
    for (QDockWidget* dock : toRepin) {
        setDockAutoHidden(dock, false);
    }

    const QByteArray defaultState = defaults::windowState();
    if (!defaultState.isEmpty()) {
        restoreState(defaultState);
    }
    for (const QString& name : defaults::autoHiddenDocks()) {
        const auto it = autoHideDocksByName_.find(name);
        if (it != autoHideDocksByName_.end() && !isAutoHidden(it->second)) {
            setDockAutoHidden(it->second, true);
        }
    }
    applyDefaultWindowGeometry();
}

void MainWindow::setDockAutoHidden(QDockWidget* dock, bool autoHidden) {
    const AutoHideOrigin& origin = autoHideOrigin_.at(dock);
    ui::AutoHideStrip* strip = origin.area == Qt::LeftDockWidgetArea ? leftStripWidget_ : rightStripWidget_;

    if (autoHidden) {
        if (flyoutDock_ == dock) {
            collapseFlyout();
        }
        autoHidden_.insert(dock);
        removeDockWidget(dock);
        dock->hide();
        strip->addPanel(dock);
    } else {
        if (flyoutDock_ == dock) {
            // Devuelve titleBarWidget()/widget() de flyoutHost_ a `dock`
            // antes de re-anclarlo (ver collapseFlyout()) - si no, quedarian
            // atrapados en el contenedor del flyout y el dock se mostraria
            // vacio al volver al layout normal.
            collapseFlyout();
        }
        autoHidden_.erase(dock);
        strip->removePanel(dock);
        // Si el vecino con el que estaba tabificado sigue pineado y visible,
        // vuelve a agruparse con el; si no (p. ej. tambien esta auto-oculto,
        // o nunca tuvo vecino), se re-ancla suelto en su area de origen.
        QDockWidget* sibling = origin.tabifySibling;
        if (sibling != nullptr && !isAutoHidden(sibling) && !sibling->isHidden()) {
            tabifyDockWidget(sibling, dock);
        } else {
            addDockWidget(origin.area, dock);
        }
        dock->show();
    }

    // Mantiene el icono/checked del boton de pin correcto incluso cuando el
    // cambio no vino de un click del usuario sobre ese mismo boton (p. ej.
    // al restaurar auto-hide guardado de una sesion anterior, ver
    // setupDocks()) - en ese caso nadie mas sincroniza el titleBarWidget().
    if (auto* titleBar = qobject_cast<DockTitleBar*>(dock->titleBarWidget())) {
        titleBar->setPinned(!autoHidden);
    }

    leftAutoHideStrip_->setVisible(!leftStripWidget_->isEmpty());
    rightAutoHideStrip_->setVisible(!rightStripWidget_->isEmpty());
}

void MainWindow::showFlyout(QDockWidget* dock) {
    if (flyoutDock_ == dock) {
        // Un segundo click sobre la misma pestana la cierra (toggle) en vez
        // de no hacer nada - ademas de ser el comportamiento esperado, un
        // "no-op" aca dejaba el estado visual de la pestana desincronizado
        // de si el flyout seguia abierto o no.
        collapseFlyout();
        return;
    }
    if (flyoutDock_ != nullptr) {
        collapseFlyout();
    }
    flyoutDock_ = dock;

    const AutoHideOrigin& origin = autoHideOrigin_.at(dock);
    const bool onLeft = origin.area == Qt::LeftDockWidgetArea;
    QWidget* titleBar = dock->titleBarWidget();
    QWidget* content = dock->widget();
    const int hintWidth = content != nullptr ? content->sizeHint().width() : 0;
    const int width = std::clamp(hintWidth > 0 ? hintWidth : 260, 180, 420);
    // centralWidget() comparte el mismo sistema de coordenadas que
    // flyoutHost_ (ambos son hijos directos de este QMainWindow), asi que
    // su geometria ya excluye el espacio que ocupan los demas paneles
    // todavia pineados - el flyout se ancla exactamente al borde de esa
    // area, sin taparlos.
    const QRect centralRect = centralWidget()->geometry();
    const int x = onLeft ? centralRect.left() : centralRect.right() - width + 1;

    if (flyoutHost_ == nullptr) {
        flyoutHost_ = new QWidget(this);
        // Un QWidget comun no pinta ningun fondo propio - sin esto el
        // flyout se veia transparente, dejando ver el lienzo a traves
        // suyo en vez de taparlo como un panel normal.
        flyoutHost_->setAutoFillBackground(true);
        auto* hostLayout = new QVBoxLayout(flyoutHost_);
        hostLayout->setContentsMargins(0, 0, 0, 0);
        hostLayout->setSpacing(0);
    }
    // Se saca titleBarWidget()/widget() del QDockWidget en vez de mostrar
    // el QDockWidget mismo como overlay: reusarlo (removido del layout via
    // removeDockWidget() pero seguia siendo el mismo QDockWidget que
    // QMainWindowLayout habia gestionado antes) peleaba con su bookkeeping
    // interno - eran la causa tanto del bug de "Propiedades" reapareciendo
    // sola como del flyout "saltando" a su sizeHint en vez de ocupar todo
    // el alto pedido. flyoutHost_ es un QWidget comun, ajeno a los docks,
    // asi que su geometria se respeta tal cual se la fijamos.
    auto* hostLayout = qobject_cast<QVBoxLayout*>(flyoutHost_->layout());
    if (titleBar != nullptr) {
        hostLayout->addWidget(titleBar);
    }
    if (content != nullptr) {
        hostLayout->addWidget(content);
    }

    flyoutHost_->setGeometry(x, centralRect.top(), width, centralRect.height());
    flyoutHost_->show();
    flyoutHost_->raise();
    flyoutHost_->setFocus();

    ui::AutoHideStrip* strip = onLeft ? leftStripWidget_ : rightStripWidget_;
    strip->setPanelActive(dock, true);

    qApp->installEventFilter(this);
}

void MainWindow::collapseFlyout() {
    if (flyoutDock_ == nullptr) {
        return;
    }
    QDockWidget* dock = flyoutDock_;
    flyoutDock_ = nullptr;
    qApp->removeEventFilter(this);

    // Devolver titleBarWidget()/widget() a `dock` antes de esconder el
    // contenedor - ver el comentario en showFlyout().
    auto* hostLayout = flyoutHost_->layout();
    if (QLayoutItem* item = hostLayout->takeAt(0)) {
        dock->setTitleBarWidget(item->widget());
        delete item;
    }
    if (QLayoutItem* item = hostLayout->takeAt(0)) {
        dock->setWidget(item->widget());
        delete item;
    }
    flyoutHost_->hide();

    const AutoHideOrigin& origin = autoHideOrigin_.at(dock);
    ui::AutoHideStrip* strip = origin.area == Qt::LeftDockWidgetArea ? leftStripWidget_ : rightStripWidget_;
    strip->setPanelActive(dock, false);
}

void MainWindow::revealDock(QDockWidget* dock) {
    if (isAutoHidden(dock)) {
        showFlyout(dock);
    } else {
        dock->show();
        dock->raise();
    }
}

bool MainWindow::isAutoHidden(QDockWidget* dock) const { return autoHidden_.count(dock) > 0; }

void MainWindow::setupMenusAndToolbars() {
    // --- Archivo ---
    QMenu* fileMenu = menuBar()->addMenu(tr("&Archivo"));
    // "Nuevo" (ambiguo: creaba un proyecto entero, con el mismo icono de
    // hoja suelta que sugeria "documento") se separa en dos acciones
    // distintas, cada una con su propio icono: proyecto = carpeta+,
    // documento = hoja suelta (icons::newDocument(), sin cambios).
    // Mismos atajos que Visual Studio: Ctrl+N para documento (el caso mas
    // frecuente), Ctrl+Shift+N para proyecto.
    QAction* newProjectAction =
        fileMenu->addAction(icons::newProject(), tr("Nuevo &proyecto..."), this, &MainWindow::onNewProject);
    newProjectAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N));

    QAction* newDocumentAction = fileMenu->addAction(icons::newDocument(), tr("Nuevo doc&umento..."), this,
                                                      [this] { projectTree_->addNewDocument(); });
    newDocumentAction->setShortcut(QKeySequence::New);

    QAction* openAction = fileMenu->addAction(icons::open(), tr("&Abrir..."), this, &MainWindow::onOpen);
    openAction->setShortcut(QKeySequence::Open);

    recentFilesMenu_ = fileMenu->addMenu(tr("Abrir &recientes"));
    rebuildRecentFilesMenu();

    QAction* saveAction = fileMenu->addAction(icons::save(), tr("&Guardar"), this, &MainWindow::onSave);
    saveAction->setShortcut(QKeySequence::Save);

    QAction* saveAsAction = fileMenu->addAction(tr("Guardar &como..."), this, &MainWindow::onSaveAs);
    saveAsAction->setShortcut(QKeySequence::SaveAs);

    // Mismo comportamiento que "Guardar" (que ya guarda todos los
    // documentos del proyecto de una) - se agrega aparte solo por claridad/
    // paridad con Visual Studio, no porque hoy hagan algo distinto.
    QAction* saveAllAction = fileMenu->addAction(icons::save(), tr("Guardar &todo"), this, &MainWindow::onSave);
    saveAllAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));

    fileMenu->addSeparator();
    // Equivalentes por menu de lo que ya ofrece el menu contextual del
    // arbol de proyecto (ui::ProjectTree), para quien no lo abra con el
    // clic derecho.
    fileMenu->addAction(tr("&Importar documento..."), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Importar documento"),
                                                            settings_.ensureWorkspacePath(),
                                                            tr("Documentos DigitalForge (*.dfc)"));
        if (path.isEmpty()) {
            return;
        }
        try {
            project_->importDocument(path);
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Error al importar"), QString::fromUtf8(error.what()));
        }
    });
    fileMenu->addAction(tr("&Exportar documento actual como..."), this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Exportar documento como"),
                                                            settings_.ensureWorkspacePath(),
                                                            tr("Documentos DigitalForge (*.dfc)"));
        if (path.isEmpty()) {
            return;
        }
        try {
            project_->exportDocument(project_->activeDocumentId(), path);
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Error al exportar"), QString::fromUtf8(error.what()));
        }
    });
    fileMenu->addAction(tr("Exportar &esquema como imagen..."), this, [this] {
        QString selectedFilter;
        const QString path = QFileDialog::getSaveFileName(
            this, tr("Exportar esquema como imagen"), QString(),
            tr("Imagen PNG (*.png);;Imagen JPEG (*.jpg *.jpeg);;Mapa de bits (*.bmp)"), &selectedFilter);
        if (path.isEmpty()) {
            return;
        }

        // itemsBoundingRect() (no la escena virtual completa, mucho mas
        // grande) para que la imagen quede recortada al circuito real, con
        // un margen chico para que nada quede pegado al borde.
        const QRectF sourceRect = activeScene()->itemsBoundingRect().adjusted(-20, -20, 20, 20);
        if (sourceRect.isEmpty()) {
            QMessageBox::warning(this, tr("Exportar esquema"),
                                  tr("El circuito esta vacio - no hay nada para exportar."));
            return;
        }

        QImage image(sourceRect.size().toSize(), QImage::Format_ARGB32);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        // render() llama a CircuitScene::drawBackground(), que pinta un
        // fondo solido - no hace falta llenar `image` de antemano.
        activeScene()->render(&painter, QRectF(), sourceRect);
        painter.end();

        QString finalPath = path;
        if (QFileInfo(path).suffix().isEmpty()) {
            if (selectedFilter.contains(QStringLiteral("*.jpg"))) {
                finalPath += ".jpg";
            } else if (selectedFilter.contains(QStringLiteral("*.bmp"))) {
                finalPath += ".bmp";
            } else {
                finalPath += ".png";
            }
        }
        if (!image.save(finalPath)) {
            QMessageBox::warning(this, tr("Error al exportar"), tr("No se pudo guardar la imagen."));
        }
    });
    fileMenu->addAction(tr("Importar desde &Logisim (.circ)..."), this, [this] {
        const QString path = QFileDialog::getOpenFileName(this, tr("Importar desde Logisim"), QString(),
                                                            tr("Circuitos Logisim (*.circ)"));
        if (path.isEmpty()) {
            return;
        }
        try {
            project_->importLogisimDocument(path);
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("Error al importar desde Logisim"), QString::fromUtf8(error.what()));
        }
    });

    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Preferencias..."), this, &MainWindow::onPreferences);
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Salir"), this, &QWidget::close);

    // --- Editar ---
    QMenu* editMenu = menuBar()->addMenu(tr("&Editar"));
    editMenu_ = editMenu; // ver setCircuitEditingEnabled()
    // Las acciones de un QUndoGroup siguen automaticamente al QUndoStack
    // activo (project_->undoGroup().setActiveStack(), ver
    // onActiveDocumentChanged) - no hace falta reconstruirlas al cambiar de
    // documento, a diferencia de un QUndoStack::createUndoAction() atado a
    // un unico stack fijo.
    QAction* undoAction = project_->undoGroup().createUndoAction(this, tr("Deshacer"));
    undoAction->setIcon(icons::undo());
    undoAction->setShortcut(QKeySequence::Undo);
    QAction* redoAction = project_->undoGroup().createRedoAction(this, tr("Rehacer"));
    redoAction->setIcon(icons::redo());
    redoAction->setShortcut(QKeySequence::Redo);
    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);
    editMenu->addSeparator();
    editMenu->addAction(icons::deleteItem(), tr("&Eliminar"), this, [this] { activeScene()->deleteSelected(); })
        ->setShortcut(QKeySequence::Delete);
    editMenu->addAction(icons::rotate(), tr("&Rotar"), this, [this] { activeScene()->rotateSelected(); })
        ->setShortcut(QStringLiteral("R"));
    editMenu->addSeparator();
    editMenu->addAction(tr("Cop&iar"), this, [this] { activeScene()->copySelected(); })->setShortcut(QKeySequence::Copy);
    editMenu->addAction(tr("Cor&tar"), this, [this] { activeScene()->cutSelected(); })->setShortcut(QKeySequence::Cut);
    editMenu->addAction(tr("&Pegar"), this, [this] { activeScene()->pasteClipboard(); })->setShortcut(QKeySequence::Paste);

    // --- Simulacion ---
    QMenu* simMenu = menuBar()->addMenu(tr("&Simulacion"));
    simMenu->addAction(icons::run(), tr("&Ejecutar"), this, [this] { project_->activeDocument()->setLiveSimulation(true); })
        ->setShortcut(QKeySequence(Qt::Key_F5));
    simMenu
        ->addAction(icons::pause(), tr("&Pausar"), this, [this] { project_->activeDocument()->setLiveSimulation(false); })
        ->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    simMenu->addAction(icons::step(), tr("Paso a &paso"), this, [this] { project_->activeDocument()->step(); })
        ->setShortcut(QKeySequence(Qt::Key_F6));
    simMenu->addAction(icons::reset(), tr("Re&iniciar"), this, [this] { project_->activeDocument()->rebuildSimulation(); })
        ->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));

    // --- Karnaugh --- (mapas de Karnaugh y tablas de verdad multi-salida:
    // las dos formas de sintetizar un circuito a partir de una funcion
    // booleana, en vez de dibujarlo compuerta por compuerta a mano)
    QMenu* karnaughMenu = menuBar()->addMenu(tr("&Karnaugh"));
    karnaughMenu->addAction(tr("Nuevo mapa de &Karnaugh..."), this,
                             [this] { projectTree_->addNewKarnaughDocument(); });
    karnaughMenu->addAction(tr("Nueva &tabla de verdad..."), this,
                             [this] { projectTree_->addNewTruthTableDocument(); });
    karnaughMenu->addSeparator();
    // Solo tiene sentido con una tabla de verdad al frente (un mapa de
    // Karnaugh YA ES un unico mapa) -- arma un mapa aparte por cada columna
    // de salida, para inspeccionar/animar el procedimiento de cada una antes
    // de generar el circuito combinado.
    karnaughMenu->addAction(tr("Generar &mapas"), this, [this] {
        const int index = documentTabBar_ != nullptr ? documentTabBar_->currentIndex() : -1;
        if (index < 0) {
            return;
        }
        const uint32_t id = documentTabBar_->tabData(index).toUInt();
        try {
            if (project_->documentKind(id) == editor::Project::DocumentKind::TruthTable) {
                truthTableViews_.at(id)->onGenerateMapsClicked();
            } else {
                QMessageBox::information(this, tr("Generar mapas"),
                                          tr("Esta accion arma un mapa de Karnaugh por columna de salida -- abra o "
                                             "cree una tabla de verdad primero."));
            }
        } catch (const std::exception&) {
        }
    });
    // Dispara la generacion sin necesitar el boton propio de la vista -- solo
    // tiene efecto con un mapa de Karnaugh o una tabla de verdad al frente;
    // en cualquier otro caso avisa en vez de no hacer nada en silencio.
    karnaughMenu->addAction(tr("&Generar circuito"), this, [this] {
        const int index = documentTabBar_ != nullptr ? documentTabBar_->currentIndex() : -1;
        if (index < 0) {
            return;
        }
        const uint32_t id = documentTabBar_->tabData(index).toUInt();
        // documentKind() lanza para un id desconocido -- en teoria el id de
        // la pestana activa siempre deberia ser valido, pero esta accion es
        // la unica que lo consulta bajo demanda (afuera de la cascada de
        // senales de Project) en vez de recibirlo ya validado de un
        // xxxAdded/xxxActivationRequested, asi que se prefiere avisar en vez
        // de arriesgar un crash si algun caso limite lo deja transitoriamente
        // desincronizado.
        try {
            switch (project_->documentKind(id)) {
                case editor::Project::DocumentKind::Karnaugh:
                    karnaughViews_.at(id)->onGenerateCircuitClicked();
                    return;
                case editor::Project::DocumentKind::TruthTable:
                    truthTableViews_.at(id)->onGenerateCircuitClicked();
                    return;
                case editor::Project::DocumentKind::Circuit:
                    QMessageBox::information(
                        this, tr("Generar circuito"),
                        tr("Esta accion sintetiza un circuito a partir de un mapa de Karnaugh o "
                           "una tabla de verdad -- abra o cree uno primero."));
                    return;
            }
        } catch (const std::exception&) {
            // Pestana en un estado transitorio (p. ej. a mitad de cerrarse) --
            // sin efecto, en vez de crashear la aplicacion entera.
        }
    });

    // --- Ver ---
    QMenu* viewMenu = menuBar()->addMenu(tr("&Ver"));
    viewMenu->addAction(icons::zoomIn(), tr("Acercar"), view_, &CircuitView::zoomIn)->setShortcut(QKeySequence::ZoomIn);
    viewMenu->addAction(icons::zoomOut(), tr("Alejar"), view_, &CircuitView::zoomOut)
        ->setShortcut(QKeySequence::ZoomOut);
    viewMenu->addAction(tr("Restablecer zoom"), view_, &CircuitView::resetZoom);
    viewMenu->addSeparator();
    gridAction_ = viewMenu->addAction(tr("Mostrar cuadricula"));
    gridAction_->setCheckable(true);
    snapAction_ = viewMenu->addAction(tr("Ajustar a cuadricula"));
    snapAction_->setCheckable(true);
    viewMenu->addSeparator();

    // Tema: tres opciones excluyentes. "Del sistema" devuelve el control al
    // modo claro/oscuro configurado en el sistema operativo; las otras dos lo
    // fijan sin importar que use el sistema (ver ui::ThemeManager).
    QMenu* themeMenu = viewMenu->addMenu(tr("Tema"));
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    const ui::ThemeMode activeTheme = ui::ThemeManager::instance().mode();
    for (const ui::ThemeMode themeMode : {ui::ThemeMode::System, ui::ThemeMode::Light, ui::ThemeMode::Dark}) {
        QAction* action = themeMenu->addAction(ui::ThemeManager::displayName(themeMode));
        action->setCheckable(true);
        action->setChecked(themeMode == activeTheme);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, themeMode] {
            ui::ThemeManager::instance().setMode(themeMode);
            settings_.themeMode = ui::ThemeManager::toSettingsValue(themeMode);
            settings_.save();
        });
    }

    viewMenu->addSeparator();
    viewMenu->addAction(tr("Restablecer disposicion de paneles"), this, &MainWindow::resetWindowLayout);
    bindActiveSceneViewActions();

    // --- Bibliotecas ---
    QMenu* libraryMenu = menuBar()->addMenu(tr("&Bibliotecas"));
    QAction* basicLibraryAction = libraryMenu->addAction(tr("Basica (integrada)"));
    basicLibraryAction->setCheckable(true);
    basicLibraryAction->setChecked(true);
    basicLibraryAction->setEnabled(false);
    basicLibraryAction->setToolTip(tr("Carga de bibliotecas JSON (74LSxx, etc.) pendiente de fase posterior"));

    // --- Ayuda ---
    QMenu* helpMenu = menuBar()->addMenu(tr("A&yuda"));
    helpMenu->addAction(tr("Buscar &actualizaciones..."), this, &MainWindow::onCheckForUpdatesManually);
    helpMenu->addAction(tr("&Acerca de DigitalForge..."), this, &MainWindow::onAbout);

    // Comprobacion de actualizaciones contra los releases de GitHub. Se crea
    // aca (una sola vez) y se dispara un chequeo silencioso al arrancar: si
    // hay una version mas nueva avisa, y si no (o si no hay red) no molesta.
    updateChecker_ = new UpdateChecker(this);
    connect(updateChecker_, &UpdateChecker::updateAvailable, this, &MainWindow::onUpdateAvailable);
    connect(updateChecker_, &UpdateChecker::upToDate, this, [this] {
        QMessageBox::information(this, tr("Actualizaciones"),
                                 tr("Estas usando la version mas reciente de DigitalForge."));
    });
    connect(updateChecker_, &UpdateChecker::checkFailed, this, [this](const QString& reason) {
        QMessageBox::information(this, tr("Actualizaciones"),
                                 tr("No se pudo comprobar si hay actualizaciones.\n\n%1").arg(reason));
    });
    updateChecker_->checkForUpdates(/*silent=*/true);

    // --- Toolbars ---
    QToolBar* mainToolBar = addToolBar(tr("Principal"));
    mainToolBar->setObjectName("mainToolBar"); // requerido por saveState(), ver setupAutoHideStrips()
    mainToolBar->addAction(newProjectAction);
    mainToolBar->addAction(newDocumentAction);
    mainToolBar->addAction(openAction);
    mainToolBar->addAction(saveAction);
    mainToolBar->addSeparator();
    mainToolBar->addAction(undoAction);
    mainToolBar->addAction(redoAction);
    mainToolBar->addSeparator();
    mainToolBar->addAction(icons::zoomIn(), tr("Zoom +"), view_, &CircuitView::zoomIn);
    mainToolBar->addAction(icons::zoomOut(), tr("Zoom -"), view_, &CircuitView::zoomOut);

    simulationToolbar_ = new ui::SimulationToolbar(project_->activeDocument(), this);
    simulationToolbar_->setObjectName("simulationToolBar");
    addToolBar(simulationToolbar_);
}

void MainWindow::onSceneSelectionChanged() {
    // Mientras la simulacion esta en ejecucion, seleccionar un componente
    // (p. ej. al conmutar una entrada con un clic) ya no actualiza el
    // inspector automaticamente - la mayoria de las propiedades no se
    // pueden editar en ese estado de todos modos (requireEditable las
    // bloquea), y el panel saltando con cada clic mientras se simula
    // resultaba mas distractivo que util. Ver "Propiedades" en el menu
    // contextual (clic derecho, CircuitScene::componentContextMenuRequested)
    // para la via explicita de verlas igual durante la simulacion.
    if (project_->activeDocument()->isLiveSimulation()) {
        return;
    }
    const QList<QGraphicsItem*> selected = activeScene()->selectedItems();
    std::vector<uint32_t> componentIds;
    for (QGraphicsItem* item : selected) {
        if (auto* component = dynamic_cast<ComponentItem*>(item)) {
            componentIds.push_back(component->componentId());
        }
    }
    inspector_->setSelectedComponents(std::move(componentIds));
}

void MainWindow::onComponentContextMenuRequested(uint32_t componentId, QPoint screenPos) {
    // CircuitScene::contextMenuEvent() ya selecciono unicamente este
    // componente antes de emitir la señal (clearSelection() +
    // setSelected(true)), asi que rotateSelected()/deleteSelected() actuan
    // exactamente sobre el - mismas acciones que ya existian en el menu
    // Editar (icons::rotate()/icons::deleteItem(), atajos R/Supr), solo que
    // aca no hacia falta que el usuario fuera al menu para algo tan comun
    // como rotar el componente que acaba de clickear (el pedido explicito:
    // "desplegar menu al dar clic derecho... opciones de rotacion").
    QMenu menu(this);
    QAction* propertiesAction = menu.addAction(tr("Propiedades"));
    connect(propertiesAction, &QAction::triggered, this, [this, componentId] {
        inspector_->setSelectedComponents({componentId});
        revealDock(inspectorDock_);
    });
    menu.addSeparator();
    // Sin setShortcut() aca a proposito: ya existen los mismos atajos
    // globales (R/Supr, ver el menu Editar arriba y
    // CircuitScene::keyPressEvent()) - duplicarlos en estas acciones
    // efimeras (creadas y destruidas en cada clic derecho) arriesgaba un
    // "Ambiguous shortcut" mientras el menu esta abierto, sin ganar nada
    // funcional (el atajo real ya funciona independientemente del menu).
    QAction* rotateAction = menu.addAction(icons::rotate(), tr("Rotar"));
    connect(rotateAction, &QAction::triggered, this, [this] { activeScene()->rotateSelected(); });
    QAction* deleteAction = menu.addAction(icons::deleteItem(), tr("Eliminar"));
    connect(deleteAction, &QAction::triggered, this, [this] { activeScene()->deleteSelected(); });

    menu.addSeparator();
    // Orden de apilado estilo draw.io - mismas cuatro acciones que ese
    // programa ofrece en su menu contextual, sin atajo propio (no hay uno
    // ya establecido para esto en el menu Editar, a diferencia de
    // Rotar/Eliminar de arriba).
    QAction* bringToFrontAction = menu.addAction(tr("Traer al frente"));
    connect(bringToFrontAction, &QAction::triggered, this, [this] { activeScene()->bringSelectedToFront(); });
    QAction* bringForwardAction = menu.addAction(tr("Traer adelante"));
    connect(bringForwardAction, &QAction::triggered, this, [this] { activeScene()->bringSelectedForward(); });
    QAction* sendBackwardAction = menu.addAction(tr("Enviar atras"));
    connect(sendBackwardAction, &QAction::triggered, this, [this] { activeScene()->sendSelectedBackward(); });
    QAction* sendToBackAction = menu.addAction(tr("Enviar al fondo"));
    connect(sendToBackAction, &QAction::triggered, this, [this] { activeScene()->sendSelectedToBack(); });

    menu.exec(screenPos);
}

void MainWindow::onNewProject() {
    // Se pide la ubicacion *antes* de tocar nada: si el usuario cancela el
    // dialogo (o hay un error creando la carpeta), el proyecto actual queda
    // exactamente como estaba - ni siquiera se llama a maybeSaveChanges().
    const QString path = promptForNewProjectPath();
    if (path.isEmpty()) {
        return;
    }
    if (!maybeSaveChanges()) {
        return;
    }
    project_->newProject();
    if (!saveToPath(path)) {
        // No deberia pasar (la carpeta ya se creo con exito en
        // promptForNewProjectPath()), pero si el guardado en si fallara por
        // otro motivo, el usuario se queda con un proyecto nuevo en memoria
        // en vez de una carpeta a medio crear sin contenido.
        return;
    }
    recoveredUnsavedContent_ = false;
    updateWindowTitle();
    refreshWindowModified();
}

QString MainWindow::promptForNewProjectPath() {
    // Arranca en la carpeta de trabajo (creandola si hace falta) en vez de en
    // el directorio que Qt recuerde por su cuenta: los proyectos nuevos deben
    // caer juntos en un lugar previsible - ver AppSettings::workspacePath.
    const QString chosen = QFileDialog::getSaveFileName(this, tr("Nuevo proyecto"), settings_.ensureWorkspacePath(),
                                                        tr("Proyectos DigitalForge (*.dfproj)"));
    if (chosen.isEmpty()) {
        return QString();
    }
    const QFileInfo info(chosen);
    const QString name = info.completeBaseName();
    const QDir parentDir = info.absoluteDir();
    if (parentDir.exists(name)) {
        QMessageBox::warning(this, tr("Error"), tr("Ya existe una carpeta \"%1\" en esa ubicacion.").arg(name));
        return QString();
    }
    // Rechaza crear el proyecto nuevo adentro de la carpeta RAIZ de OTRO
    // proyecto ya existente -- osea un ancestro `Foo/` que contenga su
    // propio `Foo.dfproj` (el mismo nombre, la convencion que arma esta
    // misma funcion mas abajo con mkpath(name) + parentDir.filePath(name) +
    // "/" + name + ".dfproj"). Sin este chequeo, el dialogo nativo de
    // Windows -- que recuerda la ultima carpeta usada entre invocaciones sin
    // importar el hint que se le pasa como `dir` -- puede dejar caer al
    // usuario adentro de un proyecto que ya creo antes; si reusa el mismo
    // nombre (tipico al reintentar guardar una sesion recuperada del
    // autoguardado, que siempre arranca sin filePath_), el resultado es un
    // proyecto anidado dentro de si mismo, un nivel mas cada vez.
    //
    // A proposito NO se rechaza solo por encontrar CUALQUIER .dfproj suelto
    // en un ancestro (por ejemplo, en la carpeta de Documentos general, que
    // puede tener archivos .dfproj de una version anterior a que cada
    // proyecto tuviera su propia subcarpeta) -- eso bloquearia crear
    // cualquier proyecto nuevo debajo de esa carpeta sin ningun anidamiento
    // real, solo por tener otro proyecto sin relacion como vecino en algun
    // nivel mas arriba.
    for (QDir ancestor = parentDir;;) {
        if (ancestor.exists(ancestor.dirName() + QStringLiteral(".dfproj"))) {
            QMessageBox::warning(
                this, tr("Error"),
                tr("La ubicacion elegida esta dentro de la carpeta del proyecto \"%1\". Elegi una ubicacion "
                   "fuera de cualquier proyecto existente.")
                    .arg(ancestor.dirName()));
            return QString();
        }
        if (!ancestor.cdUp()) {
            break;
        }
    }
    if (!parentDir.mkpath(name)) {
        QMessageBox::warning(this, tr("Error"), tr("No se pudo crear la carpeta del proyecto."));
        return QString();
    }
    return parentDir.filePath(name) + "/" + name + ".dfproj";
}

void MainWindow::onOpen() {
    if (!maybeSaveChanges()) {
        return;
    }
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Abrir proyecto"), settings_.ensureWorkspacePath(),
        tr("Proyectos y documentos DigitalForge (*.dfproj *.dfc);;Todos los archivos (*)"));
    if (path.isEmpty()) {
        return;
    }
    if (loadFromPath(path)) {
        recoveredUnsavedContent_ = false;
        updateWindowTitle();
        refreshWindowModified();
        addToRecentFiles(path);
        statusBar()->showMessage(tr("Proyecto abierto: %1").arg(path), 3000);
    }
}

bool MainWindow::openFileAtStartup(const QString& path) {
    // No pasa por maybeSaveChanges(): esto corre justo despues de construir la
    // ventana, con el documento anonimo inicial vacio y sin nada que perder.
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        statusBar()->showMessage(tr("No se encontro el archivo: %1").arg(path), 5000);
        return false;
    }
    if (!loadFromPath(path)) {
        return false;
    }
    recoveredUnsavedContent_ = false;
    updateWindowTitle();
    refreshWindowModified();
    addToRecentFiles(path);
    statusBar()->showMessage(tr("Proyecto abierto: %1").arg(path), 3000);
    return true;
}

void MainWindow::onSave() {
    if (project_->filePath().isEmpty()) {
        onSaveAs();
        return;
    }
    if (saveToPath(project_->filePath())) {
        addToRecentFiles(project_->filePath());
        statusBar()->showMessage(tr("Guardado: %1").arg(project_->filePath()), 3000);
    }
}

void MainWindow::onSaveAs() {
    const QString path = promptForNewProjectPath();
    if (path.isEmpty()) {
        return;
    }
    if (saveToPath(path)) {
        updateWindowTitle();
        addToRecentFiles(path);
        statusBar()->showMessage(tr("Guardado: %1").arg(path), 3000);
    }
}

void MainWindow::onAbout() {
    QMessageBox::about(this, tr("Acerca de DigitalForge"),
                        tr("<h3>DigitalForge %1</h3>"
                           "<p>Editor y simulador de logica digital multiplataforma.</p>"
                           "<p>Desarrollado por Erick Rashon<br>"
                           "erashon@outlook.com</p>"
                           "<p>DigitalForge es software libre y de codigo abierto, bajo licencia MIT.</p>"
                           "<br/>"
                           "<p>El codigo fuente esta disponible en <a "
                           "href=\"https://github.com/yetto-tools/DigitalForge\">yetto-tools/DigitalForge</a></p>")
                            .arg(kAppVersion));
}

void MainWindow::onCheckForUpdatesManually() {
    updateChecker_->checkForUpdates(/*silent=*/false);
}

void MainWindow::onUpdateAvailable(const QString& latestVersion, const QString& downloadUrl) {
    const auto answer = QMessageBox::question(
        this, tr("Actualizacion disponible"),
        tr("Hay una version nueva de DigitalForge disponible: <b>%1</b>.<br>"
           "Estas usando la %2.<br><br>"
           "Deseas abrir la pagina de descargas?")
            .arg(latestVersion, QString::fromUtf8(kAppVersion)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (answer == QMessageBox::Yes) {
        QDesktopServices::openUrl(QUrl(downloadUrl));
    }
}

void MainWindow::onPreferences() {
    ui::PreferencesDialog dialog(settings_, this);
    // El boton "Restablecer valores predeterminados" del dialogo actua de
    // inmediato sobre los paneles (sin esperar a Aceptar) - copia autoHidden_
    // a un vector antes de iterar, ya que setDockAutoHidden() lo modifica.
    connect(&dialog, &ui::PreferencesDialog::panelsResetRequested, this, [this] {
        const std::vector<QDockWidget*> toRepin(autoHidden_.begin(), autoHidden_.end());
        for (QDockWidget* dock : toRepin) {
            setDockAutoHidden(dock, false);
        }
    });
    if (dialog.exec() == QDialog::Accepted) {
        settings_ = dialog.values();
        settings_.save();
        autosaveTimer_->setInterval(settings_.autosaveIntervalSec * 1000);
    }
}

void MainWindow::onAutosave() {
    // Alcance deliberadamente reducido al documento activo: autoguardar
    // *todos* los documentos del proyecto en cada tick implicaria escribir
    // un manifiesto + un .dfc por documento cada minuto, mucho mas caro y
    // sin necesidad real (el usuario solo puede estar editando uno a la vez).
    try {
        formats::saveProjectToFile(*project_->activeDocument(), autosavePath_.toStdString());
    } catch (const std::exception&) {
        // Los fallos de autoguardado son silenciosos por diseno (nunca deben
        // interrumpir la edicion); el siguiente autoguardado exitoso o un
        // Guardar explicito los recuperara.
    }
}

void MainWindow::setupAutosave() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    autosavePath_ = dir + "/autosave.dfproj";

    if (QFile::exists(autosavePath_)) {
        const auto result =
            QMessageBox::question(this, tr("Recuperar autoguardado"),
                                   tr("Se encontro un autoguardado de una sesion anterior. Restaurarlo?"));
        if (result == QMessageBox::Yes && loadFromPath(autosavePath_)) {
            // Este contenido no corresponde a ningun archivo que el usuario
            // haya abierto/guardado (project_->filePath() sigue vacio) -
            // queda marcado como sin guardar aunque el QUndoStack recien
            // cargado este "limpio".
            recoveredUnsavedContent_ = true;
        }
    }

    autosaveTimer_ = new QTimer(this);
    connect(autosaveTimer_, &QTimer::timeout, this, &MainWindow::onAutosave);
    autosaveTimer_->start(settings_.autosaveIntervalSec * 1000);
}

bool MainWindow::saveToPath(const QString& path) {
    try {
        project_->saveToFile(path);
        recoveredUnsavedContent_ = false;
        refreshWindowModified();
        return true;
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Error al guardar"), QString::fromUtf8(error.what()));
        return false;
    }
}

bool MainWindow::loadFromPath(const QString& path) {
    try {
        project_->loadFromFile(path);
        return true;
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Error al abrir"), QString::fromUtf8(error.what()));
        return false;
    }
}

void MainWindow::rebuildRecentFilesMenu() {
    QSettings settings;
    const QStringList paths = settings.value("RecentFiles/paths").toStringList();

    // Poda las rutas que ya no existen en disco (movidas/borradas desde la
    // ultima sesion) en vez de mostrarlas igual y fallar recien al elegirlas.
    QStringList existing;
    for (const QString& path : paths) {
        if (QFile::exists(path)) {
            existing << path;
        }
    }
    if (existing.size() != paths.size()) {
        settings.setValue("RecentFiles/paths", existing);
    }

    recentFilesMenu_->clear();
    if (existing.isEmpty()) {
        QAction* emptyAction = recentFilesMenu_->addAction(tr("(vacio)"));
        emptyAction->setEnabled(false);
        return;
    }
    for (const QString& path : existing) {
        recentFilesMenu_->addAction(path, this, [this, path] {
            if (!maybeSaveChanges()) {
                return;
            }
            if (loadFromPath(path)) {
                recoveredUnsavedContent_ = false;
                updateWindowTitle();
                refreshWindowModified();
                addToRecentFiles(path);
                statusBar()->showMessage(tr("Proyecto abierto: %1").arg(path), 3000);
            }
        });
    }
}

void MainWindow::addToRecentFiles(const QString& path) {
    constexpr int kMaxRecentFiles = 20;
    const QString absolutePath = QFileInfo(path).absoluteFilePath();

    QSettings settings;
    QStringList paths = settings.value("RecentFiles/paths").toStringList();
    paths.removeAll(absolutePath);
    paths.prepend(absolutePath);
    while (paths.size() > kMaxRecentFiles) {
        paths.removeLast();
    }
    settings.setValue("RecentFiles/paths", paths);

    rebuildRecentFilesMenu();
}

void MainWindow::updateWindowTitle() {
    const QString activeName = project_->documentName(project_->activeDocumentId());
    const QString projectLabel =
        project_->filePath().isEmpty() ? tr("Sin guardar") : QFileInfo(project_->filePath()).fileName();
    // El marcador literal "[*]" es sustituido por Qt segun
    // QWidget::isWindowModified() (ver refreshWindowModified()); no hace
    // falta armar el texto "Titulo *" a mano aqui.
    setWindowTitle(tr("%1 - %2[*] - DigitalForge").arg(activeName, projectLabel));
}

void MainWindow::refreshWindowModified() {
    setWindowModified(project_->hasUnsavedChanges() || recoveredUnsavedContent_);
}

bool MainWindow::maybeSaveChanges(bool showProgressWhileSaving) {
    if (!project_->hasUnsavedChanges() && !recoveredUnsavedContent_) {
        return true;
    }
    const auto result =
        QMessageBox::question(this, tr("Cambios sin guardar"),
                               tr("El proyecto tiene cambios sin guardar. Guardarlos antes de continuar?"),
                               QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (result == QMessageBox::Cancel) {
        return false;
    }
    if (result == QMessageBox::Discard) {
        return true;
    }
    if (showProgressWhileSaving) {
        QProgressDialog progress(tr("Guardando..."), QString(), 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setCancelButton(nullptr);
        progress.show();
        QApplication::processEvents();
        onSave();
        progress.close();
    } else {
        onSave();
    }
    // onSave() -> onSaveAs() puede terminar en un dialogo de archivo
    // cancelado, o saveToPath() puede fallar; en ambos casos sigue habiendo
    // cambios sin guardar (o recoveredUnsavedContent_ sigue en true) y no
    // debe continuarse con la operacion que llamo a esto.
    return !project_->hasUnsavedChanges() && !recoveredUnsavedContent_;
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (maybeSaveChanges(/*showProgressWhileSaving=*/true)) {
        // Mismo criterio que arriba: el resto de este cierre (borrar el
        // autoguardado, persistir geometria/preferencias) no muestra ningun
        // dialogo propio, asi que tambien queda cubierto por un indicador
        // breve en vez de trabajar en silencio.
        QProgressDialog progress(tr("Cerrando DigitalForge..."), QString(), 0, 0, this);
        progress.setWindowModality(Qt::WindowModal);
        progress.setCancelButton(nullptr);
        progress.show();
        QApplication::processEvents();
        // El autoguardado solo tiene sentido como red de seguridad ante una
        // salida *no* segura (crash, cierre forzado); si llegamos hasta aca
        // es porque el cierre fue seguro (el usuario confirmo guardar o
        // descartar), asi que se borra para que la proxima vez que se abra
        // la app no pregunte por recuperar algo que ya se resolvio.
        QFile::remove(autosavePath_);

        // Recordar tamano/posicion de ventana y que paneles quedaron
        // auto-ocultos para la proxima sesion (ver restoreGeometry() en el
        // constructor y la restauracion por nombre en setupDocks()).
        QStringList autoHiddenNames;
        for (QDockWidget* dock : autoHidden_) {
            autoHiddenNames << dock->objectName();
        }
        // saveState() no puede registrar donde vive un panel que el auto-hide
        // saco de la ventana (removeDockWidget): se los devuelve al layout
        // antes de capturar, para que el blob guarde a que area y con que
        // vecinos volveria cada uno al pinearlo. Cuales quedan colapsados se
        // sigue guardando aparte, por nombre. Copiado a un vector porque
        // setDockAutoHidden() modifica autoHidden_ mientras se itera.
        const std::vector<QDockWidget*> toRepin(autoHidden_.begin(), autoHidden_.end());
        for (QDockWidget* dock : toRepin) {
            setDockAutoHidden(dock, false);
        }

        QSettings windowSettings;
        windowSettings.setValue("MainWindow/geometry", saveGeometry());
        windowSettings.setValue("MainWindow/state", saveState());
        windowSettings.setValue("MainWindow/autoHiddenDocks", autoHiddenNames);
        settings_.save();

        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    // La geometria del flyout se calcula una vez al desplegarlo (ver
    // showFlyout()) a partir de centralWidget()->geometry() - mas simple
    // colapsarlo que recalcularla en cada resize.
    collapseFlyout();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (flyoutDock_ != nullptr && event->type() == QEvent::MouseButtonPress) {
        // Un editor de propiedad Enum/Color abre un QComboBox/QColorDialog -
        // ambos son ventanas de nivel superior propias, ajenas a la jerarquia
        // de flyoutHost_ (isAncestorOf() de mas abajo no los alcanza pese a
        // que "pertenecen" al panel). Sin este chequeo, el primer clic para
        // elegir un item del combo o un color del dialogo se interpretaba
        // como "clic afuera" y colapsaba el flyout antes de que el propio
        // combo/dialogo llegara a procesarlo (el bug reportado: "si no esta
        // anclado no puedo editar porque pierde el foco y se oculta").
        // Mientras cualquiera de los dos siga activo, se omite el chequeo
        // por completo.
        if (QApplication::activePopupWidget() == nullptr && QApplication::activeModalWidget() == nullptr) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            QWidget* clicked = QApplication::widgetAt(mouseEvent->globalPosition().toPoint());
            // El contenido visible durante el flyout (titleBarWidget()/widget(),
            // incluido el boton de pin) vive en flyoutHost_, no en flyoutDock_
            // (que se queda vacio mientras dura el flyout - ver showFlyout()).
            // Comparar contra flyoutDock_ aca hacia que un click en el propio
            // boton de pin del flyout se interpretara como "click afuera" y lo
            // colapsara antes de que el boton llegara a procesar el click,
            // impidiendo volver a anclarlo.
            if (clicked == nullptr || !flyoutHost_->isAncestorOf(clicked)) {
                collapseFlyout();
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onDocumentAdded(uint32_t id) {
    auto* scene = new CircuitScene(project_->document(id), project_->undoStack(id), this);
    connect(scene, &CircuitScene::selectionChanged, this, &MainWindow::onSceneSelectionChanged);
    connect(scene, &CircuitScene::componentContextMenuRequested, this, &MainWindow::onComponentContextMenuRequested);
    // Valor por defecto configurado en Preferencias - no afecta documentos ya
    // abiertos, solo el que se acaba de crear (ver PreferencesDialog.hpp).
    scene->setGridVisible(settings_.defaultGridVisible);
    scene->setSnapToGridEnabled(settings_.defaultSnapToGrid);
    scenes_[id] = scene;
    // documentTabBar_ todavia no existe la primera vez que se llama (el
    // documento anonimo inicial se agrega en el constructor, antes de
    // setupCentralWidgets()) - esa pestana se agrega a mano ahi.
    if (documentTabBar_ != nullptr) {
        addDocumentTab(id);
    }
}

void MainWindow::onDocumentAboutToBeRemoved(uint32_t id) {
    if (documentTabBar_ != nullptr) {
        const int tabIndex = tabIndexForDocument(id);
        if (tabIndex >= 0) {
            documentTabBar_->removeTab(tabIndex);
        }
    }
    const auto it = scenes_.find(id);
    if (it == scenes_.end()) {
        return;
    }
    if (view_ != nullptr && view_->scene() == it->second) {
        view_->setScene(nullptr);
    }
    delete it->second;
    scenes_.erase(it);
}

void MainWindow::onActiveDocumentChanged(uint32_t id) {
    // Esta senal solo se dispara para circuitos (ver el comentario de
    // Project::activeDocumentId()/karnaughDocumentAdded en Project.hpp) -
    // siempre es correcto volver a mostrar view_ en la pila central aca,
    // sin importar que pestana estuviera al frente antes.
    if (centralStack_ != nullptr && view_ != nullptr) {
        centralStack_->setCurrentWidget(view_);
    }
    if (view_ != nullptr) {
        view_->setScene(scenes_.at(id));
        bindActiveSceneViewActions();
    }
    if (miniMap_ != nullptr && view_ != nullptr) {
        miniMap_->setTarget(scenes_.at(id), view_);
    }
    if (inspector_ != nullptr) {
        inspector_->setDocument(project_->document(id));
        inspector_->setUndoStack(project_->undoStack(id));
    }
    if (simulationToolbar_ != nullptr) {
        simulationToolbar_->setDocument(project_->document(id));
    }
    if (truthTablePanel_ != nullptr) {
        truthTablePanel_->setDocument(project_->document(id));
    }
    if (waveformPanel_ != nullptr) {
        waveformPanel_->setDocument(project_->document(id));
    }
    if (documentTabBar_ != nullptr) {
        // Puede no tener pestana todavia (se lo activo desde
        // ui::ProjectTree estando "cerrado") - addDocumentTab() la crea y
        // selecciona; si ya la tenia, solo hace falta seleccionarla (con
        // signal blocker: onDocumentTabChanged ya haria exactamente este
        // mismo setActiveDocument(), es redundante pero inofensivo evitarlo).
        const int tabIndex = tabIndexForDocument(id);
        if (tabIndex < 0) {
            addDocumentTab(id);
        } else if (documentTabBar_->currentIndex() != tabIndex) {
            const QSignalBlocker blocker(documentTabBar_);
            documentTabBar_->setCurrentIndex(tabIndex);
        }
    }
    bindActiveDocumentSignals();
    updateWindowTitle();
    refreshWindowModified();
    updateStatusBar();
}

void MainWindow::onDocumentRenamed(uint32_t id) {
    if (documentTabBar_ == nullptr) {
        return;
    }
    const int tabIndex = tabIndexForDocument(id);
    if (tabIndex >= 0) {
        documentTabBar_->setTabText(tabIndex, project_->documentName(id));
    }
}

void MainWindow::onKarnaughDocumentAdded(uint32_t id) {
    auto* view = new ui::KarnaughMapView(project_.get(), id, project_->karnaughDocument(id), this);
    karnaughViews_[id] = view;
    if (centralStack_ != nullptr) {
        centralStack_->addWidget(view);
    }
    if (documentTabBar_ != nullptr) {
        addKarnaughDocumentTab(id);
    }
}

void MainWindow::onKarnaughDocumentAboutToBeRemoved(uint32_t id) {
    if (documentTabBar_ != nullptr) {
        const int tabIndex = tabIndexForDocument(id);
        if (tabIndex >= 0) {
            documentTabBar_->removeTab(tabIndex);
        }
    }
    const auto it = karnaughViews_.find(id);
    if (it == karnaughViews_.end()) {
        return;
    }
    if (centralStack_ != nullptr && centralStack_->currentWidget() == it->second) {
        // Sin esto, quitar la pagina actual dejaria a centralStack_ sin
        // ninguna pagina visible (a diferencia de un QTabWidget, un
        // QStackedWidget no elige automaticamente otra por su cuenta).
        centralStack_->setCurrentWidget(view_);
    }
    if (centralStack_ != nullptr) {
        centralStack_->removeWidget(it->second);
    }
    delete it->second;
    karnaughViews_.erase(it);
}

void MainWindow::onKarnaughDocumentRenamed(uint32_t id) {
    if (documentTabBar_ == nullptr) {
        return;
    }
    const int tabIndex = tabIndexForDocument(id);
    if (tabIndex >= 0) {
        documentTabBar_->setTabText(tabIndex, project_->karnaughDocumentName(id));
    }
}

void MainWindow::onKarnaughDocumentActivationRequested(uint32_t id) { addKarnaughDocumentTab(id); }

void MainWindow::onTruthTableDocumentAdded(uint32_t id) {
    auto* view = new ui::TruthTableView(project_.get(), id, project_->truthTableDocument(id), this);
    truthTableViews_[id] = view;
    if (centralStack_ != nullptr) {
        centralStack_->addWidget(view);
    }
    if (documentTabBar_ != nullptr) {
        addTruthTableDocumentTab(id);
    }
}

void MainWindow::onTruthTableDocumentAboutToBeRemoved(uint32_t id) {
    if (documentTabBar_ != nullptr) {
        const int tabIndex = tabIndexForDocument(id);
        if (tabIndex >= 0) {
            documentTabBar_->removeTab(tabIndex);
        }
    }
    const auto it = truthTableViews_.find(id);
    if (it == truthTableViews_.end()) {
        return;
    }
    if (centralStack_ != nullptr && centralStack_->currentWidget() == it->second) {
        centralStack_->setCurrentWidget(view_);
    }
    if (centralStack_ != nullptr) {
        centralStack_->removeWidget(it->second);
    }
    delete it->second;
    truthTableViews_.erase(it);
}

void MainWindow::onTruthTableDocumentRenamed(uint32_t id) {
    if (documentTabBar_ == nullptr) {
        return;
    }
    const int tabIndex = tabIndexForDocument(id);
    if (tabIndex >= 0) {
        documentTabBar_->setTabText(tabIndex, project_->truthTableDocumentName(id));
    }
}

void MainWindow::onTruthTableDocumentActivationRequested(uint32_t id) { addTruthTableDocumentTab(id); }

void MainWindow::onDocumentTabChanged(int index) {
    if (index < 0) {
        return; // se cerro la ultima pestana visible - ver onDocumentTabCloseRequested()
    }
    const uint32_t id = documentTabBar_->tabData(index).toUInt();
    // documentKind() lanza para un id desconocido. Este slot puede dispararse
    // en medio de una cascada de senales de Project (p. ej. removeTab() de
    // otra pestana reasignando "la actual" mientras newProject()/
    // loadFromFile() todavia estan vaciando documents_/karnaughDocuments_/
    // truthTableDocuments_ una coleccion a la vez) -- en ese instante
    // transitorio el id de la pestana que Qt elija como nueva actual puede no
    // estar en NINGUNA coleccion todavia. No hay nada util que hacer con una
    // pestana en ese estado; se ignora en vez de crashear toda la aplicacion.
    try {
        switch (project_->documentKind(id)) {
            case editor::Project::DocumentKind::Karnaugh:
                if (centralStack_ != nullptr) {
                    centralStack_->setCurrentWidget(karnaughViews_.at(id));
                }
                setCircuitEditingEnabled(false);
                return;
            case editor::Project::DocumentKind::TruthTable:
                if (centralStack_ != nullptr) {
                    centralStack_->setCurrentWidget(truthTableViews_.at(id));
                }
                setCircuitEditingEnabled(false);
                return;
            case editor::Project::DocumentKind::Circuit:
                setCircuitEditingEnabled(true);
                project_->setActiveDocument(id);
                return;
        }
    } catch (const std::exception&) {
        return;
    }
}

void MainWindow::onDocumentTabCloseRequested(int index) {
    // Cerrar una pestana nunca saca al documento del proyecto (eso solo lo
    // hace ui::ProjectTree) - siempre tiene que quedar al menos una
    // pestana abierta, ya que view_/inspector_/etc. siempre necesitan un
    // documento activo que mostrar.
    if (documentTabBar_->count() <= 1) {
        return;
    }
    documentTabBar_->removeTab(index);
}

void MainWindow::addDocumentTab(uint32_t id) {
    const int existing = tabIndexForDocument(id);
    if (existing >= 0) {
        documentTabBar_->setCurrentIndex(existing);
        return;
    }
    const int index = documentTabBar_->addTab(project_->documentName(id));
    documentTabBar_->setTabData(index, id);
    documentTabBar_->setCurrentIndex(index);
}

void MainWindow::addKarnaughDocumentTab(uint32_t id) {
    const int existing = tabIndexForDocument(id);
    if (existing >= 0) {
        documentTabBar_->setCurrentIndex(existing);
        return;
    }
    const int index = documentTabBar_->addTab(project_->karnaughDocumentName(id));
    documentTabBar_->setTabData(index, id);
    documentTabBar_->setCurrentIndex(index);
}

void MainWindow::addTruthTableDocumentTab(uint32_t id) {
    const int existing = tabIndexForDocument(id);
    if (existing >= 0) {
        documentTabBar_->setCurrentIndex(existing);
        return;
    }
    const int index = documentTabBar_->addTab(project_->truthTableDocumentName(id));
    documentTabBar_->setTabData(index, id);
    documentTabBar_->setCurrentIndex(index);
}

void MainWindow::setCircuitEditingEnabled(bool enabled) {
    if (editMenu_ != nullptr) {
        editMenu_->setEnabled(enabled);
        // QMenu::setEnabled() solo grisa la entrada en la barra de menus -
        // los atajos de teclado (Supr, R, Ctrl+C/X/V, Ctrl+Z/Y) de cada
        // QAction hija siguen activos con su contexto Qt::WindowShortcut de
        // costumbre, sin importar el enabled del QMenu contenedor. Sin este
        // loop, Supr con una pestana de Karnaugh al frente seguia llamando a
        // activeScene()->deleteSelected() sobre el circuito oculto detras.
        for (QAction* action : editMenu_->actions()) {
            action->setEnabled(enabled);
        }
    }
    if (gridAction_ != nullptr) {
        gridAction_->setEnabled(enabled);
    }
    if (snapAction_ != nullptr) {
        snapAction_->setEnabled(enabled);
    }
}

int MainWindow::tabIndexForDocument(uint32_t id) const {
    for (int i = 0; i < documentTabBar_->count(); ++i) {
        if (documentTabBar_->tabData(i).toUInt() == id) {
            return i;
        }
    }
    return -1;
}

void MainWindow::bindActiveDocumentSignals() {
    disconnect(simulationRebuiltConnection_);
    disconnect(simulationSteppedConnection_);
    disconnect(liveSimulationChangedConnection_);
    disconnect(editBlockedConnection_);

    CircuitDocument* document = project_->activeDocument();
    simulationRebuiltConnection_ = connect(document, &CircuitDocument::simulationRebuilt, this, &MainWindow::updateStatusBar);
    simulationSteppedConnection_ = connect(document, &CircuitDocument::simulationStepped, this, &MainWindow::updateStatusBar);
    liveSimulationChangedConnection_ =
        connect(document, &CircuitDocument::liveSimulationChanged, this, &MainWindow::updateStatusBar);
    editBlockedConnection_ = connect(document, &CircuitDocument::editBlocked, this,
                                      [this](const QString& reason) { statusBar()->showMessage(reason, 4000); });
}

void MainWindow::bindActiveSceneViewActions() {
    if (gridAction_ == nullptr || snapAction_ == nullptr) {
        return;
    }
    CircuitScene* scene = activeScene();
    // gridAction_/snapAction_ solo se conectan a la escena activa - un
    // desconectado general de su senal toggled (sin importar el receptor) es
    // seguro aca precisamente porque nada mas escucha esa senal.
    disconnect(gridAction_, &QAction::toggled, nullptr, nullptr);
    disconnect(snapAction_, &QAction::toggled, nullptr, nullptr);
    {
        const QSignalBlocker blocker(gridAction_);
        gridAction_->setChecked(scene->gridVisible());
    }
    {
        const QSignalBlocker blocker(snapAction_);
        snapAction_->setChecked(scene->snapToGridEnabled());
    }
    connect(gridAction_, &QAction::toggled, scene, &CircuitScene::setGridVisible);
    connect(snapAction_, &QAction::toggled, scene, &CircuitScene::setSnapToGridEnabled);
}

CircuitScene* MainWindow::activeScene() const { return scenes_.at(project_->activeDocumentId()); }

void MainWindow::updateStatusBar() {
    const CircuitDocument* doc = project_->activeDocument();
    if (doc->oscillationDetected()) {
        simulationStateLabel_->setText(tr("● Oscilacion detectada"));
        simulationStateLabel_->setStyleSheet("font-weight: bold; padding: 0 6px; color: #cc3333;");
    } else if (doc->isLiveSimulation()) {
        simulationStateLabel_->setText(tr("▶ Ejecutando"));
        simulationStateLabel_->setStyleSheet("font-weight: bold; padding: 0 6px; color: #2fa84f;");
    } else {
        simulationStateLabel_->setText(tr("⏸ Edicion (pausada)"));
        simulationStateLabel_->setStyleSheet("font-weight: bold; padding: 0 6px; color: #808080;");
    }
}

} // namespace digitalforge::app

#include "MainWindow.moc"
