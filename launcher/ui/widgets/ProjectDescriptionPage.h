#pragma once

#include <QTextBrowser>

#include "QObjectPtr.h"
#include "modplatform/ModIndex.h"
#include "tasks/Task.h"

class QTabBar;

QT_BEGIN_NAMESPACE
class VariableSizedImageObject;
QT_END_NAMESPACE

/** This subclasses QTextBrowser to provide additional capabilities
 *  to it, like allowing for images to be shown.
 */
class ProjectDescriptionPage final : public QTextBrowser {
    Q_OBJECT

   public:
    ProjectDescriptionPage(QWidget* parent = nullptr);

    void setMetaEntry(QString entry);
    void setProject(ModPlatform::IndexedPack::Ptr pack, int selectedVersion = -1);
    void setSelectedVersion(int index);
    void setProjectNotice(const QString& notice);

   signals:
    void projectVersionSelected(int index);

   protected:
    void resizeEvent(QResizeEvent* event) override;
    QVariant loadResource(int type, const QUrl& name) override;

   public slots:
    /** Flushes the current processing happening in the page.
     *
     *  Should be called when changing the page's content entirely, to
     *  prevent old tasks from changing the new content.
     */
    void flush();

   private:
    void renderProject(bool restoreScroll = true);
    void loadChangelog();
    void openProjectLink(const QUrl& url);
    QTabBar* m_tabs = nullptr;
    ModPlatform::IndexedPack::Ptr m_pack;
    int m_selectedVersion = -1;
    int m_releaseLimit = 100;
    QHash<QString, QString> m_changelogs;
    Task::Ptr m_changelogTask;
    QString m_changelogKey;
    QString m_changelogError;
    quint64 m_projectGeneration = 0;
    shared_qobject_ptr<VariableSizedImageObject> m_image_text_object;
};
