#pragma once
#include <QCoreApplication>

// 普通编辑器沿用 MainWindow 翻译上下文；拆分源码不能改变已有 TS 键。
inline QString trMain(const char *source, const char *comment = nullptr)
{
    return QCoreApplication::translate("MainWindow", source, comment);
}
