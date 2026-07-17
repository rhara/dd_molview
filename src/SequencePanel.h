// The C++ analog of the desktop app's sequence panel: a `QTextBrowser`
// showing `dm.sequence_to_html(..., clickable=True)`'s HTML (residue
// letters and "Chain X" headers as clickable `residue:`/`chain:` links --
// see PythonBridge::residueLinkScheme/chainLinkScheme), with link
// navigation disabled so `anchorClicked` is the only thing that ever
// reacts to a click (MainWindow::onSequenceAnchorClicked).
#pragma once

#include <QTextBrowser>

class SequencePanel : public QTextBrowser {
    Q_OBJECT
public:
    explicit SequencePanel(QWidget* parent = nullptr);
};
