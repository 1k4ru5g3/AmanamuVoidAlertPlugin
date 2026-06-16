from pathlib import Path
import re

root = Path.cwd()
path = root / 'sdk' / 'PluginSDK.h'
if not path.exists():
    raise SystemExit(f'Could not find {path}')

text = path.read_text(encoding='utf-8', errors='replace')
orig = text

# Make sure offsetof is available.
if '#include <cstddef>' not in text:
    # Insert after #pragma once if possible, otherwise before PluginAbi include.
    text = re.sub(r'(#pragma\s+once\s*)', r'\1\n#include <cstddef>\n', text, count=1)
    if text == orig:
        text = text.replace('#include "PluginAbi.h"', '#include <cstddef>\n#include "PluginAbi.h"', 1)

# Replace only the too-strict host-compatible assignment.
pattern = re.compile(
    r'p->m_host_compatible\s*=\s*\(\s*abi\s*!=\s*nullptr\s*&&\s*abi->version\s*==\s*PLUGIN_SDK_VERSION\s*&&\s*abi->size_bytes\s*>=\s*sizeof\s*\(\s*HostAbi\s*\)\s*\)\s*;',
    re.DOTALL,
)
replacement = '''const size_t requiredHostSize =
        offsetof(HostAbi, enumerate_monster_mods) +
        sizeof(((HostAbi*)nullptr)->enumerate_monster_mods);

    p->m_host_compatible =
        (abi != nullptr &&
         abi->version == PLUGIN_SDK_VERSION &&
         abi->size_bytes >= requiredHostSize);'''

text2, count = pattern.subn(replacement, text, count=1)
if count != 1:
    raise SystemExit('Pattern not found. Please paste the PluginSDK_AttachHost line/block to ChatGPT.')

backup = path.with_suffix('.h.bak')
if not backup.exists():
    backup.write_text(orig, encoding='utf-8')
path.write_text(text2, encoding='utf-8')
print('OK: patched sdk/PluginSDK.h')
print('Backup:', backup)
