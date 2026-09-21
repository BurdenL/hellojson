#include "jsontab.h"
#include "jsontreemodel.h"
#include "uistrings.h"
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QTreeView>

// 树和详情表共用相同的节点操作，复制时使用原始 JSON 而非截断预览。

void JsonTab::onTreeContextMenu(const QPoint &pos)
{
    showContextMenu(m_treeView->indexAt(pos), m_treeView->viewport()->mapToGlobal(pos));
}

void JsonTab::showContextMenu(const QModelIndex &idx, const QPoint &globalPos)
{
    if (!idx.isValid()) return;
    QMenu menu(this);
    const QStringList labels = {
        trMain("Copy Value"), trMain("Copy Key"), trMain("Copy Path"),
        trMain("Copy Key / Value"), trMain("Copy Node JSON"), trMain("Copy Similar Values"),
        trMain("Copy MAP Entry"), trMain("Copy Formatted Node JSON")
    };
    QList<QAction *> actions;
    for (const auto &label : labels) actions.append(menu.addAction(label));
    menu.addSeparator();
    auto *locate = menu.addAction(trMain("Locate in Text"));
    auto *expand = menu.addAction(trMain("Expand Subtree"));
    auto *collapse = menu.addAction(trMain("Collapse Subtree"));
    QAction *chosen = menu.exec(globalPos);
    if (!chosen) return;
    if (chosen == locate) { showNode(idx); return; }
    const auto root = idx.sibling(idx.row(), 0);
    if (chosen == expand) { m_treeView->expandRecursively(root); return; }
    if (chosen == collapse) {
        QList<QModelIndex> pending{root};
        while (!pending.isEmpty()) {
            auto current = pending.takeLast();
            if (!m_treeView->isExpanded(current)) continue;
            m_treeView->collapse(current);
            for (int r = 0; r < m_model->rowCount(current); ++r)
                pending.append(m_model->index(r, 0, current));
        }
        return;
    }
    QString result;
    switch (actions.indexOf(chosen)) {
    case 0:
        result = m_model->node(idx)->isContainer()
            ? QString::fromUtf8(m_model->json(idx)) : m_model->value(idx); break;
    case 1: result = m_model->key(idx); break;
    case 2: result = m_model->path(idx); break;
    case 3: result = JsonTreeModel::quote(m_model->key(idx)) + ": " + QString::fromUtf8(m_model->json(idx)); break;
    case 4: result = QString::fromUtf8(m_model->json(idx)); break;
    case 5: result = m_model->similarValues(idx); break;
    case 6: result = JsonTreeModel::quote(m_model->key(idx)) + "," + JsonTreeModel::quote(m_model->value(idx)); break;
    case 7: result = QString::fromUtf8(m_model->json(idx, true)); break;
    default: return;
    }
    QApplication::clipboard()->setText(result);
}
