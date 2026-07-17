#include "SequencePanel.h"

SequencePanel::SequencePanel(QWidget* parent) : QTextBrowser(parent) {
    setOpenExternalLinks(false);
    // sequence_html() renders each residue as <a href="residue:{chain}/{resnum}">
    // and each chain header as <a href="chain:{chain}"> -- both unregistered
    // schemes QTextBrowser would otherwise try (and fail) to navigate to;
    // MainWindow::onSequenceAnchorClicked (wired to anchorClicked) is the
    // only thing that reacts to them.
    setOpenLinks(false);
    // A concrete font name (not the generic "monospace" family) -- resolving
    // "monospace" makes Qt populate its full font-family alias table on
    // macOS, which is slow and prints a qt.qpa.fonts warning (same fix as
    // dd_molview's sequence.py/main_window.py).
    setStyleSheet("QTextBrowser { font-family: Menlo, Consolas, 'DejaVu Sans Mono', monospace; }");
}
