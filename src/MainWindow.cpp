#include "MainWindow.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QSignalBlocker>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEngineView>

#include "DisplaySettingsPanel.h"
#include "SequencePanel.h"
#include "TableModel.h"
#include "Viewer3D.h"

namespace {

std::vector<ResiduePair> toStdVector(const QVector<ResiduePair>& v) {
    return std::vector<ResiduePair>(v.begin(), v.end());
}

QVector<ResiduePair> tableToResidues(const TableData& table) {
    QVector<ResiduePair> residues;
    int chainCol = table.columns.indexOf("chain");
    int resnumCol = table.columns.indexOf("resnum");
    if (chainCol < 0 || resnumCol < 0) {
        return residues;
    }
    for (const auto& row : table.rawRows) {
        residues.append({row[chainCol].toString(), row[resnumCol].toInt()});
    }
    return residues;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("dd_cview");

    bridge_ = std::make_unique<PythonBridge>();

    buildWidgets();
    buildMenuAndToolbar();
    restoreLayout();
    wireSignals();
    refreshView(/*recenter=*/true);
}

MainWindow::~MainWindow() = default;

// ------------------------------------------------------------------
// Widget construction
// ------------------------------------------------------------------
void MainWindow::buildWidgets() {
    proteinModel_ = new TableModel(this);
    proteinTable_ = new QTableView();
    proteinTable_->setModel(proteinModel_);
    proteinTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    proteinTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    proteinTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    ligandModel_ = new TableModel(this);
    ligandTable_ = new QTableView();
    ligandTable_->setModel(ligandModel_);
    ligandTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    ligandTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    ligandTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    contactModel_ = new TableModel(this);
    contactTable_ = new QTableView();
    contactTable_->setModel(contactModel_);
    contactTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    contactTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    contactTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);

    view3dWidget_ = new QWebEngineView();
    viewer3d_ = std::make_unique<Viewer3D>(view3dWidget_);

    sequencePanel_ = new SequencePanel();

    scoreLabel_ = new QLabel("Score: -");
    rmsdLabel_ = new QLabel("RMSD: -");
    interactionSummaryLabel_ = new QLabel();
    interactionSummaryLabel_->setWordWrap(true);

    detailWidget_ = new QWidget();
    auto* detailLayout = new QVBoxLayout();
    auto* metricsRow = new QHBoxLayout();
    metricsRow->addWidget(scoreLabel_);
    metricsRow->addWidget(rmsdLabel_);
    metricsRow->addStretch(1);
    detailLayout->addLayout(metricsRow);
    detailLayout->addWidget(interactionSummaryLabel_);
    detailLayout->addWidget(new QLabel("Contact residues (select rows to highlight in the 3D view and sequence panel)"));
    detailLayout->addWidget(contactTable_);
    detailWidget_->setLayout(detailLayout);

    settingsPanel_ = new DisplaySettingsPanel();

    // Every panel (including the 3D view itself) is its own QDockWidget --
    // movable, floatable, closable -- rather than a fixed splitter layout,
    // so any of them can be dragged to a different edge, floated as its own
    // window, or closed/reopened (via the View menu's toggleViewAction()
    // entries) independently of the rest. setObjectName is required for
    // saveState()/restoreState() (restoreLayout/closeEvent) to correctly
    // identify which saved geometry belongs to which dock.
    setDockNestingEnabled(true);
    settingsDock_ = makeDock("Settings", settingsPanel_, "settings_dock");
    proteinDock_ = makeDock("Proteins", proteinTable_, "protein_dock");
    ligandDock_ = makeDock("Ligands", ligandTable_, "ligand_dock");
    view3dDock_ = makeDock("3D View", view3dWidget_, "view3d_dock");
    sequenceDock_ = makeDock("Chains", sequencePanel_, "sequence_dock");
    contactDock_ = makeDock("Contact residues / Interaction summary", detailWidget_, "contact_dock");
    docks_ = {settingsDock_, proteinDock_, ligandDock_, view3dDock_, sequenceDock_, contactDock_};

    // Default arrangement (freely rearrangeable afterward, and restored
    // from the last session if restoreLayout finds saved state): Settings/
    // Proteins/Ligands stacked in a left-hand column; the 3D view (the
    // dominant, largest area) with Chains split to its right; Contact
    // residues/Interaction summary along the bottom, collapsed by default.
    addDockWidget(Qt::LeftDockWidgetArea, settingsDock_);
    splitDockWidget(settingsDock_, proteinDock_, Qt::Vertical);
    splitDockWidget(proteinDock_, ligandDock_, Qt::Vertical);

    addDockWidget(Qt::RightDockWidgetArea, view3dDock_);
    splitDockWidget(view3dDock_, sequenceDock_, Qt::Horizontal);

