// inject/ModelChams.cpp — DrawObject-Hook + XQZ-Zweipass + D3D-States.
//
// DrawObject-Sig (scenesystem.dll, verifiziert: 1 Treffer auf Build 09.10.):
//   48 8B C4 48 89 50 10 55 41 56
// Prolog (14 B, positionsunabhängig — kein RIP-relativer Code):
//   48 8B C4 | 48 89 50 10 | 55 | 41 56 | 48 83 EC 58
// Inline-Patch: FF 25 00 00 00 00 + Zieladresse (14 B), Trampolin mit
// kopierten 14 B + Rücksprung. Kein MinHook nötig.
//
// data-Layout (UC "Simple Chams system"): m_material @+20, m_color (4 B) @+64,
// m_objectInfo @+68. Nur m_material/m_color werden angefasst.

#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "ModelChams.h"
#include "SigScan.h"
#include "Chams.h" // Farben/Modi/Konfig (g_chams)

#pragma comment(lib, "d3d11.lib")

extern void Log(const char* fmt, ...); // DllMain.cpp

ModelChamsStatus g_mchams;
volatile int g_hookLocalTeam = -1;

namespace {
using DrawObjectFn = void(__fastcall*)(void*, void*, void*, int, void*, void*, void*, void*);
DrawObjectFn g_oDraw = nullptr;
uint8_t g_origBytes[14] = {};
void* g_target = nullptr;
void* g_tramp = nullptr;

// D3D-States auf dem Geräte-Kontext des Spiels (je Device gecacht).
ID3D11Device* g_stateDev = nullptr;
ID3D11DepthStencilState* g_dsOff = nullptr;
ID3D11BlendState* g_bsAlpha = nullptr;
// XQZ: 1x1-Weißtextur + Sampler (flache Modelle durch Wände).
ID3D11ShaderResourceView* g_xqzSRV = nullptr;
ID3D11SamplerState* g_xqzSamp = nullptr;

bool EnsureStates(ID3D11DeviceContext* ctx) {
    ID3D11Device* dev = nullptr;
    ctx->GetDevice(&dev);
    if (!dev) return false;
    if (dev != g_stateDev) {
        if (g_dsOff) { g_dsOff->Release(); g_dsOff = nullptr; }
        if (g_bsAlpha) { g_bsAlpha->Release(); g_bsAlpha = nullptr; }
        if (g_xqzSRV) { g_xqzSRV->Release(); g_xqzSRV = nullptr; }
        if (g_xqzSamp) { g_xqzSamp->Release(); g_xqzSamp = nullptr; }
        if (g_stateDev) g_stateDev->Release();
        g_stateDev = dev; // Ref von GetDevice übernommen
        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable = FALSE;
        dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        dd.StencilEnable = FALSE;
        if (FAILED(dev->CreateDepthStencilState(&dd, &g_dsOff))) {
            Log("ModelChams: DepthState create FAILED");
            return false;
        }
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].BlendEnable = TRUE;
        bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
        bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
        bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        if (FAILED(dev->CreateBlendState(&bd, &g_bsAlpha))) {
            Log("ModelChams: BlendState create FAILED");
            return false;
        }
        Log("ModelChams: D3D-States erzeugt (dev=0x%p)", dev);
    } else {
        dev->Release();
    }
    // XQZ-Textur/Sampler bei Bedarf (einmalig je Device).
    if (!g_xqzSRV || !g_xqzSamp) {
        ID3D11Device* d2 = nullptr;
        ctx->GetDevice(&d2);
        if (d2) {
            if (!g_xqzSRV) {
                uint32_t white = 0xFFFFFFFFu;
                D3D11_TEXTURE2D_DESC td{};
                td.Width = td.Height = 1;
                td.MipLevels = td.ArraySize = 1;
                td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                td.SampleDesc.Count = 1;
                td.Usage = D3D11_USAGE_IMMUTABLE;
                td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
                D3D11_SUBRESOURCE_DATA init{ &white, 4, 4 };
                ID3D11Texture2D* tex = nullptr;
                if (SUCCEEDED(d2->CreateTexture2D(&td, &init, &tex)) && tex) {
                    d2->CreateShaderResourceView(tex, nullptr, &g_xqzSRV);
                    tex->Release();
                }
            }
            if (!g_xqzSamp) {
                D3D11_SAMPLER_DESC sd{};
                sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
                sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
                sd.MaxAnisotropy = 1;
                sd.MaxLOD = D3D11_FLOAT32_MAX;
                d2->CreateSamplerState(&sd, &g_xqzSamp);
            }
            d2->Release();
            Log("ModelChams: XQZ-Textur %s Sampler %s",
                g_xqzSRV ? "OK" : "FEHLT", g_xqzSamp ? "OK" : "FEHLT");
        }
    }
    return g_dsOff && g_bsAlpha;
}

