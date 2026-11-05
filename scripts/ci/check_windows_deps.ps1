# SPDX-License-Identifier: AGPL-3.0-or-later
# SPK-5, T-FMT-05, T-FMT-06: the Windows plug-in is x64 and links the C/C++ runtime statically, so it must not depend
# on MSVCP*.dll, VCRUNTIME*.dll or the api-ms-win-crt-* forwarders. Needs dumpbin (run after ilammy/msvc-dev-cmd).
param([Parameter(Mandatory = $true)][string]$Binary)

$headers = & dumpbin /headers $Binary
if ($LASTEXITCODE -ne 0) { Write-Error "dumpbin /headers failed for $Binary"; exit 2 }
if (-not ($headers | Select-String -Pattern 'machine \(x64\)')) {
    Write-Error "$Binary is not an x64 binary"
    exit 1
}

$dependents = & dumpbin /dependents $Binary
if ($LASTEXITCODE -ne 0) { Write-Error "dumpbin /dependents failed for $Binary"; exit 2 }
$dependents | Write-Output
$runtime = $dependents | Select-String -Pattern '^\s+(MSVCP\S*\.dll|VCRUNTIME\S*\.dll|api-ms-win-crt-\S*\.dll)\s*$'
if ($runtime) {
    Write-Error ("MSVC runtime dependencies: " + (($runtime | ForEach-Object { $_.Matches[0].Groups[1].Value }) -join ', '))
    exit 1
}
Write-Output "x64, no MSVC runtime DLLs: $Binary"