    addDockWidget(Qt::BottomDockWidgetArea, contactDock_);
    resizeDocks({view3dDock_, sequenceDock_}, {700, 300}, Qt::Horizontal);
    contactDock_->setVisible(false);
}

QDockWidget* MainWindow::makeDock(const QString& title, QWidget* widget, const QString& objectName) {
    auto* dock = new QDockWidget(title);
    dock->setObjectName(objectName);
    dock->setWidget(widget);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    return dock;
}

void MainWindow::buildMenuAndToolbar() {
    QMenu* fileMenu = menuBar()->addMenu("&File");
    auto* fileToolbar = new QToolBar("Files");
    fileToolbar->setObjectName("files_toolbar");  // required for saveState() to persist/restore it silently
    addToolBar(fileToolbar);

    QAction* openReceptorsAction = fileMenu->addAction("Open Receptor(s)...");
    connect(openReceptorsAction, &QAction::triggered, this, &MainWindow::onOpenReceptors);
    fileToolbar->addAction(openReceptorsAction);

    QAction* addPosesAction = fileMenu->addAction("Add Poses...");
    connect(addPosesAction, &QAction::triggered, this, &MainWindow::onAddPoses);
    fileToolbar->addAction(addPosesAction);

    QAction* openReferenceAction = fileMenu->addAction("Open Reference...");
    connect(openReferenceAction, &QAction::triggered, this, &MainWindow::onOpenReference);
    fileToolbar->addAction(openReferenceAction);

    QAction* openManifestAction = fileMenu->addAction("Open Manifest...");
    connect(openManifestAction, &QAction::triggered, this, &MainWindow::onOpenManifest);
    fileToolbar->addAction(openManifestAction);

    QMenu* viewMenu = menuBar()->addMenu("&View");
    auto* viewToolbar = new QToolBar("View");
    viewToolbar->setObjectName("view_toolbar");  // required for saveState() to persist/restore it silently
    addToolBar(viewToolbar);

    // Same action as the settings dock's "Center on Ligand" button -- the
    // only way to explicitly re-fit the camera to the current ligand (or
    // receptor, if there's no ligand) on demand. Every other refresh
    // deliberately preserves whatever camera position the user left it at.
    QAction* centerAction = viewMenu->addAction("Center on Ligand");
    connect(centerAction, &QAction::triggered, this, &MainWindow::onCenterOnLigand);
    viewToolbar->addAction(centerAction);

    viewMenu->addSeparator();
    // Each dock's own built-in toggleViewAction() -- a checkable QAction
    // that tracks the dock's shown/hidden state in both directions -- lets
    // any panel closed via its dock be reopened from here.
    for (auto* dock : docks_) {
        viewMenu->addAction(dock->toggleViewAction());
    }
}

void MainWindow::wireSignals() {
    connect(proteinTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::onProteinSelectionChanged);
    connect(ligandTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::onLigandSelectionChanged);
    connect(contactTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &MainWindow::onContactSelectionChanged);
    connect(settingsPanel_, &DisplaySettingsPanel::settingsChanged, this, [this]() { refreshView(false); });
    connect(settingsPanel_, &DisplaySettingsPanel::centerOnLigandRequested, this, &MainWindow::onCenterOnLigand);
    connect(sequencePanel_, &QTextBrowser::anchorClicked, this, &MainWindow::onSequenceAnchorClicked);
    for (auto* dock : docks_) {
        connect(dock, &QDockWidget::visibilityChanged, this, &MainWindow::nudgeView3DRepaint);
        connect(dock, &QDockWidget::topLevelChanged, this, &MainWindow::nudgeView3DRepaint);
    }
}

void MainWindow::nudgeView3DRepaint() {
    // Showing/hiding/floating *any* dock reflows the whole dock layout,
    // which can resize the 3D view purely via layout reflow rather than a
    // top-level window resize. QWebEngineView's content is painted by a
    // native child window outside Qt's own repaint path; that path
    // reliably picks up real window resizes but not sibling-visibility-
    // driven layout reflows, leaving the newly reclaimed/vacated area a
    // dead, never-repainted region. Nudging the top-level window's size by
    // a pixel and back forces a genuine resize event once the layout has
    // settled, which does reach the native child window. Deferred via
    // singleShot(0, ...) so it runs after this event's own layout pass.
    QTimer::singleShot(0, this, [this]() {
        QSize size = this->size();
        this->resize(size.width(), size.height() + 1);
        this->resize(size);
    });
}

