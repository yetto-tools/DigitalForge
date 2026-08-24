#include "ProjectTree.hpp"

#include <QFileDialog>
#include <QFont>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <stdexcept>

#include "editor/Project.hpp"

namespace digitalforge::ui {

namespace {
constexpr int kDocumentIdRole = Qt::UserRole + 1;
} // namespace

ProjectTree::ProjectTree(editor::Project* project, QWidget* parent) : QWidget(parent), project_(project) {
    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tree_);
    layout->setContentsMargins(4, 4, 4, 4);

    connect(tree_, &QTreeWidget::itemClicked, this, &ProjectTree::onItemClicked);
    connect(tree_, &QTreeWidget::customContextMenuRequested, this, &ProjectTree::onContextMenuRequested);
    tree_->installEventFilter(this);

    // Un repoblado completo del arbol ante cualquier cambio es mucho mas
    // simple que actualizar items individuales, y con la cantidad de
    // documentos esperada (unos pocos por proyecto) el costo es
    // insignificante.
    connect(project_, &editor::Project::documentAdded, this, [this](uint32_t) { repopulate(); });
    connect(project_, &editor::Project::documentRenamed, this, [this](uint32_t) { repopulate(); });
    connect(project_, &editor::Project::activeDocumentChanged, this, &ProjectTree::refreshActiveHighlight);
    connect(project_, &editor::Project::karnaughDocumentAdded, this, [this](uint32_t) { repopulate(); });
    connect(project_, &editor::Project::karnaughDocumentRenamed, this, [this](uint32_t) { repopulate(); });
    connect(project_, &editor::Project::truthTableDocumentAdded, this, [this](uint32_t) { repopulate(); });
    connect(project_, &editor::Project::truthTableDocumentRenamed, this, [this](uint32_t) { repopulate(); });
    // Los tres "AboutToBeRemoved" se emiten ANTES de borrar la entrada de
    // Project (para que MainWindow pueda limpiar su vista/pestana mientras
    // el id todavia resuelve) -- repopulate() en ese mismo instante releeria
    // documentIds()/etc. y encontraria al documento TODAVIA ahi, dejando un
    // item fantasma con un id que un instante despues ya no existe en
    // ninguna coleccion. QueuedConnection difiere el repoblado hasta que el
    // borrado real (que ocurre sincronicamente justo despues del emit)
    // efectivamente termino, asi el arbol refleja el estado final en vez de
    // uno transitorio que nunca se vuelve a corregir solo.
    connect(project_, &editor::Project::documentAboutToBeRemoved, this, [this](uint32_t) { repopulate(); },
            Qt::QueuedConnection);
    connect(project_, &editor::Project::karnaughDocumentAboutToBeRemoved, this, [this](uint32_t) { repopulate(); },
            Qt::QueuedConnection);
    connect(project_, &editor::Project::truthTableDocumentAboutToBeRemoved, this, [this](uint32_t) { repopulate(); },
            Qt::QueuedConnection);
    // El nombre/ruta del proyecto (raiz del arbol) solo cambia con
    // Guardar/Guardar como/Abrir - ninguno de esos toca un documento
    // puntual, asi que no dispara ninguna de las senales de arriba.
    connect(project_, &editor::Project::projectMetadataChanged, this, &ProjectTree::repopulate);

    repopulate();
}

void ProjectTree::repopulate() {
    tree_->clear();
    const QString projectLabel = project_->filePath().isEmpty() ? tr("Proyecto sin guardar") : project_->projectName();
    rootItem_ = new QTreeWidgetItem(tree_, {projectLabel});

    for (const uint32_t id : project_->documentIds()) {
        auto* item = new QTreeWidgetItem(rootItem_, {project_->documentName(id)});
        item->setData(0, kDocumentIdRole, id);
    }
    for (const uint32_t id : project_->karnaughDocumentIds()) {
        auto* item = new QTreeWidgetItem(rootItem_, {tr("%1 (Karnaugh)").arg(project_->karnaughDocumentName(id))});
        item->setData(0, kDocumentIdRole, id);
    }
    for (const uint32_t id : project_->truthTableDocumentIds()) {
        auto* item =
            new QTreeWidgetItem(rootItem_, {tr("%1 (Tabla de verdad)").arg(project_->truthTableDocumentName(id))});
        item->setData(0, kDocumentIdRole, id);
    }
    rootItem_->setExpanded(true);
    refreshActiveHighlight(project_->activeDocumentId());
}

void ProjectTree::refreshActiveHighlight(uint32_t activeId) {
    if (rootItem_ == nullptr) {
        return;
    }
    for (int i = 0; i < rootItem_->childCount(); ++i) {
        QTreeWidgetItem* child = rootItem_->child(i);
        const bool isActive = child->data(0, kDocumentIdRole).toUInt() == activeId;
        QFont font = child->font(0);
        font.setBold(isActive);
        child->setFont(0, font);
    }
}

