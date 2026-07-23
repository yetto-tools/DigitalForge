#include "PreferencesDialog.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace digitalforge::ui {

using app::AppSettings;

PreferencesDialog::PreferencesDialog(const AppSettings& current, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Preferencias"));

    autosaveIntervalSpin_ = new QSpinBox(this);
    autosaveIntervalSpin_->setRange(10, 3600);
    autosaveIntervalSpin_->setSuffix(tr(" s"));
    autosaveIntervalSpin_->setValue(current.autosaveIntervalSec);

    defaultGridCheck_ = new QCheckBox(tr("Mostrar cuadricula en documentos nuevos"), this);
    defaultGridCheck_->setChecked(current.defaultGridVisible);

    defaultSnapCheck_ = new QCheckBox(tr("Ajustar a cuadricula en documentos nuevos"), this);
    defaultSnapCheck_->setChecked(current.defaultSnapToGrid);

    // Se muestra siempre una ruta concreta (la de fabrica si no hay ninguna
    // configurada) en vez de un campo vacio: el usuario tiene que poder ver
    // donde van a parar sus proyectos sin tener que adivinarlo.
    workspaceEdit_ = new QLineEdit(current.effectiveWorkspacePath(), this);
    workspaceEdit_->setMinimumWidth(320);
    auto* browseButton = new QPushButton(tr("Examinar..."), this);
    connect(browseButton, &QPushButton::clicked, this, [this](bool) { browseForWorkspace(); });
    auto* workspaceRow = new QHBoxLayout;
    workspaceRow->setContentsMargins(0, 0, 0, 0);
    workspaceRow->addWidget(workspaceEdit_, 1);
    workspaceRow->addWidget(browseButton);

    auto* form = new QFormLayout;
    form->addRow(tr("Carpeta de trabajo:"), workspaceRow);
    form->addRow(tr("Intervalo de autoguardado:"), autosaveIntervalSpin_);
    form->addRow(defaultGridCheck_);
    form->addRow(defaultSnapCheck_);

    auto* note = new QLabel(
        tr("La carpeta de trabajo es donde se crean los proyectos nuevos y donde\n"
           "abren los dialogos de archivo.\n"
           "La cuadricula/ajuste son por documento - eso solo define el valor\n"
           "inicial de los documentos que se creen de aca en adelante."),
        this);
    note->setEnabled(false); // atenuado, es solo una aclaracion

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel |
                                              QDialogButtonBox::RestoreDefaults,
                                          this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QAbstractButton::clicked, this,
            [this](bool) { resetToDefaults(); });

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(note);
    layout->addStretch(1);
    layout->addWidget(buttons);
}

AppSettings PreferencesDialog::values() const {
    AppSettings result;
    result.autosaveIntervalSec = autosaveIntervalSpin_->value();
    result.defaultGridVisible = defaultGridCheck_->isChecked();
    result.defaultSnapToGrid = defaultSnapCheck_->isChecked();

    // Si quedo la ruta de fabrica tal cual, se guarda vacia en vez de fijarla:
    // asi sigue apuntando a la carpeta de Documentos del sistema aunque esta
    // cambie de lugar (ver AppSettings::workspacePath).
    const QString typed = QDir::fromNativeSeparators(workspaceEdit_->text().trimmed());
    result.workspacePath = (typed.isEmpty() || typed == AppSettings::defaultWorkspacePath()) ? QString() : typed;
    return result;
}

void PreferencesDialog::resetToDefaults() {
    const AppSettings factory;
    autosaveIntervalSpin_->setValue(factory.autosaveIntervalSec);
    defaultGridCheck_->setChecked(factory.defaultGridVisible);
    defaultSnapCheck_->setChecked(factory.defaultSnapToGrid);
    workspaceEdit_->setText(factory.effectiveWorkspacePath());
    emit panelsResetRequested();
}

void PreferencesDialog::browseForWorkspace() {
    const QString chosen = QFileDialog::getExistingDirectory(this, tr("Elegir carpeta de trabajo"),
                                                              workspaceEdit_->text());
    if (!chosen.isEmpty()) {
        workspaceEdit_->setText(QDir::toNativeSeparators(chosen));
    }
}

} // namespace digitalforge::ui
