#include "PreferencesDialog.hpp"

#include <QAbstractButton>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
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

    auto* form = new QFormLayout;
    form->addRow(tr("Intervalo de autoguardado:"), autosaveIntervalSpin_);
    form->addRow(defaultGridCheck_);
    form->addRow(defaultSnapCheck_);

    auto* note = new QLabel(
        tr("La cuadricula/ajuste son por documento - esto solo define el valor\n"
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
    return result;
}

void PreferencesDialog::resetToDefaults() {
    const AppSettings factory;
    autosaveIntervalSpin_->setValue(factory.autosaveIntervalSec);
    defaultGridCheck_->setChecked(factory.defaultGridVisible);
    defaultSnapCheck_->setChecked(factory.defaultSnapToGrid);
    emit panelsResetRequested();
}

} // namespace digitalforge::ui
