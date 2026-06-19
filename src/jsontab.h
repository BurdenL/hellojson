#ifndef JSONTAB_H
#define JSONTAB_H

#include <QWidget>
#include <QJsonDocument>
#include <QJsonValue>
#include <QTextCursor>

class QPlainTextEdit;
class QTreeWidget;
class QTreeWidgetItem;
class QSplitter;
class QPushButton;

class JsonTab : public QWidget
{
    Q_OBJECT

public:
    explicit JsonTab(QWidget *parent = nullptr);

    QString text() const;
    void setText(const QString &text);

    void formatJson(bool compressed);
    void clear();

    bool hasDocument() const { return m_hasValidDocument; }

    // Tree visibility
    void setTreeVisible(bool visible);
    bool isTreeVisible() const;

    // Search
    void findText(const QString &text);
    bool findNext(const QString &text);
    bool findPrev(const QString &text);
    void clearFind();
    int  matchCount() const { return m_matchPositions.size(); }
    int  currentMatchIndex() const { return m_currentMatch; }

    void expandAll();
    void collapseAll();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onTreeContextMenu(const QPoint &pos);

signals:
    void contentChanged();
    void treeVisibilityChanged(bool visible);

private:
    void populateJsonTree(const QJsonValue &value,
                          QTreeWidgetItem *parent,
                          const QString &key);
    void rebuildTree();

    QPlainTextEdit     *m_inputEdit;
    QTreeWidget        *m_treeWidget;
    QWidget            *m_treeContainer;
    QPushButton        *m_showTreeBtn;
    QSplitter          *m_splitter;
    QJsonDocument       m_lastDocument;
    bool                m_hasValidDocument = false;

    // Search state
    QList<QTextCursor>  m_matchPositions;
    int                 m_currentMatch = -1;
};

#endif // JSONTAB_H
