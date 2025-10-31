#include "DataGridModelEx.h"

DataGridModelEx::DataGridModelEx(QObject *parent) : QAbstractListModel{parent} {
    connect(this, &DataGridModelEx::sourceDataChanged, this, [=] {
        if (!m_sourceData.isEmpty()) {
            updateValues(m_sourceData.at(0));
        }
        beginResetModel();
        endResetModel();
        emit countChanged();
    });
}

int DataGridModelEx::rowCount(const QModelIndex &parent) const {
    return m_sourceData.count();
}

int DataGridModelEx::count() const {
    return m_sourceData.count();
}

QVariant DataGridModelEx::data(const QModelIndex &index, int role) const {
    QVariant v;
    if (index.row() >= count() || index.row() < 0)
        return v;
    v = m_sourceData[index.row()].toMap().value(m_roles[role]);
    return v;
}

bool DataGridModelEx::move(int from, int to, int n) {
    if (from < 0 || from >= count() || to < 0 || to >= count() || n <= 0 || (from + n) > count()) {
        return false;
    }
    beginMoveRows(QModelIndex(), from, from + n - 1, QModelIndex(), to > from ? to + n : to);
    if (from > to) {
        int tfrom = from;
        int tto = to;
        from = tto;
        to = tto + n;
        n = tfrom - tto;
    }
    QList<QVariant> store;
    for (int i = 0; i < (to - from); ++i)
        store.append(m_sourceData[from + n + i]);
    for (int i = 0; i < n; ++i)
        store.append(m_sourceData[from + i]);
    for (int i = 0; i < store.count(); ++i)
        m_sourceData[from + i] = store[i];
    endMoveRows();
    return true;
}

bool DataGridModelEx::remove(int index, int count) {
    if (index < 0 || index > this->count() || this->count() <= 0) {
        return false;
    }
    beginRemoveRows(QModelIndex(), index, index + count - 1);
    m_sourceData = m_sourceData.mid(0, index) + m_sourceData.mid(index + count);
    endRemoveRows();
    return true;
}

bool DataGridModelEx::setData(const QModelIndex &index, const QVariant &value, int role) {
    const int row = index.row();
    if (row >= count() || row < 0)
        return false;
    const QByteArray property = m_roles.at(role).toUtf8();
    QMap<QString, QVariant> map = m_sourceData[row].toMap();
    map[property] = value;
    m_sourceData[row] = map;
    emitItemsChanged(row, 1, QVector<int>(1, role));
    return true;
}

void DataGridModelEx::emitItemsChanged(int index, int count, const QVector<int> &roles) {
    if (count <= 0)
        return;
    emit dataChanged(this->index(index), this->index(index + count - 1), roles);
}

QHash<int, QByteArray> DataGridModelEx::roleNames() const {
    QHash<int, QByteArray> roleNames;
    for (int i = 0; i < m_roles.size(); ++i) {
        roleNames.insert(i, m_roles.at(i).toUtf8());
    }
    return roleNames;
}

void DataGridModelEx::clear() {
    beginResetModel();
    m_sourceData.clear();
    endResetModel();
}

void DataGridModelEx::insertRole(const QString &name) {
    int roleIndex = m_roles.indexOf(name);
    if (roleIndex == -1) {
        m_roles.append(name);
    }
}

void DataGridModelEx::updateValues(const QVariant &val) {
    m_roles.clear();
    insertRole("height");
    insertRole("minimumHeight");
    insertRole("maximumHeight");
    auto object = val.toMap();
    for (auto it = object.cbegin(), end = object.cend(); it != end; ++it) {
        insertRole(it.key());
    }
}

void DataGridModelEx::append(QJSValue data) {
    if (data.isArray()) {
        auto list = data.toVariant().toList();
        if (!list.isEmpty()) {
            updateValues(list.at(0));
            beginInsertRows(QModelIndex(), count(), count() + list.count() - 1);
            m_sourceData.append(list);
            endInsertRows();
            emit countChanged();
        }
    } else {
        insert(rowCount(), data);
    }
}

