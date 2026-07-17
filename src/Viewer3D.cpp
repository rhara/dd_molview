#include "Viewer3D.h"

#include <QJsonDocument>
#include <QUrl>
#include <QWebEngineView>
#include <QWebEnginePage>

namespace {

// A properly-escaped JS/JSON string literal (including surrounding quotes)
// for `s` -- built via QJsonArray rather than hand-rolled escaping, so it's
// correct for any chain id a structure could actually contain.
QString jsStringLiteral(const QString& s) {
    QJsonArray arr;
    arr.append(s);
    QString json = QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return json.mid(1, json.size() - 2);  // strip the wrapping "[" / "]"
}

}  // namespace

Viewer3D::Viewer3D(QWebEngineView* view) : view_(view) {}

void Viewer3D::setContent(const QString& html, const QString& viewerVar) {
    viewerVar_ = viewerVar;
    // baseUrl matters for py3Dmol's assets (loaded from a CDN by URL, not
    // bundled) to be treated as same-origin-enough for QWebEngineView's
    // default security policy -- matches dd_molview-desktop's own
    // `setHtml(html, baseUrl=QUrl("https://example.com/"))`.
    view_->setHtml(html, QUrl("https://example.com/"));
}

void Viewer3D::setPlaceholder(const QString& message) {
    viewerVar_.clear();
    view_->setHtml(QString("<p style='font-family: sans-serif; color: #888;'>%1</p>").arg(message.toHtmlEscaped()));
}

void Viewer3D::zoomToChain(const QString& chain) {
    if (viewerVar_.isEmpty() || chain.isEmpty()) {
        return;
    }
    QString js = QString("if (typeof %1 !== 'undefined' && %1) { %1.zoomTo({chain: %2}); %1.render(); }")
                     .arg(viewerVar_, jsStringLiteral(chain));
    view_->page()->runJavaScript(js);
}

void Viewer3D::requestCamera(std::function<void(QJsonArray)> callback) const {
    if (viewerVar_.isEmpty()) {
        callback(QJsonArray());
        return;
    }
    QString js = QString("(typeof %1 !== 'undefined' && %1) ? %1.getView() : null").arg(viewerVar_);
    view_->page()->runJavaScript(js, [callback](const QVariant& result) {
        if (!result.isValid() || result.isNull()) {
            callback(QJsonArray());
            return;
        }
        callback(QJsonArray::fromVariantList(result.toList()));
    });
}
