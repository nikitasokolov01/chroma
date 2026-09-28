// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "settings/INISettingsObject.h"

// Launch settings remain in Prism's configuration. Chroma's interface preferences
// live alongside it, so either launcher can keep its own theme and window layout.
class ChromaSettingsObject : public INISettingsObject {
    Q_OBJECT

   public:
    explicit ChromaSettingsObject(QString sharedFilePath, QObject* parent = nullptr);

    QString uiFilePath() const { return m_uiFilePath; }
    void setFilePath(const QString& filePath) override;
    bool reload() override;
    void resumeSave() override;

   protected:
    void changeSetting(const Setting& setting, QVariant value) override;
    void resetSetting(const Setting& setting) override;
    QVariant retrieveValue(const Setting& setting) override;

   private:
    static bool isUiSetting(const Setting& setting);
    void saveUi();

    INIFile m_uiIni;
    QString m_uiFilePath;
    bool m_uiDirty = false;
    bool m_reloading = false;
};
