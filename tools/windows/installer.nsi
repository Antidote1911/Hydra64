; Installer of Hydra64 for Windows (NSIS 3, https://nsis.sourceforge.io).
;
;   makensis -DVERSION=0.1.0 -DSTAGE=<Hydra64 folder> -DROOT=<repository> -DOUTFILE=<setup.exe> installer.nsi
;
; STAGE is the folder gathered by tools/windows/package.py (the one the zip archive holds): the
; installer copies it as it is. Built by .github/workflows/release.yml on the Windows machine.
;
; What it does: copies the program into "Program Files\Hydra64" (administrator rights), adds a
; Start menu entry and, if wanted, a desktop shortcut, and registers the uninstaller in the
; "Installed apps" list. It associates no file type and leaves the user's Documents\hydra64
; folder and settings alone — the uninstaller too.
;   Silent: setup.exe /S [/D=C:\folder]      uninstall.exe /S

Unicode true
SetCompressor /SOLID lzma

!ifndef VERSION
  !error "VERSION is missing (-DVERSION=0.1.0)"
!endif
!ifndef STAGE
  !error "STAGE is missing (-DSTAGE=<folder gathered by package.py>)"
!endif
!ifndef ROOT
  !error "ROOT is missing (-DROOT=<repository>)"
!endif
!ifndef OUTFILE
  !define OUTFILE "Hydra64-${VERSION}-windows-x64-setup.exe"
!endif

!include "MUI2.nsh"
!include "x64.nsh"

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hydra64"

Name "Hydra64"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\Hydra64"
RequestExecutionLevel admin
ManifestDPIAware true
BrandingText "Hydra64 ${VERSION}"

VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "Hydra64"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "FileDescription" "Hydra64 installer"
VIAddVersionKey "LegalCopyright" "GNU General Public License, version 3"

!define MUI_ICON "${ROOT}\resources\windows\hydra64.ico"
!define MUI_UNICON "${ROOT}\resources\windows\hydra64.ico"
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${ROOT}\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; The first language is the default one; the installer follows the language of Windows.
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"

LangString NAME_PROGRAM ${LANG_ENGLISH} "Hydra64 (program, Stockfish, Shuffle, bundled databases)"
LangString NAME_PROGRAM ${LANG_FRENCH} "Hydra64 (programme, Stockfish, Shuffle, bases livrées)"
LangString NAME_DESKTOP ${LANG_ENGLISH} "Desktop shortcut"
LangString NAME_DESKTOP ${LANG_FRENCH} "Raccourci sur le bureau"
LangString NEED_64 ${LANG_ENGLISH} "Hydra64 needs a 64-bit Windows (10 version 1903 or later)."
LangString NEED_64 ${LANG_FRENCH} "Hydra64 demande un Windows 64 bits (10 version 1903 ou plus récent)."
LangString KEPT ${LANG_ENGLISH} "Your databases and settings (Documents\hydra64) are kept."
LangString KEPT ${LANG_FRENCH} "Vos bases et vos réglages (Documents\hydra64) sont conservés."

Function .onInit
  ${IfNot} ${RunningX64}
    MessageBox MB_ICONSTOP "$(NEED_64)"
    Abort
  ${EndIf}
  SetRegView 64
  SetShellVarContext all
FunctionEnd

Function un.onInit
  SetRegView 64
  SetShellVarContext all
FunctionEnd

; What belongs to Hydra64 in its folder: removed before an update (libraries of an older version
; must not stay) and by the uninstaller. Never the folder as a whole: it may have been chosen by
; hand and hold something else.
!macro RemoveProgramFiles
  Delete "$INSTDIR\*.exe"
  Delete "$INSTDIR\*.dll"
  Delete "$INSTDIR\qt.conf"
  Delete "$INSTDIR\LICENSE"
  RMDir /r "$INSTDIR\plugins"
  RMDir /r "$INSTDIR\translations"
  RMDir /r "$INSTDIR\databases"
!macroend

Section "$(NAME_PROGRAM)" SecProgram
  SectionIn RO
  ${If} ${FileExists} "$INSTDIR\hydra64.exe"
    !insertmacro RemoveProgramFiles
  ${EndIf}
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"
  CreateShortcut "$SMPROGRAMS\Hydra64.lnk" "$INSTDIR\hydra64.exe"
SectionEnd

Section "$(NAME_DESKTOP)" SecDesktop
  CreateShortcut "$DESKTOP\Hydra64.lnk" "$INSTDIR\hydra64.exe"
SectionEnd

; Entry of the "Installed apps" list (after the files: their size is known)
Section "-Register"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayName" "Hydra64"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "Publisher" "Antidote1911"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "URLInfoAbout" "https://antidote1911.github.io/Hydra64/"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "DisplayIcon" "$INSTDIR\hydra64.exe"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINSTALL_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "${UNINSTALL_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "NoRepair" 1
  SectionGetSize ${SecProgram} $0
  WriteRegDWORD HKLM "${UNINSTALL_KEY}" "EstimatedSize" $0
SectionEnd

Section "Uninstall"
  !insertmacro RemoveProgramFiles
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR" ; only if nothing else is left in it
  Delete "$SMPROGRAMS\Hydra64.lnk"
  Delete "$DESKTOP\Hydra64.lnk"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  IfSilent +2
    MessageBox MB_ICONINFORMATION "$(KEPT)"
SectionEnd