bool ProjectTree::eventFilter(QObject* watched, QEvent* event) {
    if (watched == tree_ && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_F2) {
            QTreeWidgetItem* item = tree_->currentItem();
            if (item != nullptr && item != rootItem_) {
                const QVariant idData = item->data(0, kDocumentIdRole);
                if (idData.isValid()) {
                    renameDocument(idData.toUInt(), item);
                    return true;
                }
            }
        }
        // Mismo gesto que el Explorador de Windows/Visual Studio: Supr sobre
        // el item seleccionado dispara el mismo "Quitar del proyecto" (con
        // su propia confirmacion, ver removeDocument()) que ya ofrece el
        // menu contextual -- una forma mas de invocar la misma operacion.
        if (keyEvent->key() == Qt::Key_Delete) {
            QTreeWidgetItem* item = tree_->currentItem();
            if (item != nullptr && item != rootItem_) {
                const QVariant idData = item->data(0, kDocumentIdRole);
                if (idData.isValid()) {
                    removeDocument(idData.toUInt());
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ProjectTree::onItemClicked(QTreeWidgetItem* item, int) {
    const QVariant idData = item->data(0, kDocumentIdRole);
    if (!idData.isValid()) {
        return;
    }
    const uint32_t id = idData.toUInt();
    // Un mapa de Karnaugh nunca es "el documento activo" de Project (ver
    // el comentario de karnaughDocumentActivationRequested en el .hpp) --
    // setActiveDocument() ademas no haria nada util con este id: solo
    // busca en documents_, asi que llamarlo aca para un id de Karnaugh
    // seria un no-op silencioso sin abrir ninguna pestana.
    try {
        switch (project_->documentKind(id)) {
            case editor::Project::DocumentKind::Karnaugh:
                emit karnaughDocumentActivationRequested(id);
                return;
            case editor::Project::DocumentKind::TruthTable:
                emit truthTableDocumentActivationRequested(id);
                return;
            case editor::Project::DocumentKind::Circuit:
                project_->setActiveDocument(id);
                return;
        }
    } catch (const std::exception&) {
        // documentKind() lanza para un id desconocido -- el item que
        // disparo el clic puede quedar temporalmente huerfano si otro
        // gesto (p. ej. "Quitar del proyecto") ya lo elimino y repopulate()
        // todavia no termino de reconstruir el arbol. Sin efecto en vez de
        // crashear toda la aplicacion.
    }
}

void ProjectTree::onContextMenuRequested(const QPoint& pos) {
    QTreeWidgetItem* item = tree_->itemAt(pos);
    QMenu menu(this);

    if (item == nullptr || item == rootItem_) {
        menu.addAction(tr("Agregar documento nuevo"), this, &ProjectTree::addNewDocument);
        menu.addAction(tr("Agregar mapa de Karnaugh nuevo"), this, &ProjectTree::addNewKarnaughDocument);
        menu.addAction(tr("Agregar tabla de verdad nueva"), this, &ProjectTree::addNewTruthTableDocument);
        menu.addAction(tr("Importar documento existente..."), this, &ProjectTree::importDocument);
    } else {
        const uint32_t id = item->data(0, kDocumentIdRole).toUInt();
        bool isCircuit = false;
        try {
            isCircuit = project_->documentKind(id) == editor::Project::DocumentKind::Circuit;
        } catch (const std::exception&) {
            // documentKind() lanza para un id desconocido -- el item bajo el
            // cursor puede quedar temporalmente huerfano si el arbol se
            // repoblo justo antes de este clic derecho. Sin menu en vez de
            // crashear toda la aplicacion.
            return;
        }
        menu.addAction(tr("Renombrar"), this, [this, id, item] { renameDocument(id, item); });
        if (isCircuit) {
            menu.addAction(tr("Exportar como..."), this, [this, id] { exportDocument(id); });
        }
        menu.addSeparator();
        // Tambien disponible aca (no solo con el clic derecho sobre espacio
        // vacio): importar un documento no depende de que item este
        // seleccionado, y tenerlo solo en un lugar obligaba a "deseleccionar"
        // primero para encontrarlo.
        menu.addAction(tr("Importar documento existente..."), this, &ProjectTree::importDocument);
        menu.addSeparator();
        menu.addAction(tr("Quitar del proyecto"), this, [this, id] { removeDocument(id); });
    }

    menu.exec(tree_->viewport()->mapToGlobal(pos));
}

void ProjectTree::addNewDocument() {
    // Un default literal "Documento" chocaba con el nombre del primer
    // documento de cualquier proyecto nuevo (tambien "Documento") - aceptar
    // la sugerencia tal cual, sin escribir nada, ahora rechazaba el
    // agregado por nombre duplicado, dando la falsa impresion de que el
    // proyecto no admitia mas de un documento.
    const QString suggestedName = project_->suggestUniqueDocumentName(tr("Documento"));
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Nuevo documento"), tr("Nombre:"), QLineEdit::Normal, suggestedName, &ok);
    if (ok && !name.isEmpty()) {
        try {
            project_->addDocument(name);
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("No se pudo agregar"), QString::fromUtf8(error.what()));
        }
    }
}

void ProjectTree::addNewKarnaughDocument() {
    const QString suggestedName = project_->suggestUniqueDocumentName(tr("Karnaugh"));
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nuevo mapa de Karnaugh"), tr("Nombre:"), QLineEdit::Normal,
                                                suggestedName, &ok);
    if (ok && !name.isEmpty()) {
        try {
            project_->addKarnaughDocument(name);
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("No se pudo agregar"), QString::fromUtf8(error.what()));
        }
    }
}

void ProjectTree::addNewTruthTableDocument() {
    const QString suggestedName = project_->suggestUniqueDocumentName(tr("Tabla de verdad"));
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Nueva tabla de verdad"), tr("Nombre:"), QLineEdit::Normal,
                                                suggestedName, &ok);
    if (ok && !name.isEmpty()) {
        try {
            project_->addTruthTableDocument(name);
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("No se pudo agregar"), QString::fromUtf8(error.what()));
        }
    }
}

