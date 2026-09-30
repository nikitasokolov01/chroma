// SPDX-License-Identifier: GPL-3.0-only
#include "ChromaRelease.h"

#include <QJsonObject>
#include <QRegularExpression>

namespace ChromaUpdate {
namespace {
int compareNumber(const QString& left, const QString& right)
{
    if (left.size() != right.size())
        return left.size() < right.size() ? -1 : 1;
    return QString::compare(left, right, Qt::CaseSensitive);
}
bool numeric(const QString& value)
{
    for (const auto character : value)
        if (character < QLatin1Char('0') || character > QLatin1Char('9'))
            return false;
    return !value.isEmpty();
}
std::optional<Asset> assetFor(const QJsonArray& assets, const QString& tag, const QString& name)
{
    std::optional<Asset> found;
    const auto path = "/" + repository() + "/releases/download/" + tag + "/" + name;
    for (const auto& value : assets) {
        const auto object = value.toObject();
        if (object.value("name").toString() != name)
            continue;
        if (found)
            return {};
        Asset asset{ name, QUrl(object.value("browser_download_url").toString()), object.value("size").toInteger() };
        if (asset.url.host() != "github.com" || !allowedDownloadUrl(asset.url) || asset.url.path() != path ||
            !asset.url.query().isEmpty() || asset.size <= 0 || asset.size > 2LL * 1024 * 1024 * 1024)
            return {};
        found = asset;
    }
    return found;
}
}  // namespace

QString repository()
{
    return "nikitasokolov01/chroma";
}

std::optional<Version> Version::parse(QString text)
{
    if (text.startsWith('v'))
        text.remove(0, 1);
    if (text.size() > 128)
        return {};
    static const QRegularExpression expression(
        "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?"
        "(?:\\+[0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*)?\\z");
    const auto match = expression.match(text);
    if (!match.hasMatch())
        return {};
    Version version{ text, { match.captured(1), match.captured(2), match.captured(3) }, {} };
    if (!match.captured(4).isEmpty())
        version.prerelease = match.captured(4).split('.');
    for (const auto& item : version.prerelease)
        if (numeric(item) && item.size() > 1 && item.startsWith('0'))
            return {};
    return version;
}

int Version::compare(const Version& other) const
{
    for (int i = 0; i < 3; ++i)
        if (const int difference = compareNumber(core[i], other.core[i]); difference)
            return difference;
    if (prerelease.isEmpty() != other.prerelease.isEmpty())
        return prerelease.isEmpty() ? 1 : -1;
    for (int i = 0; i < qMin(prerelease.size(), other.prerelease.size()); ++i) {
        const bool leftNumeric = numeric(prerelease[i]), rightNumeric = numeric(other.prerelease[i]);
        if (leftNumeric != rightNumeric)
            return leftNumeric ? -1 : 1;
        const int difference = leftNumeric ? compareNumber(prerelease[i], other.prerelease[i])
                                           : QString::compare(prerelease[i], other.prerelease[i], Qt::CaseSensitive);
        if (difference)
            return difference;
    }
    return prerelease.size() == other.prerelease.size() ? 0 : prerelease.size() < other.prerelease.size() ? -1 : 1;
}

bool allowedDownloadUrl(const QUrl& url)
{
    return url.isValid() && url.scheme() == "https" && url.userInfo().isEmpty() && url.fragment().isEmpty() &&
           (url.port() == -1 || url.port() == 443) &&
           (url.host() == "github.com" || url.host() == "release-assets.githubusercontent.com" ||
            url.host() == "objects.githubusercontent.com");
}

std::optional<Release> selectRelease(const QJsonArray& releases, const Version& current, bool portable, bool includePrereleases)
{
    std::optional<Release> latest;
    for (const auto& value : releases) {
        const auto object = value.toObject();
        if (!object.value("draft").isBool() || object.value("draft").toBool() || !object.value("prerelease").isBool())
            continue;
        const auto tag = object.value("tag_name").toString();
        const auto version = Version::parse(tag);
        if (!version || version->compare(current) <= 0 || (latest && version->compare(latest->version) <= 0))
            continue;
        const bool prerelease = object.value("prerelease").toBool() || !version->prerelease.isEmpty();
        if (prerelease && !includePrereleases)
            continue;
        const auto assets = object.value("assets").toArray();
        const QString packageName = "Chroma-" + version->text + (portable ? "-Windows-x64.zip" : "-Windows-x64-Setup.exe");
        const auto package = assetFor(assets, tag, packageName);
        const auto checksums = assetFor(assets, tag, "SHA256SUMS.txt");
        const auto manifest = assetFor(assets, tag, "package-manifest.json");
        if (!package || !checksums || !manifest || checksums->size > 1024 * 1024 || manifest->size > 8 * 1024 * 1024)
            continue;
        latest = Release{ *version, tag, object.value("body").toString().left(128 * 1024), prerelease, *package, *checksums, *manifest };
    }
    return latest;
}

std::optional<QByteArray> checksumFor(const QByteArray& sums, const QString& name)
{
    if (sums.size() > 1024 * 1024 || name.contains('/') || name.contains('\\'))
        return {};
    static const QRegularExpression line("^([0-9a-fA-F]{64})[ \\t]+\\*?([^\\r\\n]+)$");
    std::optional<QByteArray> found;
    for (const auto& bytes : sums.split('\n')) {
        const auto match = line.match(QString::fromUtf8(bytes).trimmed());
        if (!match.hasMatch() || match.captured(2) != name)
            continue;
        if (found)
            return {};
        found = match.captured(1).toLatin1().toLower();
    }
    return found;
}
}  // namespace ChromaUpdate