// ------------------------------------------------------------------
// Panel layout persistence
// ------------------------------------------------------------------
void MainWindow::restoreLayout() {
    QSettings settings;
    QByteArray geometry = settings.value("mainWindow/geometry").toByteArray();
    if (geometry.isEmpty() || !restoreGeometry(geometry)) {
        resize(1400, 900);
    }
    QByteArray state = settings.value("mainWindow/state").toByteArray();
    if (!state.isEmpty()) {
        restoreState(state);
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings settings;
    settings.setValue("mainWindow/geometry", saveGeometry());
    settings.setValue("mainWindow/state", saveState());
    QMainWindow::closeEvent(event);
}

// ------------------------------------------------------------------
// File loading
// ------------------------------------------------------------------
void MainWindow::onOpenReceptors() {
    QStringList paths = QFileDialog::getOpenFileNames(this, "Open receptor PDB(s)", QString(), "PDB files (*.pdb)");
    if (!paths.isEmpty()) {
        receptorPaths_ += paths;
        reload();
    }
}

void MainWindow::onAddPoses() {
    QStringList paths = QFileDialog::getOpenFileNames(this, "Open pose SDF(s)", QString(), "SDF files (*.sdf)");
    if (!paths.isEmpty()) {
        posesPaths_ += paths;
        reload();
    }
}

void MainWindow::onOpenReference() {
    QString path = QFileDialog::getOpenFileName(this, "Open reference ligand SDF", QString(), "SDF files (*.sdf)");
    if (!path.isEmpty()) {
        referencePath_ = path;
        reload();
    }
}

void MainWindow::onOpenManifest() {
    QString path = QFileDialog::getOpenFileName(this, "Open ensemble manifest.json", QString(), "JSON files (*.json)");
    if (!path.isEmpty()) {
        manifestPath_ = path;
        reload();
    }
}

void MainWindow::reload() {
    loadFromPaths(receptorPaths_, posesPaths_, referencePath_, manifestPath_);
}

void MainWindow::loadFromPaths(const QStringList& receptorPaths, const QStringList& posesPaths,
                                const QString& referencePath, const QString& manifestPath) {
    receptorPaths_ = receptorPaths;
    posesPaths_ = posesPaths;
    referencePath_ = referencePath;
    manifestPath_ = manifestPath;

    try {
        bridge_->loadAll(receptorPaths_, posesPaths_, referencePath_, manifestPath_);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Load failed", QString::fromUtf8(e.what()));
        return;
    }

    settingsPanel_->setReferenceAvailable(bridge_->hasReference());
    selectedResidues_.clear();

    TableData proteins = bridge_->proteinTable();
    proteinModel_->setTable(proteins);
    proteinDock_->setWindowTitle(QString("Proteins (%1)").arg(bridge_->receptorCount()));
    if (bridge_->receptorCount() > 0) {
        // Receptor rows are always in ascending "index" order (see
        // dd_molview.dashboard.receptors_dataframe), so row position ==
        // index here -- same assumption dd_molview-desktop's own
        // `self.protein_table.selectRow(self.active_receptor_idx)` makes.
        proteinTable_->selectRow(bridge_->activeReceptorIndex());
    }

    refreshLigandTable();
    refreshView(/*recenter=*/true);
}

// ------------------------------------------------------------------
// Selection handlers
// ------------------------------------------------------------------
void MainWindow::onProteinSelectionChanged() {
    auto rows = proteinTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return;
    }
    int index = proteinModel_->rawValue(rows.first().row(), "index").toInt();
    bridge_->setActiveReceptor(index);
    selectedResidues_.clear();
    refreshLigandTable();
    // Deliberately *not* recenter=true: these structures are typically
    // pre-aligned (e.g. a dd_seq-produced ensemble), so switching the
    // active receptor leaves the camera exactly where it was.
    refreshView(false);
}

void MainWindow::onLigandSelectionChanged() {
    auto rows = ligandTable_->selectionModel()->selectedRows();
    if (rows.isEmpty()) {
        return;
    }
    int index = ligandModel_->rawValue(rows.first().row(), "index").toInt();
    bridge_->setActiveLigand(index);
    selectedResidues_.clear();
    refreshView(false);
}

void MainWindow::onContactSelectionChanged() {
    auto rows = contactTable_->selectionModel()->selectedRows();
    QVector<ResiduePair> residues;
    for (const auto& idx : rows) {
        QString chain = contactModel_->rawValue(idx.row(), "chain").toString();
        int resnum = contactModel_->rawValue(idx.row(), "resnum").toInt();
        residues.append({chain, resnum});
    }
    selectedResidues_ = residues;
    refreshView(false);
}

