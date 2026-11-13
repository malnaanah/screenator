; Screenator 8.0 installer for Windows
; Build with: makensis screenator.nsi          (Windows NSIS)
;             makensis -DPOSIX_BUILD screenator.nsi   (Linux/macOS NSIS)
; The wrapper ./build-installer.sh picks the right form.
;
; The PDF option registers Screenator as *one of* the applications that can
; open PDF files ("Open with" and Settings > Default apps).  It never sets
; Screenator as the default PDF handler, so the user's preferred viewer is
; left untouched.  The option is selected by default; it is harmless because
; the default viewer is not changed.
;
; Installing or uninstalling also removes PDF associations left behind by an
; older portable Screenator copy (the generic pdf_auto_file ProgID and stale
; OpenWithList entries), which Windows otherwise offers no way to remove.

Unicode true
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"

!define APPNAME "Screenator"
!define APPVERSION "8.0"
!define PUBLISHER "Screenator contributors"
!define APPEXE "Screenator.exe"
!define UNINSTKEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\Screenator"

; Remove stale Screenator PDF associations.  PFX gives the labels a unique
; prefix so the macro can be used in both the installer and the uninstaller.
!macro CleanStalePdfAssoc PFX
  DeleteRegKey HKCU "Software\Classes\Applications\Screenator.exe"
  DeleteRegKey HKCU "Software\Classes\Screenator.pdf"
  DeleteRegValue HKCU "Software\Classes\.pdf\OpenWithProgids" "Screenator.pdf"
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithProgids" "Screenator.pdf"

  ; A portable copy associated through "Open with" gets the generic
  ; pdf_auto_file ProgID, whose command points at the portable EXE.
  ReadRegStr $R2 HKCU "Software\Classes\pdf_auto_file\shell\open\command" ""
  StrCmp $R2 "" ${PFX}_auto_done
  StrCpy $R4 $R2
  ${PFX}_auto_scan:
    StrCpy $R5 $R4 14
    StrCmpS $R5 "Screenator.exe" ${PFX}_auto_match
    StrCpy $R4 $R4 "" 1
    StrCmp $R4 "" ${PFX}_auto_done
    Goto ${PFX}_auto_scan
  ${PFX}_auto_match:
    DeleteRegKey HKCU "Software\Classes\pdf_auto_file"
    DeleteRegValue HKCU "Software\Classes\.pdf\OpenWithProgids" "pdf_auto_file"
    DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithProgids" "pdf_auto_file"
    ReadRegStr $R2 HKCU "Software\Classes\.pdf" ""
    StrCmp $R2 "pdf_auto_file" 0 ${PFX}_auto_done
    DeleteRegValue HKCU "Software\Classes\.pdf" ""
  ${PFX}_auto_done:

  ; Stale Screenator entries in the per-user OpenWithList.  Deleting the
  ; current value shifts the next one into the same index, so the index is
  ; not advanced after a deletion.
  StrCpy $R0 0
  ${PFX}_owl_loop:
    EnumRegValue $R1 HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithList" $R0
    StrCmp $R1 "" ${PFX}_owl_done
    ReadRegStr $R2 HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithList" "$R1"
    StrCmp $R2 "" ${PFX}_owl_next
    StrCpy $R4 $R2
    ${PFX}_owl_scan:
      StrCpy $R5 $R4 14
      StrCmpS $R5 "Screenator.exe" ${PFX}_owl_remove
      StrCpy $R4 $R4 "" 1
      StrCmp $R4 "" ${PFX}_owl_next
      Goto ${PFX}_owl_scan
    ${PFX}_owl_remove:
      DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithList" "$R1"
      Goto ${PFX}_owl_loop
    ${PFX}_owl_next:
      IntOp $R0 $R0 + 1
      Goto ${PFX}_owl_loop
  ${PFX}_owl_done:
  DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf\OpenWithList" "MRUList"

  DeleteRegKey HKCU "Software\Screenator\Capabilities"
  DeleteRegValue HKCU "Software\RegisteredApplications" "Screenator"
!macroend

