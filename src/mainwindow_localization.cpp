#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "jsontab.h"
#include <QApplication>
#include <QTranslator>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QEvent>
#include <QDebug>

// 持久化只保存界面偏好，不保存文档文本。内嵌翻译优先于外部文件。

void MainWindow::persistSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "HelloJson", "HelloJson");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("language", m_currentLocale);
    settings.setValue("lastDirectory", m_lastDirectory);
    if (auto *tab = currentTab()) settings.setValue("editorLayout", tab->viewState());
}

QString MainWindow::translationFilePath(const QString &locale) const
{
    const QString fileName = "hellojson_" + locale + ".qm";

    QStringList searchDirs;
    searchDirs << ":/i18n";
    searchDirs << QApplication::applicationDirPath();
    searchDirs << QDir::currentPath();

    for (const QString &dir : searchDirs) {
        QString fullPath = dir + "/" + fileName;
        if (QFile::exists(fullPath))
            return fullPath;
    }
    return fileName;
}

void MainWindow::loadTranslation(const QString &locale)
{
    qApp->removeTranslator(m_qtTranslator);
    if (locale != "en") {
        const QString name = "qtbase_" + locale + ".qm";
        if (m_qtTranslator->load(":/i18n/" + name) ||
            m_qtTranslator->load(QApplication::applicationDirPath() + "/" + name))
            qApp->installTranslator(m_qtTranslator);
    }
    if (locale == "en") {
        qApp->removeTranslator(m_translator);
        return;
    }

    QString path = translationFilePath(locale);
    if (m_translator->load(path)) {
        qApp->installTranslator(m_translator);
    } else {
        qWarning() << "Failed to load translation for locale:" << locale
                   << " tried path:" << path;
    }
}

void MainWindow::switchLanguage(const QString &locale)
{
    m_currentLocale = locale;
    qApp->removeTranslator(m_translator);
    loadTranslation(locale);

    ui->actionEnglish->setChecked(locale == "en");
    ui->actionChinese->setChecked(locale == "zh_CN");
    ui->actionTraditionalChinese->setChecked(locale == "zh_TW");

    ui->retranslateUi(this);

    // Refresh all tabs' tree labels in the new language
    for (int i = 0; i < ui->tabWidget->count(); ++i) {
        auto *tab = qobject_cast<JsonTab *>(ui->tabWidget->widget(i));
        if (tab) {
            tab->refreshLanguage();
            if (tab->property("defaultTitle").toBool())
                ui->tabWidget->setTabText(i, tr("Untitled"));
        }
    }
    refreshDynamicTexts();
    QSettings(QSettings::IniFormat, QSettings::UserScope, "HelloJson", "HelloJson").setValue("language", locale);
}

void MainWindow::onLanguageChanged()
{
    if (ui->actionTraditionalChinese->isChecked())
        switchLanguage("zh_TW");
    else if (ui->actionChinese->isChecked())
        switchLanguage("zh_CN");
    else
        switchLanguage("en");
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::LanguageChange) {
        ui->retranslateUi(this);
    }
    QMainWindow::changeEvent(event);
}
