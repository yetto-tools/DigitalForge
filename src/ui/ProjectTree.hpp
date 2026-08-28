#pragma once

#include <QWidget>

#include <cstdint>

class QTreeWidget;
class QTreeWidgetItem;

namespace digitalforge::editor {
class Project;
}

namespace digitalforge::ui {

// Panel izquierdo (dock) que muestra el proyecto activo como un arbol, al
// estilo del Explorador de soluciones de Visual Studio: la raiz es el
// proyecto (o "Proyecto sin guardar"), sus hijos son los documentos
// (circuitos) que contiene. Un clic en un documento lo activa
// (editor::Project::setActiveDocument); el menu contextual permite agregar/
// importar/exportar/renombrar/quitar. No es dueno de editor::Project - solo
// lo observa y le pide operaciones.
class ProjectTree : public QWidget {
    Q_OBJECT

public:
    explicit ProjectTree(editor::Project* project, QWidget* parent = nullptr);

    // Expuesto para que MainWindow pueda ofrecer "Nuevo documento"/"Nuevo
    // mapa de Karnaugh" desde el menu Archivo, ademas del menu contextual
    // de este arbol - misma operacion, dos formas de invocarla.
    void addNewDocument();
    void addNewKarnaughDocument();
    void addNewTruthTableDocument();
    void addNewExcitationTableDocument();

signals:
    // Un mapa de Karnaugh no tiene "documento activo" en editor::Project
    // (ver el comentario junto a Project::addKarnaughDocument()) -- a
    // diferencia de un circuito, que se activa via
    // project_->setActiveDocument() directo desde onItemClicked(), un clic
    // sobre un item de mapa de Karnaugh emite esto en cambio, para que
    // MainWindow (el unico que sabe de pestanas) decida que hacer.
    void karnaughDocumentActivationRequested(uint32_t id);
    // Mirror de la de arriba, para una tabla de verdad.
    void truthTableDocumentActivationRequested(uint32_t id);
    // Mirror de las dos de arriba, para una tabla de excitacion.
    void excitationTableDocumentActivationRequested(uint32_t id);

protected:
    // Intercepta F2 sobre tree_ (instalado como event filter) para
    // renombrar el documento seleccionado, igual que el Explorador de
    // Windows/Visual Studio.
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onItemClicked(QTreeWidgetItem* item, int column);
    void onContextMenuRequested(const QPoint& pos);
    void repopulate();
    void refreshActiveHighlight(uint32_t activeId);

private:
    void importDocument();
    void exportDocument(uint32_t id);
    // Las dos de abajo actuan sobre CUALQUIERA de los cuatro tipos de
    // documento -- consultan project_->documentKind(id) internamente y
    // llaman al metodo de Project que corresponda, para que
    // onItemClicked()/el menu contextual/F2 no necesiten saber que tipo de
    // item tienen enfrente.
    void renameDocument(uint32_t id, QTreeWidgetItem* item);
    void removeDocument(uint32_t id);

    editor::Project* project_;
    QTreeWidget* tree_;
    QTreeWidgetItem* rootItem_ = nullptr;
};

} // namespace digitalforge::ui