; Path separator for the build host.  NSIS on Windows wants backslashes for
; the File instruction, while the POSIX port wants forward slashes.  Pass
; /DPOSIX_BUILD when building on Linux or macOS.
!ifdef POSIX_BUILD
  !define SEP "/"
!else
  !define SEP "\"
!endif

Name "${APPNAME} ${APPVERSION}"
!ifndef SETUPNAME
  !define SETUPNAME "Screenator-8.0-win64-setup"
!endif
OutFile "..${SEP}release${SEP}${SETUPNAME}.exe"
InstallDir "$LOCALAPPDATA\Programs\Screenator"
InstallDirRegKey HKCU "Software\Screenator" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
SetCompressorDictSize 32

VIProductVersion "8.0.0.0"
VIAddVersionKey "ProductName" "${APPNAME}"
VIAddVersionKey "FileDescription" "${APPNAME} installer"
VIAddVersionKey "FileVersion" "${APPVERSION}"
VIAddVersionKey "ProductVersion" "${APPVERSION}"
VIAddVersionKey "CompanyName" "${PUBLISHER}"
VIAddVersionKey "LegalCopyright" "GPL-2.0-or-later"

!define MUI_ICON "..${SEP}assets${SEP}icon.ico"
!define MUI_UNICON "..${SEP}assets${SEP}icon.ico"
!define MUI_ABORTWARNING

!define MUI_WELCOMEPAGE_TITLE "${APPNAME} ${APPVERSION}"
!define MUI_WELCOMEPAGE_TEXT "This wizard will install ${APPNAME}, a screen annotation tool for Windows with PDF markup support.$\r$\n$\r$\nThe PDF association task is selected by default. It adds ${APPNAME} to the Open with list and to Default apps, and removes PDF associations left by older portable copies, without changing your default PDF viewer."

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..${SEP}COPYING"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\${APPEXE}"
!define MUI_FINISHPAGE_RUN_TEXT "Start ${APPNAME}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ------------------------------------------------------------------
; sections
; ------------------------------------------------------------------

Section "${APPNAME} (required)" SEC_APP
  SectionIn RO
  SetOutPath "$INSTDIR"
  SetOverwrite on
  File "..${SEP}dist${SEP}${APPEXE}"
  File "..${SEP}dist${SEP}pdfium.dll"
  File /r "..${SEP}dist${SEP}Screenator Data"
  WriteUninstaller "$INSTDIR\uninst.exe"

  CreateDirectory "$SMPROGRAMS\${APPNAME}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\${APPEXE}"
  CreateShortcut "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk" \
                 "$INSTDIR\uninst.exe"

  WriteRegStr HKCU "Software\Screenator" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\Screenator" "Version" "${APPVERSION}"

  WriteRegStr HKCU "${UNINSTKEY}" "DisplayName" "${APPNAME}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayVersion" "${APPVERSION}"
  WriteRegStr HKCU "${UNINSTKEY}" "Publisher" "${PUBLISHER}"
  WriteRegStr HKCU "${UNINSTKEY}" "DisplayIcon" "$INSTDIR\${APPEXE}"
  WriteRegStr HKCU "${UNINSTKEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKCU "${UNINSTKEY}" "UninstallString" "$\"$INSTDIR\uninst.exe$\""
  WriteRegStr HKCU "${UNINSTKEY}" "QuietUninstallString" "$\"$INSTDIR\uninst.exe$\" /S"
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoModify" 1
  WriteRegDWORD HKCU "${UNINSTKEY}" "NoRepair" 1
  ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
  WriteRegDWORD HKCU "${UNINSTKEY}" "EstimatedSize" "$0"
SectionEnd

Section "Desktop shortcut" SEC_DESKTOP
  CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\${APPEXE}"
SectionEnd

