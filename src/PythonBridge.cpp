// IMPORTANT: pybind11/Python.h must be the first include in this file --
// see PythonBridge.h's top comment (Python.h and Qt's qobjectdefs.h both
// define a bare `slots` symbol; whichever is parsed first wins).
#include <pybind11/embed.h>
#include <pybind11/stl.h>

#include "PythonBridge.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

#include <cstdlib>
#include <stdexcept>

namespace py = pybind11;

#ifndef DD_CVIEW_PYTHON_DIR
#error "DD_CVIEW_PYTHON_DIR must be defined by CMake (see CMakeLists.txt)"
#endif
#ifndef DD_CVIEW_PYTHON_HOME
#error "DD_CVIEW_PYTHON_HOME must be defined by CMake (see CMakeLists.txt)"
#endif

namespace {

// Calls `session.<name>(args...)`, converting a Python exception into a
// std::runtime_error carrying its formatted traceback -- the only kind of
// failure a C++ caller (MainWindow) needs to catch and show the user (e.g.
// an unparseable PDB/SDF from File > Open).
template <typename Ret, typename... Args>
Ret callMethod(py::object& session, const char* name, Args&&... args) {
    try {
        return session.attr(name)(std::forward<Args>(args)...).template cast<Ret>();
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(e.what());
    }
}

void callVoidMethod(py::object& session, const char* name) {
    try {
        session.attr(name)();
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(e.what());
    }
}

std::string residuesToJson(const std::vector<ResiduePair>& residues) {
    QJsonArray arr;
    for (const auto& [chain, resnum] : residues) {
        QJsonArray pair;
        pair.append(chain);
        pair.append(resnum);
        arr.append(pair);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact).toStdString();
}

std::string settingsToJson(const DisplaySettings& s) {
    QJsonObject obj;
    obj["receptor_style"] = s.receptorStyle;
    obj["color_by_ss"] = s.colorBySecondaryStructure;
    obj["only_near_ligand"] = s.onlyNearLigand;
    obj["show_hbonds"] = s.showHbonds;
    obj["show_hydrophobic"] = s.showHydrophobic;
    obj["show_salt_bridges"] = s.showSaltBridges;
    obj["show_electrostatic"] = s.showElectrostatic;
    obj["show_pi_stacking"] = s.showPiStacking;
    obj["show_pi_halogen"] = s.showPiHalogen;
    obj["show_sulfur_halogen"] = s.showSulfurHalogen;
    obj["show_contact_residues"] = s.showContactResidues;
    obj["contact_cutoff"] = s.contactCutoff;
    obj["show_reference"] = s.showReference;
    return QJsonDocument(obj).toJson(QJsonDocument::Compact).toStdString();
}

QJsonObject parseJsonObject(const std::string& text) {
    return QJsonDocument::fromJson(QByteArray::fromStdString(text)).object();
}

TableData parseTable(const std::string& text) {
    TableData data;
    QJsonObject obj = parseJsonObject(text);
    for (const auto& v : obj["columns"].toArray()) {
        data.columns << v.toString();
    }
    for (const auto& rowVal : obj["rows"].toArray()) {
        QStringList row;
        for (const auto& cell : rowVal.toArray()) {
            row << cell.toString();
        }
        data.rows << row;
    }
    for (const auto& rowVal : obj["raw_rows"].toArray()) {
        data.rawRows << rowVal.toArray();
    }
    return data;
}

}  // namespace

struct PythonBridge::Impl {
    // Declaration order matters: members are destroyed in reverse
    // declaration order, so `session` (a live Python object) is released
    // *before* `interpreter` finalizes the interpreter it belongs to.
    std::unique_ptr<py::scoped_interpreter> interpreter;
    py::object session;
};

PythonBridge::PythonBridge() : impl_(std::make_unique<Impl>()) {
    // dd_viewer/dd_molview (and their own dependencies -- RDKit, biopython,
    // pandas, numpy) live in a specific conda env's site-packages (see
    // README.md's install instructions); point the embedded interpreter's
    // PYTHONHOME at that env's prefix *before* Py_Initialize runs, so
    // `site.py` finds them the same way `python3` invoked from that env
    // would, regardless of what environment (if any) is active in the
    // shell that launched dd_cview.
    setenv("PYTHONHOME", DD_CVIEW_PYTHON_HOME, 1);

    impl_->interpreter = std::make_unique<py::scoped_interpreter>();

    py::module_ sys = py::module_::import("sys");
    // Drop any cwd-derived entry (an empty string, or the interpreter's
    // own inferred cwd) from sys.path before importing anything: dd_cview,
    // dd_viewer, and dd_molview all live in sibling directories under the
    // same ~/work parent, so if dd_cview is launched with cwd set to that
    // parent directory, a bare "" entry resolves to a *namespace* package
    // shadowing the real editable-installed one (a directory literally
    // named "dd_viewer" sitting right there, with no __init__.py at that
    // level) -- see the project's PROMPT for how this was diagnosed genuinely
    // returning `None` for `dd_viewer.__file__`, silently importing an
    // empty package.
    py::list rawPath = sys.attr("path");
    py::list cleanPath;
    for (auto item : rawPath) {
        std::string entry = py::str(item);
        if (!entry.empty()) {
            cleanPath.append(item);
        }
    }
    sys.attr("path") = cleanPath;
    sys.attr("path").attr("insert")(0, std::string(DD_CVIEW_PYTHON_DIR));

    py::module_ backend = py::module_::import("dd_cview_backend");
    impl_->session = backend.attr("create_session")();
}

