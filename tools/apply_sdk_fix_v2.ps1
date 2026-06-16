$ErrorActionPreference = "Stop"
$path = Join-Path (Get-Location) "sdk\PluginSDK.h"
if (!(Test-Path $path)) { throw "Could not find $path" }

$text = Get-Content $path -Raw
$orig = $text

if ($text -notmatch [regex]::Escape('#include <cstddef>')) {
    if ($text -match '#pragma\s+once') {
        $text = [regex]::Replace($text, '(#pragma\s+once\s*)', "`$1`r`n#include <cstddef>`r`n", 1)
    } else {
        $text = $text.Replace('#include "PluginAbi.h"', "#include <cstddef>`r`n#include `"PluginAbi.h`"")
    }
}

$pattern = 'p->m_host_compatible\s*=\s*\(\s*abi\s*!=\s*nullptr\s*&&\s*abi->version\s*==\s*PLUGIN_SDK_VERSION\s*&&\s*abi->size_bytes\s*>=\s*sizeof\s*\(\s*HostAbi\s*\)\s*\)\s*;'
$replacement = @'
const size_t requiredHostSize =
        offsetof(HostAbi, enumerate_monster_mods) +
        sizeof(((HostAbi*)nullptr)->enumerate_monster_mods);

    p->m_host_compatible =
        (abi != nullptr &&
         abi->version == PLUGIN_SDK_VERSION &&
         abi->size_bytes >= requiredHostSize);
'@

$newText = [regex]::Replace($text, $pattern, $replacement, 1)
if ($newText -eq $text) {
    throw "Pattern not found. Please paste the PluginSDK_AttachHost line/block to ChatGPT."
}

$backup = "$path.bak"
if (!(Test-Path $backup)) {
    Set-Content -Path $backup -Value $orig -NoNewline -Encoding UTF8
}
Set-Content -Path $path -Value $newText -NoNewline -Encoding UTF8
Write-Host "OK: patched sdk/PluginSDK.h"
Write-Host "Backup: $backup"
