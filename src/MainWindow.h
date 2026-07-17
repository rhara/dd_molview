// MainWindow: the C++/Qt analog of dd_molview's
// `desktop/main_window.py::MainWindow`. Holds the display-only state the
// Python original kept on `self` (active row selections live in
// PythonBridge's Session instead -- see PythonBridge.h) and one central
// `refreshView()` method that recomputes everything downstream of it,
// called explicitly from every relevant Qt signal handler.
#pragma once

#include <QMainWindow>
#include <QVector>

#include <memory>

#include "PythonBridge.h"

class QAction;
class QCloseEvent;
class QDockWidget;
class QLabel;
class QTableView;
class QUrl;
class QWebEngineView;

class DisplaySettingsPanel;
class SequencePanel;
class TableModel;
class Viewer3D;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void loadFromPaths(const QStringList& receptorPaths, const QStringList& posesPaths,
                        const QString& referencePath, const QString& manifestPath);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onOpenReceptors();
    void onAddPoses();
    void onOpenReference();
    void onOpenManifest();
    void onCenterOnLigand();
    void onProteinSelectionChanged();
    void onLigandSelectionChanged();
    void onContactSelectionChanged();
    void onSequenceAnchorClicked(const QUrl& url);
    void nudgeView3DRepaint();

private:
    void buildWidgets();
    void buildMenuAndToolbar();
    void restoreLayout();
    void wireSignals();
    void reload();
    QDockWidget* makeDock(const QString& title, QWidget* widget, const QString& objectName);

    void refreshLigandTable();
    // `recenter`: see dd_molview's own `_refresh_view` docstring -- `true`
    // (load, and "Center on Ligand") resets the 3D camera to build_view's
    // default auto-fit; `false` (everything else: row switches, settings
    // changes, residue selection) preserves whatever camera position the
    // user last left it at.
    void refreshView(bool recenter);
    void updateDetailLabels(const ViewResult& result);

    std::unique_ptr<PythonBridge> bridge_;

    // Loaded-file state, re-sent to PythonBridge::loadAll on every reload
    // (File menu additions are cumulative, matching dd_molview-desktop's
    // own _receptor_paths/_poses_paths/_reference_path/_manifest_path).
    QStringList receptorPaths_;
    QStringList posesPaths_;
    QString referencePath_;
    QString manifestPath_;

    // The sequence-panel / contact-table's current multi-residue pick.
    // Empty means "no explicit selection" (only auto contact-cutoff
    // residues highlight), matching the Python original's `Optional[list]`
    // collapsing an empty pick back to `None`.
    QVector<ResiduePair> selectedResidues_;

    QTableView* proteinTable_ = nullptr;
    QTableView* ligandTable_ = nullptr;
    QTableView* contactTable_ = nullptr;
    TableModel* proteinModel_ = nullptr;
    TableModel* ligandModel_ = nullptr;
    TableModel* contactModel_ = nullptr;

    QWebEngineView* view3dWidget_ = nullptr;
    std::unique_ptr<Viewer3D> viewer3d_;

    SequencePanel* sequencePanel_ = nullptr;

    QLabel* scoreLabel_ = nullptr;
    QLabel* rmsdLabel_ = nullptr;
    QLabel* interactionSummaryLabel_ = nullptr;
    QWidget* detailWidget_ = nullptr;

    DisplaySettingsPanel* settingsPanel_ = nullptr;

    QDockWidget* settingsDock_ = nullptr;
    QDockWidget* proteinDock_ = nullptr;
    QDockWidget* ligandDock_ = nullptr;
    QDockWidget* view3dDock_ = nullptr;
    QDockWidget* sequenceDock_ = nullptr;
    QDockWidget* contactDock_ = nullptr;
    QList<QDockWidget*> docks_;
};
