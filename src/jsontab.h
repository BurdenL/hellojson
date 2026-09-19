#ifndef JSONTAB_H
#define JSONTAB_H

#include <QWidget>
#include <QTextCursor>
#include <QModelIndex>

class QPlainTextEdit;
class QTreeView;
class QTableView;
class QLabel;
class JsonTreeModel;
class JsonDetailsModel;
class QSplitter;
class QPushButton;
class QStackedWidget;
class LargeFileView;

class JsonTab : public QWidget
{
    Q_OBJECT

public:
    explicit JsonTab(QWidget *parent = nullptr);
    enum class OpenMode { Automatic, LargeFile };
    bool openFile(const QString &path, OpenMode mode, QString *error);
    bool isLargeFile() const { return m_largeFileView != nullptr; }
    QString filePath() const { return m_filePath; }

    QString text() const;
    void setText(const QString &text);

    void formatJson(bool compressed);
    void clear();

    bool hasDocument() const { return m_hasValidDocument; }
    QString parseError() const;
    void refreshLanguage();
    void toggleLayout();
    void paste();
    void removeNewlines();
    void removeBackslashes();
    void setNodeSearch(bool enabled);
    bool isNodeSearch() const { return m_nodeSearch; }

    // Tree visibility
    void setTreeVisible(bool visible);
    bool isTreeVisible() const;

    // Search
    void findText(const QString &text);
    bool findNext(const QString &text);
    bool findPrev(const QString &text);
    void clearFind();
    int  matchCount() const { return m_nodeSearch ? m_nodeMatches.size() : m_matchPositions.size(); }
    int  currentMatchIndex() const { return m_nodeSearch ? m_currentNodeMatch : m_currentMatch; }

    void expandAll();
    void collapseAll();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onTreeContextMenu(const QPoint &pos);

signals:
    void contentChanged();
    void treeVisibilityChanged(bool visible);
    void searchResultsChanged();

private:
    void selectNodeMatch(int position);
    void showNode(const QModelIndex &index);
    void showContextMenu(const QModelIndex &index, const QPoint &globalPos);
    void replaceText(const QString &text);

    QPlainTextEdit     *m_inputEdit;
    QStackedWidget     *m_pages;
    LargeFileView     *m_largeFileView = nullptr;
    QString            m_filePath;
    QTreeView          *m_treeView;
    QTableView         *m_detailsView;
    JsonTreeModel      *m_model;
    JsonDetailsModel   *m_detailsModel;
    QLabel            *m_pathLabel;
    QLabel            *m_treeLabel;
    QPushButton       *m_hideTreeBtn;
    QWidget            *m_treeContainer;
    QPushButton        *m_showTreeBtn;
    QSplitter          *m_splitter;
    bool                m_hasValidDocument = false;
    bool                m_nodeSearch = false;
    QVector<uint32_t>   m_nodeMatches;
    int                m_currentNodeMatch = -1;
    QString            m_searchText;

    // Search state
    QList<QTextCursor>  m_matchPositions;
    int                 m_currentMatch = -1;
};

#endif // JSONTAB_H