void MainWindow::onSequenceAnchorClicked(const QUrl& url) {
    if (url.scheme() == bridge_->chainLinkScheme()) {
        viewer3d_->zoomToChain(url.path());
        return;
    }
    if (url.scheme() != bridge_->residueLinkScheme()) {
        return;
    }
    QString path = url.path();  // "A/235"
    int slash = path.indexOf('/');
    if (slash < 0) {
        return;
    }
    ResiduePair residue{path.left(slash), path.mid(slash + 1).toInt()};

    // Ctrl (Cmd on macOS, via Qt's platform-standard swap) toggles the
    // clicked residue into/out of the existing selection, matching the
    // contact-residue table's own multi-pick gesture; a plain click always
    // replaces the selection with just this one.
    bool extend = QApplication::keyboardModifiers() & Qt::ControlModifier;
    {
        // Clear the contact table's own selection so it doesn't silently
        // override this pick on its next selectionChanged signal; block
        // its signal since we're about to set selectedResidues_ ourselves.
        QSignalBlocker blocker(contactTable_->selectionModel());
        contactTable_->clearSelection();
    }
    if (extend && !selectedResidues_.isEmpty()) {
        int pos = selectedResidues_.indexOf(residue);
        if (pos >= 0) {
            selectedResidues_.remove(pos);
        } else {
            selectedResidues_.append(residue);
        }
    } else {
        selectedResidues_ = {residue};
    }
    refreshView(false);
}

void MainWindow::onCenterOnLigand() {
    refreshView(true);
}

void MainWindow::refreshLigandTable() {
    // Rebuilds against the *active* receptor only (dashboard.py's
    // O(n_ligands), never O(n_receptors * n_ligands), contract); the
    // bridge itself snaps active_ligand_idx to the first still-visible
    // entry if the previous one belonged only to a different receptor
    // (see LigandEntry.receptor_index).
    TableData ligands = bridge_->ligandTable();
    ligandModel_->setTable(ligands);
    ligandDock_->setWindowTitle(QString("Ligands (%1)").arg(ligands.rows.size()));
    if (ligands.rows.isEmpty()) {
        return;
    }
    int activeIndex = bridge_->activeLigandIndex();
    int indexCol = ligands.columns.indexOf("index");
    int row = 0;
    for (int r = 0; r < ligands.rawRows.size(); ++r) {
        if (ligands.rawRows[r][indexCol].toInt() == activeIndex) {
            row = r;
            break;
        }
    }
    QSignalBlocker blocker(ligandTable_->selectionModel());
    ligandTable_->selectRow(row);
}

// ------------------------------------------------------------------
// Central recompute-and-redraw
// ------------------------------------------------------------------
void MainWindow::refreshView(bool recenter) {
    double cutoff = settingsPanel_->contactCutoff();
    TableData contacts = bridge_->contactTable(cutoff);
    contactModel_->setTable(contacts);

    QVector<ResiduePair> highlightResidues;
    if (settingsPanel_->showContactResidues()) {
        highlightResidues = tableToResidues(contacts);
    }

    // The 3D view's own update may be deferred behind an async
    // page().runJavaScript() round-trip (reading back the live camera
    // before replacing the page) -- see Viewer3D::requestCamera. Nothing
    // else below depends on that round-trip, so it updates synchronously
    // regardless of which branch below runs.
    auto render = [this](QJsonArray savedCamera) {
        DisplaySettings settings = settingsPanel_->currentSettings();
        ViewResult result = bridge_->buildView(settings, toStdVector(selectedResidues_), savedCamera);
        if (result.hasContent) {
            viewer3d_->setContent(result.html, result.viewerVar);
        } else {
            viewer3d_->setPlaceholder("Open a receptor PDB (File menu) to get started.");
        }
        updateDetailLabels(result);
    };

    if (recenter || !viewer3d_->hasContent()) {
        render(QJsonArray());
    } else {
        viewer3d_->requestCamera(render);
    }

    if (bridge_->receptorCount() > 0) {
        QString html = bridge_->sequenceHtml(toStdVector(highlightResidues), toStdVector(selectedResidues_));
        sequencePanel_->setHtml(html);
    } else {
        sequencePanel_->setHtml(QString());
    }
}

void MainWindow::updateDetailLabels(const ViewResult& result) {
    DetailInfo info = bridge_->detailInfo();
    scoreLabel_->setText(info.hasScore ? QString("Score: %1").arg(info.score, 0, 'g', 12) : "Score: -");
    rmsdLabel_->setText(info.hasRmsd ? QString("RMSD (Å): %1").arg(info.rmsd, 0, 'f', 2) : "RMSD (Å): -");
    if (info.hasPose) {
        interactionSummaryLabel_->setText(
            QString("H-bond candidates: %1 / Hydrophobic contacts: %2 / Salt bridges: %3 / "
                    "Electrostatic interactions: %4 / Pi-stacking: %5 / Pi-halogen bonds: %6 / "
                    "Sulfur-halogen bonds: %7")
                .arg(result.hbonds)
                .arg(result.hydrophobic)
                .arg(result.saltBridges)
                .arg(result.electrostatic)
                .arg(result.piStacking)
                .arg(result.piHalogen)
                .arg(result.sulfurHalogen));
    } else {
        interactionSummaryLabel_->setText(QString());
    }
}