PythonBridge::~PythonBridge() = default;

void PythonBridge::loadAll(const QStringList& receptorPaths, const QStringList& posesPaths,
                            const QString& referencePath, const QString& manifestPath) {
    py::list rpaths;
    for (const auto& p : receptorPaths) {
        rpaths.append(p.toStdString());
    }
    py::list ppaths;
    for (const auto& p : posesPaths) {
        ppaths.append(p.toStdString());
    }
    py::object ref = referencePath.isEmpty() ? py::object(py::none()) : py::object(py::str(referencePath.toStdString()));
    py::object manifest = manifestPath.isEmpty() ? py::object(py::none()) : py::object(py::str(manifestPath.toStdString()));

    try {
        impl_->session.attr("load_all")(rpaths, ppaths, ref, manifest);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(e.what());
    }
}

int PythonBridge::receptorCount() const {
    return callMethod<int>(impl_->session, "receptor_count");
}

int PythonBridge::ligandCount() const {
    return callMethod<int>(impl_->session, "ligand_count");
}

bool PythonBridge::hasReference() const {
    return callMethod<bool>(impl_->session, "has_reference");
}

void PythonBridge::setActiveReceptor(int index) {
    try {
        impl_->session.attr("set_active_receptor")(index);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(e.what());
    }
}

void PythonBridge::setActiveLigand(int index) {
    try {
        impl_->session.attr("set_active_ligand")(index);
    } catch (const py::error_already_set& e) {
        throw std::runtime_error(e.what());
    }
}

int PythonBridge::activeReceptorIndex() const {
    return callMethod<int>(impl_->session, "active_receptor_index");
}

int PythonBridge::activeLigandIndex() const {
    return callMethod<int>(impl_->session, "active_ligand_index");
}

TableData PythonBridge::proteinTable() const {
    return parseTable(callMethod<std::string>(impl_->session, "protein_table_json"));
}

TableData PythonBridge::ligandTable() const {
    return parseTable(callMethod<std::string>(impl_->session, "ligand_table_json"));
}

TableData PythonBridge::contactTable(double cutoff) const {
    return parseTable(callMethod<std::string>(impl_->session, "contact_table_json", cutoff));
}

QString PythonBridge::sequenceHtml(const std::vector<ResiduePair>& highlightResidues,
                                    const std::vector<ResiduePair>& selectedResidues) const {
    std::string html = callMethod<std::string>(
        impl_->session, "sequence_html", residuesToJson(highlightResidues), residuesToJson(selectedResidues));
    return QString::fromStdString(html);
}

QString PythonBridge::residueLinkScheme() const {
    return QString::fromStdString(callMethod<std::string>(impl_->session, "residue_link_scheme"));
}

QString PythonBridge::chainLinkScheme() const {
    return QString::fromStdString(callMethod<std::string>(impl_->session, "chain_link_scheme"));
}

ViewResult PythonBridge::buildView(const DisplaySettings& settings, const std::vector<ResiduePair>& selectedResidues,
                                    const QJsonArray& savedCamera) const {
    std::string cameraJson;
    if (!savedCamera.isEmpty()) {
        cameraJson = QJsonDocument(savedCamera).toJson(QJsonDocument::Compact).toStdString();
    }
    std::string resultJson = callMethod<std::string>(
        impl_->session, "build_view_html", settingsToJson(settings), residuesToJson(selectedResidues), cameraJson);

    QJsonObject obj = parseJsonObject(resultJson);
    ViewResult result;
    if (!obj["html"].isNull()) {
        result.hasContent = true;
        result.html = obj["html"].toString();
        result.viewerVar = obj["viewer_var"].toString();
    }
    result.hbonds = obj["hbonds"].toInt();
    result.hydrophobic = obj["hydrophobic"].toInt();
    result.saltBridges = obj["salt_bridges"].toInt();
    result.electrostatic = obj["electrostatic"].toInt();
    result.piStacking = obj["pi_stacking"].toInt();
    result.piHalogen = obj["pi_halogen"].toInt();
    result.sulfurHalogen = obj["sulfur_halogen"].toInt();
    return result;
}

DetailInfo PythonBridge::detailInfo() const {
    QJsonObject obj = parseJsonObject(callMethod<std::string>(impl_->session, "detail_info_json"));
    DetailInfo info;
    info.hasPose = obj["has_pose"].toBool();
    if (!obj["score"].isNull()) {
        info.hasScore = true;
        info.score = obj["score"].toDouble();
    }
    if (!obj["rmsd"].isNull()) {
        info.hasRmsd = true;
        info.rmsd = obj["rmsd"].toDouble();
    }
    return info;
}