void HsvToRgbHook(float h, float& r, float& g, float& b) {
    h = fmodf(h, 360.f);
    if (h < 0.f) h += 360.f;
    float c = 1.f;
    float x = c * (1.f - fabsf(fmodf(h / 60.f, 2.f) - 1.f));
    float rr = 0.f, gg = 0.f, bb = 0.f;
    if (h < 60.f) { rr = c; gg = x; }
    else if (h < 120.f) { rr = x; gg = c; }
    else if (h < 180.f) { gg = c; bb = x; }
    else if (h < 240.f) { gg = x; bb = c; }
    else if (h < 300.f) { rr = x; bb = c; }
    else { rr = c; bb = x; }
    r = rr; g = gg; b = bb;
}

// Modell-Team aus Materialnamen: "ctm_" -> 3, sonst "tm_" -> 2, sonst 0.
int TeamFromMaterial(const char* name) {
    if (strstr(name, "ctm_")) return 3;
    if (strstr(name, "tm_")) return 2;
    return 0;
}

// Diagnose: unbekannte Spielermodelle loggen (einmalig, max 30).
void LogModelName(const char* name) {
    static int s_n = 0;
    if (s_n >= 30) return;
    s_n++;
    g_mchams.uniqueNames = s_n;
    Log("ModelChams: Modell #%d '%s'", s_n, name);
}

void __fastcall HkDraw(void* anim, void* dx11, void* data, int cnt,
                       void* view, void* layer, void* u1, void* u2) {
    InterlockedIncrement(&g_mchams.drawsSeen);
    __try {
        // Master aus? → passthrough (kein Effekt, kein Risiko).
        if (!g_chams.glowEnable || !data || !dx11) {
            g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);
            return;
        }
        void* mat = *(void**)((uint8_t*)data + 20);
        if (!mat) {
            g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);
            return;
        }
        // Materialname via vtable[0] (CMaterial2::get_name).
        using GetNameFn = const char*(__fastcall*)(void*);
        GetNameFn getName = *(GetNameFn**)mat ? (*(GetNameFn**)mat)[0] : nullptr;
        if (!getName) {
            g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);
            return;
        }
        const char* name = getName(mat);
        if (!name || strncmp(name, "characters/models", 17) != 0) {
            g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);
            return;
        }
        int modelTeam = TeamFromMaterial(name);
        if (modelTeam == 0) {
            // Kein Spieler-Modell (Arme, Waffen, Chicken...) — aber loggen.
            LogModelName(name);
            g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);
            return;
        }
        int localTeam = g_hookLocalTeam;
        if (localTeam < 2) { // eigener Spieler unbekannt → nichts tun
            g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);
            return;
        }
        bool isMate = (modelTeam == localTeam);
        if (isMate && !g_chams.glowTeammates) {
            g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);
            return;
        }
        if (!isMate && !g_chams.glowEnemies) {
            g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);
            return;
        }

        auto mode = (ChamsMaterialMode)g_chams.material;
        const float* base = isMate ? g_chams.teamColor : g_chams.enemyColor;
        float tSec = (float)GetTickCount() / 1000.f;
        float cr = base[0], cg = base[1], cb = base[2];
        float alpha = 1.f;
        bool visWhite = false; // sichtbarer Pass neutral (Feld-Tint wirkt)
        switch (mode) {
        case ChamsMaterialMode::Glass:
            alpha = (float)(g_chams.glassAlpha < 0 ? 0
                            : g_chams.glassAlpha > 255 ? 255
                            : g_chams.glassAlpha) / 255.f;
            break;
        case ChamsMaterialMode::Rainbow: {
            float phase = tSec * 90.f + (float)(((uintptr_t)data >> 4) & 255);
            HsvToRgbHook(phase, cr, cg, cb);
            break;
        }
        case ChamsMaterialMode::Pulse: {
            float s = sinf(tSec * 4.f);
            if (s < 0.f) s = -s;
            alpha = 0.35f + 0.65f * s;
            break;
        }
        case ChamsMaterialMode::Ghost:
            alpha = 40.f / 255.f;
            break;
        case ChamsMaterialMode::Health:
            // HP-Farbe kommt aus m_clrRender (Chams.cpp) — Hook malt nur XQZ
            // in Teamfarbe, sichtbarer Pass bleibt neutral.
            visWhite = true;
            break;
        case ChamsMaterialMode::Flat:
        default:
            break;
        }

        auto pack = [](float r, float g, float b, float a) -> uint32_t {
            auto cl = [](float v) -> uint32_t {
                if (v < 0.f) v = 0.f;
                if (v > 1.f) v = 1.f;
                return (uint32_t)(v * 255.f);
            };
            return (cl(a) << 24) | (cl(r) << 16) | (cl(g) << 8) | cl(b);
        };

        auto* ctx = (ID3D11DeviceContext*)dx11;
        if (!EnsureStates(ctx)) {
            g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);
            return;
        }
        uint8_t* colPtr = (uint8_t*)data + 64;
        uint32_t origCol = *(uint32_t*)colPtr;
        ID3D11DepthStencilState* oldDS = nullptr;
        UINT stencilRef = 0;
        ID3D11BlendState* oldBS = nullptr;
        FLOAT blendF[4] = {};
        UINT sampleMask = 0;
        ctx->OMGetDepthStencilState(&oldDS, &stencilRef);
        ctx->OMGetBlendState(&oldBS, blendF, &sampleMask);

        uint32_t visCol = visWhite ? 0xFFFFFFFFu : pack(cr, cg, cb, alpha);
        uint32_t xqzCol = pack(cr, cg, cb, 1.f); // durch Wand: immer deckend

        // Pass 1: XQZ (Depth aus).
        ctx->OMSetDepthStencilState(g_dsOff, 0);
        ctx->OMSetBlendState(g_bsAlpha, blendF, 0xFFFFFFFF);
        *(uint32_t*)colPtr = xqzCol;
        g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);
        // Pass 2: normal (Depth an).
        ctx->OMSetDepthStencilState(oldDS, stencilRef);
        *(uint32_t*)colPtr = visCol;
        g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);

        // Restore.
        *(uint32_t*)colPtr = origCol;
        ctx->OMSetDepthStencilState(oldDS, stencilRef);
        ctx->OMSetBlendState(oldBS, blendF, sampleMask);
        if (oldDS) oldDS->Release();
        if (oldBS) oldBS->Release();
        InterlockedIncrement(&g_mchams.playersColored);
        return;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Im Zweifel: original zeichnen (Spiel lebt weiter).
        __try {
            g_oDraw(anim, dx11, data, cnt, view, layer, u1, u2);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return;
    }
}

