#include "pch.h"
#include "memory.h"
#include <cstring>

#ifdef _WIN32
// ── Windows: Toolhelp + RPM/WPM ──────────────────────────────────────────────
// Toolhelp unter UNICODE-Build: PROCESSENTRY32/MODULEENTRY32 sind dort auf die
// W-Varianten gemappt (A-Suffix-Varianten existieren im SDK nicht) — Namen
// daher explizit nach Wide wandeln und die *W-Funktionen aufrufen.
static bool OwO_ToWide(const char* s, WCHAR* out, size_t cap) {
    if (!s || !out || cap == 0) return false;
    return MultiByteToWideChar(CP_ACP, 0, s, -1, out, (int)cap) > 0;
}

bool Memory::Init(const char* processName) {
    WCHAR want[256]{};
    if (!OwO_ToWide(processName, want, 256))
        return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(PROCESSENTRY32W);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, want) == 0) {
                m_pid = entry.th32ProcessID;
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    if (!found)
        return false;

    m_handle = OpenProcess(PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_QUERY_INFORMATION, FALSE, m_pid);
    return m_handle != nullptr;
}

void Memory::Close() {
    if (m_handle) {
        CloseHandle(m_handle);
        m_handle = nullptr;
    }
    m_pid = 0;
}

uintptr_t Memory::GetModuleBase(const char* moduleName) {
    WCHAR want[256]{};
    if (!OwO_ToWide(moduleName, want, 256))
        return 0;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(MODULEENTRY32W);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    uintptr_t base = 0;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, want) == 0) {
                base = (uintptr_t)entry.modBaseAddr;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return base;
}

uintptr_t Memory::GetModuleSize(const char* moduleName) {
    WCHAR want[256]{};
    if (!OwO_ToWide(moduleName, want, 256))
        return 0;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(MODULEENTRY32W);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    uintptr_t size = 0;
    if (Module32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szModule, want) == 0) {
                size = (uintptr_t)entry.modBaseSize;
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return size;
}

int Memory::ReadMany(const ReadReq* reqs, int count, uint8_t* ok) {
    if (ok && count > 0) memset(ok, 0, (size_t)count);
    if (!reqs || count <= 0 || !m_handle) return 0;
    int good = 0;
    for (int i = 0; i < count; ++i) {
        const ReadReq& r = reqs[i];
        if (!r.buf || r.size == 0) continue;
        if (ReadProcessMemory(m_handle, (LPCVOID)r.addr, r.buf, r.size, nullptr)) {
            ++good;
            if (ok) ok[i] = 1;
        }
    }
    return good;
}

size_t Memory::ReadBufferPartial(uintptr_t addr, void* buf, size_t sz) {
    if (!m_handle || !buf || sz == 0) return 0;
    static const size_t pageSz = [] { SYSTEM_INFO si{}; GetSystemInfo(&si); return (size_t)si.dwPageSize; }();
    uint8_t* dst = (uint8_t*)buf;
    size_t done = 0;
    while (done < sz) {
        uintptr_t cur = addr + done;
        size_t pageLeft = pageSz - (size_t)(cur % pageSz);
        size_t chunk = (sz - done < pageLeft) ? (sz - done) : pageLeft;
        SIZE_T got = 0;
        if (!ReadProcessMemory(m_handle, (LPCVOID)cur, dst + done, chunk, &got) || got == 0)
            break;
        done += (size_t)got;
        if (got < chunk) break;
    }
    return done;
}

#else
// ── Linux: /proc-Scan + process_vm_readv/writev ───────────────────────────────
// Hinweis: ggf. `echo 0 | sudo tee /proc/sys/kernel/yama/ptrace_scope`
// falls das Lesen mit EPERM scheitert (siehe README-LINUX.md).
#include <dirent.h>
#include <sys/uio.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

