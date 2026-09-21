#pragma once
#include "largetreeindex.h"
#include <QAbstractItemModel>
#include <vector>
class QThread;

class LargeTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    static constexpr int WindowSize = 256;
    static constexpr int ResidentLimit = 4096;
    explicit LargeTreeModel(QObject *parent = nullptr);
    ~LargeTreeModel() override;
    // Observe lifetime only; ownership remains internal (deleted after finishing).
    QThread *workerThreadForDiagnostics() const { return m_thread; }
    void openFile(const QString &path);
    QModelIndex index(int row, int column, const QModelIndex &parent = {}) const override;
    QModelIndex parent(const QModelIndex &index) const override;
    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex & = {}) const override { return 3; }
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    bool hasChildren(const QModelIndex &parent = {}) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;
    void requestMore(const QModelIndex &parent);
    void changePage(const QModelIndex &parent, bool next);
    void releaseChildren(const QModelIndex &parent);
    void cancel();
    void retranslate();
    bool busy() const { return m_busy; }
    int residentNodes() const { return m_resident; }
    QString temporaryIndexPath() const { return m_lastBatch.cachePath; }
    qint64 diskBytes() const { return m_lastBatch.diskBytes; }
    bool lastCacheHit() const { return m_lastBatch.cacheHit; }
    qint64 firstRow(const QModelIndex &index) const;
    bool canNextPage(const QModelIndex &index) const;
    bool hasMore(const QModelIndex &index) const;
    QModelIndex revealPath(const QVector<LargeTreeRecord> &path);
    const LargeTreeRecord *record(const QModelIndex &index) const;
signals:
    void scanRequested(const LargeTreeRequest &request);
    void stateChanged();
    void statusChanged(const QString &text);
    void batchLoaded();
private:
    // 仅拥有当前窗口的节点；翻页/折叠可删除节点，旧 QModelIndex 随之失效。
    struct Node {
        LargeTreeRecord record;
        Node *parent = nullptr;
        int row = 0;
        qint64 firstRow = 0, nextOffset = -1;
        bool done = false;
        bool manualFetch = false; // revealed ancestors wait for an explicit expansion/load
        std::vector<std::unique_ptr<Node>> children;
    };
    Node *node(const QModelIndex &index) const;
    QModelIndex indexFor(Node *node, int column = 0) const;
    int count(Node *node) const;
    void submit(Node *node, bool root);
    void accept(const LargeTreeBatch &batch);
    QString m_path;
    // 不以界面为 parent：关闭后线程可能仍在结束一次读取，由 finished 触发释放。
    QThread *m_thread;
    std::shared_ptr<std::atomic<quint64>> m_generation;
    std::unique_ptr<Node> m_root;
    Node *m_pending = nullptr;
    bool m_busy = false, m_paused = false;
    int m_resident = 0;
    LargeTreeBatch m_lastBatch;
};