bool WriteJump(void* at, void* detour) {
    uint8_t buf[14];
    buf[0] = 0xFF; buf[1] = 0x25;
    buf[2] = buf[3] = buf[4] = buf[5] = 0;
    *(void**)(buf + 6) = detour;
    DWORD old = 0;
    if (!VirtualProtect(at, 14, PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(at, buf, 14);
    DWORD tmp = 0;
    VirtualProtect(at, 14, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), at, 14);
    return true;
}
} // namespace

bool InitModelChams() {
    if (g_mchams.hooked) return true;
    HMODULE scene = GetModuleHandleW(L"scenesystem.dll");
    if (!scene) {
        Log("ModelChams: scenesystem.dll nicht geladen");
        return false;
    }
    static const uint8_t kPat[] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x50, 0x10, 0x55, 0x41, 0x56,
        0x48, 0x83, 0xEC, 0x58, 0x4C, 0x8B, 0x32, 0x33, 0xED, 0x4D,
        0x85, 0xF6, 0x0F, 0x84, 0x70, 0x01, 0x00, 0x00
    };
    uintptr_t fn = ScanPattern(scene, kPat, "xxxxxxxxxxxxxxxxxxxxxxxxxxxx");
    if (!fn) {
        Log("ModelChams: DrawObject-Sig(28B) NICHT gefunden (Update?)");
        return false;
    }
    Log("ModelChams: DrawObject @ 0x%p (RVA=0x%llX, erwartet 0x4BCF70)",
        (void*)fn, (unsigned long long)(fn - (uintptr_t)scene));
    g_tramp = VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!g_tramp) {
        Log("ModelChams: Trampolin-Alloc FAILED");
        return false;
    }
    memcpy(g_origBytes, (void*)fn, 14);
    memcpy(g_tramp, (void*)fn, 14);
    // Rücksprung: jmp [rip+0] -> fn+14
    uint8_t* tb = (uint8_t*)g_tramp + 14;
    tb[0] = 0xFF; tb[1] = 0x25;
    tb[2] = tb[3] = tb[4] = tb[5] = 0;
    *(void**)(tb + 6) = (void*)(fn + 14);
    FlushInstructionCache(GetCurrentProcess(), g_tramp, 28);

    g_target = (void*)fn;
    if (!WriteJump(g_target, (void*)&HkDraw)) {
        Log("ModelChams: Patch FAILED err=%lu", GetLastError());
        VirtualFree(g_tramp, 0, MEM_RELEASE);
        g_tramp = nullptr;
        return false;
    }
    // Readback: steht der Patch wirklich?
    uint8_t rb[14] = {};
    memcpy(rb, g_target, 14);
    bool ok = (rb[0] == 0xFF && rb[1] == 0x25 && *(void**)(rb + 6) == (void*)&HkDraw);
    Log("ModelChams: Patch-Readback %s", ok ? "OK" : "ABWEICHEND!");
    if (!ok) {
        memcpy(g_target, g_origBytes, 14);
        VirtualFree(g_tramp, 0, MEM_RELEASE);
        g_tramp = nullptr;
        return false;
    }
    g_oDraw = (DrawObjectFn)g_tramp;
    g_mchams.hooked = true;
    g_mchams.hookAddr = fn;
    Log("ModelChams: Hook AKTIV");
    return true;
}