namespace {
bool ProcNameMatches(uint32_t pid, const char* want) {
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/comm", pid);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char comm[256]{};
    bool ok = (fgets(comm, sizeof(comm), f) != nullptr);
    fclose(f);
    if (!ok) return false;
    comm[strcspn(comm, "\r\n")] = '\0';
    if (strcasecmp(comm, want) == 0) return true;
    // Fallback: argv[0]-Basis aus cmdline (comm ist auf 15 Zeichen gekürzt).
    snprintf(path, sizeof(path), "/proc/%u/cmdline", pid);
    f = fopen(path, "r");
    if (!f) return false;
    char cmd[1024]{};
    size_t n = fread(cmd, 1, sizeof(cmd) - 1, f);
    fclose(f);
    if (n == 0) return false;
    const char* base = strrchr(cmd, '/');
    base = base ? base + 1 : cmd;
    // Ein Eintrag kann "cs2.sh" o.ä. Wrapper sein — exakter Match zählt.
    return strcasecmp(base, want) == 0;
}

// /proc-Scan nach exaktem Prozessnamen (comm/cmdline, siehe ProcNameMatches).
uint32_t FindPidByName(const char* want) {
    if (!want || !*want) return 0;
    DIR* d = opendir("/proc");
    if (!d) return 0;
    uint32_t pid = 0;
    struct dirent* e = nullptr;
    while ((e = readdir(d)) != nullptr) {
        char* end = nullptr;
        unsigned long v = strtoul(e->d_name, &end, 10);
        if (!end || *end != '\0' || v == 0) continue;
        if (ProcNameMatches((uint32_t)v, want)) { pid = (uint32_t)v; break; }
    }
    closedir(d);
    return pid;
}

// Modul-Range aus /proc/<pid>/maps: niedrigste Start-, höchste Endadresse
// aller Mappings desselben Dateipfads (basename-Vergleich, case-insensitiv).
bool ModuleRange(uint32_t pid, const char* module, uintptr_t& outBase, uintptr_t& outEnd) {
    outBase = 0;
    outEnd = 0;
    char path[64];
    snprintf(path, sizeof(path), "/proc/%u/maps", pid);
    FILE* f = fopen(path, "r");
    if (!f) return false;
    char* line = nullptr;
    size_t cap = 0;
    ssize_t len = 0;
    while ((len = getline(&line, &cap, f)) > 0) {
        unsigned long long lo = 0, hi = 0;
        if (sscanf(line, "%llx-%llx", &lo, &hi) != 2) continue;
        const char* p = strchr(line, '/');
        if (!p) continue; // anonymes Mapping, kein Modul
        // Pfad bis Zeilenende/Leerzeichen/" (deleted)"
        char mod[512]{};
        size_t i = 0;
        while (*p && *p != ' ' && *p != '\n' && *p != '(' && i + 1 < sizeof(mod))
            mod[i++] = *p++;
        mod[i] = '\0';
        const char* base = strrchr(mod, '/');
        base = base ? base + 1 : mod;
        if (strcasecmp(base, module) != 0) continue;
        if (outBase == 0 || lo < outBase) outBase = (uintptr_t)lo;
        if (hi > outEnd) outEnd = (uintptr_t)hi;
    }
    free(line);
    fclose(f);
    return outBase != 0 && outEnd > outBase;
}
} // namespace

bool Memory::Init(const char* processName) {
    // Reihenfolge: erst nativ (cs2), dann Proton (cs2.exe) — pro Aufruf neu,
    // passt damit für beide Startarten, egal welche der User benutzt.
    uint32_t pid = FindPidByName(processName);
    bool viaAlt = false;
    if (pid == 0 && processName && strcasecmp(processName, OWO_PROC_NAME_ALT) != 0) {
        pid = FindPidByName(OWO_PROC_NAME_ALT);
        viaAlt = pid != 0;
    }
    if (pid == 0) return false;
    if (viaAlt)
        printf("[mem] Prozess via Fallback gefunden: %s (Proton)\n", OWO_PROC_NAME_ALT);
    m_pid = (uint32_t)pid;
    m_handle = (void*)(uintptr_t)m_pid;
    return true;
}

void Memory::Close() {
    m_handle = nullptr;
    m_pid = 0;
}

uintptr_t Memory::GetModuleBase(const char* moduleName) {
    uintptr_t base = 0, end = 0;
    if (ModuleRange(m_pid, moduleName, base, end))
        return base;
    // Fallback: nativ client.so ↔ Proton client.dll (klassische .dll im maps).
    if (moduleName && strcasecmp(moduleName, OWO_CLIENT_MODULE_ALT) != 0 &&
        ModuleRange(m_pid, OWO_CLIENT_MODULE_ALT, base, end)) {
        printf("[mem] Modul via Fallback gefunden: %s (Proton)\n", OWO_CLIENT_MODULE_ALT);
        return base;
    }
    return 0;
}

uintptr_t Memory::GetModuleSize(const char* moduleName) {
    uintptr_t base = 0, end = 0;
    if (ModuleRange(m_pid, moduleName, base, end)) return end - base;
    if (moduleName && strcasecmp(moduleName, OWO_CLIENT_MODULE_ALT) != 0 &&
        ModuleRange(m_pid, OWO_CLIENT_MODULE_ALT, base, end))
        return end - base;
    return 0;
}

