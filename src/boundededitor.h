#pragma once
#include "editorlimits.h"
#include <QPlainTextEdit>
#include <functional>

// 普通编辑器的所有用户输入共用容量检查，不能只限制工具栏的粘贴按钮。
// rejected 仅报告原因，不清空文档，也不接管撤销栈。
class BoundedEditor final : public QPlainTextEdit
{
public:
    static constexpr int CharacterLimit = EditorLimits::InputCharacters;
    explicit BoundedEditor(QWidget *parent);
    std::function<void()> rejected;

protected:
    void insertFromMimeData(const QMimeData *source) override;
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;

private:
    bool accepts(qsizetype count);
};