// ── DrawIndexed-Recon (Fallback-Pfad): Stride-Häufigkeiten loggen ──────────
// Reine Analyse (kein Effekt): findet Spieler-Strides ohne Signaturen.
// Technik: PRIVATE VTable-Kopie pro Context-Objekt (vtable-swap) statt
// globalem Slot-Patch — immun gegen Slot-Reverts Dritter (Overlay etc.).
// DrawIndexed = Index 12 (ID3D11DeviceContext).
namespace {
using DrawIndexedFn = void(__stdcall*)(ID3D11DeviceContext*, UINT, UINT, INT);
DrawIndexedFn g_oDrawIndexed = nullptr;
void* g_diObj = nullptr;          // Game-Context-Objekt (lpVtbl geswappt)
void** g_diMyTable = nullptr;     // private Kopie (128 Einträge)
void** g_diGlobalTable = nullptr; // Original-Tabelle (für Restore)
ID3D11Device* g_diDev = nullptr;  // Gerät dahinter (Wechsel erkennen)

struct Freq { UINT stride = 0; long n = 0; long long idxSum = 0; };
Freq g_freq[96];
int g_freqUsed = 0;
DWORD g_freqLastLog = 0;
// Index-Histogramm nur für den XQZ-Stride (Shafts vs. Spieler trennen).
struct Ihist { UINT idx = 0; long n = 0; };
Ihist g_ihist[32];
int g_ihistUsed = 0;

void __stdcall HkDrawIndexed(ID3D11DeviceContext* ctx, UINT idxCount, UINT start, INT baseV) {
    InterlockedIncrement(&g_mchams.diDraws);
    __try {
        ID3D11Buffer* vb = nullptr;
        UINT stride = 0, off = 0;
        ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &off);
        if (vb) vb->Release();
        if (stride < 512) {
            int slot = -1;
            for (int i = 0; i < g_freqUsed; i++) {
                if (g_freq[i].stride == stride) { slot = i; break; }
            }
            if (slot < 0 && g_freqUsed < 96) {
                slot = g_freqUsed++;
                g_freq[slot].stride = stride;
            }
            if (slot >= 0) {
                g_freq[slot].n++;
                g_freq[slot].idxSum += idxCount;
            }
            // Index-Histogramm für XQZ-Stride (jeder Call; Shafts vs Spieler).
            // Bei Stride-Wechsel zurücksetzen (alte Werte wären falsch).
            {
                static int s_lastStride = -1;
                if (s_lastStride != g_chams.xqzStride) {
                    s_lastStride = g_chams.xqzStride;
                    g_ihistUsed = 0;
                }
            }
            if ((int)stride == g_chams.xqzStride) {
                int hs = -1;
                for (int i = 0; i < g_ihistUsed; i++) {
                    if (g_ihist[i].idx == idxCount) { hs = i; break; }
                }
                if (hs < 0 && g_ihistUsed < 32) {
                    hs = g_ihistUsed++;
                    g_ihist[hs].idx = idxCount;
                }
                if (hs >= 0) g_ihist[hs].n++;
            }
        }
        DWORD now = GetTickCount();
        if (now - g_freqLastLog > 10000) {
            g_freqLastLog = now;
            // Top 12 nach Häufigkeit (Bubble, winzig).
            for (int a = 0; a < g_freqUsed; a++)
                for (int b = a + 1; b < g_freqUsed; b++)
                    if (g_freq[b].n > g_freq[a].n) {
                        Freq t = g_freq[a]; g_freq[a] = g_freq[b]; g_freq[b] = t;
                    }
            char line[512] = {};
            int pos = snprintf(line, sizeof(line), "DI-Stride Top (n=%d):", g_freqUsed);
            for (int i = 0; i < g_freqUsed && i < 12 && pos < 400; i++) {
                long avg = g_freq[i].n ? (long)(g_freq[i].idxSum / g_freq[i].n) : 0;
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos,
                                " [s=%u c=%ld ai=%ld]", g_freq[i].stride,
                                g_freq[i].n, avg);
            }
            Log("%s", line);
            // Index-Histogramm (Top 8 nach Häufigkeit).
            for (int a = 0; a < g_ihistUsed; a++)
                for (int b = a + 1; b < g_ihistUsed; b++)
                    if (g_ihist[b].n > g_ihist[a].n) {
                        Ihist t = g_ihist[a]; g_ihist[a] = g_ihist[b]; g_ihist[b] = t;
                    }
            char hline[384] = {};
            int hpos = snprintf(hline, sizeof(hline), "XQZ-Hist (s=%d,n=%d):",
                                g_chams.xqzStride, g_ihistUsed);
            for (int i = 0; i < g_ihistUsed && i < 8 && hpos < 300; i++)
                hpos += snprintf(hline + hpos, sizeof(hline) - (size_t)hpos,
                                 " [idx=%u c=%ld]", g_ihist[i].idx, g_ihist[i].n);
            Log("%s", hline);
        }
        // ── XQZ-Pass: Spieler-Stride depth-off + Weißtextur (durch Wände) ──
        // Team kommt aus der Feld-Tönung (sichtbarer Pass folgt danach).
        // Index-Range grenzt Shafts/Deco aus (s. XQZ-Hist).
        if (g_chams.glowEnable && g_chams.xqzEnable && (int)stride == g_chams.xqzStride &&
            (int)idxCount >= g_chams.xqzIdxMin && (int)idxCount <= g_chams.xqzIdxMax) {
            if (EnsureStates(ctx) && g_xqzSRV && g_xqzSamp) {
                ID3D11DepthStencilState* oldDS = nullptr;
                UINT sr = 0;
                ID3D11ShaderResourceView* oldSRV = nullptr;
                ID3D11SamplerState* oldSamp = nullptr;
                ctx->OMGetDepthStencilState(&oldDS, &sr);
                ctx->PSGetShaderResources(0, 1, &oldSRV);
                ctx->PSGetSamplers(0, 1, &oldSamp);
                ctx->OMSetDepthStencilState(g_dsOff, 0);
                ctx->PSSetShaderResources(0, 1, &g_xqzSRV);
                ctx->PSSetSamplers(0, 1, &g_xqzSamp);
                g_oDrawIndexed(ctx, idxCount, start, baseV);
                ctx->OMSetDepthStencilState(oldDS, sr);
                ctx->PSSetShaderResources(0, 1, &oldSRV);
                ctx->PSSetSamplers(0, 1, &oldSamp);
                if (oldDS) oldDS->Release();
                if (oldSRV) oldSRV->Release();
                if (oldSamp) oldSamp->Release();
                InterlockedIncrement(&g_mchams.xqzDraws);
            }
        }
        g_oDrawIndexed(ctx, idxCount, start, baseV);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Orig überspringen ist sicherer als Crash (ein Dreieck fehlt).
    }
}
} // namespace

