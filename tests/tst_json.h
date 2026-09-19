#pragma once
#include <QObject>
class JsonTests : public QObject
{
    Q_OBJECT
private slots:
    void parse_data();
    void parse();
    void modelAndCopy();
    void losslessFormatting();
    void generatedDocuments();
    void largeArrayAndDepth();
    void tabIntegration();
    void windowIntegration();
    void clipboardActions();
};
