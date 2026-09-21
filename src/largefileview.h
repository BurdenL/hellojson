#pragma once
#include "jsondatasource.h"
#include "largetreemodel.h"
#include "largefiletasks.h"
#include <QWidget>
#include <atomic>
#include <memory>

class QThread;
class QPlainTextEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeView;
class QVBoxLayout;
class QComboBox;
class QToolButton;
class QAction;

class PageReader : public QObject
{
    Q_OBJECT
public:
    explicit PageReader(std::shared_ptr<std::atomic<quint64>> latest);
public slots:
    void load(const QString &path, qint64 page, quint64 request, bool reopen);
signals:
    void ready(const JsonTextPage &page);
private:
    std::shared_ptr<std::atomic<quint64>> m_latest;
    QString m_path;
    std::unique_ptr<FileJsonSource> m_source;
};

class LargeFileView : public QWidget
{
    Q_OBJECT
public:
    explicit LargeFileView(QWidget *parent = nullptr);
    ~LargeFileView() override;
    // Observe lifetime only; ownership remains internal (deleted after finishing).
    QThread *workerThreadForDiagnostics() const { return m_thread; }
    void openFile(const QString &path);
    void goToByte(qint64 offset);
    void retranslate();
    bool isLoading() const { return m_loading; }
    const JsonTextPage &currentPage() const { return m_page; }
    QString filePath() const { return m_path; }
signals:
    void requestPage(const QString &path, qint64 page, quint64 request, bool reopen);
    void pageLoaded();
private:
    void load(qint64 page, bool reopen = false);
    void updateControls();
    QModelIndex activeContainer() const;
    void updateTreeControls();
    void setupOperations(QVBoxLayout *layout);
    void updateTaskControls();
    void startTask(LargeTaskRequest request);
    void searchFile(bool more);
    void locateCursor();
    void nodeTask(LargeTaskKind kind);
    void saveTransformed(bool compact);
    qint64 cursorByteOffset() const;
    LargeFileTasks *m_tasks;
    QLineEdit *m_query;
    QComboBox *m_matches;
    QPushButton *m_search, *m_searchMore, *m_cancelTask, *m_locate;
    QToolButton *m_fileOperations;
    QAction *m_formatFile, *m_compactFile;
    QLabel *m_taskStatus;
    qint64 m_searchNext = 0, m_fileSize = -1;
    QDateTime m_fileModified;
    QString m_searchQuery;
    bool m_searchHasMore = false;
    LargeTreeModel *m_treeModel;
    QTreeView *m_tree;
    QLabel *m_treeStatus;
    QPushButton *m_moreChildren;
    QPushButton *m_previousChildren;
    QPushButton *m_nextChildren;
    QPushButton *m_cancelScan;
    QThread *m_thread;
    std::shared_ptr<std::atomic<quint64>> m_latest;
    QString m_path;
    JsonTextPage m_page;
    bool m_loading = false;
    qint64 m_requestedOffset = -1;
    QLabel *m_notice;
    QLabel *m_status;
    QLabel *m_jumpLabel;
    QPlainTextEdit *m_text;
    QLineEdit *m_offset;
    QPushButton *m_first;
    QPushButton *m_previous;
    QPushButton *m_next;
    QPushButton *m_last;
    QPushButton *m_jump;
};
