// Read-only Qt table model backing every table in dd_cview (protein /
// ligand / contact-residue) -- the C++ analog of dd_molview's
// `desktop/table_model.py::DataFrameTableModel`, wrapping a `TableData`
// (see PythonBridge.h) instead of a `pandas.DataFrame`.
#pragma once

#include <QAbstractTableModel>

#include "PythonBridge.h"

class TableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit TableModel(QObject* parent = nullptr);

    // A no-op if `table` is equal (by columns + display rows) to what's
    // already loaded -- a model reset clears the attached view's
    // selection as a Qt-level side effect, and callers recompute their
    // TableData on every refresh regardless of whether it actually
    // changed; without this check, e.g. picking a contact-table row would
    // immediately un-pick itself the moment that same refresh re-set an
    // identical table. Mirrors DataFrameTableModel.set_dataframe's own
    // `df.equals` short-circuit.
    void setTable(const TableData& table);
    const TableData& table() const { return table_; }

    // Raw (non-stringified) value of `row`'s `columnName` cell, or a null
    // QJsonValue if that column doesn't exist -- for reading back e.g. the
    // "index"/"chain"/"resnum" columns as real numbers after a selection
    // change, instead of re-parsing the display string.
    QJsonValue rawValue(int row, const QString& columnName) const;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    TableData table_;
};
