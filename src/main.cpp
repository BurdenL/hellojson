#include "mainwindow.h"

#include <QApplication>
#include <QIcon>
#include <QTimer>
#include <QSettings>
#include <QTemporaryDir>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setApplicationName("HelloJson");
    a.setApplicationVersion(QStringLiteral(HELLOJSON_VERSION));
    a.setOrganizationName("HelloJson");
    a.setOrganizationDomain("io.github.burdenl");
#if defined(Q_OS_LINUX)
    // Match the desktop entry so Wayland shells can associate windows and icons.
    a.setDesktopFileName("io.github.burdenl.hellojson");
#endif
    // Multiple raster sizes avoid an SVG plugin dependency at startup.
    QIcon icon;
    for (int size : {16, 20, 24, 32, 40, 48, 64, 128, 256, 512, 1024})
        icon.addFile(QString(":/icons/hellojson-%1.png").arg(size), QSize(size, size));
    a.setWindowIcon(icon);

    QTemporaryDir smokeSettings;
    if (a.arguments().contains("--smoke-test"))
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, smokeSettings.path());
    MainWindow w;
    w.show();
    if (a.arguments().contains("--smoke-test"))
        QTimer::singleShot(1000, &w, &QWidget::close);

    return QApplication::exec();
}