void ProjectTree::importDocument() {
    const QString path =
        QFileDialog::getOpenFileName(this, tr("Importar documento"), QString(), tr("Documentos DigitalForge (*.dfc)"));
    if (path.isEmpty()) {
        return;
    }
    try {
        project_->importDocument(path);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Error al importar"), QString::fromUtf8(error.what()));
    }
}

void ProjectTree::exportDocument(uint32_t id) {
    const QString path = QFileDialog::getSaveFileName(this, tr("Exportar documento como"), QString(),
                                                        tr("Documentos DigitalForge (*.dfc)"));
    if (path.isEmpty()) {
        return;
    }
    try {
        project_->exportDocument(id, path);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("Error al exportar"), QString::fromUtf8(error.what()));
    }
}

void ProjectTree::renameDocument(uint32_t id, QTreeWidgetItem* item) {
    const editor::Project::DocumentKind kind = project_->documentKind(id);
    // item->text(0) para un item de Karnaugh/tabla de verdad incluye el
    // sufijo " (Karnaugh)"/" (Tabla de verdad)" agregado en repopulate() --
    // se usa el nombre real en cambio para que el dialogo arranque
    // mostrandolo sin ese sufijo colandose como si fuera parte del nombre.
    QString currentName;
    switch (kind) {
        case editor::Project::DocumentKind::Circuit:
            currentName = item->text(0);
            break;
        case editor::Project::DocumentKind::Karnaugh:
            currentName = project_->karnaughDocumentName(id);
            break;
        case editor::Project::DocumentKind::TruthTable:
            currentName = project_->truthTableDocumentName(id);
            break;
    }
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Renombrar documento"), tr("Nombre:"), QLineEdit::Normal, currentName, &ok);
    if (ok && !name.isEmpty()) {
        try {
            switch (kind) {
                case editor::Project::DocumentKind::Circuit:
                    project_->renameDocument(id, name);
                    break;
                case editor::Project::DocumentKind::Karnaugh:
                    project_->renameKarnaughDocument(id, name);
                    break;
                case editor::Project::DocumentKind::TruthTable:
                    project_->renameTruthTableDocument(id, name);
                    break;
            }
        } catch (const std::exception& error) {
            QMessageBox::warning(this, tr("No se pudo renombrar"), QString::fromUtf8(error.what()));
        }
    }
}

void ProjectTree::removeDocument(uint32_t id) {
    const auto result = QMessageBox::question(
        this, tr("Quitar del proyecto"), tr("Quitar este documento del proyecto? El archivo en disco no se borra."));
    if (result != QMessageBox::Yes) {
        return;
    }
    try {
        switch (project_->documentKind(id)) {
            case editor::Project::DocumentKind::Circuit:
                project_->removeDocument(id);
                break;
            case editor::Project::DocumentKind::Karnaugh:
                project_->removeKarnaughDocument(id);
                break;
            case editor::Project::DocumentKind::TruthTable:
                project_->removeTruthTableDocument(id);
                break;
        }
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("No se puede quitar"), QString::fromUtf8(error.what()));
    }
}

} // namespace digitalforge::ui
