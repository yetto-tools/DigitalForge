#include "PropertyInspector.hpp"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QUndoStack>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

#include "components/ComponentInstance.hpp"
#include "editor/CircuitDocument.hpp"
#include "editor/UndoCommands.hpp"
#include "ui/IconFactory.hpp"

namespace digitalforge::ui {

using components::PropertyDescriptor;
using components::PropertyType;
using components::PropertyValue;
using editor::CircuitDocument;

namespace {

// QPlainTextEdit no ofrece una senal "editingFinished" como QLineEdit; para
// confirmar el texto de `notes` (multilinea) solo cuando el usuario termina
// de editar (y no en cada tecla, lo que saturaria la pila de deshacer), se
// necesita observar cuando el widget pierde el foco.
class FocusOutPlainTextEdit : public QPlainTextEdit {
    Q_OBJECT

public:
    explicit FocusOutPlainTextEdit(QWidget* parent = nullptr) : QPlainTextEdit(parent) {}

signals:
    void focusLost();

protected:
    void focusOutEvent(QFocusEvent* event) override {
        QPlainTextEdit::focusOutEvent(event);
        emit focusLost();
    }
};

const QString kMixedPlaceholder = QObject::tr("(varios)");

// Descriptor comun a todos los componentes seleccionados, junto con el valor
// de cada uno (en el mismo orden que PropertyInspector::componentIds_, salvo
// que se hayan filtrado ids que ya no existen).
struct CommonProperty {
    PropertyDescriptor descriptor; // metadatos tomados del primer componente
    std::vector<PropertyValue> values;

