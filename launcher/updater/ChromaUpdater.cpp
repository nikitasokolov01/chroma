// SPDX-License-Identifier: GPL-3.0-only
#include "ChromaUpdater.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextBlock>
#include <QImage>
#include <QVBoxLayout>
#include "Application.h"
#include "BuildConfig.h"
#include "ui/MainWindow.h"
#include "ui/widgets/InlineWorkspace.h"

ChromaUpdateTransfer::ChromaUpdateTransfer(QNetworkAccessManager* network, QObject* parent) : QObject(parent), m_network(network) {}
ChromaUpdateTransfer::~ChromaUpdateTransfer()
{
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
    }
}
void ChromaUpdateTransfer::start(const QUrl& url, qint64 maximum, qint64 expectedSize, const QByteArray& sha256, const QString& path)
{
    if (m_active)
        return;
    m_active = true;
    m_maximum = maximum;
    m_expectedSize = expectedSize;
    m_expectedHash = sha256;
    m_api = url.host() == "api.github.com";
    if (!path.isEmpty()) {
        m_file = std::make_unique<QSaveFile>(path);
        m_file->setDirectWriteFallback(false);
        if (!m_file->open(QIODevice::WriteOnly)) {
            fail(tr("The update could not be saved: %1").arg(m_file->errorString()));
            return;
        }
    }
    request(url);
}
void ChromaUpdateTransfer::request(const QUrl& url)
{
    const bool validApi = url.isValid() && url.scheme() == "https" && url.host() == "api.github.com" && url.userInfo().isEmpty() &&
                          url.fragment().isEmpty() && (url.port() == -1 || url.port() == 443) &&
                          url.path() == "/repos/" + ChromaUpdate::repository() + "/releases";
    if (!m_network || (m_api ? !validApi : !ChromaUpdate::allowedDownloadUrl(url))) {
        fail(tr("The update server returned an untrusted download address."));
        return;
    }
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(60000);
    request.setRawHeader("User-Agent", "Chroma-Updater");
    request.setRawHeader("Accept-Encoding", "identity");
    if (m_api) {
        request.setRawHeader("Accept", "application/vnd.github+json");
        request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    }
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, &ChromaUpdateTransfer::readAvailable);
    connect(m_reply, &QNetworkReply::downloadProgress, this, &ChromaUpdateTransfer::progress);
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        if (!m_active || !m_reply)
            return;
        auto* reply = m_reply.data();
        const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (code >= 300 && code < 400) {
            const QUrl next = reply->url().resolved(reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
            reply->disconnect(this);
            reply->deleteLater();
            m_reply.clear();
            if (++m_redirects > 5) {
                fail(tr("The update server redirected too many times."));
                return;
            }
            this->request(next);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || code != 200) {
            fail(tr("Could not download the update (HTTP %1): %2").arg(code).arg(reply->errorString()));
            return;
        }
        readAvailable();
        if (!m_active)
            return;
        if (m_expectedSize >= 0 && m_received != m_expectedSize) {
            fail(tr("The update download is incomplete. Try again."));
            return;
        }
        if (!m_expectedHash.isEmpty() && m_hash.result().toHex() != m_expectedHash) {
            fail(tr("The update checksum did not match. Nothing was installed."));
            return;
        }
        if (m_file && !m_file->commit()) {
            fail(tr("The verified update could not be saved."));
            return;
        }
        m_active = false;
        m_reply.clear();
        reply->deleteLater();
        emit succeeded(m_data);
    });
}
void ChromaUpdateTransfer::readAvailable()
{
    if (!m_active || !m_reply)
        return;
    if (m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
        m_reply->readAll();
        return;
    }
    while (m_reply && m_reply->bytesAvailable()) {
        const QByteArray bytes = m_reply->read(256 * 1024);
        m_received += bytes.size();
        if (m_received > m_maximum || (m_expectedSize >= 0 && m_received > m_expectedSize)) {
            fail(tr("The update exceeded its expected size."));
            return;
        }
        m_hash.addData(bytes);
        if (m_file) {
            if (m_file->write(bytes) != bytes.size()) {
                fail(tr("There is not enough space to save the update."));
                return;
            }
        } else {
            m_data += bytes;
        }
    }
}
void ChromaUpdateTransfer::fail(const QString& reason)
{
    if (!m_active)
        return;
    m_active = false;
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    if (m_file) {
        m_file->cancelWriting();
        m_file.reset();
    }
    emit failed(reason);
}
void ChromaUpdateTransfer::cancel()
{
    fail(tr("Update download cancelled."));
}

