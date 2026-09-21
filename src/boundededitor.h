#pragma once
#include <QPlainTextEdit>
#include <QMimeData>
#include <QKeyEvent>
#include <QInputMethodEvent>
#include <functional>

// Covers menu paste, keyboard paste, drag/drop and input-method commits.
class BoundedEditor final : public QPlainTextEdit
{
public:
    static constexpr int CharacterLimit = 2 * 1024 * 1024;
    explicit BoundedEditor(QWidget *parent) : QPlainTextEdit(parent) {}
    std::function<void()> rejected;
protected:
    bool accepts(qsizetype count) {
        if (qint64(document()->characterCount() - 1) - textCursor().selectedText().size() + count <= CharacterLimit) return true;
        if (rejected) rejected();
        return false;
    }
    void insertFromMimeData(const QMimeData *source) override {
        if (source->hasText() && accepts(source->text().size())) QPlainTextEdit::insertFromMimeData(source);
    }
    void keyPressEvent(QKeyEvent *event) override {
        if (!event->text().isEmpty() && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier)) &&
            event->key() != Qt::Key_Backspace && event->key() != Qt::Key_Delete && !accepts(event->text().size())) return;
        QPlainTextEdit::keyPressEvent(event);
    }
    void inputMethodEvent(QInputMethodEvent *event) override {
        if (accepts(event->commitString().size())) QPlainTextEdit::inputMethodEvent(event);
    }
};