    [[nodiscard]] bool mixed() const {
        return std::any_of(values.begin(), values.end(),
                            [this](const PropertyValue& v) { return v != values.front(); });
    }
};

// Interseccion de propiedades (mismo id y mismo PropertyType) presentes en
// TODAS las `instances`, sin exigir que compartan typeId - asi campos
// comunes como label/bodyColor/notes se pueden editar aunque los tipos de
// componente seleccionados sean distintos.
[[nodiscard]] std::vector<CommonProperty> intersectProperties(
    const std::vector<const components::ComponentInstance*>& instances) {
    std::vector<CommonProperty> result;
    if (instances.empty()) {
        return result;
    }

    for (const PropertyDescriptor& descriptor : instances.front()->definition().properties) {
        CommonProperty candidate;
        candidate.descriptor = descriptor;
        candidate.values.push_back(instances.front()->property(descriptor.id));

        bool presentEverywhere = true;
        for (std::size_t i = 1; i < instances.size(); ++i) {
            const auto& otherProperties = instances[i]->definition().properties;
            const auto it = std::find_if(otherProperties.begin(), otherProperties.end(),
                                          [&](const PropertyDescriptor& other) {
                                              return other.id == descriptor.id && other.type == descriptor.type;
                                          });
            if (it == otherProperties.end()) {
                presentEverywhere = false;
                break;
            }
            candidate.values.push_back(instances[i]->property(descriptor.id));
        }

        if (presentEverywhere) {
            result.push_back(std::move(candidate));
        }
    }
    return result;
}

} // namespace

PropertyInspector::PropertyInspector(editor::CircuitDocument* document, QUndoStack* undoStack, QWidget* parent)
    : QWidget(parent), document_(nullptr), undoStack_(undoStack) {
    titleLabel_ = new QLabel(tr("Sin seleccion"), this);
    titleLabel_->setStyleSheet("font-weight: bold;");

    auto* outer = new QVBoxLayout(this);
    outer->addWidget(titleLabel_);
    outer->addStretch(1); // mantiene el formulario (vacio) fijado arriba antes de cualquier seleccion

    setDocument(document);
}

void PropertyInspector::setDocument(editor::CircuitDocument* document) {
    disconnect(propertyChangedConnection_);
    disconnect(componentAboutToBeRemovedConnection_);
    document_ = document;
    propertyChangedConnection_ = connect(document_, &CircuitDocument::propertyChanged, this,
                                          &PropertyInspector::onPropertyChanged);
    componentAboutToBeRemovedConnection_ = connect(document_, &CircuitDocument::componentAboutToBeRemoved, this,
                                                    &PropertyInspector::onComponentAboutToBeRemoved);
    setSelectedComponents({});
}

void PropertyInspector::setSelectedComponents(std::vector<uint32_t> componentIds) {
    componentIds_ = std::move(componentIds);
    // Diferido: setSelectedComponents() se llama de forma sincrona desde
    // MainWindow::onSceneSelectionChanged(), que a su vez cuelga
    // directamente del manejo de mouseMoveEvent de QGraphicsScene mientras
    // se arrastra un rubber-band de seleccion - un solo gesto de arrastre
    // dispara selectionChanged() (y por lo tanto esta llamada) decenas de
    // veces por segundo. rebuildForm() borra y crea formContainer_
    // sincronicamente; hacerlo reentrantemente desde adentro del propio
    // procesamiento de eventos de mouse de la vista grafica crasheaba
    // (SIGSEGV en QWidgetPrivate::setVisible). Se pospone a la siguiente
    // vuelta del bucle de eventos, igual que ya hace onPropertyChanged() mas
    // abajo por el mismo motivo.
    QTimer::singleShot(0, this, &PropertyInspector::rebuildForm);
}

void PropertyInspector::onPropertyChanged(uint32_t componentId) {
    // Diferido: esto puede dispararse de forma sincrona desde dentro de uno
    // de los propios manejadores de senales de los widgets de propiedades
    // (p. ej. QComboBox::currentTextChanged para initialValue de
    // wiring.input), y rebuildForm() elimina formContainer_, lo que
    // destruiria ese mismo widget mientras aun esta desenrollando su propia
    // emision de senal. Reconstruir en la siguiente iteracion del bucle de
    // eventos evita el use-after-free.
    if (std::find(componentIds_.begin(), componentIds_.end(), componentId) != componentIds_.end()) {
        QTimer::singleShot(0, this, &PropertyInspector::rebuildForm);
    }
}

void PropertyInspector::onComponentAboutToBeRemoved(uint32_t componentId) {
    const auto it = std::find(componentIds_.begin(), componentIds_.end(), componentId);
    if (it != componentIds_.end()) {
        componentIds_.erase(it);
        QTimer::singleShot(0, this, &PropertyInspector::rebuildForm);
    }
}

void PropertyInspector::pushPropertyChange(const std::string& propertyId, const PropertyValue& newValue) {
    if (!document_->requireEditable(QStringLiteral("cambiar una propiedad"))) {
        // El/los widget(s) ya muestran el nuevo valor; se difiere la
        // reconstruccion (que los destruiria) en lugar de hacerlo de forma
        // sincrona desde dentro del propio manejador de senal del widget.
        QTimer::singleShot(0, this, &PropertyInspector::rebuildForm);
        return;
    }

    std::vector<uint32_t> liveIds;
    for (const uint32_t id : componentIds_) {
        if (document_->component(id) != nullptr) {
            liveIds.push_back(id);
        }
    }
    if (liveIds.empty()) {
        return;
    }

    if (liveIds.size() > 1) {
        undoStack_->beginMacro(tr("Cambiar propiedad en %1 componentes").arg(liveIds.size()));
    }
    for (const uint32_t id : liveIds) {
        const components::ComponentInstance* live = document_->component(id);
        undoStack_->push(
            new editor::ChangePropertyCommand(document_, id, propertyId, live->property(propertyId), newValue));
    }
    if (liveIds.size() > 1) {
        undoStack_->endMacro();
    }
}

void PropertyInspector::rebuildForm() {
    if (formContainer_ != nullptr) {
        delete formContainer_;
        formContainer_ = nullptr;
    }

    std::vector<const components::ComponentInstance*> instances;
    for (const uint32_t id : componentIds_) {
        if (const components::ComponentInstance* instance = document_->component(id)) {
            instances.push_back(instance);
        }
    }

    if (instances.empty()) {
        titleLabel_->setText(tr("Sin seleccion"));
        return;
    }

    if (instances.size() == 1) {
        titleLabel_->setText(QString::fromStdString(instances.front()->definition().displayName) + " (" +
                              QString::fromStdString(instances.front()->typeId()) + ")");
    } else {
        titleLabel_->setText(tr("%1 componentes seleccionados").arg(instances.size()));
    }

    formContainer_ = new QWidget(this);
    auto* sectionsLayout = new QVBoxLayout(formContainer_);
    sectionsLayout->setContentsMargins(0, 0, 0, 0);

    // Simulacion / Apariencia / General, en ese orden - derivadas de los
    // flags affectsSimulation/affectsAppearance que ya tiene cada
    // PropertyDescriptor, sin agregar un campo de schema nuevo.
    QFormLayout* sections[3] = {nullptr, nullptr, nullptr};
    const QString sectionNames[3] = {tr("Simulacion"), tr("Apariencia"), tr("General")};

    const auto sectionFor = [](const PropertyDescriptor& descriptor) -> int {
        if (descriptor.affectsSimulation) return 0;
        if (descriptor.affectsAppearance) return 1;
        return 2;
    };

    const auto formFor = [&](int index) -> QFormLayout* {
        if (sections[index] == nullptr) {
            auto* header = new QLabel(sectionNames[index], formContainer_);
            header->setStyleSheet("font-weight: bold;");
            sectionsLayout->addWidget(header);
            auto* sectionWidget = new QWidget(formContainer_);
            sections[index] = new QFormLayout(sectionWidget);
            sections[index]->setContentsMargins(0, 0, 0, 6);
            sectionsLayout->addWidget(sectionWidget);
        }
        return sections[index];
    };

    for (const CommonProperty& common : intersectProperties(instances)) {
        const PropertyDescriptor& descriptor = common.descriptor;
        const std::string propertyId = descriptor.id;
        const PropertyValue defaultValue = descriptor.defaultValue;
        const bool mixed = common.mixed();
        const PropertyValue current = common.values.front();

        QWidget* editorWidget = nullptr;

        switch (descriptor.type) {
            case PropertyType::Boolean: {
                auto* checkBox = new QCheckBox(formContainer_);
                if (mixed) {
                    checkBox->setTristate(true);
                    checkBox->setCheckState(Qt::PartiallyChecked);
                } else {
                    checkBox->setChecked(std::get<bool>(current));
                }
                connect(checkBox, &QCheckBox::checkStateChanged, this, [this, propertyId](Qt::CheckState state) {
                    if (state == Qt::PartiallyChecked) return; // aun sin una intencion clara del usuario
                    pushPropertyChange(propertyId, PropertyValue{state == Qt::Checked});
                });
                editorWidget = checkBox;
                break;
            }
            case PropertyType::Integer:
            case PropertyType::UnsignedInteger: {
                auto* spinBox = new QSpinBox(formContainer_);
                int minValue = 0;
                int maxValue = std::numeric_limits<int>::max();
                if (descriptor.type == PropertyType::Integer) {
                    if (descriptor.minValue) minValue = static_cast<int>(std::get<int64_t>(*descriptor.minValue));
                    if (descriptor.maxValue) maxValue = static_cast<int>(std::get<int64_t>(*descriptor.maxValue));
                } else {
                    if (descriptor.minValue) minValue = static_cast<int>(std::get<uint64_t>(*descriptor.minValue));
                    if (descriptor.maxValue) maxValue = static_cast<int>(std::get<uint64_t>(*descriptor.maxValue));
                }
                if (mixed) {
                    // El sentinel (minValue - 1) queda fuera del rango real
                    // de la propiedad, asi que solo se muestra cuando este
                    // widget arranca en estado "mixto" - nunca coincide con
                    // un valor legitimo de ningun componente seleccionado.
                    spinBox->setRange(minValue - 1, maxValue);
                    spinBox->setSpecialValueText(kMixedPlaceholder);
                    spinBox->setValue(minValue - 1);
                } else {
                    spinBox->setRange(minValue, maxValue);
                    const int64_t value = descriptor.type == PropertyType::Integer
                                               ? std::get<int64_t>(current)
                                               : static_cast<int64_t>(std::get<uint64_t>(current));
                    spinBox->setValue(static_cast<int>(value));
                }
                const bool isUnsigned = descriptor.type == PropertyType::UnsignedInteger;
                const int mixedSentinel = minValue - 1;
                connect(spinBox, &QSpinBox::valueChanged, this,
                        [this, propertyId, isUnsigned, mixedSentinel, mixed](int value) {
                            if (mixed && value == mixedSentinel) return; // todavia no editado por el usuario
                            const PropertyValue newValue =
                                isUnsigned ? PropertyValue{static_cast<uint64_t>(value)}
                                           : PropertyValue{static_cast<int64_t>(value)};
                            pushPropertyChange(propertyId, newValue);
                        });
                editorWidget = spinBox;
                break;
            }
            case PropertyType::String: {
                if (descriptor.multiline) {
                    auto* textEdit = new FocusOutPlainTextEdit(formContainer_);
                    textEdit->setFixedHeight(60);
                    if (mixed) {
                        textEdit->setPlaceholderText(kMixedPlaceholder);
                    } else {
                        textEdit->setPlainText(QString::fromStdString(std::get<std::string>(current)));
                    }
                    // textEdited (no aplica a QPlainTextEdit) - se rastrea la
                    // edicion real del usuario a mano para no reenviar el
                    // mismo comando cuando el foco simplemente entra y sale
                    // sin que el usuario haya tocado nada.
                    auto editedByUser = std::make_shared<bool>(false);
                    connect(textEdit, &QPlainTextEdit::textChanged, this,
                            [editedByUser]() { *editedByUser = true; });
                    connect(textEdit, &FocusOutPlainTextEdit::focusLost, this,
                            [this, propertyId, textEdit, editedByUser]() {
                                if (!*editedByUser) return;
                                pushPropertyChange(propertyId, PropertyValue{textEdit->toPlainText().toStdString()});
                            });
                    editorWidget = textEdit;
                } else {
                    auto* lineEdit = new QLineEdit(formContainer_);
                    if (mixed) {
                        lineEdit->setPlaceholderText(kMixedPlaceholder);
                    } else {
                        lineEdit->setText(QString::fromStdString(std::get<std::string>(current)));
                    }
                    auto editedByUser = std::make_shared<bool>(false);
                    connect(lineEdit, &QLineEdit::textEdited, this, [editedByUser]() { *editedByUser = true; });
                    connect(lineEdit, &QLineEdit::editingFinished, this,
                            [this, propertyId, lineEdit, editedByUser]() {
                                if (!*editedByUser) return;
                                pushPropertyChange(propertyId, PropertyValue{lineEdit->text().toStdString()});
                            });
                    editorWidget = lineEdit;
                }
                break;
            }
            case PropertyType::Enum: {
                auto* comboBox = new QComboBox(formContainer_);
                if (mixed) {
                    comboBox->addItem(kMixedPlaceholder);
                }
                for (const std::string& option : descriptor.enumOptions) {
                    comboBox->addItem(QString::fromStdString(option));
                }
                comboBox->setCurrentText(mixed ? kMixedPlaceholder : QString::fromStdString(std::get<std::string>(current)));
                connect(comboBox, &QComboBox::currentTextChanged, this, [this, propertyId](const QString& text) {
                    if (text == kMixedPlaceholder) return; // todavia no editado por el usuario
                    pushPropertyChange(propertyId, PropertyValue{text.toStdString()});
                });
                editorWidget = comboBox;
                break;
            }
            case PropertyType::Color: {
                auto* colorButton = new QPushButton(formContainer_);
                colorButton->setFixedWidth(48);
                if (mixed) {
                    colorButton->setText(kMixedPlaceholder);
                } else {
                    const QString hex = QString::fromStdString(std::get<std::string>(current));
                    colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(hex));
                }
                const std::string defaultHex = std::get<std::string>(defaultValue);
                connect(colorButton, &QPushButton::clicked, this, [this, propertyId, mixed, defaultHex]() {
                    const QString startHex =
                        mixed ? QString::fromStdString(defaultHex)
                              : QString::fromStdString(std::get<std::string>(document_->component(componentIds_.front())
                                                                                  ->property(propertyId)));
                    const QColor chosen = QColorDialog::getColor(QColor(startHex), this, tr("Elegir color"));
                    if (!chosen.isValid()) return;
                    pushPropertyChange(propertyId, PropertyValue{chosen.name(QColor::HexRgb).toStdString()});
                });
                editorWidget = colorButton;
                break;
            }
        }

        if (editorWidget == nullptr) {
            continue;
        }
        editorWidget->setToolTip(QString::fromStdString(descriptor.description));

        auto* resetButton = new QToolButton(formContainer_);
        resetButton->setIcon(icons::reset());
        resetButton->setToolTip(tr("Restablecer a valor por defecto"));
        resetButton->setAutoRaise(true);
        connect(resetButton, &QToolButton::clicked, this,
                [this, propertyId, defaultValue]() { pushPropertyChange(propertyId, defaultValue); });

        auto* row = new QWidget(formContainer_);
        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->addWidget(editorWidget, 1);
        rowLayout->addWidget(resetButton);

        formFor(sectionFor(descriptor))->addRow(QString::fromStdString(descriptor.displayName), row);
    }

    static_cast<QVBoxLayout*>(layout())->insertWidget(1, formContainer_);
}

} // namespace digitalforge::ui

#include "PropertyInspector.moc"
