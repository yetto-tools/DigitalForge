#include "ComponentPalette.hpp"

#include <QApplication>
#include <QDrag>
#include <QLineEdit>
#include <QMimeData>
#include <QStyleHints>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <vector>

#include "IconFactory.hpp"
#include "components/ComponentDefinition.hpp"

namespace digitalforge::ui {

using components::ComponentCategory;

namespace {
constexpr int kTypeIdRole = Qt::UserRole + 1;

QString categoryLabel(ComponentCategory category) { return QString::fromUtf8(components::toString(category)); }

// Orden pedagogico estandar (NOT/Buffer -> AND/OR basicas -> sus
// complementos NAND/NOR -> paridad XOR/XNOR -> variantes tri-state de
// control de bus al final) en vez del alfabetico por typeId que da
// ComponentRegistry::registeredTypeIds() (un std::map<string,...> - el
// pedido explicito fue "ordenadas por orden logico de operaciones y no
// alfabetico"). Cualquier gates.* que no aparezca aca (no deberia haber
// ninguno hoy) cae al final, en el orden que ya traiga.
const std::vector<std::string>& gatesDisplayOrder() {
    static const std::vector<std::string> order = {
        "gates.not",  "gates.buffer", "gates.and",  "gates.or",  "gates.nand",
        "gates.nor",  "gates.xor",    "gates.xnor", "gates.tristateBuffer", "gates.tristateInverter",
    };
    return order;
}

// Reordena `typeIds` (ya agrupados por categoria) segun gatesDisplayOrder()
// si `category` es Gates; para el resto de las categorias no toca nada (el
// pedido fue especificamente sobre las compuertas logicas).
void applyDisplayOrder(ComponentCategory category, std::vector<std::string>& typeIds) {
    if (category != ComponentCategory::Gates) {
        return;
    }
    const std::vector<std::string>& order = gatesDisplayOrder();
    std::stable_sort(typeIds.begin(), typeIds.end(), [&order](const std::string& a, const std::string& b) {
        const auto rankOf = [&order](const std::string& id) {
            const auto it = std::find(order.begin(), order.end(), id);
            return it == order.end() ? order.size() : static_cast<std::size_t>(std::distance(order.begin(), it));
        };
        return rankOf(a) < rankOf(b);
    });
}
} // namespace

PaletteTreeWidget::PaletteTreeWidget(QWidget* parent) : QTreeWidget(parent) {
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
}

QMimeData* PaletteTreeWidget::mimeData(const QList<QTreeWidgetItem*>& items) const {
    if (items.isEmpty()) {
        return nullptr;
    }
    const QVariant typeId = items.first()->data(0, kTypeIdRole);
    if (!typeId.isValid()) {
        return nullptr; // un encabezado de categoria, no una hoja arrastrable
    }
    auto* mime = new QMimeData();
    mime->setData(kComponentDragMimeType, typeId.toString().toUtf8());
    return mime;
}

ComponentPalette::ComponentPalette(const components::ComponentRegistry& registry, QWidget* parent)
    : QWidget(parent), registry_(registry) {
    searchBox_ = new QLineEdit(this);
    searchBox_->setPlaceholderText(tr("Buscar componentes..."));

    tree_ = new PaletteTreeWidget(this);
    tree_->setHeaderHidden(true);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(searchBox_);
    layout->addWidget(tree_);
    layout->setContentsMargins(4, 4, 4, 4);

    connect(searchBox_, &QLineEdit::textChanged, this, &ComponentPalette::applyFilter);
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        const QVariant typeId = item->data(0, kTypeIdRole);
        if (typeId.isValid()) {
            emit placementRequested(typeId.toString().toStdString());
        }
    });
    connect(QApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
            [this](Qt::ColorScheme) { refreshIcons(); });

    populate();
}

void ComponentPalette::populate() {
    std::map<ComponentCategory, std::vector<std::string>> byCategory;
    for (const std::string& typeId : registry_.registeredTypeIds()) {
        byCategory[registry_.definition(typeId).category].push_back(typeId);
    }

    static constexpr ComponentCategory kCategoryOrder[] = {
        ComponentCategory::Wiring,      ComponentCategory::Gates,     ComponentCategory::Plexers,
        ComponentCategory::Arithmetic,  ComponentCategory::Memory,   ComponentCategory::Subcircuits,
        ComponentCategory::IO,          ComponentCategory::Base,     ComponentCategory::Analysis,
        ComponentCategory::Ic74LS,
    };

    for (const ComponentCategory category : kCategoryOrder) {
        const auto it = byCategory.find(category);
        if (it == byCategory.end() || it->second.empty()) {
            continue;
        }
        applyDisplayOrder(category, it->second);
        auto* categoryItem = new QTreeWidgetItem(tree_, {categoryLabel(category)});
        for (const std::string& typeId : it->second) {
            const auto& definition = registry_.definition(typeId);
            auto* leaf = new QTreeWidgetItem(categoryItem, {QString::fromStdString(definition.displayName)});
            leaf->setData(0, kTypeIdRole, QString::fromStdString(typeId));
            leaf->setToolTip(0, QString::fromStdString(definition.description));
            leaf->setIcon(0, componentIcon(definition));
        }
        categoryItem->setExpanded(true);
    }
}

void ComponentPalette::refreshIcons() {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* category = tree_->topLevelItem(i);
        for (int j = 0; j < category->childCount(); ++j) {
            QTreeWidgetItem* leaf = category->child(j);
            const QVariant typeId = leaf->data(0, kTypeIdRole);
            if (typeId.isValid()) {
                leaf->setIcon(0, componentIcon(registry_.definition(typeId.toString().toStdString())));
            }
        }
    }
}

void ComponentPalette::applyFilter(const QString& text) {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* category = tree_->topLevelItem(i);
        bool anyVisible = false;
        for (int j = 0; j < category->childCount(); ++j) {
            QTreeWidgetItem* leaf = category->child(j);
            const bool matches = text.isEmpty() || leaf->text(0).contains(text, Qt::CaseInsensitive);
            leaf->setHidden(!matches);
            anyVisible = anyVisible || matches;
        }
        category->setHidden(!anyVisible);
    }
}

} // namespace digitalforge::ui