namespace { void EnsureDeviceHook(ID3D11Device* dev); }

void EnsureDiHook(IDXGISwapChain* sc) {
    if (!sc) return;    // Gerät holen (Wechsel = Re-Install).
    ID3D11Device* dev = nullptr;
    if (FAILED(sc->GetDevice(IID_PPV_ARGS(&dev))) || !dev) return;
    ID3D11DeviceContext* ctx = nullptr;
    dev->GetImmediateContext(&ctx);
    if (!ctx) { dev->Release(); return; }

    if (g_diObj && g_diDev == dev && g_diMyTable) {
        // Swap noch drin? (Objekt-vtbl zeigt auf unsere Kopie?)
        void* cur = *(void* volatile*)g_diObj;
        if (cur != (void*)g_diMyTable) {
            // Objekt restauriert/neu — Swap erneut setzen.
            DWORD old = 0;
            if (VirtualProtect(g_diObj, sizeof(void*), PAGE_READWRITE, &old)) {
                *(void**)g_diObj = (void*)g_diMyTable;
                DWORD tmp = 0;
                VirtualProtect(g_diObj, sizeof(void*), old, &tmp);
                Log("DI-Swap: Objekt-vtbl erneut gesetzt");
            }
        }
        ctx->Release();
        dev->Release();
        return;
    }

    // Neu installieren (altes aufräumen).
    if (g_diMyTable) { VirtualFree(g_diMyTable, 0, MEM_RELEASE); g_diMyTable = nullptr; }
    if (g_diObj) { ((IUnknown*)g_diObj)->Release(); g_diObj = nullptr; }
    if (g_diDev) { g_diDev->Release(); g_diDev = nullptr; }

    __try {
        void** global = *(void***)ctx;
        void** mine = (void**)VirtualAlloc(nullptr, 128 * sizeof(void*),
                                           MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!mine) {
            Log("DI-Swap: Alloc FAILED");
            ctx->Release(); dev->Release();
            return;
        }
        for (int i = 0; i < 128; i++) mine[i] = global[i]; // SEH-geschützt
        void* orig = mine[12];
        mine[12] = (void*)&HkDrawIndexed;
        DWORD old = 0;
        if (!VirtualProtect(ctx, sizeof(void*), PAGE_READWRITE, &old)) {
            Log("DI-Swap: Protect FAILED err=%lu", GetLastError());
            VirtualFree(mine, 0, MEM_RELEASE);
            ctx->Release(); dev->Release();
            return;
        }
        *(void**)ctx = (void*)mine;
        DWORD tmp = 0;
        VirtualProtect(ctx, sizeof(void*), old, &tmp);
        g_diObj = (void*)ctx; // Ref behalten (Release beim Unload)
        g_diMyTable = mine;
        g_diGlobalTable = global;
        g_diDev = dev; // Ref behalten
        g_oDrawIndexed = (DrawIndexedFn)orig;
        g_mchams.diHooked = true;
        Log("DI-Swap: aktiv (ctx=0x%p dev=0x%p orig=0x%p)", ctx, dev, orig);
        EnsureDeviceHook(dev); // Deferred-Ctx-Hook (Spieler-Renderpfad?)
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("DI-Swap: Setup-Exception");
        ctx->Release();
        dev->Release();
    }
}