bool Memory::ReadBuffer(uintptr_t addr, void* buf, size_t sz) {
    if (!m_pid || !buf || sz == 0) return false;
    uint8_t* dst = (uint8_t*)buf;
    size_t done = 0;
    while (done < sz) {
        struct iovec local{ dst + done, sz - done };
        struct iovec remote{ (void*)(addr + done), sz - done };
        ssize_t n = process_vm_readv((pid_t)m_pid, &local, 1, &remote, 1, 0);
        if (n <= 0) {
            if (done == 0)
                printf("[mem] readv fail @0x%llX sz=%zu errno=%d (ptrace_scope?)\n",
                       (unsigned long long)addr, sz, errno);
            return false;
        }
        done += (size_t)n;
    }
    return true;
}

bool Memory::WriteBuffer(uintptr_t addr, const void* buf, size_t sz) {
    if (!m_pid || !buf || sz == 0) return false;
    const uint8_t* src = (const uint8_t*)buf;
    size_t done = 0;
    while (done < sz) {
        struct iovec local{ (void*)(src + done), sz - done };
        struct iovec remote{ (void*)(addr + done), sz - done };
        ssize_t n = process_vm_writev((pid_t)m_pid, &local, 1, &remote, 1, 0);
        if (n <= 0) return false;
        done += (size_t)n;
    }
    return true;
}

// ── Batch-Reads: EIN process_vm_readv pro Slice (IOV_MAX = 1024) ─────────────
// process_vm_readv liefert bei unmapped Regionen einen Teil-Erfolg: alle iovecs
// vor der Fehlerstelle sind gelesen. Die Abbruchstelle wird aus der Byte-Anzahl
// abgeleitet; der Rest wird ab dort erneut (ggf. einzeln) nachgefasst.
int Memory::ReadMany(const ReadReq* reqs, int count, uint8_t* ok) {
    if (ok && count > 0) memset(ok, 0, (size_t)count);
    if (!m_pid || !reqs || count <= 0) return 0;
    constexpr int kSlice = 512; // ½·IOV_MAX — Reserve für andere Nutzer im Prozess
    int good = 0;
    int base = 0;
    while (base < count) {
        int n = count - base;
        if (n > kSlice) n = kSlice;

        struct iovec liov[kSlice], riov[kSlice];
        size_t total = 0;
        for (int j = 0; j < n; ++j) {
            const ReadReq& r = reqs[base + j];
            liov[j].iov_base = r.buf;
            liov[j].iov_len  = r.size;
            riov[j].iov_base = (void*)(uintptr_t)r.addr;
            riov[j].iov_len  = r.size;
            total += r.size;
        }

        ssize_t got = process_vm_readv((pid_t)m_pid, liov, (unsigned long)n, riov, (unsigned long)n, 0);
        if (got < 0) got = 0;

        if ((size_t)got >= total) {
            for (int j = 0; j < n; ++j) {
                if (ok) ok[base + j] = 1;
                ++good;
            }
            base += n;
            continue;
        }

        size_t consumed = 0;
        int failedAt = -1;
        for (int j = 0; j < n; ++j) {
            const ReadReq& r = reqs[base + j];
            if (consumed + r.size <= (size_t)got) {
                consumed += r.size;
                if (ok) ok[base + j] = 1;
                ++good;
                continue;
            }
            failedAt = j;
            // Fehlenden Teil einzeln nachfassen (erwartbar unmapped → scheitert;
            // nur bei EINTR/Signal o.ä. kann das noch vollständig gelingen).
            size_t have = (size_t)got > consumed ? (size_t)got - consumed : 0;
            if (have < r.size) {
                struct iovec l1{ (uint8_t*)r.buf + have, r.size - have };
                struct iovec r1{ (void*)(uintptr_t)(r.addr + have), r.size - have };
                ssize_t n2 = process_vm_readv((pid_t)m_pid, &l1, 1, &r1, 1, 0);
                if (n2 > 0 && (size_t)n2 == r.size - have) {
                    if (ok) ok[base + j] = 1;
                    ++good;
                }
            }
            break;
        }
        if (failedAt < 0) failedAt = n - 1; // Schutz (sollte nicht vorkommen)
        base += failedAt + 1;
    }
    return good;
}

// Tolerant: EIN process_vm_readv liefert die tatsächlich lesbare Teillänge
// einer zusammenhängenden Region (z.B. SigScan-Image mit unmapped Lücken).
size_t Memory::ReadBufferPartial(uintptr_t addr, void* buf, size_t sz) {
    if (!m_pid || !buf || sz == 0) return 0;
    struct iovec local{ buf, sz };
    struct iovec remote{ (void*)(uintptr_t)addr, sz };
    ssize_t n = process_vm_readv((pid_t)m_pid, &local, 1, &remote, 1, 0);
    return n > 0 ? (size_t)n : 0;
}
#endif
