// SPDX-License-Identifier: GPL-3.0-only
#include "ProjectDescriptionPage.h"
#include <QDateTime>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTabBar>
#include <QUrlQuery>
#include "Markdown.h"
#include "StringUtils.h"
#include "VariableSizedImageObject.h"
#include "modplatform/flame/FlameAPI.h"
#include "ui/widgets/SmoothScroll.h"

namespace {
QString escaped(const QString& text)
{
    return text.toHtmlEscaped();
}
bool webUrl(const QUrl& url)
{
    return url.isValid() && (url.scheme() == "https" || url.scheme() == "http");
}
QString link(const QString& url, const QString& label)
{
    return webUrl(QUrl(url)) ? QString("<a href=\"%1\">%2</a>").arg(escaped(url), escaped(label)) : escaped(label);
}
QString dateText(const QString& value)
{
    const auto date = QDateTime::fromString(value, Qt::ISODate);
    return date.isValid() ? QLocale().toString(date.date(), QLocale::ShortFormat) : QString();
}
QString dependencyType(ModPlatform::DependencyType type)
{
    using enum ModPlatform::DependencyType;
    switch (type) {
        case REQUIRED:
            return QObject::tr("Required");
        case OPTIONAL:
            return QObject::tr("Optional");
        case INCOMPATIBLE:
            return QObject::tr("Incompatible");
        case EMBEDDED:
            return QObject::tr("Embedded");
        case TOOL:
            return QObject::tr("Tool");
        case INCLUDE:
            return QObject::tr("Included");
        default:
            return QObject::tr("Dependency");
    }
}
}  // namespace

ProjectDescriptionPage::ProjectDescriptionPage(QWidget* parent) : QTextBrowser(parent), m_image_text_object(new VariableSizedImageObject)
{
    m_image_text_object->setParent(this);
    document()->documentLayout()->registerHandler(QTextFormat::ImageObject, m_image_text_object.get());
    SmoothScroll::install(this);
    setOpenLinks(false);
    setOpenExternalLinks(false);
    connect(this, &QTextBrowser::anchorClicked, this, &ProjectDescriptionPage::openProjectLink);
    m_tabs = new QTabBar(this);
    m_tabs->setObjectName("projectInformationTabs");
    m_tabs->setAccessibleName(tr("Project information sections"));
    m_tabs->setExpanding(false);
    m_tabs->setDrawBase(false);
    m_tabs->setUsesScrollButtons(true);
    m_tabs->addTab(tr("About"));
    m_tabs->addTab(tr("Gallery"));
    m_tabs->addTab(tr("Releases"));
    m_tabs->hide();
    connect(m_tabs, &QTabBar::currentChanged, this, [this] {
        renderProject(false);
        loadChangelog();
    });
}

void ProjectDescriptionPage::setMetaEntry(QString entry)
{
    if (m_image_text_object)
        m_image_text_object->setMetaEntry(entry);
}

void ProjectDescriptionPage::setProject(ModPlatform::IndexedPack::Ptr pack, int selectedVersion)
{
    const bool changed = !m_pack || !pack || m_pack->provider != pack->provider || m_pack->addonId != pack->addonId;
    if (changed) {
        ++m_projectGeneration;
        if (m_changelogTask)
            m_changelogTask->abort();
        m_changelogKey.clear();
        m_changelogError.clear();
        m_changelogs.clear();
        m_releaseLimit = 100;
        QSignalBlocker block(m_tabs);
        m_tabs->setCurrentIndex(0);
    }
    m_pack = std::move(pack);
    m_selectedVersion = selectedVersion;
    if (m_pack && m_selectedVersion < 0 && !m_pack->versions.isEmpty())
        m_selectedVersion = 0;
    m_tabs->setVisible(bool(m_pack));
    setViewportMargins(0, m_pack ? m_tabs->sizeHint().height() + 8 : 0, 0, 0);
    m_tabs->setGeometry(0, 0, width(), m_tabs->sizeHint().height());
    if (m_pack)
        setMetaEntry(m_pack->provider == ModPlatform::ResourceProvider::FLAME ? "FlamePacks" : "ModrinthPacks");
    renderProject(!changed);
    loadChangelog();
}

