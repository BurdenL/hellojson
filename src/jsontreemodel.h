#ifndef JSONTREEMODEL_H
#define JSONTREEMODEL_H

#include "jsonindex.h"
#include <QAbstractItemModel>
#include <QAbstractTableModel>

// Owns UTF-8 source and flat nodes. QModelIndex stores a node ID, never a pointer.
// Display strings are decoded only when the view asks for a visible cell.
class JsonTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    explicit JsonTreeModel(QObject *parent = nullptr);
    bool setJson(const QByteArray &source);
    void clear();
    void retranslate();
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex & = {}) const override { return 3; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    const IndexNode *node(const QModelIndex &index) const;
    QString key(const QModelIndex &index) const;
    QString value(const QModelIndex &index) const;
    QString path(const QModelIndex &index) const;
    QByteArray json(const QModelIndex &index, bool pretty = false) const;
    QString similarValues(const QModelIndex &index) const;
    QModelIndex indexForId(uint32_t id, int column = 0) const;
    QModelIndex indexAtOffset(int byteOffset) const;
    QVector<uint32_t> findNodes(const QString &query) const;
    int errorOffset() const { return m_index.errorOffset(); }
    QString errorMessage() const { return m_index.errorMessage(); }
    const QByteArray &source() const { return m_source; }
    static QByteArray format(const QByteArray &validJson, bool pretty, int outputLimit = 8 * 1024 * 1024);
    static QString quote(const QString &text);
    static QString unescape(const QString &text);
private:
    QByteArray m_source;
    JsonIndex m_index;
    QVector<uint32_t> m_children;
    QVector<uint32_t> m_starts;
    bool m_valid = false;
};

// A flat view of the selected node's children (or the selected scalar itself).
class JsonDetailsModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit JsonDetailsModel(JsonTreeModel *source, QObject *parent = nullptr);
    void select(const QModelIndex &index);
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex & = {}) const override { return 2; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    QModelIndex sourceIndex(int row) const;
private:
    JsonTreeModel *m_source;
    QModelIndex m_selected;
};
#endif
