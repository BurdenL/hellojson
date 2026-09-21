#include "mainwindow.h"

#include <QApplication>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QTemporaryDir smokeSettings;
    if (a.arguments().contains("--smoke-test"))
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, smokeSettings.path());
    MainWindow w;
    w.show();
    if (a.arguments().contains("--smoke-test"))
        QTimer::singleShot(1000, &w, &QWidget::close);

    return QApplication::exec();
}