void ProjectDescriptionPage::setSelectedVersion(int index)
{
    if (m_selectedVersion == index)
        return;
    m_selectedVersion = index;
    m_changelogError.clear();
    renderProject(false);
    loadChangelog();
}

void ProjectDescriptionPage::setProjectNotice(const QString& notice)
{
    if (!m_pack)
        return;
    m_pack->extraData.notice = notice;
    renderProject();
}

void ProjectDescriptionPage::resizeEvent(QResizeEvent* event)
{
    QTextBrowser::resizeEvent(event);
    m_tabs->setGeometry(0, 0, width(), m_tabs->sizeHint().height());
}

QVariant ProjectDescriptionPage::loadResource(int, const QUrl&)
{
    // An invalid/null variant makes QTextDocument fall back to local files.
    // A non-null empty response prevents that fallback; images are handled
    // exclusively by VariableSizedImageObject's HTTP(S) cache.
    return QByteArray("");
}

void ProjectDescriptionPage::renderProject(bool restoreScroll)
{
    const int scroll = verticalScrollBar()->value();
    flush();
    if (!m_pack) {
        clear();
        return;
    }
    const auto& pack = *m_pack;
    const auto& extra = pack.extraData;
    const auto provider =
        pack.addonId.toString().isEmpty() ? tr("Local file") : ModPlatform::ProviderCapabilities::readableName(pack.provider);
    QString html = QString("<p style='color:%1'>%2</p><h2>%3</h2>")
                       .arg(palette().color(QPalette::PlaceholderText).name(), escaped(provider), escaped(pack.name));
    QStringList authors;
    for (const auto& author : pack.authors)
        authors.append(link(author.url, author.name));
    if (!authors.isEmpty())
        html += "<p>" + tr("By %1").arg(authors.join(", ")) + "</p>";
    html += "<p>" + escaped(pack.description) + "</p>";
    if (!extra.notice.isEmpty())
        html += "<p><b>" + escaped(extra.notice) + "</b></p>";
    if (extra.status == "archived" || extra.status == "abandoned")
        html += "<p><b>" + tr("Archived project") + "</b>. " + tr("See the provider page for its current status.") + "</p>";
    if (webUrl(QUrl(pack.websiteUrl)))
        html += "<p>" + link(pack.websiteUrl, tr("Open on %1").arg(provider)) + "</p>";

    if (m_tabs->currentIndex() == 0) {
        QString rows;
        const auto row = [&rows](const QString& title, const QString& value) {
            if (!value.isEmpty())
                rows += QString("<tr><td style='padding:4px 14px 4px 0'><b>%1</b></td><td>%2</td></tr>").arg(escaped(title), value);
        };
        if (extra.downloads >= 0)
            row(tr("Downloads"), QLocale().toString(extra.downloads));
        if (extra.followers >= 0)
            row(tr("Followers"), QLocale().toString(extra.followers));
        row(tr("Updated"), dateText(extra.updated));
        row(tr("Published"), dateText(extra.published));
        row(tr("Categories"), escaped(extra.categories.join(", ")));
        row(tr("Loaders"), escaped(extra.loaders.join(", ")));
        auto versions = extra.gameVersions;
        if (versions.isEmpty())
            for (const auto& version : pack.versions)
                versions.append(version.mcVersion);
        versions.removeDuplicates();
        row(tr("Minecraft"), escaped(versions.join(", ")));
        row(tr("License"), link(extra.licenseUrl, extra.license));
        row(tr("Client"), escaped(extra.clientSide));
        row(tr("Server"), escaped(extra.serverSide));
        row(tr("Environment"), escaped(extra.environments.join(", ")));
        if (!rows.isEmpty())
            html += "<table cellspacing='0'>" + rows + "</table>";
        QStringList links;
        const auto addLink = [&links](const QString& url, const QString& title) {
            if (webUrl(QUrl(url)))
                links.append(link(url, title));
        };
        addLink(extra.sourceUrl, tr("Source code"));
        addLink(extra.issuesUrl, tr("Report an issue"));
        addLink(extra.wikiUrl, tr("Wiki"));
        addLink(extra.discordUrl, tr("Community"));
        for (const auto& donation : extra.donate)
            addLink(donation.url, donation.platform);
        if (!links.isEmpty())
            html += "<p>" + links.join(" &nbsp; | &nbsp; ") + "</p>";
        html += "<hr>";
        if (!extra.body.isEmpty())
            html += extra.bodyIsHtml ? extra.body : markdownToHTML(extra.body.toUtf8());
        else
            html += "<p>" +
                    (pack.extraDataLoaded || !extra.notice.isEmpty() ? tr("No additional description is available.")
                                                                     : tr("Loading additional project information...")) +
                    "</p>";
    } else if (m_tabs->currentIndex() == 1) {
        if (extra.gallery.isEmpty())
            html += "<p>" + tr("This project has no gallery images.") + "</p>";
        for (const auto& image : extra.gallery) {
            if (!webUrl(QUrl(image.url)))
                continue;
            html += "<p><a href=\"" + escaped(image.url) + "\"><img width='640' src=\"" + escaped(image.url) + "\"></a></p>";
            if (!image.title.isEmpty())
                html += "<h3>" + escaped(image.title) + "</h3>";
            if (!image.description.isEmpty())
                html += "<p>" + escaped(image.description) + "</p>";
        }
    } else {
        if (pack.versions.isEmpty())
            html += "<p>" +
                    (!pack.versionsError.isEmpty() ? escaped(pack.versionsError)
                     : pack.versionsLoaded         ? tr("No compatible releases are available.")
                                                   : tr("Loading releases...")) +
                    "</p>";
        else {
            QString releaseList = "<h3>" + tr("Available releases") + "</h3><p>" +
                                  tr("Choose a release to inspect its changes and dependencies.") + "</p><ul>";
            for (int i = 0; i < qMin(qsizetype(m_releaseLimit), pack.versions.size()); ++i) {
                const auto& version = pack.versions[i];
                releaseList += QString("<li><a href='chroma-version:%1'>%2</a>%3</li>")
                                   .arg(i)
                                   .arg(escaped(version.getVersionDisplayString()), i == m_selectedVersion ? tr(" (Viewing)") : "");
            }
            releaseList += "</ul>";
            if (pack.versions.size() > m_releaseLimit)
                releaseList += "<p><a href='chroma-more:releases'>" + tr("Show more releases") + "</a></p>";
            if (m_selectedVersion >= 0 && m_selectedVersion < pack.versions.size()) {
                const auto& version = pack.versions[m_selectedVersion];
                html += "<hr><h3>" + escaped(version.version) + "</h3>";
                html += "<p>" + escaped(version.version_type.toString()) + " | " + dateText(version.date) + "</p>";
                auto gameVersions = version.mcVersion;
                gameVersions.removeDuplicates();
                html += "<p>" + tr("Minecraft: %1").arg(escaped(gameVersions.join(", "))) + "</p><h3>" + tr("Dependencies") + "</h3>";
                if (version.dependencies.isEmpty())
                    html += "<p>" + tr("No dependencies are listed for this release.") + "</p>";
                else {
                    html += "<ul>";
                    for (const auto& dep : version.dependencies) {
                        const auto id = dep.addonId.toString();
                        const auto url = pack.provider == ModPlatform::ResourceProvider::MODRINTH
                                             ? "https://modrinth.com/project/" + id
                                             : "https://www.curseforge.com/projects/" + id;
                        const auto label = id.isEmpty() ? tr("Version %1").arg(dep.version) : tr("Project %1").arg(id);
                        html += "<li>" + escaped(dependencyType(dep.type)) + " - " + link(url, label) + "</li>";
                    }
                    html += "</ul>";
                }
                html += "<h3>" + tr("Changelog") + "</h3>";
                QString changelog = version.changelog;
                if (pack.provider == ModPlatform::ResourceProvider::FLAME && changelog.isEmpty()) {
                    const auto key = version.fileId.toString();
                    if (m_changelogs.contains(key))
                        changelog = m_changelogs.value(key);
                    else if (!m_changelogError.isEmpty())
                        html += "<p>" + escaped(m_changelogError) + " <a href='chroma-retry:changelog'>" + tr("Retry") + "</a></p>";
                    else
                        html += "<p>" + tr("Loading changelog...") + "</p>";
                }
                if (!changelog.isEmpty())
                    html += pack.provider == ModPlatform::ResourceProvider::FLAME ? changelog : markdownToHTML(changelog.toUtf8());
                else if (pack.provider == ModPlatform::ResourceProvider::MODRINTH || m_changelogs.contains(version.fileId.toString()))
                    html += "<p>" + tr("No changelog was supplied for this release.") + "</p>";
            }
            html += "<hr>" + releaseList;
        }
    }
    setHtml(StringUtils::htmlListPatch(html));
    verticalScrollBar()->setValue(restoreScroll ? scroll : 0);
}

