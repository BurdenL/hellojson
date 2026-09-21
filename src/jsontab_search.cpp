#include "editorlimits.h"
#include "jsontab.h"
#include "jsontreemodel.h"
#include "uistrings.h"
#include <QPlainTextEdit>
#include <QTreeView>
#include <QLabel>

// 两种搜索共享界面，但保存不同的定位信息：文本光标或稳定节点 ID。
// 文档/模型变化后必须同时清理两类结果，避免使用旧位置。

void JsonTab::setNodeSearch(bool enabled)
{
    clearFind();
    m_nodeSearch = enabled;
}

void JsonTab::selectNodeMatch(int position)
{
    if (position < 0 || position >= m_nodeMatches.size()) return;
    m_currentNodeMatch = position;
    auto idx = m_model->indexForId(m_nodeMatches[position]);
    setTreeVisible(true);
    for (auto parent = idx.parent(); parent.isValid(); parent = parent.parent())
        m_treeView->expand(parent);
    m_treeView->setCurrentIndex(idx);
    m_treeView->scrollTo(idx);
    showNode(idx);
}

void JsonTab::showNode(const QModelIndex &idx)
{
    const auto *n = m_model->node(idx);
    if (!n) return;
    QTextCursor cursor(m_inputEdit->document());
    const auto &source = m_model->source();
    const int start = QString::fromUtf8(source.constData(), int(n->valueOffset)).size();
    const int length = QString::fromUtf8(source.constData() + n->valueOffset, int(n->valueLen)).size();
    cursor.setPosition(start);
    cursor.setPosition(start + length, QTextCursor::KeepAnchor);
    m_inputEdit->setTextCursor(cursor);
    m_inputEdit->ensureCursorVisible();
}

void JsonTab::expandAll()
{
    m_treeView->expandAll();
}

void JsonTab::collapseAll()
{
    m_treeView->collapseAll();
}

void JsonTab::findText(const QString &text)
{
    if (isLargeFile()) return;
    m_searchLimited = false;
    m_searchText = text;
    clearFind();
    if (text.isEmpty()) return;
    if (m_nodeSearch) {
        if (!m_hasValidDocument) {
            m_hasValidDocument = m_model->setJson(this->text().toUtf8());
            if (!m_hasValidDocument) return;
        }
        m_nodeMatches = m_model->findNodes(text);
        m_searchLimited = m_nodeMatches.size() >= EditorLimits::SearchMatches;
        selectNodeMatch(0);
        return;
    }

    QTextDocument *doc = m_inputEdit->document();
    QTextCursor cursor(doc);

    QTextCharFormat highlightFmt;
    highlightFmt.setBackground(QColor(255, 255, 0));    // yellow
    highlightFmt.setForeground(Qt::black);

    QTextCharFormat activeFmt;
    activeFmt.setBackground(QColor(255, 165, 0));       // orange

    while (true) {
        cursor = doc->find(text, cursor);
        if (cursor.isNull()) break;
        if (m_matchPositions.size() >= EditorLimits::SearchMatches) { m_searchLimited = true; break; }
        m_matchPositions.append(cursor);
    }

    // Apply highlights
    QList<QTextEdit::ExtraSelection> extras;
    for (int i = 0; i < m_matchPositions.size(); ++i) {
        QTextEdit::ExtraSelection sel;
        sel.cursor = m_matchPositions[i];
        sel.format = (i == 0) ? activeFmt : highlightFmt;
        extras.append(sel);
    }
    m_inputEdit->setExtraSelections(extras);

    // Jump to first match
    if (!m_matchPositions.isEmpty()) {
        m_currentMatch = 0;
        m_inputEdit->setTextCursor(m_matchPositions[0]);
        m_inputEdit->ensureCursorVisible();
    }
}

bool JsonTab::findNext(const QString &text)
{
    if (isLargeFile()) return false;
    if (m_nodeSearch) {
        if (m_nodeMatches.isEmpty()) findText(text);
        else selectNodeMatch((m_currentNodeMatch + 1) % m_nodeMatches.size());
        return !m_nodeMatches.isEmpty();
    }
    if (m_matchPositions.isEmpty()) {
        findText(text);
        return !m_matchPositions.isEmpty();
    }

    int prev = m_currentMatch;
    m_currentMatch = (m_currentMatch + 1) % m_matchPositions.size();

    // Update highlight: prev → yellow, current → orange
    QList<QTextEdit::ExtraSelection> extras = m_inputEdit->extraSelections();
    if (prev >= 0 && prev < extras.size())
        extras[prev].format.setBackground(QColor(255, 255, 0));
    if (m_currentMatch >= 0 && m_currentMatch < extras.size())
        extras[m_currentMatch].format.setBackground(QColor(255, 165, 0));
    m_inputEdit->setExtraSelections(extras);

    m_inputEdit->setTextCursor(m_matchPositions[m_currentMatch]);
    m_inputEdit->ensureCursorVisible();
    return true;
}

bool JsonTab::findPrev(const QString &text)
{
    if (isLargeFile()) return false;
    if (m_nodeSearch) {
        if (m_nodeMatches.isEmpty()) findText(text);
        else selectNodeMatch((m_currentNodeMatch - 1 + m_nodeMatches.size()) % m_nodeMatches.size());
        return !m_nodeMatches.isEmpty();
    }
    if (m_matchPositions.isEmpty()) {
        findText(text);
        return !m_matchPositions.isEmpty();
    }

    int prev = m_currentMatch;
    m_currentMatch = (m_currentMatch - 1 + m_matchPositions.size()) % m_matchPositions.size();

    QList<QTextEdit::ExtraSelection> extras = m_inputEdit->extraSelections();
    if (prev >= 0 && prev < extras.size())
        extras[prev].format.setBackground(QColor(255, 255, 0));
    if (m_currentMatch >= 0 && m_currentMatch < extras.size())
        extras[m_currentMatch].format.setBackground(QColor(255, 165, 0));
    m_inputEdit->setExtraSelections(extras);

    m_inputEdit->setTextCursor(m_matchPositions[m_currentMatch]);
    m_inputEdit->ensureCursorVisible();
    return true;
}

void JsonTab::clearFind()
{
    m_nodeMatches.clear();
    m_currentNodeMatch = -1;
    m_matchPositions.clear();
    m_currentMatch = -1;
    m_inputEdit->setExtraSelections({});
}