ChromaUpdater::ChromaUpdater(QWidget* parent, const QString& appDir, const QString& dataDir, bool portable, QNetworkAccessManager* network)
    : m_window(parent)
    , m_network(network)
    , m_settings(QDir(dataDir).filePath("chroma-updates.ini"), QSettings::IniFormat)
    , m_appDir(appDir)
    , m_dataDir(dataDir)
    , m_portable(portable)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &ChromaUpdater::checkBackgroundUpdates);
    schedule(true);
}
ChromaUpdater::~ChromaUpdater()
{
    cancelTransfer();
}
void ChromaUpdater::setWindow(QWidget* window)
{
    if (m_window == window)
        return;
    if (m_dialog)
        m_dialog->reject();
    m_window = window;
}
bool ChromaUpdater::getAutomaticallyChecksForUpdates()
{
    return m_settings.value("Automatic", true).toBool();
}
double ChromaUpdater::getUpdateCheckInterval()
{
    return qBound(0.0, m_settings.value("Interval", 3600).toDouble(), 604800.0);
}
bool ChromaUpdater::getBetaAllowed()
{
    return m_settings.value("Prereleases", true).toBool();
}
void ChromaUpdater::setAutomaticallyChecksForUpdates(bool check)
{
    m_settings.setValue("Automatic", check);
    schedule();
}
void ChromaUpdater::setUpdateCheckInterval(double seconds)
{
    m_settings.setValue("Interval", qBound(0.0, seconds, 604800.0));
    schedule();
}
void ChromaUpdater::setBetaAllowed(bool allowed)
{
    m_settings.setValue("Prereleases", allowed);
}
void ChromaUpdater::schedule(bool startup)
{
    m_timer.stop();
    if (m_keepStage || !getAutomaticallyChecksForUpdates())
        return;
    if (startup) {
        m_timer.start(5000);
        return;
    }
    const qint64 interval = static_cast<qint64>(getUpdateCheckInterval());
    if (!interval)
        return;
    const auto now = QDateTime::currentSecsSinceEpoch();
    const auto last = m_settings.value("LastCheck", 0).toLongLong();
    const auto elapsed = last > now ? interval : now - last;
    m_timer.start(static_cast<int>(qMax<qint64>(5, interval - elapsed) * 1000));
}
void ChromaUpdater::setStatus(const QString& text)
{
    m_status = text;
    if (m_message)
        m_message->setText(text);
    emit statusChanged(text);
}
void ChromaUpdater::checkForUpdates()
{
    check(true);
}
void ChromaUpdater::checkBackgroundUpdates()
{
    check(false);
}
void ChromaUpdater::showAvailableUpdate()
{
    if (m_keepStage)
        return;
    if (!m_release) {
        checkForUpdates();
        return;
    }
    // Review the known release immediately, even during a periodic refresh.
    // Package transfers continue in their existing details page.
    if (m_checking) {
        cancelTransfer();
        m_releases = {};
        m_checking = false;
        m_busy = false;
        schedule();
        emit canCheckForUpdatesChanged(true);
    }
    m_manual = true;
    m_dismissedTags.remove(m_release->tag);
    emit availableUpdateChanged();
    offer();
}
void ChromaUpdater::dismissAvailableUpdate()
{
    if (m_keepStage || !m_release || m_dismissedTags.contains(m_release->tag))
        return;
    m_dismissedTags.insert(m_release->tag);
    emit availableUpdateChanged();
}
void ChromaUpdater::check(bool manual)
{
    if (m_keepStage)
        return;
    if (m_busy || (!manual && m_dialog)) {
        if (manual && m_checking)
            m_manual = true;
        if (getAutomaticallyChecksForUpdates() && getUpdateCheckInterval() > 0)
            m_timer.start(60000);
        return;
    }
    if (m_dialog) {
        showAvailableUpdate();
        return;
    }
    m_manual = manual;
    m_busy = true;
    m_checking = true;
    // Keep the last valid offer throughout a refresh. A transient network
    // failure must not make an already discovered update disappear.
    if (manual && m_release) {
        m_dismissedTags.remove(m_release->tag);
        emit availableUpdateChanged();
    }
    m_releases = {};
    m_timer.stop();
    setStatus(tr("Checking GitHub for Chroma updates…"));
    emit canCheckForUpdatesChanged(false);
    fetchPage(1);
}
ChromaUpdateTransfer* ChromaUpdater::transfer()
{
    cancelTransfer();
    auto* task = new ChromaUpdateTransfer(m_network, this);
    m_transfer = task;
    connect(task, &ChromaUpdateTransfer::failed, this, &ChromaUpdater::fail);
    return task;
}
void ChromaUpdater::cancelTransfer()
{
    if (m_transfer) {
        m_transfer->disconnect(this);
        m_transfer->cancel();
        m_transfer->deleteLater();
        m_transfer.clear();
    }
}
void ChromaUpdater::fetchPage(int page)
{
    auto* task = transfer();
    connect(task, &ChromaUpdateTransfer::succeeded, this, [this, page](const QByteArray& data) {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(data, &error);
        if (error.error != QJsonParseError::NoError || !document.isArray()) {
            fail(tr("GitHub returned an invalid release list."));
            return;
        }
        const auto releases = document.array();
        for (const auto& release : releases)
            m_releases.append(release);
        if (releases.size() == 100) {
            if (page >= 10) {
                fail(tr("The release history is too large to check safely."));
                return;
            }
            fetchPage(page + 1);
        } else {
            finishCheck();
        }
    });
    task->start(QUrl("https://api.github.com/repos/" + ChromaUpdate::repository() + "/releases?per_page=100&page=" + QString::number(page)),
                4 * 1024 * 1024);
}
void ChromaUpdater::finishCheck()
{
    auto current = ChromaUpdate::Version::parse(BuildConfig.versionString());
    // Only a tag for this build's core version can identify its prerelease channel.
    const auto tag = ChromaUpdate::Version::parse(BuildConfig.GIT_TAG);
    if (current && tag && tag->core == current->core)
        current = tag;
    QFile installedVersion(QDir(m_appDir).filePath("update/installed-version.txt"));
    if (installedVersion.size() <= 256 && installedVersion.open(QIODevice::ReadOnly)) {
        const auto installed = ChromaUpdate::Version::parse(QString::fromUtf8(installedVersion.readAll()).trimmed());
        if (current && installed && installed->core == current->core && (!tag || tag->core != current->core))
            current = installed;
    }
    if (!current) {
        fail(tr("This development version cannot be compared with published releases."));
        return;
    }
    const auto previousTag = availableTag();
    m_release = ChromaUpdate::selectRelease(m_releases, *current, m_portable, getBetaAllowed());
    m_releases = {};
    m_busy = false;
    m_checking = false;
    m_ready = false;
    m_settings.setValue("LastCheck", QDateTime::currentSecsSinceEpoch());
    schedule();
    emit canCheckForUpdatesChanged(true);
    if (m_release) {
        if (m_manual)
            m_dismissedTags.remove(m_release->tag);
        setStatus(tr("Chroma %1 is available%2.").arg(m_release->version.text, m_release->prerelease ? tr(" (prerelease)") : QString()));
        emit availableUpdateChanged();
        if (previousTag != m_release->tag)
            emit releaseAvailable();
        // Automatic checks surface the global notice without taking focus or
        // opening a page, including when the user is on the home screen.
        if (m_manual && m_window)
            offer();
    } else {
        emit availableUpdateChanged();
        setStatus(tr("Chroma is up to date."));
        if (m_manual)
            QMessageBox::information(m_window, tr("Chroma updates"), m_status);
    }
}
void ChromaUpdater::fail(const QString& reason)
{
    const bool checking = m_checking;
    cancelTransfer();
    m_busy = false;
    m_checking = false;
    if (!checking)
        m_ready = false;
    setStatus(reason);
    if (m_action) {
        m_action->setText(tr("Try download again"));
        m_action->setEnabled(true);
    }
    if (m_progress)
        m_progress->hide();
    // Avoid retrying a failing server continuously on an automatic schedule.
    m_settings.setValue("LastCheck", QDateTime::currentSecsSinceEpoch());
    schedule();
    emit canCheckForUpdatesChanged(true);
    if (m_manual && !m_dialog) {
        if (checking && m_release) {
            showAvailableUpdate();
            setStatus(tr("The latest check failed: %1 You can still review the previously found release.").arg(reason));
        } else {
            QMessageBox::warning(m_window, tr("Chroma updates"), reason);
        }
    }
}

