#pragma once
// inject/SigScan.h — minimaler Pattern-Scanner (intern, .text-Sektion).
#include <windows.h>
#include <cstdint>
#include <cstring>

inline uintptr_t ScanPattern(HMODULE mod, const uint8_t* pat, const char* mask) {
    if (!mod || !pat || !mask) return 0;
    auto* dos = (IMAGE_DOS_HEADER*)mod;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto* nt = (IMAGE_NT_HEADERS64*)((uint8_t*)mod + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
    size_t n = strlen(mask);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; i++, sec++) {
        if (memcmp(sec->Name, ".text", 5) != 0) continue;
        uint8_t* base = (uint8_t*)mod + sec->VirtualAddress;
        size_t sz = sec->Misc.VirtualSize;
        for (size_t k = 0; k + n <= sz; k++) {
            bool ok = true;
            for (size_t j = 0; j < n; j++) {
                if (mask[j] == 'x' && base[k + j] != pat[j]) { ok = false; break; }
            }
            if (ok) return (uintptr_t)(base + k);
        }
    }
    return 0;
}
