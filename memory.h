#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// memory.h — Prozess-Memory (Windows RPM/WPM + Toolhelp / Linux vm_readv + proc)
// API bewusst schmal (char*): Aufrufer bleiben plattformfrei.
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <cstdint>
#ifdef _WIN32
#include <windows.h>
#include <TlHelp32.h>
#endif

// Eine Batch-Leseanfrage (Reader-Fastpath): addr → buf, size Bytes.
struct ReadReq {
    uintptr_t addr;
    void*     buf;
    size_t    size;
};

class Memory {
public:
    Memory()
#ifdef _WIN32
        : m_pid(0), m_handle(nullptr) {}
#else
        : m_pid(0), m_handle(nullptr) {}
#endif
    ~Memory() { Close(); }

    bool Init(const char* processName);
    void Close();

    uintptr_t GetModuleBase(const char* moduleName);

    // Image-Größe des Moduls (für Signatur-Scans). 0 = nicht gefunden.
    uintptr_t GetModuleSize(const char* moduleName);

#ifdef _WIN32
    template<typename T>
    T Read(uintptr_t addr) {
        T val{};
        ReadProcessMemory(m_handle, (LPCVOID)addr, &val, sizeof(T), nullptr);
        return val;
    }

    template<typename T>
    bool Read(uintptr_t addr, T& val) {
        return ReadProcessMemory(m_handle, (LPCVOID)addr, &val, sizeof(T), nullptr) != FALSE;
    }

    bool ReadBuffer(uintptr_t addr, void* buf, size_t sz) {
        return ReadProcessMemory(m_handle, (LPCVOID)addr, buf, sz, nullptr) != FALSE;
    }

    template<typename T>
    bool Write(uintptr_t addr, const T& val) {
        return WriteProcessMemory(m_handle, (LPVOID)addr, &val, sizeof(T), nullptr) != FALSE;
    }

    bool WriteBuffer(uintptr_t addr, const void* buf, size_t sz) {
        return WriteProcessMemory(m_handle, (LPVOID)addr, buf, sz, nullptr) != FALSE;
    }
#else
    template<typename T>
    T Read(uintptr_t addr) {
        T val{};
        ReadBuffer(addr, &val, sizeof(T));
        return val;
    }

    template<typename T>
    bool Read(uintptr_t addr, T& val) {
        return ReadBuffer(addr, &val, sizeof(T));
    }

    bool ReadBuffer(uintptr_t addr, void* buf, size_t sz);
    template<typename T>
    bool Write(uintptr_t addr, const T& val) {
        return WriteBuffer(addr, &val, sizeof(T));
    }
    bool WriteBuffer(uintptr_t addr, const void* buf, size_t sz);
#endif

    // ── Batch-Reads (Performance-Pfad des Readers) ─────────────────────────────
    // Führt count Reads aus. Linux: ein process_vm_readv pro Slice (½·IOV_MAX
    // iovecs); Windows: RPM-Schleife. ok[i]=1, wenn reqs[i] vollständig gelesen
    // wurde (ok darf nullptr sein). Rückgabe: Anzahl vollständig gelesener Einträge.
    int ReadMany(const ReadReq* reqs, int count, uint8_t* ok = nullptr);

    // Toleranter Read: liest zusammenhängend ab addr so viele Bytes wie möglich
    // (Linux: process_vm_readv-Teillänge; Windows: seitenweise RPM).
    // Rückgabe: Anzahl tatsächlich gelesener Bytes (0 = nichts/Fehler).
    size_t ReadBufferPartial(uintptr_t addr, void* buf, size_t sz);

    bool IsValid() {
#ifdef _WIN32
        return m_handle != nullptr;
#else
        return m_pid != 0;
#endif
    }
    uint32_t Pid() { return m_pid; }

private:
#ifdef _WIN32
    DWORD m_pid;
    HANDLE m_handle;
#else
    uint32_t m_pid;
    void*    m_handle; // ungenutzt (nur IsValid-Semantik), pid trägt alles
#endif
};