namespace {
// Release notes are untrusted text. Markdown formatting is supported, but
// images and any other document resources are never loaded from disk or URL.
class ReleaseNotesDocument : public QTextDocument {
   public:
    explicit ReleaseNotesDocument(QObject* parent) : QTextDocument(parent)
    {
        setResourceProvider([](const QUrl&) { return QVariant::fromValue(QImage()); });
    }

   protected:
    QVariant loadResource(int, const QUrl&) override { return QVariant::fromValue(QImage()); }
};
void populateReleaseNotes(QTextBrowser* notes, const QString& markdown)
{
    auto* document = new ReleaseNotesDocument(notes);
    document->setDefaultFont(notes->font());
    notes->setDocument(document);
    document->setMarkdown(markdown.left(128 * 1024), QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) | QTextDocument::MarkdownNoHTML);
    // Remove image placeholders as well: the notes should remain readable when
    // a release contains a banner, badge, or screenshot.
    QList<QPair<int, int>> images;
    for (auto block = document->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (fragment.isValid() && fragment.charFormat().isImageFormat())
                images.append({ fragment.position(), fragment.length() });
        }
    for (auto it = images.crbegin(); it != images.crend(); ++it) {
        QTextCursor cursor(document);
        cursor.setPosition(it->first);
        cursor.setPosition(it->first + it->second, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
}
}  // namespace

void ChromaUpdater::offer()
{
    if (!m_release || !m_window)
        return;
    if (m_dialog) {
        if (auto* window = qobject_cast<MainWindow*>(m_window.data()))
            window->openInlinePage(m_dialog, tr("What’s new"));
        else {
            m_dialog->show();
            m_dialog->raise();
        }
        return;
    }
    auto* dialog = new QDialog(m_window);
    dialog->setObjectName("chromaUpdateDialog");
    dialog->setWindowTitle(tr("What’s new in Chroma"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setProperty("chromaOwnsScrolling", true);
    dialog->resize(620, 520);
    m_dialog = dialog;
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(20, 16, 20, 16);
    layout->setSpacing(12);
    auto* heading =
        new QLabel(tr("Chroma %1%2").arg(m_release->version.text, m_release->prerelease ? tr(" · Prerelease") : QString()), dialog);
    heading->setObjectName("chromaUpdateHeading");
    heading->setTextFormat(Qt::PlainText);
    heading->setWordWrap(true);
    auto headingFont = heading->font();
    headingFont.setPixelSize(24);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    layout->addWidget(heading);
    auto* introduction = new QLabel(tr("What’s new"), dialog);
    introduction->setObjectName("chromaUpdateNotesHeading");
    auto introductionFont = introduction->font();
    introductionFont.setBold(true);
    introduction->setFont(introductionFont);
    layout->addWidget(introduction);
    auto* notes = new QTextBrowser(dialog);
    notes->setObjectName("chromaUpdateNotes");
    notes->setAccessibleName(tr("Release notes"));
    notes->setReadOnly(true);
    notes->setOpenLinks(false);
    notes->setOpenExternalLinks(false);
    notes->setFrameShape(QFrame::NoFrame);
    notes->setMinimumSize(0, 100);
    notes->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    populateReleaseNotes(notes, m_release->notes.isEmpty() ? tr("A new Chroma release is available.") : m_release->notes);
    layout->addWidget(notes, 1);
    m_message = new QLabel(tr("Your instances, accounts and settings will be preserved. Downloading does not close Chroma."), dialog);
    m_message->setTextFormat(Qt::PlainText);
    m_message->setWordWrap(true);
    m_message->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_message);
    m_progress = new QProgressBar(dialog);
    m_progress->hide();
    layout->addWidget(m_progress);
    auto* buttons = new QDialogButtonBox(dialog);
    m_action = buttons->addButton(m_ready ? tr("Install and restart") : tr("Download update"), QDialogButtonBox::ActionRole);
    m_action->setObjectName("chromaUpdateAction");
    auto* later = buttons->addButton(tr("Later"), QDialogButtonBox::RejectRole);
    connect(later, &QPushButton::clicked, this, [this, dialog] {
        dismissAvailableUpdate();
        dialog->reject();
    });
    connect(m_action, &QPushButton::clicked, this, [this] { m_ready ? install() : download(); });
    layout->addWidget(buttons);
    connect(dialog, &QDialog::finished, this, [this] {
        cancelTransfer();
        m_busy = false;
        m_checking = false;
        if (!m_keepStage) {
            m_ready = false;
            m_stage.reset();
        }
        m_dialog.clear();
        m_message.clear();
        m_progress.clear();
        m_action.clear();
        schedule();
        emit canCheckForUpdatesChanged(!m_keepStage);
    });
    if (auto* window = qobject_cast<MainWindow*>(m_window.data()))
        window->openInlinePage(dialog, tr("What’s new"));
    else
        dialog->show();
}
void ChromaUpdater::download()
{
    if (!m_release || m_busy || m_keepStage)
        return;
    m_manual = true;
    m_busy = true;
    m_ready = false;
    m_stage = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/Chroma-update-XXXXXX");
    if (!m_stage->isValid()) {
        fail(tr("A temporary folder could not be created for the update."));
        return;
    }
    m_action->setEnabled(false);
    m_progress->setRange(0, 0);
    m_progress->show();
    setStatus(tr("Verifying the release checksums…"));
    emit canCheckForUpdatesChanged(false);
    auto* task = transfer();
    connect(task, &ChromaUpdateTransfer::succeeded, this, [this](const QByteArray& sums) {
        const auto package = ChromaUpdate::checksumFor(sums, m_release->package.name);
        const auto manifest = ChromaUpdate::checksumFor(sums, m_release->manifest.name);
        if (!package || !manifest) {
            fail(tr("The release is missing valid package checksums."));
            return;
        }
        m_packageHash = *package;
        m_manifestHash = *manifest;
        downloadManifest();
    });
    task->start(m_release->checksums.url, 1024 * 1024, m_release->checksums.size);
}
void ChromaUpdater::downloadManifest()
{
    auto* task = transfer();
    connect(task, &ChromaUpdateTransfer::succeeded, this, [this] {
        QFile file(m_stage->filePath("package-manifest.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            fail(tr("The verified update manifest could not be read."));
            return;
        }
        const auto manifest = QJsonDocument::fromJson(file.readAll()).object();
        if (manifest.value("version").toString() != m_release->version.text ||
            !manifest.value(m_portable ? "portableFiles" : "installedFiles").isArray() ||
            manifest.value(m_portable ? "portableFiles" : "installedFiles").toArray().isEmpty()) {
            fail(tr("The update manifest does not match this release or installation type."));
            return;
        }
        downloadPackage();
    });
    task->start(m_release->manifest.url, 8 * 1024 * 1024, m_release->manifest.size, m_manifestHash,
                m_stage->filePath("package-manifest.json"));
}
void ChromaUpdater::downloadPackage()
{
    setStatus(tr("Downloading Chroma %1…").arg(m_release->version.text));
    auto* task = transfer();
    connect(task, &ChromaUpdateTransfer::progress, this, [this](qint64 received, qint64 total) {
        if (m_progress && total > 0) {
            m_progress->setRange(0, 100);
            m_progress->setValue(static_cast<int>(received * 100 / total));
        }
    });
    connect(task, &ChromaUpdateTransfer::succeeded, this, [this] {
        m_busy = false;
        m_ready = true;
        m_progress->hide();
        m_action->setText(tr("Install and restart"));
        m_action->setEnabled(true);
        setStatus(tr("The update is verified and ready. Close any running games, then install and restart Chroma."));
        emit canCheckForUpdatesChanged(true);
    });
    task->start(m_release->package.url, 2LL * 1024 * 1024 * 1024, m_release->package.size, m_packageHash,
                m_stage->filePath(m_release->package.name));
}
void ChromaUpdater::install()
{
    if (!m_ready || !m_stage || !m_release)
        return;
    if (!APPLICATION->updatesAreAllowed()) {
        setStatus(tr("Close all running games before installing this update."));
        return;
    }
    auto* window = qobject_cast<MainWindow*>(m_window.data());
    // Closing pages may open a nested save confirmation. Keep the verified
    // release and package stable until the user finishes that decision.
    m_keepStage = true;
    m_timer.stop();
    emit canCheckForUpdatesChanged(false);
    const bool approved = !window || window->prepareInlineNavigation();
    m_keepStage = false;
    schedule();
    emit canCheckForUpdatesChanged(true);
    if (!m_dialog)
        offer();
    if (!approved)
        return;
    if (!APPLICATION->updatesAreAllowed()) {
        setStatus(tr("Close all running games before installing this update."));
        return;
    }
    const QString helper = m_stage->filePath("apply-chroma-update.ps1");
    if (!QFileInfo::exists(helper) && !QFile::copy(QDir(m_appDir).filePath("update/apply-chroma-update.ps1"), helper)) {
        setStatus(tr("The update helper is missing. Reinstall Chroma using the download from its GitHub releases."));
        return;
    }
    const QJsonObject request{ { "parentPid", QCoreApplication::applicationPid() },
                               { "applicationDir", m_appDir },
                               { "packagePath", m_stage->filePath(m_release->package.name) },
                               { "packageSha256", QString::fromLatin1(m_packageHash) },
                               { "manifestPath", m_stage->filePath("package-manifest.json") },
                               { "manifestSha256", QString::fromLatin1(m_manifestHash) },
                               { "version", m_release->version.text },
                               { "mode", m_portable ? "portable" : "installed" },
                               { "restart", true },
                               { "restartArguments", QJsonArray{ "--dir", m_dataDir } },
                               { "noIntegration", QFileInfo::exists(QDir(m_appDir).filePath("chroma-no-integration")) } };
    const QString requestPath = m_stage->filePath("request.json");
    QSaveFile file(requestPath);
    const auto bytes = QJsonDocument(request).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        setStatus(tr("The update request could not be saved."));
        return;
    }
    const QString powershell =
        QDir(qEnvironmentVariable("SystemRoot", "C:/Windows")).filePath("System32/WindowsPowerShell/v1.0/powershell.exe");
    APPLICATION->updateIsRunning(true);
    QProcess process;
    process.setProgram(powershell);
    process.setArguments({ "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden", "-File", helper,
                           "-RequestPath", requestPath });
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments* args) { args->flags |= 0x08000000; });  // CREATE_NO_WINDOW
#endif
    if (!process.startDetached()) {
        APPLICATION->updateIsRunning(false);
        setStatus(tr("The update helper could not start. Your current version is unchanged."));
        return;
    }
    m_stage->setAutoRemove(false);
    APPLICATION->closeAllWindows();
    APPLICATION->quit();
}
