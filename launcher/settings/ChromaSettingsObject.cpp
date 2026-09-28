// SPDX-License-Identifier: GPL-3.0-only
#include "ChromaSettingsObject.h"

#include <QDir>
#include <QFileInfo>
#include <QScopedValueRollback>
#include <QSet>

#include "settings/Setting.h"

ChromaSettingsObject::ChromaSettingsObject(QString sharedFilePath, QObject* parent)
    : INISettingsObject(sharedFilePath, parent), m_uiFilePath(QFileInfo(sharedFilePath).dir().filePath("chroma-ui.cfg"))
{
    if (QFileInfo::exists(m_uiFilePath))
        m_uiIni.loadFile(m_uiFilePath);
}

bool ChromaSettingsObject::isUiSetting(const Setting& setting)
{
    static const QSet<QString> keys = {
        "ApplicationTheme",
        "AccentColor",
        "IconTheme",
        "BackgroundCat",
        "TheCat",
        "CatOpacity",
        "CatFit",
        "MenuBarInsteadOfToolBar",
        "StatusBarVisible",
        "ToolbarsLocked",
        "InstSortMode",
        "InstRenamingMode",
        "SelectedInstance",
        "LastUsedGroupForNewInstance",
        "ConsoleFont",
        "ConsoleFontSize",
        "ShowGameTime",
        "ShowGlobalGameTime",
        "ShowGameTimeWithoutDays",
        "MainWindowState",
        "ConsoleWindowState",
    };
    for (const auto& key : setting.configKeys()) {
        if (keys.contains(key) || key.endsWith("Geometry") || key.startsWith("UI/") || key.startsWith("WideBarVisibility_") ||
            key.startsWith("Chroma"))
            return true;
    }
    return false;
}

void ChromaSettingsObject::setFilePath(const QString& filePath)
{
    m_filePath = filePath;
    m_uiFilePath = QFileInfo(filePath).dir().filePath("chroma-ui.cfg");
    reload();
}

bool ChromaSettingsObject::reload()
{
    INIFile shared;
    INIFile ui;
    if (!shared.loadFile(m_filePath) || (QFileInfo::exists(m_uiFilePath) && !ui.loadFile(m_uiFilePath)))
        return false;
    m_ini = std::move(shared);
    m_uiIni = std::move(ui);
    m_doSave = false;
    m_uiDirty = false;

    // SettingsObject emits the freshly loaded values via Setting::set. Notify
    // observers, but do not materialize defaults or rewrite Prism's configuration.
    QScopedValueRollback<bool> reloading(m_reloading, true);
    return SettingsObject::reload();
}

void ChromaSettingsObject::resumeSave()
{
    INISettingsObject::resumeSave();
    m_doSave = false;
    if (m_uiDirty)
        saveUi();
}

void ChromaSettingsObject::saveUi()
{
    m_uiDirty = true;
    if (!m_suspendSave && m_uiIni.saveFile(m_uiFilePath))
        m_uiDirty = false;
}

void ChromaSettingsObject::changeSetting(const Setting& setting, QVariant value)
{
    if (m_reloading || !contains(setting.id()))
        return;
    if (!isUiSetting(setting)) {
        INISettingsObject::changeSetting(setting, value);
        return;
    }
    auto keys = setting.configKeys();
    if (value.isValid())
        m_uiIni.set(keys.takeFirst(), value);
    for (const auto& key : keys)
        m_uiIni.remove(key);
    saveUi();
}

void ChromaSettingsObject::resetSetting(const Setting& setting)
{
    if (m_reloading || !contains(setting.id()))
        return;
    if (!isUiSetting(setting)) {
        INISettingsObject::resetSetting(setting);
        return;
    }
    for (const auto& key : setting.configKeys())
        m_uiIni.remove(key);
    saveUi();
}

QVariant ChromaSettingsObject::retrieveValue(const Setting& setting)
{
    if (!isUiSetting(setting))
        return INISettingsObject::retrieveValue(setting);
    if (contains(setting.id())) {
        for (const auto& key : setting.configKeys()) {
            if (m_uiIni.contains(key))
                return m_uiIni.value(key);
        }
    }
    return {};
}
