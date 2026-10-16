// tools/Injector.cpp — LoadLibrary-Injector für OwOEspInject.dll (mit Diagnose).
// Nutzung: OwOEspInjector.exe [prozess.exe] [pfad\zur.dll]

#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>

static DWORD FindPid(const wchar_t* name) {
    DWORD pid = 0;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

// Prüft ob das Modul im Zielprozess angekommen ist (Bestätigung über Snapshot).
static bool ModuleInProcess(DWORD pid, const wchar_t* modBase) {
    bool found = false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) {
        printf("[!] Modulsnapshot fehlgeschlagen (%lu) — evtl. Rechte?\n", GetLastError());
        return false;
    }
    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            if (_wcsicmp(me.szModule, modBase) == 0) { found = true; break; }
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);
    return found;
}

// Echte PE-Prüfung: MZ + e_lfanew + PE-Signatur + Machine.
// (GetBinaryType versagt auf DLLs grundsätzlich — kein Kriterium.)
// Gibt Machine zurück (0x8664=x64) oder 0 bei kaputter Datei.
static DWORD PeMachine(const wchar_t* path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return 0;
    BYTE hdr[0x40] = {};
    DWORD rd = 0;
    DWORD mach = 0;
    if (ReadFile(f, hdr, sizeof(hdr), &rd, nullptr) && rd == sizeof(hdr) &&
        hdr[0] == 'M' && hdr[1] == 'Z') {
        DWORD lfanew = 0;
        memcpy(&lfanew, hdr + 0x3C, 4);
        if (lfanew < 0x100000 &&
            SetFilePointer(f, lfanew, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER) {
            BYTE pe[6] = {};
            if (ReadFile(f, pe, sizeof(pe), &rd, nullptr) && rd == sizeof(pe) &&
                pe[0] == 'P' && pe[1] == 'E' && pe[2] == 0 && pe[3] == 0) {
                mach = pe[4] | ((DWORD)pe[5] << 8);
            }
        }
    }
    CloseHandle(f);
    return mach;
}

static void Pause() {
    printf("[i] Enter zum Schliessen ...\n");
    getchar();
}

int wmain(int argc, wchar_t** argv) {
    // Eject-Befehl: OwOEspInjector.exe unload [prozess.exe]
    if (argc >= 2 && _wcsicmp(argv[1], L"unload") == 0) {
        const wchar_t* procName = (argc >= 3) ? argv[2] : L"cs2.exe";
        HANDLE hEv = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Local\\OwOEspInjectUnload");
        if (!hEv) {
            printf("[!] Kein Eject-Event (err=%lu) — DLL nicht geladen?\n", GetLastError());
            Pause();
            return 1;
        }
        SetEvent(hEv);
        CloseHandle(hEv);
        printf("[*] Eject-Signal gesendet, warte auf Entladung ...\n");
        DWORD pid = FindPid(procName);
        bool gone = (pid == 0);
        for (int i = 0; i < 10 && !gone; i++) {
            Sleep(500);
            if (pid && !ModuleInProcess(pid, L"OwOEspInject.dll"))
                gone = true;
        }
        printf(gone ? "[+] Entladen bestaetigt.\n" : "[?] Modul noch da — END im Spiel oder CS2-Neustart.\n");
        Pause();
        return gone ? 0 : 1;
    }

    const wchar_t* procName = L"cs2.exe";
    wchar_t dllPath[MAX_PATH] = {};

    if (argc >= 3) {
        procName = argv[1];
        wcsncpy_s(dllPath, argv[2], _TRUNCATE);
    } else {
        if (argc >= 2) procName = argv[1];
        GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
        wchar_t* slash = wcsrchr(dllPath, L'\\');
        if (slash) wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - dllPath), L"OwOEspInject.dll");
    }
    printf("[*] Ziel: %ls\n", procName);
    printf("[*] DLL:  %ls\n", dllPath);

    DWORD attr = GetFileAttributesW(dllPath);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        printf("[!] DLL nicht gefunden (%lu).\n", GetLastError());
        printf("    Nutzung: OwOEspInjector.exe [prozess.exe] [pfad\\zur.dll]\n");
        Pause();
        return 1;
    }
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (GetFileAttributesExW(dllPath, GetFileExInfoStandard, &fad)) {
        ULONGLONG sz = ((ULONGLONG)fad.nFileSizeHigh << 32) | fad.nFileSizeLow;
        printf("[*] DLL-Groesse: %llu Bytes\n", sz);
    }
    // Hinweis: OwOEspInject.dll hat KEINE extra DLL-Abhängigkeiten mehr
    // (kein MinHook) — nur System-DLLs. Falls LoadLibrary trotzdem 0 liefert,
    // fehlt evtl. MSVC-Runtime (x64) im Ziel oder AntiCheat blockt.
    DWORD mach = PeMachine(dllPath);
    printf("[*] PE-Machine: 0x%X (0x8664=x64)\n", mach);
    if (mach != 0x8664) {
        printf("[!] DLL ist keine gueltige x64-Datei (kaputter Build?) — neu bauen!\n");
        Pause();
        return 1;
    }

    DWORD pid = FindPid(procName);
    if (!pid) {
        printf("[!] Prozess %ls nicht gefunden. Spiel zuerst starten.\n", procName);
        Pause();
        return 1;
    }
    printf("[+] PID: %u\n", pid);

    HANDLE hProc = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                               PROCESS_VM_OPERATION | PROCESS_VM_WRITE | PROCESS_VM_READ,
                               FALSE, pid);
    if (!hProc) {
        printf("[!] OpenProcess fehlgeschlagen (%lu). Als Admin starten?\n", GetLastError());
        Pause();
        return 1;
    }
    printf("[+] OpenProcess OK\n");

    size_t bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(hProc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        printf("[!] VirtualAllocEx fehlgeschlagen (%lu)\n", GetLastError());
        CloseHandle(hProc);
        Pause();
        return 1;
    }
    if (!WriteProcessMemory(hProc, remote, dllPath, bytes, nullptr)) {
        printf("[!] WriteProcessMemory fehlgeschlagen (%lu)\n", GetLastError());
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        Pause();
        return 1;
    }
    printf("[+] Pfad geschrieben\n");

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    LPTHREAD_START_ROUTINE loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    printf("[*] LoadLibraryW @ 0x%p\n", loadLib);
    HANDLE hThread = CreateRemoteThread(hProc, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!hThread) {
        printf("[!] CreateRemoteThread fehlgeschlagen (%lu). AntiCheat/geschuetzter Prozess?\n",
               GetLastError());
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        CloseHandle(hProc);
        Pause();
        return 1;
    }

    printf("[*] Remote-Thread laeuft ...\n");
    DWORD wait = WaitForSingleObject(hThread, 15000);
    DWORD remoteMod = 0;
    GetExitCodeThread(hThread, &remoteMod);
    CloseHandle(hThread);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);

    if (wait == WAIT_TIMEOUT)
        printf("[!] Timeout — Thread haengt (Loader-Lock? AntiCheat?).\n");
    printf("[*] Remote-LoadLibrary Ergebnis: 0x%X\n", remoteMod);
    if (remoteMod == 0) {
        printf("[!] LoadLibrary im Ziel schlug fehl.\n");
        printf("    - x64-DLL in x64-Prozess? (PE-Machine oben muss 0x8664 sein)\n");
        printf("    - Datei fuer den Zielprozess lesbar? (Pfad ohne Spezialrechte waehlen)\n");
        printf("    - Schutzsoftware blockt Remote-Threads? (Test: CS2 ohne VAC: -insecure)\n");
        CloseHandle(hProc);
        Pause();
        return 1;
    }

    // Sofort-Check: Ist das Modul JETZT (Millisekunden nach LoadLibrary) da?
    // GetModuleHandleW braucht nur den Dateinamen, kein Pfad.
    {
        wchar_t modName[] = L"OwOEspInject.dll";
        size_t nb = (wcslen(modName) + 1) * sizeof(wchar_t);
        LPVOID rName = VirtualAllocEx(hProc, nullptr, nb, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        DWORD hModNow = 0;
        if (rName && WriteProcessMemory(hProc, rName, modName, nb, nullptr)) {
            LPTHREAD_START_ROUTINE getMod =
                (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "GetModuleHandleW");
            HANDLE hT = CreateRemoteThread(hProc, nullptr, 0, getMod, rName, 0, nullptr);
            if (hT) {
                WaitForSingleObject(hT, 10000);
                GetExitCodeThread(hT, &hModNow);
                CloseHandle(hT);
            } else {
                printf("[?] GetModuleHandle-Thread fehlgeschlagen (%lu)\n", GetLastError());
            }
        }
        if (rName) VirtualFreeEx(hProc, rName, 0, MEM_RELEASE);
        printf("[*] GetModuleHandle direkt nach Load: 0x%X (LoadLibrary war 0x%X)\n",
               hModNow, remoteMod);
        if (hModNow == 0)
            printf("[!] Modul SOFORT wieder weg — Schutzsoftware wirft es raus?\n");
        else if (hModNow != remoteMod)
            printf("[?] Anderes Handle als LoadLibrary — zweite Instanz schon drin?\n");
    }

    // Modul per Snapshot verifizieren (mit Retry + Trefferliste)
    const wchar_t* base = wcsrchr(dllPath, L'\\');
    base = base ? base + 1 : dllPath;
    bool modOk = false;
    for (int i = 0; i < 6 && !modOk; i++) {
        if (i > 0) Sleep(500);
        // Exakter Name ...
        if (ModuleInProcess(pid, base)) { modOk = true; break; }
        // ... oder irgendein Modul mit "inject" im Namen (Trefferliste).
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
        if (snap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W me{};
            me.dwSize = sizeof(me);
            int count = 0;
            if (Module32FirstW(snap, &me)) {
                do {
                    count++;
                    wchar_t low[MAX_MODULE_NAME32 + 1] = {};
                    wcsncpy_s(low, me.szModule, _TRUNCATE);
                    _wcslwr_s(low);
                    if (wcsstr(low, L"inject")) {
                        printf("[*] inject-artiges Modul: %ls @ 0x%p\n",
                               me.szModule, me.modBaseAddr);
                        modOk = true;
                    }
                } while (Module32NextW(snap, &me));
            }
            if (i == 0) printf("[*] Module im Ziel: %d sichtbar\n", count);
            CloseHandle(snap);
        }
    }
    if (modOk)
        printf("[+] Modul %ls im Ziel bestaetigt.\n", base);
    else
        printf("[!] Modul %ls NICHT im Ziel (6 Versuche). DLL wurde evtl. wieder entladen!\n", base);

    CloseHandle(hProc);

    char tmp[MAX_PATH] = {};
    GetTempPathA(MAX_PATH, tmp);
    printf("[+] Injiziert! Erwartetes Log: %sOwOEspInject.log\n", tmp);

    // Auf die Logdatei warten: beweist dass DllMain/MainThread im Ziel lief.
    char logPath[MAX_PATH] = {};
    snprintf(logPath, sizeof(logPath), "%sOwOEspInject.log", tmp);
    bool logSeen = false;
    for (int i = 0; i < 20; i++) {
        DWORD a = GetFileAttributesA(logPath);
        if (a != INVALID_FILE_ATTRIBUTES) { logSeen = true; break; }
        Sleep(500);
    }
    if (logSeen)
        printf("[+] Logdatei da — DLL laeuft im Ziel. Inhalt pruefen!\n");
    else
        printf("[!] KEINE Logdatei nach 10s — DllMain/Thread im Ziel lief NICHT.\n"
               "    -> CS2 neugestartet? (alte DLL noch drin?)\n"
               "    -> Anderen Injector versucht? (Manual-Map startet DllMain ggf. nicht)\n");

    printf("[i] Fallback-Log direkt neben der DLL: OwOEspInject.log (falls TEMP blockiert)\n");
    printf("[i] Im Spiel: INSERT = Test-UI an/aus, END = entladen.\n");
    Pause();
    return 0;
}
