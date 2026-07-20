// PythonBridge: the one seam between dd_cview's native C++/Qt GUI shell and
// dd_viewer/dd_cview_core's existing Python computational logic (PDB/SDF
// parsing, contact/interaction detection, scoring, RMSD, sequence
// extraction, 3D-scene HTML generation). See python/dd_cview_backend.py for
// the Python-side counterpart (`Session`) this class talks to.
//
// Deliberately Python/pybind11-agnostic in this header (only Qt value types
// and std containers) -- pybind11's `Python.h` must never be visible in a
// translation unit that also pulls in `qobjectdefs.h` (any QObject-derived
// class), since both define a bare `slots` symbol. Only PythonBridge.cpp
// includes <pybind11/...>; everything else (MainWindow, etc.) only sees
// this header.
#pragma once

#include <QJsonArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>
#include <utility>
#include <vector>

using ResiduePair = std::pair<QString, int>;  // (chain, resnum)

// Mirrors the desktop app's DisplaySettingsPanel getters (dd_molview's
// controls.py) one field per control.
struct DisplaySettings {
    QString receptorStyle = QStringLiteral("cartoon");
    bool colorBySecondaryStructure = false;
    bool onlyNearLigand = false;
    bool showHbonds = true;
    bool showHydrophobic = false;
    bool showSaltBridges = true;
    bool showElectrostatic = false;
    bool showPiStacking = true;
    bool showPiHalogen = true;
    bool showSulfurHalogen = true;
    // Highlight (yellow, in both the 3D view and the sequence panel) every
    // residue that has at least one *actual detected interaction* with the
    // ligand -- the union of whichever of the interaction lists above are
    // currently enabled -- not merely every residue within `contactCutoff`
    // of the ligand (that broader, purely-distance-based set is still
    // shown, unaffected by this flag, in the separate Contact residues
    // table).
    bool showInteractingResidues = true;
    double contactCutoff = 3.0;
    bool showReference = false;
};

// One table (protein / ligand / contact-residue), the same shape as
// `dd_molview.desktop.table_model.DataFrameTableModel` wraps: `rows` holds
// the display strings (`data(Qt::DisplayRole)`'s value), `rawRows` the
// original JSON-safe values (for reading e.g. the "index"/"chain"/"resnum"
// columns back out as real numbers instead of re-parsing display text).
struct TableData {
    QStringList columns;
    QVector<QStringList> rows;
    QVector<QJsonArray> rawRows;

    bool operator==(const TableData& other) const {
        return columns == other.columns && rows == other.rows;
    }
};

// One recomputed 3D-view frame: the scene HTML plus the interaction counts
// that went into it (bundled together so the "Interaction summary" label
// and the rendered scene always describe the exact same computed state).
struct ViewResult {
    bool hasContent = false;
    QString html;
    QString viewerVar;
    int hbonds = 0;
    int hydrophobic = 0;
    int saltBridges = 0;
    int electrostatic = 0;
    int piStacking = 0;
    int piHalogen = 0;
    int sulfurHalogen = 0;
    // The exact (chain, resnum) set rendered yellow in this frame (the
    // union of whichever enabled interaction lists' residues, computed
    // server-side -- see DisplaySettings::showInteractingResidues) --
    // handed back so the C++ side can reuse the identical set for the
    // "Zoom to Highlighted Residues" button without recomputing it.
    QVector<ResiduePair> highlightResidues;
};

struct DetailInfo {
    bool hasPose = false;
    bool hasScore = false;
    double score = 0.0;
    bool hasRmsd = false;
    double rmsd = 0.0;
};

class PythonBridge {
public:
    PythonBridge();
    ~PythonBridge();

    PythonBridge(const PythonBridge&) = delete;
    PythonBridge& operator=(const PythonBridge&) = delete;

    // Loading. Throws std::runtime_error (message already formatted from
    // the Python traceback) on a bad path / unparseable file.
    void loadAll(const QStringList& receptorPaths, const QStringList& posesPaths,
                 const QString& referencePath, const QString& manifestPath);

    int receptorCount() const;
    int ligandCount() const;
    bool hasReference() const;

    void setActiveReceptor(int index);
    void setActiveLigand(int index);
    int activeReceptorIndex() const;
    int activeLigandIndex() const;  // may change after ligandTable() if the previous active row is no longer visible

    TableData proteinTable() const;
    TableData ligandTable() const;
    TableData contactTable(double cutoff) const;

    QString sequenceHtml(const std::vector<ResiduePair>& highlightResidues,
                          const std::vector<ResiduePair>& selectedResidues) const;
    QString residueLinkScheme() const;
    QString chainLinkScheme() const;

    // `savedCamera`: empty array means "no saved camera -- use build_view's
    // own default auto-fit", matching dd_viewer.html_with_initial_view's
    // `view=None` contract.
    ViewResult buildView(const DisplaySettings& settings, const std::vector<ResiduePair>& selectedResidues,
                          const QJsonArray& savedCamera) const;

    DetailInfo detailInfo() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