// ── Watchdog: Patches verifizieren + bei Abweichung re-patchen ─────────────
// Erkennt, ob Dritte (Overlay, Integrity-Check) unsere Haken entfernen.
// Zählt Reverts (Diagnose im UI), loggt max. alle 5s.
void VerifyModelHooks() {
    static int s_diState = -1;   // -1=unbekannt, 0=aus, 1=intakt, 2=repariert
    static int s_drawState = -1;
    static DWORD s_lastLog = 0;
    DWORD now = GetTickCount();

    // DrawIndexed-Objekt-Swap (private VTable — immun gegen Slot-Reverts)
    if (g_diObj && g_diMyTable) {
        void* cur = *(void* volatile*)g_diObj;
        if (cur == (void*)g_diMyTable) {
            if (s_diState != 1) {
                s_diState = 1;
                Log("Watchdog: DI-Swap intakt");
            }
        } else {
            // Objekt zeigt nicht mehr auf unsere Kopie (Kontext neu?) —
            // EnsureDiHook (pro Present) installiert neu; hier nur zählen.
            g_mchams.diReverts++;
            if (now - s_lastLog > 5000) {
                s_lastLog = now;
                Log("Watchdog: DI-Swap WEG #%ld (obj-vtbl=0x%p) — Re-Install läuft",
                    g_mchams.diReverts, cur);
            }
            s_diState = 2;
            g_mchams.diHooked = false; // EnsureDiHook installiert neu
            if (g_diObj) { ((IUnknown*)g_diObj)->Release(); g_diObj = nullptr; }
            if (g_diMyTable) {
                VirtualFree(g_diMyTable, 0, MEM_RELEASE);
                g_diMyTable = nullptr;
            }
            if (g_diDev) { g_diDev->Release(); g_diDev = nullptr; }
        }
    }

    // DrawObject-Code (14 B Detour erwartet)
    if (g_target && g_mchams.hooked) {
        uint8_t* t = (uint8_t*)g_target;
        bool intact = (t[0] == 0xFF && t[1] == 0x25 && *(void**)(t + 6) == (void*)&HkDraw);
        if (intact) {
            if (s_drawState != 1) {
                s_drawState = 1;
                Log("Watchdog: DrawObject-Patch intakt");
            }
        } else {
            if (WriteJump(g_target, (void*)&HkDraw)) {
                g_mchams.drawReverts++;
                if (now - s_lastLog > 5000) {
                    s_lastLog = now;
                    Log("Watchdog: DrawObject REVERT #%ld (jetzt 0x%02X 0x%02X) -> re-patch",
                        g_mchams.drawReverts, t[0], t[1]);
                }
                s_drawState = 2;
            } else if (s_drawState != 0) {
                s_drawState = 0;
                Log("Watchdog: DrawObject-Patch weg, Re-Patch FAILED");
            }
        }
    }
}

