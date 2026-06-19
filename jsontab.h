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

    // Search
    void findText(const QString &text);
    bool findNext(const QString &text);
    bool findPrev(const QString &text);
    void clearFind();
    int  matchCount() const { return m_matchPositions.size(); }
    int  currentMatchIndex() const { return m_currentMatch; }

    void expandAll();
    void collapseAll();

private slots:
    void onTreeContextMenu(const QPoint &pos);

signals:
    void contentChanged();

private:
    void populateJsonTree(const QJsonValue &value,
                          QTreeWidgetItem *parent,
                          const QString &key);
    void rebuildTree();

    QPlainTextEdit     *m_inputEdit;
    QTreeWidget        *m_treeWidget;
    QSplitter          *m_splitter;
    QJsonDocument       m_lastDocument;
    bool                m_hasValidDocument = false;

    // Search state
    QList<QTextCursor>  m_matchPositions;
    int                 m_currentMatch = -1;
};

#endif // JSONTAB_H
