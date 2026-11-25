; Installer of Hydra64 for Windows (NSIS 3, https://nsis.sourceforge.io).
;
;   makensis -DVERSION=1.0.0 -DSTAGE=<Hydra64 folder> -DROOT=<repository> -DOUTFILE=<setup.exe>
;            [-DLICENSE=<licence text>] installer.nsi
;
; LICENSE: the text of the licence page. NSIS reads a file without a byte order mark in the
; Windows code page ("©" comes out wrong): the workflow passes a copy of LICENSE that starts
; with the UTF-8 mark. Without it, the LICENSE file of the repository is taken as it is.
;
; STAGE is the folder gathered by tools/windows/package.py (the one the zip archive holds): the
; installer copies it as it is. Built by .github/workflows/release.yml on the Windows machine.
;
; What it does: copies the program into "Program Files\Hydra64" (administrator rights), adds a
; Start menu entry and, if wanted, a desktop shortcut, and registers the uninstaller in the
; "Installed apps" list. If wanted too: Hydra64 databases (.hy64, .hy64i) open with a double-click,
; and Hydra64 is offered in the "Open with" list of .pgn files — without taking them from the
; program that opens them today. A processor without AVX2 (Stockfish needs it) is reported.
; The user's Documents\hydra64 folder and settings are left alone — by the uninstaller too.
;   Silent: setup.exe /S [/D=C:\folder]      uninstall.exe /S

Unicode true
SetCompressor /SOLID lzma

!ifndef VERSION
  !error "VERSION is missing (-DVERSION=1.0.0)"
!endif
!ifndef STAGE
  !error "STAGE is missing (-DSTAGE=<folder gathered by package.py>)"
!endif
!ifndef ROOT
  !error "ROOT is missing (-DROOT=<repository>)"
!endif
!ifndef LICENSE
  !define LICENSE "${ROOT}\LICENSE"
!endif
!ifndef OUTFILE
  !define OUTFILE "Hydra64-${VERSION}-windows-x64-setup.exe"
!endif

!include "MUI2.nsh"
!include "x64.nsh"

!define UNINSTALL_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Hydra64"
!define CLASSES "Software\Classes"
!define PF_AVX2_INSTRUCTIONS_AVAILABLE 40 ; IsProcessorFeaturePresent
!define SHCNE_ASSOCCHANGED 0x08000000

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
; First and last pages: the logo (tools/windows/make_installer_art.sh)
!define MUI_WELCOMEFINISHPAGE_BITMAP "${ROOT}\resources\windows\installer-side.bmp"
!define MUI_UNWELCOMEFINISHPAGE_BITMAP "${ROOT}\resources\windows\installer-side.bmp"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${LICENSE}"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
; "Run Hydra64" on the last page: started by the Explorer, hence with the user's ordinary rights
; and not with the administrator rights of the installer.
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_FUNCTION RunAsUser
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; The first language is the default one; the installer follows the language of Windows.
!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "French"

LangString NAME_PROGRAM ${LANG_ENGLISH} "Hydra64"
LangString NAME_PROGRAM ${LANG_FRENCH} "Hydra64"
LangString DESC_PROGRAM ${LANG_ENGLISH} "The program, Stockfish, Shuffle, the bundled databases, a Start menu entry."
LangString DESC_PROGRAM ${LANG_FRENCH} "Le programme, Stockfish, Shuffle, les bases livrées, une entrée du menu Démarrer."
LangString NAME_DESKTOP ${LANG_ENGLISH} "Desktop shortcut"
LangString NAME_DESKTOP ${LANG_FRENCH} "Raccourci sur le bureau"
LangString DESC_DESKTOP ${LANG_ENGLISH} "A Hydra64 icon on the desktop."
LangString DESC_DESKTOP ${LANG_FRENCH} "Une icône Hydra64 sur le bureau."
LangString DESC_FILES ${LANG_ENGLISH} ".hy64 databases open with a double-click. Hydra64 is added to $\"Open with$\" for .pgn files."
LangString DESC_FILES ${LANG_FRENCH} "Les bases .hy64 s'ouvrent par double-clic. Hydra64 est ajouté à « Ouvrir avec » pour les .pgn."
LangString NAME_FILES ${LANG_ENGLISH} ".hy64 and .pgn files"
LangString NAME_FILES ${LANG_FRENCH} "Fichiers .hy64 et .pgn"
LangString TYPE_BASE ${LANG_ENGLISH} "Hydra64 database"
LangString TYPE_BASE ${LANG_FRENCH} "Base Hydra64"
LangString TYPE_PGN ${LANG_ENGLISH} "Chess games (PGN)"
LangString TYPE_PGN ${LANG_FRENCH} "Parties d'échecs (PGN)"
LangString NO_AVX2 ${LANG_ENGLISH} "This processor does not have the AVX2 instructions.$\r$\n$\r$\nHydra64 itself will start, but the built-in Stockfish engine needs them: there will be no automatic evaluation and no built-in opponent unless you add another UCI engine.$\r$\n$\r$\nInstall anyway?"
LangString NO_AVX2 ${LANG_FRENCH} "Ce processeur n'a pas les instructions AVX2.$\r$\n$\r$\nHydra64 lui-même démarrera, mais le moteur Stockfish intégré en a besoin : pas d'évaluation automatique ni d'adversaire intégré, à moins d'ajouter un autre moteur UCI.$\r$\n$\r$\nInstaller quand même ?"
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
  System::Call 'kernel32::IsProcessorFeaturePresent(i ${PF_AVX2_INSTRUCTIONS_AVAILABLE}) i .r0'
  ${If} $0 == 0
    MessageBox MB_ICONEXCLAMATION|MB_YESNO "$(NO_AVX2)" /SD IDYES IDYES +2
    Abort
  ${EndIf}
FunctionEnd

Function RunAsUser
  Exec '"$WINDIR\explorer.exe" "$INSTDIR\hydra64.exe"'
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
  Delete "$INSTDIR\THIRD-PARTY.md"
  RMDir /r "$INSTDIR\licenses"
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

; File types. .hy64 and .hy64i (index of a database in four files) are Hydra64's own: it becomes
; their program. .pgn belongs to many programs: Hydra64 is only added to its "Open with" list.
Section "$(NAME_FILES)" SecFiles
  WriteRegStr HKLM "${CLASSES}\Hydra64.Database" "" "$(TYPE_BASE)"
  WriteRegStr HKLM "${CLASSES}\Hydra64.Database\DefaultIcon" "" "$INSTDIR\hydra64.exe,0"
  WriteRegStr HKLM "${CLASSES}\Hydra64.Database\shell\open\command" "" '"$INSTDIR\hydra64.exe" "%1"'
  WriteRegStr HKLM "${CLASSES}\Hydra64.Pgn" "" "$(TYPE_PGN)"
  WriteRegStr HKLM "${CLASSES}\Hydra64.Pgn\DefaultIcon" "" "$INSTDIR\hydra64.exe,0"
  WriteRegStr HKLM "${CLASSES}\Hydra64.Pgn\shell\open\command" "" '"$INSTDIR\hydra64.exe" "%1"'
  WriteRegStr HKLM "${CLASSES}\.hy64" "" "Hydra64.Database"
  WriteRegStr HKLM "${CLASSES}\.hy64i" "" "Hydra64.Database"
  WriteRegStr HKLM "${CLASSES}\.pgn\OpenWithProgids" "Hydra64.Pgn" ""
  System::Call 'shell32::SHChangeNotify(i ${SHCNE_ASSOCCHANGED}, i 0, p 0, p 0)'
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

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SecProgram} "$(DESC_PROGRAM)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "$(DESC_DESKTOP)"
  !insertmacro MUI_DESCRIPTION_TEXT ${SecFiles} "$(DESC_FILES)"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
  !insertmacro RemoveProgramFiles
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR" ; only if nothing else is left in it
  Delete "$SMPROGRAMS\Hydra64.lnk"
  Delete "$DESKTOP\Hydra64.lnk"
  DeleteRegKey HKLM "${UNINSTALL_KEY}"
  ; File types: only what still points at Hydra64
  ReadRegStr $0 HKLM "${CLASSES}\.hy64" ""
  ${If} $0 == "Hydra64.Database"
    DeleteRegKey HKLM "${CLASSES}\.hy64"
  ${EndIf}
  ReadRegStr $0 HKLM "${CLASSES}\.hy64i" ""
  ${If} $0 == "Hydra64.Database"
    DeleteRegKey HKLM "${CLASSES}\.hy64i"
  ${EndIf}
  DeleteRegValue HKLM "${CLASSES}\.pgn\OpenWithProgids" "Hydra64.Pgn"
  DeleteRegKey HKLM "${CLASSES}\Hydra64.Database"
  DeleteRegKey HKLM "${CLASSES}\Hydra64.Pgn"
  System::Call 'shell32::SHChangeNotify(i ${SHCNE_ASSOCCHANGED}, i 0, p 0, p 0)'
  IfSilent +2
    MessageBox MB_ICONINFORMATION "$(KEPT)"
SectionEnd
