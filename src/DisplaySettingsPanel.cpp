#include "DisplaySettingsPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace {
// Matches dd_viewer.scene.RECEPTOR_STYLES = ("cartoon", "stick", "surface").
const QStringList kReceptorStyles = {"cartoon", "stick", "surface"};
}  // namespace

DisplaySettingsPanel::DisplaySettingsPanel(QWidget* parent) : QWidget(parent) {
    centerOnLigandButton_ = new QPushButton("Center on Ligand");
    centerOnLigandButton_->setToolTip(
        "Re-fit the 3D camera to the current ligand (or receptor, if none is loaded). "
        "The only other way the camera moves is by dragging/zooming it yourself -- "
        "picking a different protein/ligand row or changing a display setting never "
        "moves it on its own.");

    receptorStyleCombo_ = new QComboBox();
    receptorStyleCombo_->addItems(kReceptorStyles);

    colorBySSCheck_ = new QCheckBox("Color by secondary structure (cartoon only)");

    showHbondsCheck_ = new QCheckBox("Show hydrogen bond candidates");
    showHbondsCheck_->setChecked(true);
    showHydrophobicCheck_ = new QCheckBox("Show hydrophobic contacts");
    showSaltBridgesCheck_ = new QCheckBox("Show salt bridges");
    showSaltBridgesCheck_->setChecked(true);
    showElectrostaticCheck_ = new QCheckBox("Show electrostatic interactions (partial-charge based)");
    showPiStackingCheck_ = new QCheckBox("Show pi-stacking");
    showPiStackingCheck_->setChecked(true);
    showPiHalogenCheck_ = new QCheckBox("Show pi-halogen bonds");
    showPiHalogenCheck_->setChecked(true);
    showSulfurHalogenCheck_ = new QCheckBox("Show sulfur-halogen bonds");
    showSulfurHalogenCheck_->setChecked(true);
    showContactResiduesCheck_ = new QCheckBox("Highlight contact residues");
    showContactResiduesCheck_->setChecked(true);
    onlyNearLigandCheck_ = new QCheckBox("Show ribbon/residues near ligand only");

    // 2.0-10.0 Å, step 0.5 -> integer ticks 0..16 mapped to 2.0 + 0.5*tick
    contactCutoffSlider_ = new QSlider(Qt::Horizontal);
    contactCutoffSlider_->setRange(0, 16);
    contactCutoffSlider_->setValue(2);  // -> 3.0
    contactCutoffLabel_ = new QLabel();
    updateCutoffLabel(contactCutoffSlider_->value());

    showReferenceCheck_ = new QCheckBox("Show reference ligand");
    showReferenceCheck_->setEnabled(false);

    buildLayout();
    wireSignals();
}

void DisplaySettingsPanel::buildLayout() {
    auto* group = new QGroupBox("Display settings");
    auto* form = new QFormLayout();
    form->addRow("Receptor style", receptorStyleCombo_);
    form->addRow(colorBySSCheck_);
    form->addRow(onlyNearLigandCheck_);
    form->addRow(showHbondsCheck_);
    form->addRow(showHydrophobicCheck_);
    form->addRow(showSaltBridgesCheck_);
    form->addRow(showElectrostaticCheck_);
    form->addRow(showPiStackingCheck_);
    form->addRow(showPiHalogenCheck_);
    form->addRow(showSulfurHalogenCheck_);
    form->addRow(showContactResiduesCheck_);

    auto* cutoffRow = new QHBoxLayout();
    cutoffRow->addWidget(contactCutoffSlider_);
    cutoffRow->addWidget(contactCutoffLabel_);
    form->addRow("Contact residue cutoff", cutoffRow);

    form->addRow(showReferenceCheck_);
    group->setLayout(form);

    auto* layout = new QVBoxLayout();
    layout->addWidget(centerOnLigandButton_);
    layout->addWidget(group);
    layout->addStretch(1);
    setLayout(layout);
}

void DisplaySettingsPanel::wireSignals() {
    connect(centerOnLigandButton_, &QPushButton::clicked, this, &DisplaySettingsPanel::centerOnLigandRequested);
    connect(receptorStyleCombo_, &QComboBox::currentIndexChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(colorBySSCheck_, &QCheckBox::checkStateChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(onlyNearLigandCheck_, &QCheckBox::checkStateChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(showHbondsCheck_, &QCheckBox::checkStateChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(showHydrophobicCheck_, &QCheckBox::checkStateChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(showSaltBridgesCheck_, &QCheckBox::checkStateChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(showElectrostaticCheck_, &QCheckBox::checkStateChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(showPiStackingCheck_, &QCheckBox::checkStateChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(showPiHalogenCheck_, &QCheckBox::checkStateChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(showSulfurHalogenCheck_, &QCheckBox::checkStateChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(showContactResiduesCheck_, &QCheckBox::checkStateChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(showReferenceCheck_, &QCheckBox::checkStateChanged, this, &DisplaySettingsPanel::settingsChanged);
    connect(contactCutoffSlider_, &QSlider::valueChanged, this, [this](int value) {
        updateCutoffLabel(value);
        emit settingsChanged();
    });
}

void DisplaySettingsPanel::updateCutoffLabel(int sliderValue) {
    contactCutoffLabel_->setText(QString::number(2.0 + 0.5 * sliderValue, 'f', 1) + " Å");
}

double DisplaySettingsPanel::contactCutoff() const {
    return 2.0 + 0.5 * contactCutoffSlider_->value();
}

bool DisplaySettingsPanel::showContactResidues() const {
    return showContactResiduesCheck_->isChecked();
}

DisplaySettings DisplaySettingsPanel::currentSettings() const {
    DisplaySettings s;
    s.receptorStyle = receptorStyleCombo_->currentText();
    s.colorBySecondaryStructure = colorBySSCheck_->isChecked();
    s.onlyNearLigand = onlyNearLigandCheck_->isChecked();
    s.showHbonds = showHbondsCheck_->isChecked();
    s.showHydrophobic = showHydrophobicCheck_->isChecked();
    s.showSaltBridges = showSaltBridgesCheck_->isChecked();
    s.showElectrostatic = showElectrostaticCheck_->isChecked();
    s.showPiStacking = showPiStackingCheck_->isChecked();
    s.showPiHalogen = showPiHalogenCheck_->isChecked();
    s.showSulfurHalogen = showSulfurHalogenCheck_->isChecked();
    s.showContactResidues = showContactResiduesCheck_->isChecked();
    s.contactCutoff = contactCutoff();
    s.showReference = showReferenceCheck_->isChecked();
    return s;
}

void DisplaySettingsPanel::setReferenceAvailable(bool available) {
    showReferenceCheck_->setEnabled(available);
    if (!available) {
        showReferenceCheck_->setChecked(false);
    }
}
