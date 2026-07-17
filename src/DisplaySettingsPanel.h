// Display-setting controls -- the C++ analog of dd_molview's
// `desktop/controls.py::DisplaySettingsPanel`. Same controls, same
// defaults.
#pragma once

#include <QWidget>

#include "PythonBridge.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;

class DisplaySettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit DisplaySettingsPanel(QWidget* parent = nullptr);

    DisplaySettings currentSettings() const;
    bool showContactResidues() const;
    double contactCutoff() const;

    // Enables/disables "Show reference ligand" (and unchecks it when
    // disabled) -- called whenever the loaded reference ligand changes.
    void setReferenceAvailable(bool available);

signals:
    void settingsChanged();
    void centerOnLigandRequested();

private:
    void buildLayout();
    void wireSignals();
    void updateCutoffLabel(int sliderValue);

    QPushButton* centerOnLigandButton_;
    QComboBox* receptorStyleCombo_;
    QCheckBox* colorBySSCheck_;
    QCheckBox* onlyNearLigandCheck_;
    QCheckBox* showHbondsCheck_;
    QCheckBox* showHydrophobicCheck_;
    QCheckBox* showSaltBridgesCheck_;
    QCheckBox* showElectrostaticCheck_;
    QCheckBox* showPiStackingCheck_;
    QCheckBox* showPiHalogenCheck_;
    QCheckBox* showSulfurHalogenCheck_;
    QCheckBox* showContactResiduesCheck_;
    QSlider* contactCutoffSlider_;
    QLabel* contactCutoffLabel_;
    QCheckBox* showReferenceCheck_;
};
