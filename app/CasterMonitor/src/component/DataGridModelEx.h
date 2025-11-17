#pragma once

#include <QObject>
#include <QAbstractListModel>
#include <QQmlEngine>
#include <QItemSelectionModel>
#include "stdafx.h"

class DataGridModelEx : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY_AUTO(QList<QVariant>, sourceData)
    QML_ELEMENT
public:
    explicit DataGridModelEx(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    int count() const;
    Q_INVOKABLE void clear();
    Q_INVOKABLE void append(QJSValue data);
    Q_INVOKABLE QVariant get(int index);
    Q_INVOKABLE bool move(int from, int to, int n);
    Q_INVOKABLE bool remove(int index, int count = 1);
    Q_INVOKABLE bool insert(int index, QJSValue data);
    Q_INVOKABLE bool set(int index, QJSValue data);
    Q_INVOKABLE void removeItems(const QModelIndexList &list);
    Q_INVOKABLE void selectRange(QItemSelectionModel *selectionModel, int startRow, int endRow);
    Q_INVOKABLE void sortByKey(const QString &key, bool ascending = true);
    Q_INVOKABLE void clearSort();
    Q_INVOKABLE void refreshSort();

Q_SIGNALS:
    void countChanged();

private:
    void insertRole(const QString &name);

private:
    void emitItemsChanged(int index, int count, const QVector<int> &roles);
    void updateValues(const QVariant &val);
    QList<QString> m_roles;

    QString m_sortKey;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
    bool m_sorted = false; // 标记是否已经排序

};
