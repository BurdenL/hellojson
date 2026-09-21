#include "boundededitor.h"
#include <QMimeData>
#include <QKeyEvent>
#include <QInputMethodEvent>

BoundedEditor::BoundedEditor(QWidget *parent) : QPlainTextEdit(parent) {}

bool BoundedEditor::accepts(qsizetype count)
{
    // characterCount 包含文档末尾的隐式段落标记，选区替换则先减去旧内容。
    const qint64 remaining = document()->characterCount() - 1 - textCursor().selectedText().size();
    if (remaining + count <= CharacterLimit) return true;
    if (rejected) rejected();
    return false;
}

void BoundedEditor::insertFromMimeData(const QMimeData *source)
{
    // Qt 的右键粘贴、键盘粘贴和拖放最终都经过这里。
    if (source->hasText() && accepts(source->text().size()))
        QPlainTextEdit::insertFromMimeData(source);
}

void BoundedEditor::keyPressEvent(QKeyEvent *event)
{
    // 超限时仍允许删除、快捷键和撤销，不让用户困在无法缩减的文档里。
    const bool insertsText = !event->text().isEmpty()
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier))
        && event->key() != Qt::Key_Backspace && event->key() != Qt::Key_Delete;
    if (insertsText && !accepts(event->text().size())) return;
    QPlainTextEdit::keyPressEvent(event);
}

void BoundedEditor::inputMethodEvent(QInputMethodEvent *event)
{
    if (accepts(event->commitString().size()))
        QPlainTextEdit::inputMethodEvent(event);
}
