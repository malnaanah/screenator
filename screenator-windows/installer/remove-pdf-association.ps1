# Remove Screenator PDF associations for the current user.
#
# Windows' "Open with > Choose another app" can create a generic
# `pdf_auto_file` ProgID and an entry in the per-user OpenWithList.  Those
# entries survive deleting a portable EXE and are not removable from the
# normal Windows UI.  This script removes every Screenator-related PDF
# association, whether it came from a portable copy or an installed one.
#
# It deliberately does NOT touch
#   HKCU\...\FileExts\.pdf\UserChoice
# so the user's chosen default PDF viewer is preserved.

$ErrorActionPreference = 'SilentlyContinue'
$removed = New-Object System.Collections.Generic.List[string]
$classes = 'HKCU:\Software\Classes'
$classesReg = 'HKCU\Software\Classes'
$exts = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pdf'

function Remove-Key($path, $label) {
    if (Test-Path $path) {
        Remove-Item $path -Recurse -Force
        $removed.Add($label)
    }
}

function Remove-Value($path, $name, $label) {
    if (-not (Test-Path $path)) { return }
    $item = Get-Item $path
    if ($item.GetValueNames() -contains $name) {
        Remove-ItemProperty -Path $path -Name $name -Force
        $removed.Add("$label ($name)")
    }
}

function Test-ScreenatorCommand($path) {
    if (-not (Test-Path $path)) { return $false }
    $cmd = (Get-ItemProperty $path).'(default)'
    return ($cmd -and ($cmd -match '(?i)screenator\.exe'))
}

# 1. App registration and ProgID created by this or an older Screenator.
Remove-Key "$classes\Applications\Screenator.exe" 'Applications\Screenator.exe'
Remove-Key "$classes\Screenator.pdf" 'Screenator.pdf ProgID'

# 2. The generic pdf_auto_file ProgID used by "Open with" for a portable copy.
$autoKey = "$classes\pdf_auto_file"
$autoCmd = "$autoKey\shell\open\command"
$autoIsScreenator = Test-ScreenatorCommand $autoCmd
$autoGone = -not (Test-Path $autoKey)
if ($autoIsScreenator) {
    Remove-Key $autoKey 'pdf_auto_file (portable Screenator)'
    Remove-Value "$classes\.pdf\OpenWithProgids" 'pdf_auto_file' 'OpenWithProgids'
    Remove-Value "$exts\OpenWithProgids" 'pdf_auto_file' 'FileExts OpenWithProgids'
}
# Remove a .pdf default that points at pdf_auto_file when that ProgID is a
# portable Screenator handler or has already been removed (dangling).
if ((Test-Path "$classes\.pdf") -and
    ((Get-Item "$classes\.pdf").GetValue('') -eq 'pdf_auto_file') -and
    ($autoIsScreenator -or $autoGone)) {
    # Remove-ItemProperty cannot reliably remove the unnamed default value;
    # reg.exe can.
    & reg.exe delete "$classesReg\.pdf" /ve /f | Out-Null
    $removed.Add('.pdf default -> pdf_auto_file')
}

# 3. Stale Screenator.exe entries in the per-user OpenWithList.
$owl = "$exts\OpenWithList"
if (Test-Path $owl) {
    $key = Get-Item $owl
    foreach ($name in @($key.GetValueNames())) {
        if ($name -eq 'MRUList') { continue }
        $val = $key.GetValue($name)
        if ($val -and ($val -match '(?i)screenator\.exe')) {
            Remove-ItemProperty -Path $owl -Name $name -Force
            $removed.Add("OpenWithList $name=$val")
        }
    }
    # Let Windows rebuild the MRU order without the removed entries.
    Remove-ItemProperty -Path $owl -Name 'MRUList' -Force
}

# 4. Default-apps capabilities and the RegisteredApplications entry.
Remove-Key 'HKCU:\Software\Screenator\Capabilities' 'Screenator Capabilities'
Remove-Value 'HKCU:\Software\RegisteredApplications' 'Screenator' 'RegisteredApplications'

# 5. ProgID entries in both OpenWithProgids locations.
Remove-Value "$classes\.pdf\OpenWithProgids" 'Screenator.pdf' 'OpenWithProgids'
Remove-Value "$exts\OpenWithProgids" 'Screenator.pdf' 'FileExts OpenWithProgids'

# Refresh Explorer so the Open with menu is rebuilt.
Add-Type -Namespace Shell32 -Name Native -MemberDefinition @'
[DllImport("shell32.dll")] public static extern void SHChangeNotify(int eventId, int flags, IntPtr item1, IntPtr item2);
'@
[Shell32.Native]::SHChangeNotify(0x08000000, 0, [IntPtr]::Zero, [IntPtr]::Zero)

if ($removed.Count -eq 0) {
    Write-Host 'No stale Screenator PDF associations were found.'
} else {
    Write-Host 'Removed:'
    foreach ($r in $removed) { Write-Host "  $r" }
}
Write-Host ''
Write-Host 'Your default PDF viewer is unchanged.'
