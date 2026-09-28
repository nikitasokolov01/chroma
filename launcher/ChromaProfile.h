// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <QString>
#include <QStringList>

namespace ChromaProfile {

struct ProfileInfo {
    QString root;
    QString instancesPath;
    QString iconsPath;
    int instanceCount = 0;
    QString error;
};

// Read an existing Prism profile without changing any files. Paths are absolute;
// existing symlinks are resolved so aliases identify the same profile.
ProfileInfo inspect(const QString& folder);
QStringList detectedRoots();
bool sameProfilePath(const QString& first, const QString& second);

// Conservative Windows preflight: any running Prism executable may own the
// profile. Other platforms rely on the launcher's lock and one-launcher rule.
QString runningPrismError();

// The selection belongs to Chroma's separate state directory, never to the
// selected Prism folder. An absent selection returns an empty path without error.
// Loading does not require the profile to be available; inspect it before use.
// Saving an empty profileRoot clears the selection atomically.
QString loadSelectedProfile(const QString& stateRoot, QString* error = nullptr);
bool saveSelectedProfile(const QString& stateRoot, const QString& profileRoot, QString* error = nullptr);

}  // namespace ChromaProfile
