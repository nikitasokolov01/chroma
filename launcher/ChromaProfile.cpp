// SPDX-License-Identifier: GPL-3.0-only
#include "ChromaProfile.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include "settings/INIFile.h"

#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>
#include <tlhelp32.h>
// clang-format on
#endif

namespace ChromaProfile {
namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("ChromaProfile", text);
}

QString resolvedPath(const QString& path, const QString& root = QDir::currentPath())
{
    const QString absolute = QDir::cleanPath(QDir(root).absoluteFilePath(path));
    QFileInfo info(absolute);
    QStringList missing;
    // Resolve existing ancestors as well, for a not-yet-created directory below
    // a legitimate symlink or junction.
    while (!info.exists() && !info.isSymLink() && !info.isJunction()) {
        const QString parent = info.absolutePath();
        if (parent == info.absoluteFilePath())
            break;
        missing.prepend(info.fileName());
        info.setFile(parent);
    }
    QString result = info.canonicalFilePath();
    if (result.isEmpty())
        result = info.absoluteFilePath();
    for (const QString& part : missing)
        result = QDir(result).filePath(part);
    return QDir::fromNativeSeparators(QDir::cleanPath(result));
}

QString comparablePath(const QString& path)
{
    QString result = resolvedPath(path);
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

bool containsPath(const QString& parent, const QString& child)
{
    const QString a = comparablePath(parent);
    const QString b = comparablePath(child);
    return a == b || b.startsWith(a.endsWith('/') ? a : a + '/');
}

bool readIni(const QString& path, INIFile* settings)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    file.close();
    QSettings probe(path, QSettings::IniFormat);
    probe.setFallbacksEnabled(false);
    probe.allKeys();
    return probe.status() == QSettings::NoError && settings->loadFile(path);
}

void setError(QString* error, const QString& text)
{
    if (error)
        *error = text;
}

}  // namespace

ProfileInfo inspect(const QString& folder)
{
    ProfileInfo result;
    if (folder.trimmed().isEmpty()) {
        result.error = tr("Choose the Prism Launcher data folder.");
        return result;
    }
    result.root = resolvedPath(folder);
    if (!QFileInfo(result.root).isDir()) {
        result.error = tr("The selected Prism folder is unavailable: %1").arg(result.root);
        return result;
    }
    const QString config = QDir(result.root).filePath("prismlauncher.cfg");
    INIFile settings;
    if (!QFileInfo(config).isFile() || !readIni(config, &settings)) {
        result.error = tr("This folder does not contain a readable, valid prismlauncher.cfg file.");
        return result;
    }
    result.instancesPath = resolvedPath(settings.value("InstanceDir", "instances").toString(), result.root);
    result.iconsPath = resolvedPath(settings.value("IconsDir", "icons").toString(), result.root);
    const QFileInfo instancesInfo(result.instancesPath);
    if (instancesInfo.exists() && (!instancesInfo.isDir() || !instancesInfo.isReadable())) {
        result.error = tr("The configured instances folder cannot be read: %1").arg(result.instancesPath);
        return result;
    }
    // Match InstanceList::discoverInstances, including supported linked instances
    // and its exclusion of aliases to another instance in the same folder.
    QDirIterator it(result.instancesPath, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Readable | QDir::Hidden, QDirIterator::FollowSymlinks);
    while (it.hasNext()) {
        const QFileInfo entry(it.next());
        if (!QFileInfo::exists(QDir(entry.absoluteFilePath()).filePath("instance.cfg")))
            continue;
        if (entry.isSymLink() && QFileInfo(entry.symLinkTarget()).canonicalPath() == instancesInfo.canonicalFilePath())
            continue;
        ++result.instanceCount;
    }
    return result;
}

QStringList detectedRoots()
{
    QStringList candidates;
    const QString environmentRoot = qEnvironmentVariable("PRISMLAUNCHER_DATA_DIR");
    if (!environmentRoot.isEmpty())
        candidates.append(environmentRoot);
#ifdef Q_OS_WIN
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty())
        candidates.append(QDir(appData).filePath("PrismLauncher"));
