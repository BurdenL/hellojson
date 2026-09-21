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
    void collapsedTreeButtonPosition();
    void windowIntegration();
    void languageSwitching();
    void safeSaveAndModification();
    void unsavedProtection();
    void editingBudgets();
    void settingsPersistence();
    void clipboardActions();
    void utf8Pages_data();
    void utf8Pages();
    void boundedSources();
    void largeFileIntegration();
    void largeTreeParse_data();
    void largeTreeParse();
    void largeTreeScanner();
    void largeTreeModel();
    void largeTreeCache();
    void largeTreeCancellation();
    void largeTransform_data();
    void largeTransform();
    void largeTaskSearch();
    void largeTaskFiles();
    void largeTaskLocate();
    void largeTaskCancellation();
    void largeDepthBoundary_data();
    void largeDepthBoundary();
    void largeModeSwitching();
};
