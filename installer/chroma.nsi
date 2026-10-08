Unicode true
!include MUI2.nsh
!include LogicLib.nsh
!include FileFunc.nsh
!include x64.nsh

Name "Chroma ${VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\Chroma"
InstallDirRegKey HKCU "Software\Chroma" "InstallPath"
RequestExecutionLevel user
SetCompressor /SOLID zlib
ShowInstDetails show
ShowUninstDetails show
VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=1033 "ProductName" "Chroma"
VIAddVersionKey /LANG=1033 "FileDescription" "Chroma per-user installer"
VIAddVersionKey /LANG=1033 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=1033 "LegalCopyright" "Chroma and upstream contributors"

Var NoIntegration
!define MUI_ABORTWARNING
!define MUI_ICON "${PROJECT_ROOT}\program_info\chroma.ico"
!define MUI_UNICON "${PROJECT_ROOT}\program_info\chroma.ico"
!define MUI_FINISHPAGE_RUN "$INSTDIR\chroma.exe"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_FINISHPAGE_TEXT "Chroma is installed. Profiles and shared Prism data are kept separately and are preserved when Chroma is uninstalled."
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${PROJECT_ROOT}\LICENSE"
!define MUI_PAGE_HEADER_TEXT "Microsoft Visual C++ runtime terms"
!define MUI_PAGE_HEADER_SUBTEXT "These terms apply only to the Microsoft runtime DLLs."
!define MUI_LICENSEPAGE_TEXT_TOP "The following terms cover the bundled Microsoft runtime DLLs only. Chroma and other open-source components retain their own licenses."
!define MUI_LICENSEPAGE_CHECKBOX
!insertmacro MUI_PAGE_LICENSE "${PROJECT_ROOT}\docs\licenses\Microsoft-Visual-Cpp-Runtime-14.44.rtf"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE English

Function .onInit
    SetShellVarContext current
    ${IfNot} ${RunningX64}
        MessageBox MB_ICONSTOP "Chroma requires 64-bit Windows."
        Abort
    ${EndIf}
    StrCpy $NoIntegration 0
    ${GetParameters} $0
    ClearErrors
    ${GetOptions} $0 "/NOINTEGRATION" $1
    ${IfNot} ${Errors}
        StrCpy $NoIntegration 1
    ${EndIf}
FunctionEnd

Function .onVerifyInstDir
    ; A portable folder can contain real profiles. Do not change its data mode.
    IfFileExists "$INSTDIR\portable.txt" 0 done
    Abort
done:
FunctionEnd

Section "Chroma" SEC_MAIN
    SetShellVarContext current
    IfFileExists "$INSTDIR\portable.txt" 0 +3
        MessageBox MB_ICONSTOP "Choose a new folder. This folder contains a portable launcher."
        Abort
    ; This list is generated only from a new, empty release staging directory.
    !include "${INSTALL_MANIFEST}"
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    ${If} $NoIntegration == 1
        FileOpen $0 "$INSTDIR\chroma-no-integration" w
        FileWrite $0 "No shell or registry integration.$\r$\n"
        FileClose $0
    ${Else}
        Delete "$INSTDIR\chroma-no-integration"
        WriteRegStr HKCU "Software\Chroma" "InstallPath" "$INSTDIR"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma" "DisplayName" "Chroma"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma" "DisplayVersion" "${VERSION}"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma" "Publisher" "Chroma contributors"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma" "InstallLocation" "$INSTDIR"
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma" "UninstallString" '"$INSTDIR\Uninstall.exe"'
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma" "QuietUninstallString" '"$INSTDIR\Uninstall.exe" /S'
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma" "DisplayIcon" '"$INSTDIR\chroma.exe",0'
        WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma" "URLInfoAbout" "https://github.com/nikitasokolov01/chroma"
        WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma" "NoModify" 1
        WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma" "NoRepair" 1
        CreateDirectory "$SMPROGRAMS\Chroma"
        CreateShortCut "$SMPROGRAMS\Chroma\Chroma.lnk" "$INSTDIR\chroma.exe" "" "$INSTDIR\chroma.exe" 0
        CreateShortCut "$SMPROGRAMS\Chroma\Uninstall Chroma.lnk" "$INSTDIR\Uninstall.exe"
    ${EndIf}
SectionEnd

Section Uninstall
    SetShellVarContext current
    IfFileExists "$INSTDIR\chroma-no-integration" skipIntegration
    ; Remove registration only if it still belongs to this installation.
    ReadRegStr $0 HKCU "Software\Chroma" "InstallPath"
    ${If} $0 == $INSTDIR
        DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Chroma"
        DeleteRegKey HKCU "Software\Chroma"
        Delete "$SMPROGRAMS\Chroma\Chroma.lnk"
        Delete "$SMPROGRAMS\Chroma\Uninstall Chroma.lnk"
        RMDir "$SMPROGRAMS\Chroma"
    ${EndIf}
skipIntegration:
    ; No recursive directory deletion, profile paths, or wildcard deletes.
    !include "${UNINSTALL_MANIFEST}"
    Delete "$INSTDIR\chroma-no-integration"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
SectionEnd