#endif
    candidates.append(QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)).filePath("PrismLauncher"));
    candidates.append(QDir::home().filePath(".local/share/PrismLauncher"));
    candidates.append(QDir::home().filePath(".var/app/org.prismlauncher.PrismLauncher/data/PrismLauncher"));
    const QDir executableFolder(QCoreApplication::applicationDirPath());
    candidates.append(executableFolder.filePath("../PrismLauncher"));
    candidates.append(executableFolder.filePath("UserData"));
    candidates.append(executableFolder.absolutePath());
    QStringList result;
    QSet<QString> seen;
    for (const QString& candidate : candidates) {
        const QString key = comparablePath(candidate);
        if (seen.contains(key))
            continue;
        seen.insert(key);
        const auto profile = inspect(candidate);
        if (profile.error.isEmpty())
            result.append(profile.root);
    }
    return result;
}

bool sameProfilePath(const QString& first, const QString& second)
{
    return !first.isEmpty() && !second.isEmpty() && comparablePath(first) == comparablePath(second);
}

QString runningPrismError()
{
#ifdef Q_OS_WIN
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return tr("Cannot check whether Prism Launcher is running (Windows error %1). Close Prism and try again.").arg(GetLastError());
    PROCESSENTRY32W process{};
    process.dwSize = sizeof(process);
    BOOL found = Process32FirstW(snapshot, &process);
    while (found) {
        const QString name = QString::fromWCharArray(process.szExeFile);
        if (name.compare("prismlauncher.exe", Qt::CaseInsensitive) == 0 ||
            name.compare("prismlauncher-console.exe", Qt::CaseInsensitive) == 0) {
            CloseHandle(snapshot);
            return tr(
                "Close Prism Launcher and any games it launched before opening this folder in Chroma. Only one launcher can use the "
                "shared folder at a time.");
        }
        found = Process32NextW(snapshot, &process);
    }
    const DWORD status = GetLastError();
    CloseHandle(snapshot);
    if (status != ERROR_NO_MORE_FILES)
        return tr("Cannot check whether Prism Launcher is running (Windows error %1). Close Prism and try again.").arg(status);
#endif
    return {};
}

QString loadSelectedProfile(const QString& stateRoot, QString* error)
{
    setError(error, {});
    if (stateRoot.trimmed().isEmpty()) {
        setError(error, tr("The Chroma settings folder is unavailable."));
        return {};
    }
    QFile file(QDir(stateRoot).filePath("profile.json"));
    if (!file.exists())
        return {};
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, tr("Cannot read the saved Prism folder: %1").arg(file.errorString()));
        return {};
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.read(1024 * 1024), &parseError);
    const auto object = document.object();
    const QString selected = object.value("profileRoot").toString();
    if (!file.atEnd() || file.error() != QFileDevice::NoError || parseError.error != QJsonParseError::NoError || !document.isObject() ||
        object.value("formatVersion").toInt() != 1 || !object.value("profileRoot").isString() ||
        (!selected.isEmpty() && !QDir::isAbsolutePath(selected))) {
        setError(error, tr("The saved Prism folder selection is invalid. Choose the folder again."));
        return {};
    }
    return selected.isEmpty() ? QString() : resolvedPath(selected);
}

bool saveSelectedProfile(const QString& stateRoot, const QString& profileRoot, QString* error)
{
    setError(error, {});
    ProfileInfo profile;
    if (!profileRoot.isEmpty()) {
        profile = inspect(profileRoot);
        if (!profile.error.isEmpty()) {
            setError(error, profile.error);
            return false;
        }
    }
    if (stateRoot.trimmed().isEmpty()) {
        setError(error, tr("The Chroma settings folder is unavailable."));
        return false;
    }
    const QString statePath = resolvedPath(stateRoot);
    if (!profileRoot.isEmpty() && (containsPath(profile.root, statePath) || containsPath(profile.instancesPath, statePath) ||
                                   containsPath(profile.iconsPath, statePath))) {
        setError(error, tr("Chroma must save its folder selection outside the shared Prism folder."));
        return false;
    }
    if (!QDir().mkpath(statePath)) {
        setError(error, tr("Cannot create the Chroma settings folder: %1").arg(statePath));
        return false;
    }
    const QString selectionPath = QDir(statePath).filePath("profile.json");
    const QFileInfo selectionInfo(selectionPath);
    if (selectionInfo.isSymLink() || selectionInfo.isJunction()) {
        setError(error, tr("The saved folder selection must be a regular file: %1").arg(selectionPath));
        return false;
    }
    const QByteArray contents = QJsonDocument(QJsonObject{ { "formatVersion", 1 }, { "profileRoot", profile.root } }).toJson();
    QSaveFile file(selectionPath);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit()) {
        setError(error, tr("Cannot save the Prism folder selection: %1").arg(file.errorString()));
        return false;
    }
    return true;
}

}  // namespace ChromaProfile