void ProjectDescriptionPage::loadChangelog()
{
    if (!m_pack || m_pack->provider != ModPlatform::ResourceProvider::FLAME || m_tabs->currentIndex() != 2 || m_selectedVersion < 0 ||
        m_selectedVersion >= m_pack->versions.size())
        return;
    const auto version = m_pack->versions[m_selectedVersion];
    const auto key = version.fileId.toString();
    if (!version.changelog.isEmpty() || m_changelogs.contains(key) ||
        (m_changelogTask && m_changelogTask->isRunning() && m_changelogKey == key))
        return;
    if (m_changelogTask)
        m_changelogTask->abort();
    m_changelogKey = key;
    const auto generation = m_projectGeneration;
    const auto response = std::make_shared<QByteArray>();
    static const FlameAPI api;
    m_changelogTask = api.getModFileChangelog(m_pack->addonId.toInt(), version.fileId.toInt(), response);
    connect(m_changelogTask.get(), &Task::succeeded, this, [this, response, key, generation] {
        if (generation != m_projectGeneration)
            return;
        m_changelogs.insert(key, QJsonDocument::fromJson(*response).object()["data"].toString());
        m_changelogError.clear();
        renderProject();
    });
    connect(m_changelogTask.get(), &Task::failed, this, [this, key, generation](const QString&) {
        if (generation != m_projectGeneration || key != m_changelogKey)
            return;
        m_changelogError = tr("The changelog is unavailable.");
        renderProject();
    });
    m_changelogTask->start();
}

void ProjectDescriptionPage::openProjectLink(const QUrl& url)
{
    if (url.scheme() == "chroma-version") {
        bool valid = false;
        const auto index = url.path().toInt(&valid);
        if (valid && m_pack && index >= 0 && index < m_pack->versions.size()) {
            setSelectedVersion(index);
            emit projectVersionSelected(index);
        }
        return;
    }
    if (url.scheme() == "chroma-more") {
        m_releaseLimit += 100;
        renderProject();
        return;
    }
    if (url.scheme() == "chroma-retry") {
        m_changelogError.clear();
        loadChangelog();
        renderProject();
        return;
    }
    if (url.scheme().isEmpty()) {
        if (url.hasFragment()) {
            scrollToAnchor(url.fragment());
            return;
        }
        const auto remote = QUrlQuery(url).queryItemValue("remoteUrl", QUrl::FullyDecoded);
        if (webUrl(QUrl(remote)))
            QDesktopServices::openUrl(QUrl(remote));
        return;
    }
    if (webUrl(url))
        QDesktopServices::openUrl(url);
}

void ProjectDescriptionPage::flush()
{
    if (m_image_text_object)
        m_image_text_object->flush();
}