Section "PDF Open with list" SEC_PDF
  ; Remove associations left by an older portable copy before registering the
  ; installed one.  This is what makes the old "Open with" entry disappear.
  !insertmacro CleanStalePdfAssoc ins

  ; Register the executable as an application that can open PDFs.
  WriteRegStr HKCU "Software\Classes\Applications\${APPEXE}" \
              "FriendlyAppName" "${APPNAME}"
  WriteRegStr HKCU "Software\Classes\Applications\${APPEXE}" \
              "ApplicationCompany" "${PUBLISHER}"
  WriteRegStr HKCU "Software\Classes\Applications\${APPEXE}\DefaultIcon" "" \
              "$INSTDIR\${APPEXE},0"
  WriteRegStr HKCU "Software\Classes\Applications\${APPEXE}\shell\open\command" "" \
              '"$INSTDIR\${APPEXE}" "%1"'
  WriteRegStr HKCU "Software\Classes\Applications\${APPEXE}\SupportedTypes" \
              ".pdf" ""

  ; A ProgID for the PDF handler, used by "Open with".
  WriteRegStr HKCU "Software\Classes\Screenator.pdf" "" "PDF Document"
  WriteRegStr HKCU "Software\Classes\Screenator.pdf" "FriendlyTypeName" "PDF Document"
  WriteRegStr HKCU "Software\Classes\Screenator.pdf" "PerceivedType" "document"
  WriteRegStr HKCU "Software\Classes\Screenator.pdf\DefaultIcon" "" \
              "$INSTDIR\${APPEXE},0"
  WriteRegStr HKCU "Software\Classes\Screenator.pdf\shell\open\command" "" \
              '"$INSTDIR\${APPEXE}" "%1"'

  ; Offer it in the "Open with" submenu but leave the default handler alone.
  WriteRegStr HKCU "Software\Classes\.pdf\OpenWithProgids" "Screenator.pdf" ""

  ; Capabilities so it also appears in Settings > Apps > Default apps.
  WriteRegStr HKCU "Software\Screenator\Capabilities" "ApplicationName" "${APPNAME}"
  WriteRegStr HKCU "Software\Screenator\Capabilities" "ApplicationDescription" \
              "Screen annotation and PDF markup"
  WriteRegStr HKCU "Software\Screenator\Capabilities" "ApplicationIcon" \
              "$INSTDIR\${APPEXE},0"
  WriteRegStr HKCU "Software\Screenator\Capabilities\FileAssociations" \
              ".pdf" "Screenator.pdf"
  WriteRegStr HKCU "Software\RegisteredApplications" "${APPNAME}" \
              "Software\Screenator\Capabilities"

  System::Call 'shell32::SHChangeNotify(i, i, i, i) (0x08000000, 0, 0, 0)'
SectionEnd

; The PDF task is on by default (it does not change the default viewer); the
; desktop shortcut stays opt-in.
Function .onInit
  SectionSetFlags ${SEC_DESKTOP} 0
FunctionEnd

; ------------------------------------------------------------------
; descriptions
; ------------------------------------------------------------------

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_APP} \
    "The ${APPNAME} application plus pdfium.dll and the Screenator Data folder."
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP} \
    "Create a shortcut to ${APPNAME} on the desktop."
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_PDF} \
    "Recommended and selected by default. Adds ${APPNAME} to the right-click Open with menu and to Settings > Default apps, and removes PDF associations left by older portable copies. Your current default PDF viewer is not changed."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

; ------------------------------------------------------------------
; uninstall
; ------------------------------------------------------------------

Section "Uninstall"
  Delete "$DESKTOP\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
  Delete "$SMPROGRAMS\${APPNAME}\Uninstall ${APPNAME}.lnk"
  RMDir "$SMPROGRAMS\${APPNAME}"

  Delete "$INSTDIR\${APPEXE}"
  Delete "$INSTDIR\pdfium.dll"
  Delete "$INSTDIR\uninst.exe"
  RMDir /r "$INSTDIR\Screenator Data"
  RMDir "$INSTDIR"

  ; Remove the association, including entries left by older portable copies.
  !insertmacro CleanStalePdfAssoc un
  DeleteRegKey HKCU "Software\Screenator"
  DeleteRegKey HKCU "${UNINSTKEY}"

  System::Call 'shell32::SHChangeNotify(i, i, i, i) (0x08000000, 0, 0, 0)'
SectionEnd
