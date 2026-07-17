#include "TableModel.h"

TableModel::TableModel(QObject* parent) : QAbstractTableModel(parent) {}

void TableModel::setTable(const TableData& table) {
    if (table == table_) {
        return;
    }
    beginResetModel();
    table_ = table;
    endResetModel();
}

QJsonValue TableModel::rawValue(int row, const QString& columnName) const {
    int col = table_.columns.indexOf(columnName);
    if (col < 0 || row < 0 || row >= table_.rawRows.size()) {
        return QJsonValue();
    }
    return table_.rawRows[row][col];
}

int TableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : table_.rows.size();
}

int TableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : table_.columns.size();
}

QVariant TableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || role != Qt::DisplayRole) {
        return QVariant();
    }
    return table_.rows[index.row()][index.column()];
}

QVariant TableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (role != Qt::DisplayRole) {
        return QVariant();
    }
    if (orientation == Qt::Horizontal) {
        return table_.columns[section];
    }
    return section;
}
