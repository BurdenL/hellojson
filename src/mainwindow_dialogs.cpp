#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "jsontab.h"
#include "jsontreemodel.h"
#include <QDialog>
#include <QMessageBox>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

// 独立工具与说明对话框，不持有当前标签页的文档状态。

void MainWindow::showUnicodeConverter()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Unicode / Escape Conversion"));
    dialog.resize(650, 450);
    auto *layout = new QVBoxLayout(&dialog);
    auto *source = new QPlainTextEdit(&dialog);
    source->setPlaceholderText(tr("Input escaped text"));
    auto *output = new QPlainTextEdit(&dialog);
    output->setReadOnly(true);
    auto *convert = new QPushButton(tr("Decode Escapes"), &dialog);
    auto *encode = new QPushButton(tr("Encode Unicode"), &dialog);
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(convert);
    buttons->addWidget(encode);
    layout->addWidget(source);
    layout->addLayout(buttons);
    layout->addWidget(output);
    connect(convert, &QPushButton::clicked, &dialog, [source, output] {
        output->setPlainText(JsonTreeModel::unescape(source->toPlainText()));
    });
    connect(encode, &QPushButton::clicked, &dialog, [source, output] {
        QString encoded;
        for (QChar c : source->toPlainText()) {
            if (c.unicode() > 0x7f) encoded += QStringLiteral("\\u%1").arg(uint(c.unicode()), 4, 16, QChar('0'));
            else {
                QString quoted = JsonTreeModel::quote(QString(c));
                encoded += quoted.mid(1, quoted.size() - 2);
            }
        }
        output->setPlainText(encoded);
    });
    dialog.exec();
}

void MainWindow::onAbout()
{
    QMessageBox::about(this, tr("About Hello JSON"),
        tr("<h3>Hello JSON v0.1</h3>"
           "<p>A lightweight JSON formatter and viewer built with Qt.</p>"
           "<p>Features:<br>"
           "&bull; JSON format &amp; compress<br>"
           "&bull; Tree structure viewer<br>"
           "&bull; Multi-tab editing<br>"
           "&bull; Text search<br>"
           "&bull; Copy value / path</p>"
           "<p style='margin-top:12pt'>"
           "<b>Developer:</b> BurdenL<br>"
           "<b>GitHub:</b> <a href='https://github.com/BurdenL/hellojson'>"
           "github.com/BurdenL/hellojson</a></p>"));
}

void MainWindow::onOpenSource()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("Open Source Notice"));
    dlg.resize(520, 360);
    auto *layout = new QVBoxLayout(&dlg);

    auto *browser = new QTextBrowser(&dlg);
    browser->setOpenExternalLinks(true);
    browser->setHtml(
        tr("<h3>Open Source Software Notice</h3>"
           "<p>Hello JSON is built with the following open source components:</p>"
           "<p><b>Qt</b> &mdash; Cross-platform application framework<br>"
           "Version: 6.x &nbsp;|&nbsp; "
           "<a href='https://www.qt.io/licensing/'>License</a><br>"
           "Qt is available under the LGPL v3 / GPL v2 / Commercial license.</p>"
           "<p><b>GCC / MinGW-w64</b><br>"
           "C++ compiler toolchain</p>"
           "<p>Full source code of this application is available at:<br>"
           "<a href='https://github.com/burden/hellojson'>"
           "github.com/burden/hellojson</a></p>"));
    browser->setReadOnly(true);
    layout->addWidget(browser);

    auto *closeBtn = new QPushButton(tr("Close"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout->addWidget(closeBtn);

    dlg.exec();
}

void MainWindow::onLicense()
{
    QDialog dlg(this);
    dlg.setWindowTitle(tr("License"));
    dlg.resize(520, 420);
    auto *layout = new QVBoxLayout(&dlg);

    auto *browser = new QTextBrowser(&dlg);
    browser->setReadOnly(true);
    browser->setPlainText(
        "MIT License\n\n"
        "Copyright (c) 2025 Hello JSON Contributors\n\n"
        "Permission is hereby granted, free of charge, to any person obtaining a copy\n"
        "of this software and associated documentation files (the \"Software\"), to deal\n"
        "in the Software without restriction, including without limitation the rights\n"
        "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell\n"
        "copies of the Software, and to permit persons to whom the Software is\n"
        "furnished to do so, subject to the following conditions:\n\n"
        "The above copyright notice and this permission notice shall be included in\n"
        "all copies or substantial portions of the Software.\n\n"
        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
        "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
        "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n"
        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN\n"
        "THE SOFTWARE.\n");
    layout->addWidget(browser);

    auto *closeBtn = new QPushButton(tr("Close"), &dlg);
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout->addWidget(closeBtn);

    dlg.exec();
}

// A corner button cannot be dragged into the document tabs.
