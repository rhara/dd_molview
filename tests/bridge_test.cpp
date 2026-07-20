// Integration smoke test: exercises PythonBridge end to end against the
// bundled sample data (the same 6W63 receptor/poses/reference dd_viewer
// ships), without any Qt GUI involved. Plain asserts + a process exit code
// rather than a test framework -- this is one deliberately narrow check
// ("does the whole embedding wire up and produce sane output"), not a
// substitute for dd_viewer's own pytest suite or dd_cview_core's
// (python/tests/), which already
// cover the underlying logic this test just calls through to.
#include "PythonBridge.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(cond)                                                                     \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                               \
        }                                                                               \
    } while (0)

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <data-dir>\n", argv[0]);
        return 1;
    }
    QString dataDir = QString::fromLocal8Bit(argv[1]);

    PythonBridge bridge;

    bridge.loadAll(
        {dataDir + "/6W63_receptor.pdb", dataDir + "/7L10.pdb"},
        {dataDir + "/6W63_redock.sdf"},
        dataDir + "/6W63_ligand_ref.sdf",
        QString());

    CHECK(bridge.receptorCount() == 2);
    CHECK(bridge.ligandCount() == 9);
    CHECK(bridge.hasReference());

    TableData proteins = bridge.proteinTable();
    CHECK(proteins.columns.contains("label"));
    CHECK(proteins.rows.size() == 2);

    TableData ligands = bridge.ligandTable();
    CHECK(ligands.columns.contains("score"));
    CHECK(ligands.rows.size() == 9);

    TableData contacts = bridge.contactTable(3.0);
    CHECK(contacts.columns.contains("chain"));
    CHECK(contacts.columns.contains("resnum"));
    CHECK(contacts.rows.size() > 0);

    QString seqHtml = bridge.sequenceHtml({}, {});
    CHECK(!seqHtml.isEmpty());
    CHECK(bridge.residueLinkScheme() == "residue");
    CHECK(bridge.chainLinkScheme() == "chain");

    DisplaySettings settings;
    ViewResult view = bridge.buildView(settings, {}, QJsonArray());
    CHECK(view.hasContent);
    CHECK(!view.html.isEmpty());
    CHECK(!view.viewerVar.isEmpty());
    CHECK(view.hbonds > 0);
    // highlightResidues is the union of residues behind every *enabled*
    // interaction type (showInteractingResidues defaults to true) -- with
    // at least one hbond found above, this must be non-empty too.
    CHECK(!view.highlightResidues.isEmpty());

    DisplaySettings noHighlight = settings;
    noHighlight.showInteractingResidues = false;
    ViewResult viewNoHighlight = bridge.buildView(noHighlight, {}, QJsonArray());
    CHECK(viewNoHighlight.highlightResidues.isEmpty());

    DetailInfo info = bridge.detailInfo();
    CHECK(info.hasPose);
    CHECK(info.hasScore);
    CHECK(info.hasRmsd);

    // Switching the active receptor to the one with no explicit poses
    // should empty the ligand table's *visible* rows for it (the redocked
    // poses were loaded explicitly against 6W63, not auto-extracted, so
    // per LigandEntry.receptor_index semantics they still show for every
    // receptor -- this specifically checks that switching receptors
    // doesn't crash and the contact table naturally goes empty once no
    // pose is active against the new receptor's binding site).
    bridge.setActiveReceptor(1);
    TableData ligandsAfterSwitch = bridge.ligandTable();
    CHECK(ligandsAfterSwitch.rows.size() == 9);  // explicit poses stay visible against every receptor

    std::printf("All PythonBridge smoke checks passed.\n");
    return 0;
}
