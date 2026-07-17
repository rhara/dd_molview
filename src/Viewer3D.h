// Thin wrapper around the 3D-view QWebEngineView: owns the "which JS
// variable does the currently-loaded py3Dmol scene live in" bookkeeping
// (`dd_molview`'s `MainWindow._last_viewer_var`) and the two JS round-trips
// built on it -- reading back the live camera position before replacing the
// page (so a refresh that shouldn't move the camera can carry it forward),
// and jumping the camera to a clicked chain.
#pragma once

#include <QJsonArray>
#include <QString>

#include <functional>

class QWebEngineView;

class Viewer3D {
public:
    explicit Viewer3D(QWebEngineView* view);

    // Loads a freshly built scene and remembers `viewerVar` (the JS
    // variable name it lives in, from PythonBridge::buildView's
    // ViewResult) for the next requestCamera()/zoomToChain() call.
    void setContent(const QString& html, const QString& viewerVar);

    // No receptor/pose loaded yet -- clears the remembered viewer variable
    // too, so a stale requestCamera()/zoomToChain() call against the
    // previous (now-replaced) page can't fire.
    void setPlaceholder(const QString& message);

    bool hasContent() const { return !viewerVar_.isEmpty(); }
    QString currentViewerVar() const { return viewerVar_; }

    // Re-fits the camera to `chain` in the *currently loaded* scene (a live
    // zoomTo()+render() call) -- deliberately moves the camera, unlike a
    // residue click, so it's called directly rather than going through a
    // full MainWindow::refreshView.
    void zoomToChain(const QString& chain);

    // Reads the live scene's current camera position asynchronously (a
    // page().runJavaScript() round-trip); `callback` receives an empty
    // QJsonArray if there's no content loaded or the page has no answer.
    void requestCamera(std::function<void(QJsonArray)> callback) const;

private:
    QWebEngineView* view_;
    QString viewerVar_;
};