// ── Deferred-Contexts: Spieler rendern evtl. nicht über Immediate ──────────
// Hook auf ID3D11Device::CreateDeferredContext (Idx 27, vtable-swap am
// Game-Device). Jeder neue Deferred-Ctx bekommt DI-Idx12 auf unseren Hook
// (geteilte Tabelle, keine Refs — das Spiel besitzt die Objekte).
// Unload: SEH-geschützter Restore-Loop (tote Objekte werden übersprungen).
namespace {
using CreateDefCtxFn = HRESULT(__stdcall*)(ID3D11Device*, UINT, ID3D11DeviceContext**);
CreateDefCtxFn g_oCreateDefCtx = nullptr;
void* g_devObj = nullptr;        // Game-Device (vtable geswappt)
void** g_devMyTable = nullptr;   // private Device-Tabelle (64 Einträge)
void** g_devGlobalTable = nullptr; // Original (Restore beim Unload)
void** g_defSharedTable = nullptr; // geteilte Deferred-Ctx-Tabelle (128)
void* g_defObjs[64] = {};        // Ring der geswappten Deferred-Objekte
void* g_defOrig[64] = {};        // deren Original-VTables (Restore beim Unload)
int g_defUsed = 0;
struct DFreq { UINT stride = 0; long n = 0; long long idxSum = 0; };
DFreq g_dFreq[64];
int g_dFreqUsed = 0;
DWORD g_dFreqLastLog = 0;

void __stdcall HkDrawIndexedDef(ID3D11DeviceContext* ctx, UINT idxCount, UINT start, INT baseV) {
    InterlockedIncrement(&g_mchams.defDraws);
    __try {
        ID3D11Buffer* vb = nullptr;
        UINT stride = 0, off = 0;
        ctx->IAGetVertexBuffers(0, 1, &vb, &stride, &off);
        if (vb) vb->Release();
        if (stride < 512) {
            int slot = -1;
            for (int i = 0; i < g_dFreqUsed; i++) {
                if (g_dFreq[i].stride == stride) { slot = i; break; }
            }
            if (slot < 0 && g_dFreqUsed < 64) {
                slot = g_dFreqUsed++;
                g_dFreq[slot].stride = stride;
            }
            if (slot >= 0) {
                g_dFreq[slot].n++;
                g_dFreq[slot].idxSum += idxCount;
            }
        }
        DWORD now = GetTickCount();
        if (now - g_dFreqLastLog > 10000) {
            g_dFreqLastLog = now;
            for (int a = 0; a < g_dFreqUsed; a++)
                for (int b = a + 1; b < g_dFreqUsed; b++)
                    if (g_dFreq[b].n > g_dFreq[a].n) {
                        DFreq t = g_dFreq[a]; g_dFreq[a] = g_dFreq[b]; g_dFreq[b] = t;
                    }
            char line[512] = {};
            int pos = snprintf(line, sizeof(line), "DD-Stride Top (n=%d):", g_dFreqUsed);
            for (int i = 0; i < g_dFreqUsed && i < 12 && pos < 400; i++) {
                long avg = g_dFreq[i].n ? (long)(g_dFreq[i].idxSum / g_dFreq[i].n) : 0;
                pos += snprintf(line + pos, sizeof(line) - (size_t)pos,
                                " [s=%u c=%ld ai=%ld]", g_dFreq[i].stride,
                                g_dFreq[i].n, avg);
            }
            Log("%s", line);
        }
        // XQZ-Pass auch hier (gleiche Stride-/Idx-Gates wie immediate).
        if (g_chams.glowEnable && g_chams.xqzEnable && (int)stride == g_chams.xqzStride &&
            (int)idxCount >= g_chams.xqzIdxMin && (int)idxCount <= g_chams.xqzIdxMax) {
            if (EnsureStates(ctx) && g_xqzSRV && g_xqzSamp) {
                ID3D11DepthStencilState* oldDS = nullptr;
                UINT sr = 0;
                ID3D11ShaderResourceView* oldSRV = nullptr;
                ID3D11SamplerState* oldSamp = nullptr;
                ctx->OMGetDepthStencilState(&oldDS, &sr);
                ctx->PSGetShaderResources(0, 1, &oldSRV);
                ctx->PSGetSamplers(0, 1, &oldSamp);
                ctx->OMSetDepthStencilState(g_dsOff, 0);
                ctx->PSSetShaderResources(0, 1, &g_xqzSRV);
                ctx->PSSetSamplers(0, 1, &g_xqzSamp);
                g_oDrawIndexed(ctx, idxCount, start, baseV);
                ctx->OMSetDepthStencilState(oldDS, sr);
                ctx->PSSetShaderResources(0, 1, &oldSRV);
                ctx->PSSetSamplers(0, 1, &oldSamp);
                if (oldDS) oldDS->Release();
                if (oldSRV) oldSRV->Release();
                if (oldSamp) oldSamp->Release();
                InterlockedIncrement(&g_mchams.xqzDraws);
            }
        }
        g_oDrawIndexed(ctx, idxCount, start, baseV);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

HRESULT __stdcall HkCreateDeferredContext(ID3D11Device* dev, UINT flags,
                                          ID3D11DeviceContext** pp) {
    HRESULT hr = g_oCreateDefCtx(dev, flags, pp);
    __try {
        if (SUCCEEDED(hr) && pp && *pp && g_defSharedTable) {
            // Einmalig: Shared-Tabelle aus echter Ctx-VTable füllen.
            static bool s_defTableInit = false;
            if (!s_defTableInit) {
                void** gv = *(void***)(*pp);
                for (int i = 0; i < 128; i++) g_defSharedTable[i] = gv[i];
                g_defSharedTable[12] = (void*)&HkDrawIndexedDef;
                s_defTableInit = true;
                Log("DeferredCtx: Shared-Tabelle initialisiert (orig DI=0x%p)", gv[12]);
            }
            DWORD old = 0;
            if (VirtualProtect(*pp, sizeof(void*), PAGE_READWRITE, &old)) {
                void* origTable = *(void**)(*pp);
                *(void**)(*pp) = (void*)g_defSharedTable;
                DWORD tmp = 0;
                VirtualProtect(*pp, sizeof(void*), old, &tmp);
                int slot = g_defUsed % 64;
                g_defObjs[slot] = *pp;
                g_defOrig[slot] = origTable;
                g_defUsed++;
                g_mchams.defCtx = g_defUsed > 64 ? 64 : g_defUsed;
                static DWORD s_lastDefLog = 0;
                DWORD now = GetTickCount();
                if (now - s_lastDefLog > 30000) {
                    s_lastDefLog = now;
                    Log("DeferredCtx: #%d geswappt (ctx=0x%p)", g_defUsed, *pp);
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return hr;
}

void EnsureDeviceHook(ID3D11Device* dev) {
    if (!dev) return;
    if (g_devObj == (void*)dev && g_devMyTable) {
        // Swap noch drin?
        void* cur = *(void* volatile*)g_devObj;
        if (cur != (void*)g_devMyTable) {
            DWORD old = 0;
            if (VirtualProtect(g_devObj, sizeof(void*), PAGE_READWRITE, &old)) {
                *(void**)g_devObj = (void*)g_devMyTable;
                DWORD tmp = 0;
                VirtualProtect(g_devObj, sizeof(void*), old, &tmp);
                Log("DeviceHook: Re-Swap");
            }
        }
        return;
    }
    // Neu installieren.
    __try {
        void** global = *(void***)dev;
        void** mine = (void**)VirtualAlloc(nullptr, 64 * sizeof(void*),
                                           MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!mine) return;
        for (int i = 0; i < 64; i++) mine[i] = global[i];
        if (!g_defSharedTable) {
            g_defSharedTable = (void**)VirtualAlloc(nullptr, 128 * sizeof(void*),
                                                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (!g_defSharedTable) { VirtualFree(mine, 0, MEM_RELEASE); return; }
            // Inhalt folgt beim ersten Deferred-Ctx (Kontext-VTable kopieren).
        }
        g_oCreateDefCtx = (CreateDefCtxFn)mine[27];
        mine[27] = (void*)&HkCreateDeferredContext;
        DWORD old = 0;
        if (!VirtualProtect(dev, sizeof(void*), PAGE_READWRITE, &old)) {
            VirtualFree(mine, 0, MEM_RELEASE);
            return;
        }
        *(void**)dev = (void*)mine;
        DWORD tmp = 0;
        VirtualProtect(dev, sizeof(void*), old, &tmp);
        g_devObj = (void*)dev;
        g_devMyTable = mine;
        g_devGlobalTable = global;
        Log("DeviceHook: CreateDeferredContext geswappt (dev=0x%p orig=0x%p)",
            dev, g_oCreateDefCtx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log("DeviceHook: Setup-Exception");
    }
}
} // namespace

void ShutdownModelChams() {
    if (g_target && g_mchams.hooked) {
        DWORD old = 0;
        if (VirtualProtect(g_target, 14, PAGE_EXECUTE_READWRITE, &old)) {
            memcpy(g_target, g_origBytes, 14);
            DWORD tmp = 0;
            VirtualProtect(g_target, 14, old, &tmp);
            FlushInstructionCache(GetCurrentProcess(), g_target, 14);
        }
        g_mchams.hooked = false;
        Log("ModelChams: Hook entfernt");
    }
    g_oDraw = nullptr;
    g_target = nullptr;
    if (g_tramp) { VirtualFree(g_tramp, 0, MEM_RELEASE); g_tramp = nullptr; }
    // DI-Swap zurückbauen (Objekt-vtbl -> globale Tabelle), Refs freigeben.
    if (g_diObj && g_diGlobalTable) {
        __try {
            DWORD old = 0;
            if (VirtualProtect(g_diObj, sizeof(void*), PAGE_READWRITE, &old)) {
                *(void**)g_diObj = (void*)g_diGlobalTable;
                DWORD tmp = 0;
                VirtualProtect(g_diObj, sizeof(void*), old, &tmp);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        ((IUnknown*)g_diObj)->Release();
        g_diObj = nullptr;
    }
    // Deferred-Ring: was noch lebt, auf Original-VTable zurückbauen.
    // Tote Objekte fangen SEH ab (werden übersprungen).
    for (int i = 0; i < 64; i++) {
        if (!g_defObjs[i] || !g_defOrig[i]) continue;
        __try {
            DWORD old = 0;
            if (VirtualProtect(g_defObjs[i], sizeof(void*), PAGE_READWRITE, &old)) {
                *(void**)g_defObjs[i] = g_defOrig[i];
                DWORD tmp = 0;
                VirtualProtect(g_defObjs[i], sizeof(void*), old, &tmp);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_defObjs[i] = nullptr;
        g_defOrig[i] = nullptr;
    }
    if (g_defSharedTable) { VirtualFree(g_defSharedTable, 0, MEM_RELEASE); g_defSharedTable = nullptr; }
    // Device-Swap zurückbauen (sonst baumelt CreateDeferredContext nach Unload).
    if (g_devObj && g_devGlobalTable) {
        __try {
            DWORD old = 0;
            if (VirtualProtect(g_devObj, sizeof(void*), PAGE_READWRITE, &old)) {
                *(void**)g_devObj = (void*)g_devGlobalTable;
                DWORD tmp = 0;
                VirtualProtect(g_devObj, sizeof(void*), old, &tmp);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        g_devObj = nullptr;
    }
    if (g_devMyTable) { VirtualFree(g_devMyTable, 0, MEM_RELEASE); g_devMyTable = nullptr; }
    g_devGlobalTable = nullptr;
    if (g_diMyTable) { VirtualFree(g_diMyTable, 0, MEM_RELEASE); g_diMyTable = nullptr; }
    g_diGlobalTable = nullptr;
    g_oDrawIndexed = nullptr;
    if (g_diDev) { g_diDev->Release(); g_diDev = nullptr; }
    if (g_mchams.diHooked) {
        g_mchams.diHooked = false;
        Log("DI-Swap: entfernt");
    }
    if (g_dsOff) { g_dsOff->Release(); g_dsOff = nullptr; }
    if (g_bsAlpha) { g_bsAlpha->Release(); g_bsAlpha = nullptr; }
    if (g_xqzSRV) { g_xqzSRV->Release(); g_xqzSRV = nullptr; }
    if (g_xqzSamp) { g_xqzSamp->Release(); g_xqzSamp = nullptr; }
    if (g_stateDev) { g_stateDev->Release(); g_stateDev = nullptr; }
}