bool DataGridModelEx::insert(int index, QJSValue data) {
    if (index < 0 || index > count()) {
        return false;
    }
    auto object = data.toVariant();
    updateValues(object);
    beginInsertRows(QModelIndex(), index, index);
    m_sourceData.insert(index, object);
    endInsertRows();
    emit countChanged();
    return true;
}

QVariant DataGridModelEx::get(int index) {
    if (index < 0 || index > count() || count() <= 0) {
        return {};
    }
    return m_sourceData[index];
}

bool DataGridModelEx::set(int index, QJSValue data) {
    if (index < 0 || index > count() || count() <= 0) {
        return false;
    }
    auto object = data.toVariant();
    m_sourceData[index] = object;
    emit dataChanged(createIndex(index, 0), createIndex(index, 0));
    return true;
}

void DataGridModelEx::removeItems(const QModelIndexList &list) {
    QModelIndexList sortedList = list;
    std::sort(sortedList.begin(), sortedList.end(),
              [](const QModelIndex &a, const QModelIndex &b) { return a.row() > b.row(); });
    if (sortedList.length() > 50) {
        beginResetModel();
        for (const QModelIndex &index : sortedList) {
            int row = index.row();
            m_sourceData.removeAt(row);
        }
        endResetModel();
    } else {
        for (const QModelIndex &index : sortedList) {
            int row = index.row();
            beginRemoveRows(QModelIndex(), row, row);
            m_sourceData.removeAt(row);
            endRemoveRows();
        }
    }
}

void DataGridModelEx::selectRange(QItemSelectionModel *selectionModel, int startRow, int endRow) {
    if (!selectionModel) {
        return;
    }
    QModelIndex topLeft = index(startRow, 0);
    QModelIndex bottomRight = index(endRow, 0);
    QItemSelection selection(topLeft, bottomRight);
    selectionModel->select(selection, QItemSelectionModel::Select | QItemSelectionModel::Rows);
}

void DataGridModelEx::sortByKey(const QString &key, bool ascending)
{
    if (!m_roles.contains(key)) {
        // qWarning() << "Invalid key for sorting:" << key;
        return;
    }

    m_sortKey = key;
    m_sortOrder = ascending ?  Qt::DescendingOrder:Qt::AscendingOrder ;
    m_sorted = true;  // 标记为已排序


    beginResetModel();

    // 对 sourceData 进行排序
    std::sort(m_sourceData.begin(), m_sourceData.end(), [&](const QVariant &a, const QVariant &b) {
        QVariant va = a.toMap().value(key);
        QVariant vb = b.toMap().value(key);

        // 根据 QVariant 的类型进行排序
        if (va.type() == QVariant::String && vb.type() == QVariant::String) {
            // 字符串类型，直接用字符串比较
            return ascending ? va.toString() < vb.toString() : va.toString() > vb.toString();
        }
        else if (va.type() == QVariant::Int && vb.type() == QVariant::Int) {
            // 整型类型，转换为 int 进行比较
            return ascending ? va.toInt() < vb.toInt() : va.toInt() > vb.toInt();
        }
        else if (va.type() == QVariant::Double && vb.type() == QVariant::Double) {
            // 浮动类型，转换为 double 进行比较
            return ascending ? va.toDouble() < vb.toDouble() : va.toDouble() > vb.toDouble();
        }
        else if (va.type() == QVariant::Date && vb.type() == QVariant::Date) {
            // 日期类型，直接用日期比较
            return ascending ? va.toDate() < vb.toDate() : va.toDate() > vb.toDate();
        }
        else {
            // 如果无法识别类型，默认按字符串处理
            return ascending ? va.toString() < vb.toString() : va.toString() > vb.toString();
        }
    });

    endResetModel();
    emit countChanged();
}

void DataGridModelEx::clearSort()
{
    m_sorted = false;  // 清除排序标记
    beginResetModel();
    endResetModel();
    emit countChanged();
}

void DataGridModelEx::refreshSort()
{
    if(m_sorted)
    {
        sortByKey(m_sortKey,m_sortOrder);
    }
}
