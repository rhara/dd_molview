#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>
#include <QMessageBox>
#include <QTimer>

#include <cstdlib>

#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    // A stable place for QSettings (MainWindow::restoreLayout/closeEvent)
    // to persist window geometry and dock arrangement across sessions --
    // without these, QSettings() falls back to the executable's own name,
    // which can differ between a build directory binary and an installed
    // one.
    QApplication::setOrganizationName("dd_cview");
    QApplication::setApplicationName("dd_cview");
    QApplication::setApplicationVersion("0.1.0");
    app.setWindowIcon(QIcon(":/icon.png"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        "dd_cview: a native C++/Qt multi-panel workbench for interactive protein-ligand structure analysis.");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption receptorOption("receptor", "Receptor PDB path (repeatable).", "path");
    QCommandLineOption posesOption("poses", "Pose SDF path (repeatable).", "path");
    QCommandLineOption referenceOption("reference", "Reference ligand SDF path.", "path");
    QCommandLineOption manifestOption("manifest", "Ensemble manifest.json path.", "path");
    parser.addOption(receptorOption);
    parser.addOption(posesOption);
    parser.addOption(referenceOption);
    parser.addOption(manifestOption);
    parser.process(app);

    QStringList receptorPaths = parser.values(receptorOption);
    QStringList posesPaths = parser.values(posesOption);
    QString referencePath = parser.value(referenceOption);
    QString manifestPath = parser.value(manifestOption);

    std::unique_ptr<MainWindow> window;
    try {
        window = std::make_unique<MainWindow>();
    } catch (const std::exception& e) {
        QMessageBox::critical(
            nullptr, "dd_cview failed to start",
            QString("Could not initialize the embedded Python interpreter (dd_viewer/dd_molview):\n\n%1")
                .arg(QString::fromUtf8(e.what())));
        return 1;
    }
    window->show();

    if (!receptorPaths.isEmpty() || !posesPaths.isEmpty() || !referencePath.isEmpty() || !manifestPath.isEmpty()) {
        window->loadFromPaths(receptorPaths, posesPaths, referencePath, manifestPath);
    }

    // Headless verification hook (also useful under QT_QPA_PLATFORM=offscreen
    // in CI, matching how dd_molview-desktop's own test suite grabs
    // QWidget::grab() screenshots without a real display): if set, capture
    // the window to this path a couple seconds after the 3D view's async
    // JS round-trip has had time to settle, then quit.
    if (const char* screenshotPath = std::getenv("DD_CVIEW_SCREENSHOT")) {
        QString path = QString::fromLocal8Bit(screenshotPath);
        QTimer::singleShot(2000, window.get(), [win = window.get(), path]() {
            win->grab().save(path);
            QApplication::quit();
        });
    }

    return QApplication::exec();
}
