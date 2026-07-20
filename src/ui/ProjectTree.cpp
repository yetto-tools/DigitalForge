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
    connect(project_, &editor::Project::documentAboutToBeRemoved, this, [this](uint32_t) { repopulate(); });
    connect(project_, &editor::Project::documentRenamed, this, [this](uint32_t) { repopulate(); });
    connect(project_, &editor::Project::activeDocumentChanged, this, &ProjectTree::refreshActiveHighlight);
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
    }
    return QWidget::eventFilter(watched, event);
}

void ProjectTree::onItemClicked(QTreeWidgetItem* item, int) {
    const QVariant idData = item->data(0, kDocumentIdRole);
    if (idData.isValid()) {
        project_->setActiveDocument(idData.toUInt());
    }
}

void ProjectTree::onContextMenuRequested(const QPoint& pos) {
    QTreeWidgetItem* item = tree_->itemAt(pos);
    QMenu menu(this);

    if (item == nullptr || item == rootItem_) {
        menu.addAction(tr("Agregar documento nuevo"), this, &ProjectTree::addNewDocument);
        menu.addAction(tr("Importar documento existente..."), this, &ProjectTree::importDocument);
    } else {
        const uint32_t id = item->data(0, kDocumentIdRole).toUInt();
        menu.addAction(tr("Renombrar"), this, [this, id, item] { renameDocument(id, item); });
        menu.addAction(tr("Exportar como..."), this, [this, id] { exportDocument(id); });
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
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Renombrar documento"), tr("Nombre:"), QLineEdit::Normal, item->text(0), &ok);
    if (ok && !name.isEmpty()) {
        try {
            project_->renameDocument(id, name);
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
        project_->removeDocument(id);
    } catch (const std::exception& error) {
        QMessageBox::warning(this, tr("No se puede quitar"), QString::fromUtf8(error.what()));
    }
}

} // namespace digitalforge::ui
