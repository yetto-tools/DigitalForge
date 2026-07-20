#pragma once

#include <QTreeWidget>
#include <QWidget>

#include <string>

#include "components/ComponentRegistry.hpp"
#include "editor/DragDrop.hpp"

class QLineEdit;

namespace digitalforge::ui {

// El payload de arrastre predeterminado de QTreeWidget es un formato interno
// de lista de datos de item que el lienzo no puede consumir; esta anulacion
// exporta en su lugar nuestro propio tipo mime typeId, solo para los items
// hoja (componentes).
class PaletteTreeWidget : public QTreeWidget {
    Q_OBJECT
public:
    explicit PaletteTreeWidget(QWidget* parent = nullptr);

protected:
    QMimeData* mimeData(const QList<QTreeWidgetItem*>& items) const override;
};

// Arbol del panel izquierdo con los tipos de componentes registrados,
// agrupados por categoria, con un filtro de texto. Al hacer doble clic en una
// hoja se emite placementRequested(); las hojas tambien son fuentes de
// arrastre (ver PaletteTreeWidget) para la colocacion mediante arrastrar y
// soltar.
class ComponentPalette : public QWidget {
    Q_OBJECT

public:
    explicit ComponentPalette(const components::ComponentRegistry& registry, QWidget* parent = nullptr);

signals:
    void placementRequested(const std::string& typeId);

private:
    void populate();
    void applyFilter(const QString& text);
    // Vuelve a pedir cada icono a IconFactory (que relee el palette activo)
    // cuando el usuario cambia el tema claro/oscuro del sistema en caliente.
    void refreshIcons();

    const components::ComponentRegistry& registry_;
    PaletteTreeWidget* tree_;
    QLineEdit* searchBox_;
};

} // namespace digitalforge::ui
