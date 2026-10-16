#include "pch.h"
#include "MenuWidgets.h"
#include "../core/Globals.h"
#include <cstdio>
#include <cctype>
#include <cfloat>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// ui/MenuWidgets.cpp — Glutenium-Widgets (Port von payload/src/ui/ClickGui.cpp)
// Alle Animation-State steckt in ImGui's StateStorage (pro ID) — die Widgets
// sind frame-unabhaengig und brauchen keine globalen Structs.
// ─────────────────────────────────────────────────────────────────────────────

namespace ui {

float g_menuScale = 1.0f;

float Px(float designPx) { return designPx * g_menuScale; }

// ── Animation helpers ────────────────────────────────────────────────────────
float Approach(float current, float target, float rate, float dt) {
    const float step = 1.f - std::exp(-rate * dt);
    float next = current + (target - current) * step;
    if (std::fabs(target - next) < 0.0015f) next = target;
    return next;
}

float HoverTowards(float current, float target, float dt, float durationMs) {
    const float step = dt * 1000.f / durationMs;
    if (current < target) return target - current < step ? target : current + step;
    return current - target < step ? target : current - step;
}

float EaseOutCubic(float t) {
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return 1.f - std::pow(1.f - t, 3.f);
}

float EaseOutBack(float t, float overshoot) {
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    const float c3 = overshoot + 1.f;
    return 1.f + c3 * std::pow(t - 1.f, 3.f) + overshoot * std::pow(t - 1.f, 2.f);
}

ImU32 ColU32(const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); }

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
    t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                  a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

float GetAnim(ImGuiID id, const char* key, float def) {
    ImGui::PushID(id);
    float v = ImGui::GetStateStorage()->GetFloat(ImGui::GetID(key), def);
    ImGui::PopID();
    return v;
}

void SetAnim(ImGuiID id, const char* key, float v) {
    ImGui::PushID(id);
    ImGui::GetStateStorage()->SetFloat(ImGui::GetID(key), v);
    ImGui::PopID();
}

// ── Fonts / Text ─────────────────────────────────────────────────────────────
static float BodySize() { return 14.f * g_menuScale; }

float MeasureText(const char* text, float size) {
    ImFont* f = g_fontBody ? g_fontBody : ImGui::GetFont();
    return f->CalcTextSizeA(size * g_menuScale, FLT_MAX, 0.f, text).x;
}

void DrawTextCentered(ImDrawList* dl, ImVec2 center, const char* text, float size, ImU32 col) {
    ImFont* f = g_fontBody ? g_fontBody : ImGui::GetFont();
    ImVec2 ts = f->CalcTextSizeA(size * g_menuScale, FLT_MAX, 0.f, text);
    dl->AddText(f, size * g_menuScale, ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), col, text);
}

void DrawTextRight(ImDrawList* dl, ImVec2 topRight, const char* text, float size, ImU32 col) {
    ImFont* f = g_fontBody ? g_fontBody : ImGui::GetFont();
    ImVec2 ts = f->CalcTextSizeA(size * g_menuScale, FLT_MAX, 0.f, text);
    dl->AddText(f, size * g_menuScale, ImVec2(topRight.x - ts.x, topRight.y), col, text);
}

void ElideText(char* dest, size_t destSize, const char* src, float maxWidth, float size) {
    ImFont* f = g_fontBody ? g_fontBody : ImGui::GetFont();
    const float fs = size * g_menuScale;
    if (f->CalcTextSizeA(fs, FLT_MAX, 0.f, src).x <= maxWidth) {
        snprintf(dest, destSize, "%s", src);
        return;
    }
    size_t len = strlen(src);
    while (len > 0) {
        char buf[256];
        size_t keep = len > 128 ? 128 : len;
        memcpy(buf, src, keep);
        buf[keep] = 0;
        // "..." + tail anhaengen (strncat ist hier sicher: n begrenzt Restplatz)
        strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
        if (f->CalcTextSizeA(fs, FLT_MAX, 0.f, buf).x <= maxWidth) { snprintf(dest, destSize, "%s", buf); return; }
        // ein Zeichen (UTF-8-sicher grob: bis 4 Bytes) zurueck
        size_t back = 1;
        while (back < 4 && len >= back && (src[len - back] & 0xC0) == 0x80) ++back;
        len -= back;
        if (len < 1) break;
        size_t keep2 = len > 128 ? 128 : len;
        memcpy(buf, src, keep2);
        buf[keep2] = 0;
        strncat(buf, "...", sizeof(buf) - strlen(buf) - 1);
        if (f->CalcTextSizeA(fs, FLT_MAX, 0.f, buf).x <= maxWidth) { snprintf(dest, destSize, "%s", buf); return; }
    }
    snprintf(dest, destSize, "...");
}

// ── VkToString (aus alter Widgets-Datei uebernommen) ─────────────────────────
const char* VkToString(int vk) {
    switch (vk) {
    case VK_LBUTTON:  return "Links";
    case VK_RBUTTON:  return "Rechts";
    case VK_MBUTTON:  return "Mitte";
    case VK_XBUTTON1: return "Mouse4";
    case VK_XBUTTON2: return "Mouse5";
    case VK_LCONTROL: return "Strg";
    case VK_RCONTROL: return "Strg R";
    case VK_LMENU:    return "Alt";
    case VK_RMENU:    return "Alt R";
    case VK_LSHIFT:   return "Shift";
    case VK_RSHIFT:   return "Shift R";
    case VK_CAPITAL:  return "Caps";
    case VK_TAB:      return "Tab";
    case VK_SPACE:    return "Space";
    case VK_RETURN:   return "Enter";
    case VK_BACK:     return "Back";
    case VK_DELETE:   return "Entf";
    default:
        if (vk >= 'A' && vk <= 'Z') { static char b[4]; b[0]=(char)vk; b[1]=0; return b; }
        if (vk >= '0' && vk <= '9') { static char b2[4]; b2[0]=(char)vk; b2[1]=0; return b2; }
        if (vk >= VK_F1 && vk <= VK_F12) { static char b3[8]; snprintf(b3, 8, "F%d", vk - VK_F1 + 1); return b3; }
        return "?";
    }
}

// ── Hit-Testing fuer DrawList-Widgets ────────────────────────────────────────
namespace {
struct WRect { ImVec2 mn, mx; bool Contains(const ImVec2& p) const {
    return p.x >= mn.x && p.x <= mx.x && p.y >= mn.y && p.y <= mx.y; } };

// Klick nur wenn: Maus im Rect, Menue-Fenster hovered (kein anderes Fenster
// darueber), keine Key-Bind-Capture laeuft und kein Picker darueber liegt.
ImVec2 g_blockMin(0, 0), g_blockMax(0, 0);
bool   g_blockActive = false;

static bool Blocked(const WRect& r) {
    if (!g_blockActive) return false;
    WRect b{ g_blockMin, g_blockMax };
    return r.Contains(ImGui::GetIO().MousePos) && b.Contains(ImGui::GetIO().MousePos);
}

bool Clicked(const WRect& r, const char* id) {
    (void)id;
    ImGuiIO& io = ImGui::GetIO();
    if (!r.Contains(io.MousePos)) return false;
    if (Blocked(r)) return false;
    if (!ImGui::IsWindowHovered()) return false;
    return ImGui::IsMouseClicked(0) && !g_keyBinding;
}

bool RightClicked(const WRect& r, const char* id) {
    (void)id;
    ImGuiIO& io = ImGui::GetIO();
    if (!r.Contains(io.MousePos)) return false;
    if (Blocked(r)) return false;
    if (!ImGui::IsWindowHovered()) return false;
    return ImGui::IsMouseClicked(1) && !g_keyBinding;
}

bool Held(const WRect& r) {
    ImGuiIO& io = ImGui::GetIO();
    if (!r.Contains(io.MousePos)) return false;
    if (Blocked(r)) return false;
    if (!ImGui::IsWindowHovered()) return false;
    return ImGui::IsMouseDown(0) && !g_keyBinding;
}
} // namespace

// ui::Scope (Deklaration im Header) — nach dem anonymous namespace.
void SetPickerBlock(const ImVec2& mn, const ImVec2& mx, bool active) {
    g_blockMin = mn; g_blockMax = mx; g_blockActive = active;
}

// ── Surface ──────────────────────────────────────────────────────────────────
void Surface(ImDrawList* dl, const ImVec2& min, const ImVec2& max,
             float radius, const ImVec4& tint) {
    dl->AddRectFilled(min, max, ColU32(tint), radius);
}

// ── Toggle ───────────────────────────────────────────────────────────────────
bool ToggleWidget(ImDrawList* dl, const char* id, const ImVec2& pos,
                  bool* value, float dt) {
    const ImGuiID iid = ImGui::GetID(id);
    const float w = Px(layout::ToggleW), h = Px(layout::ToggleH);
    WRect r{ pos, ImVec2(pos.x + w, pos.y + h) };
    bool pressed = Clicked(r, id);
    if (pressed) *value = !*value;

    float anim = GetAnim(iid, "tgl", *value ? 1.f : 0.f);
    anim = HoverTowards(anim, *value ? 1.f : 0.f, dt, 200.f);
    SetAnim(iid, "tgl", anim);

    const Palette& pal = Pal();
    const float e = EaseOutCubic(anim);
    dl->AddRectFilled(pos, r.mx, ColU32(Mix(pal.track, pal.accent, e)), h * 0.5f);

    // Thumb mit outBack-Ueberschwinger
    const float thumbD = Px(layout::ThumbD);
    const float pad = Px(layout::TogglePad);
    const float travel = w - thumbD - pad * 2.f;
    const float cx = pos.x + pad + thumbD * 0.5f + travel * EaseOutBack(e);
    const float cy = pos.y + h * 0.5f;
    const ImVec2 tmin(cx - thumbD * 0.5f, cy - thumbD * 0.5f);
    const ImVec2 tmax(cx + thumbD * 0.5f, cy + thumbD * 0.5f);
    // Weisser Thumb auf grauem Track (aus) bzw. Akzent-Track (an) —
    // so ist AN/AUS auch am Toggle selbst ablesbar.
    dl->AddRectFilled(tmin, tmax, ColU32(pal.textBright), thumbD * 0.5f);
    return pressed;
}

// ── 50px Setting-Karte ───────────────────────────────────────────────────────
namespace {
// Zeichnet Karte+Label, gibt Innen-Rechteck rechts fuer Controls zurueck.
void CardRow(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
             const char* label, float dt) {
    const float h = Px(layout::SettingH);
    WRect r{ pos, ImVec2(pos.x + width, pos.y + h) };
    const ImGuiID iid = ImGui::GetID(id);

    float hover = GetAnim(iid, "hov", 0.f);
    hover = HoverTowards(hover, Held(r) || r.Contains(ImGui::GetIO().MousePos) ? 1.f : 0.f, dt);
    SetAnim(iid, "hov", hover);

    const Palette& pal = Pal();
    Surface(dl, pos, r.mx, Px(layout::SettingRad), Mix(pal.setting, pal.settingHover, hover));
    // Hover-Rand (borderStrong), sonst hauchfein
    dl->AddRect(pos, r.mx,
                ColU32(Mix(pal.border, pal.accentDim, hover)),
                Px(layout::SettingRad), 0, Px(1.f));

    dl->AddText(g_fontBody, Px(14.f), ImVec2(pos.x + Px(14.f), pos.y + Px(8.f)),
                ColU32(pal.text), label);
}

ImVec2 RowCtlPos(const ImVec2& pos, float width, float ctlW) {
    return ImVec2(pos.x + width - Px(14.f) - ctlW, pos.y + Px(13.f));
}
} // namespace

bool BoolRow(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
             const char* label, bool* value, float dt) {
    CardRow(dl, id, pos, width, label, dt);
    const ImVec2 tpos = RowCtlPos(pos, width, Px(layout::ToggleW));
    return ToggleWidget(dl, id, tpos, value, dt);
}

bool SliderRow(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
               const char* label, float* value, float mn, float mx,
               const char* fmt, float dt) {
    CardRow(dl, id, pos, width, label, dt);
    const ImGuiID iid = ImGui::GetID(id);
    const Palette& pal = Pal();

    // Wert rechts oben
    char val[32];
    snprintf(val, sizeof(val), fmt, *value);
    DrawTextRight(dl, ImVec2(pos.x + width - Px(14.f), pos.y + Px(8.f)), val,
                  14.f, ColU32(pal.textBright));

    // Track 4px unten
    const float trackY = pos.y + Px(32.f);
    ImVec2 tr{ pos.x + Px(14.f), trackY };
    ImVec2 br{ pos.x + width - Px(14.f), trackY + Px(4.f) };
    WRect hit{ ImVec2(tr.x - Px(9.f), tr.y - Px(12.f)), ImVec2(br.x + Px(9.f), br.y + Px(12.f)) };

    float norm = (mx > mn) ? (*value - mn) / (mx - mn) : 0.f;
    norm = norm < 0.f ? 0.f : (norm > 1.f ? 1.f : norm);
    float knob = GetAnim(iid, "kn", norm);
    knob = Approach(knob, norm, 20.f, dt);
    SetAnim(iid, "kn", knob);

    dl->AddRectFilled(tr, br, ColU32(pal.track), Px(2.f));
    const float knobX = tr.x + (br.x - tr.x) * knob;
    if (knobX > tr.x + 1.f)
        dl->AddRectFilled(tr, ImVec2(knobX, br.y), ColU32(pal.accent), Px(2.f));
    const float hov = Held(hit) ? 1.f : 0.f;
    const float knobR = Px(6.f) + Px(1.5f) * hov;
    dl->AddCircleFilled(ImVec2(knobX, (tr.y + br.y) * 0.5f), knobR,
                        ColU32(hov > 0.5f ? pal.textBright : pal.text), 24);

    // Interaktion — Drag bleibt "klebrig", bis die Maus losgelassen wird.
    bool dragging = GetAnim(iid, "dg", 0.f) > 0.5f && ImGui::IsMouseDown(0);
    if (!dragging && Clicked(hit, id)) { SetAnim(iid, "dg", 1.f); dragging = true; }
    if (!ImGui::IsMouseDown(0)) SetAnim(iid, "dg", 0.f);
    if (dragging) {
        float n = (ImGui::GetIO().MousePos.x - tr.x + Px(9.f)) / (br.x - tr.x + Px(18.f));
        n = n < 0.f ? 0.f : (n > 1.f ? 1.f : n);
        *value = mn + (mx - mn) * n;
    }
    return dragging;
}

bool SliderRowInt(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
                  const char* label, int* value, int mn, int mx,
                  const char* fmt, float dt) {
    float tmp = (float)*value;
    bool dragging = SliderRow(dl, id, pos, width, label, &tmp, (float)mn, (float)mx, fmt, dt);
    int iv = (int)(tmp + (tmp < 0.f ? -0.5f : 0.5f));
    if (iv != *value) *value = iv;
    return dragging;
}

bool ChoiceRow(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
               const char* label, int* value, const char* const* options,
               int count, float dt) {
    CardRow(dl, id, pos, width, label, dt);
    if (*value < 0 || *value >= count) *value = 0;
    const char* cur = options[*value];
    const Palette& pal = Pal();

    const float tw = MeasureText(cur, 14.f);
    const float bw = tw + Px(24.f) < Px(84.f) ? Px(84.f) : tw + Px(24.f);
    ImVec2 bmin(pos.x + width - Px(14.f) - bw, pos.y + Px(11.f));
    ImVec2 bmax(bmin.x + bw, bmin.y + Px(28.f));
    WRect r{ bmin, bmax };

    float hover = GetAnim(ImGui::GetID(id), "ch", 0.f);
    hover = HoverTowards(hover, r.Contains(ImGui::GetIO().MousePos) ? 1.f : 0.f, dt);
    SetAnim(ImGui::GetID(id), "ch", hover);

    dl->AddRectFilled(bmin, bmax, ColU32(Mix(pal.button, pal.buttonHover, hover)), Px(8.f));
    dl->AddRect(bmin, bmax, ColU32(Mix(pal.border, pal.accentDim, hover)), Px(8.f), 0, Px(1.f));
    DrawTextCentered(dl, ImVec2((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f), cur,
                     14.f, ColU32(pal.text));

    if (Clicked(r, id)) { *value = (*value + 1) % count; return true; }
    return false;
}

bool KeyRow(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
            const char* label, int* key, int target, float dt) {
    CardRow(dl, id, pos, width, label, dt);
    const Palette& pal = Pal();

    const bool listening = g_keyBinding && g_keyBindTarget == target;
    const char* txt = listening ? "..." : VkToString(*key);
    const float tw = MeasureText(txt, 14.f);
    const float bw = (tw + Px(32.f) < Px(90.f)) ? Px(90.f) : tw + Px(32.f);
    ImVec2 bmin(pos.x + width - Px(14.f) - bw, pos.y + Px(9.f));
    ImVec2 bmax(bmin.x + bw, bmin.y + Px(32.f));
    WRect r{ bmin, bmax };

    float hover = GetAnim(ImGui::GetID(id), "kb", 0.f);
    hover = HoverTowards(hover, listening || r.Contains(ImGui::GetIO().MousePos) ? 1.f : 0.f, dt);
    SetAnim(ImGui::GetID(id), "kb", hover);

    dl->AddRectFilled(bmin, bmax, ColU32(Mix(pal.button, pal.buttonHover, hover)), Px(8.f));
    dl->AddRect(bmin, bmax, ColU32(Mix(pal.border, listening ? pal.accent : pal.borderStrong, hover)),
                Px(8.f), 0, Px(1.f));
    DrawTextCentered(dl, ImVec2((bmin.x + bmax.x) * 0.5f, (bmin.y + bmax.y) * 0.5f), txt,
                     14.f, ColU32(pal.text));

    if (Clicked(r, id)) {
        g_keyBindTarget = target;
        g_keyBinding = true;
        return true;
    }
    return false;
}

// ── ColorRow + HSV-Picker ────────────────────────────────────────────────────
namespace {
float HueOf(const ImVec4& c) {
    const float mx = c.x > c.y ? (c.x > c.z ? c.x : c.z) : (c.y > c.z ? c.y : c.z);
    const float mn = c.x < c.y ? (c.x < c.z ? c.x : c.z) : (c.y < c.z ? c.y : c.z);
    const float d = mx - mn;
    if (d <= 0.0001f) return 0.f;
    float h;
    if (mx == c.x)      h = (c.y - c.z) / d + (c.y < c.z ? 6.f : 0.f);
    else if (mx == c.y) h = (c.z - c.x) / d + 2.f;
    else                h = (c.x - c.y) / d + 4.f;
    return h / 6.f;
}
} // namespace

bool ColorRow(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
              const char* label, float* color4, float dt) {
    CardRow(dl, id, pos, width, label, dt);
    const ImGuiID iid = ImGui::GetID(id);
    ImVec2 smin(pos.x + width - Px(14.f) - Px(24.f), pos.y + Px(13.f));
    ImVec2 smax(smin.x + Px(24.f), smin.y + Px(24.f));
    WRect r{ smin, smax };

    dl->AddRectFilled(smin, smax, ColU32(ImVec4(color4[0], color4[1], color4[2], color4[3])), Px(6.f));
    dl->AddRect(smin, smax, ColU32(Pal().borderStrong), Px(6.f), 0, Px(1.f));

    float open = GetAnim(iid, "op", 0.f);
    if (Clicked(r, id)) {
        SetAnim(iid, "op", open > 0.5f ? 0.f : 1.f);
        SetAnim(iid, "hue", HueOf(ImVec4(color4[0], color4[1], color4[2], color4[3])));
        open = GetAnim(iid, "op", 0.f);
    }
    return open > 0.5f;
}

void ColorPickerPopup(ImDrawList* dl, const char* id, const ImVec2& anchorRight,
                      float limitX, float* color4, float dt) {
    const ImGuiID iid = ImGui::GetID(id);
    const Palette& pal = Pal();

    // Nur zeichnen, wenn ColorRow (gleiche ID) den Picker geoeffnet hat.
    if (GetAnim(iid, "op", 0.f) <= 0.5f) {
        SetPickerBlock(ImVec2(0, 0), ImVec2(0, 0), false);
        return;
    }

    const float w = Px(layout::PickerW);
    const float h = Px(layout::PickerPad) + Px(layout::PickerFieldH) + Px(layout::PickerPad) +
                    Px(layout::PickerStripH) + Px(layout::PickerPad) + Px(12.f) + Px(layout::PickerPad);
    float x = anchorRight.x + Px(8.f);
    if (x + w > limitX) x = anchorRight.x - Px(8.f) - w;
    const float y = anchorRight.y - Px(3.f);
    ImVec2 pmin(x, y), pmax(x + w, y + h);
    WRect r{ pmin, pmax };

    float hue = GetAnim(iid, "hue", 0.f);
    ImVec4 cur(color4[0], color4[1], color4[2], color4[3]);

    dl->AddRectFilled(pmin, pmax, ColU32(ImVec4(0.08f, 0.08f, 0.09f, 0.98f)), Px(12.f));
    dl->AddRect(pmin, pmax, ColU32(pal.borderStrong), Px(12.f), 0, Px(1.f));

    // SV-Feld (x = Sättigung, y = Wert invertiert)
    ImVec2 fmin(pmin.x + Px(layout::PickerPad), pmin.y + Px(layout::PickerPad));
    ImVec2 fmax(fmin.x + w - Px(layout::PickerPad) * 2.f, fmin.y + Px(layout::PickerFieldH));
    float hr, hg, hb;
    ImGui::ColorConvertHSVtoRGB(hue, 1.f, 1.f, hr, hg, hb);
    const ImU32 hueCol = ImGui::ColorConvertFloat4ToU32(ImVec4(hr, hg, hb, 1.f));
    // Weiß→Hue horizontal, transparent→schwarz vertikal
    dl->AddRectFilledMultiColor(fmin, fmax, IM_COL32(255,255,255,255), hueCol, hueCol, IM_COL32(255,255,255,255));
    dl->AddRectFilledMultiColor(fmin, fmax, IM_COL32(0,0,0,0), IM_COL32(0,0,0,0), IM_COL32(0,0,0,255), IM_COL32(0,0,0,255));

    // Hue-Strip
    ImVec2 smin(fmin.x, fmax.y + Px(layout::PickerPad));
    ImVec2 smax(smin.x + fmax.x - fmin.x, smin.y + Px(layout::PickerStripH));
    const float seg = (smax.x - smin.x) / 6.f;
    const ImU32 rainbow[7] = {
        IM_COL32(255,0,0,255), IM_COL32(255,255,0,255), IM_COL32(0,255,0,255),
        IM_COL32(0,255,255,255), IM_COL32(0,0,255,255), IM_COL32(255,0,255,255),
        IM_COL32(255,0,0,255) };
    for (int i = 0; i < 6; ++i)
        dl->AddRectFilledMultiColor(ImVec2(smin.x + seg * i, smin.y),
                                    ImVec2(smin.x + seg * (i + 1), smax.y),
                                    rainbow[i], rainbow[i + 1], rainbow[i + 1], rainbow[i]);

    // Interaktion SV
    ImGuiIO& io = ImGui::GetIO();
    const float mx = io.MousePos.x, my = io.MousePos.y;
    if (io.MouseDown[0] && r.Contains(io.MousePos)) {
        if (my >= fmin.y && my <= fmax.y && mx >= fmin.x && mx <= fmax.x) {
            float sat = (mx - fmin.x) / (fmax.x - fmin.x);
            float val = 1.f - (my - fmin.y) / (fmax.y - fmin.y);
            sat = sat < 0.f ? 0.f : (sat > 1.f ? 1.f : sat);
            val = val < 0.f ? 0.f : (val > 1.f ? 1.f : val);
            float rgb[3];
            ImGui::ColorConvertHSVtoRGB(hue, sat, val, rgb[0], rgb[1], rgb[2]);
            color4[0] = rgb[0]; color4[1] = rgb[1]; color4[2] = rgb[2];
        } else if (my >= smin.y && my <= smax.y && mx >= smin.x && mx <= smax.x) {
            float nh = (mx - smin.x) / (smax.x - smin.x);
            nh = nh < 0.f ? 0.f : (nh > 1.f ? 1.f : nh);
            SetAnim(iid, "hue", nh);
            // Hue merken, aus aktueller Farbe S/V retten
            const float cmax = cur.x > cur.y ? (cur.x > cur.z ? cur.x : cur.z) : (cur.y > cur.z ? cur.y : cur.z);
            const float cmin = cur.x < cur.y ? (cur.x < cur.z ? cur.x : cur.z) : (cur.y < cur.z ? cur.y : cur.z);
            const float sat = cmax <= 0.f ? 1.f : (cmax - cmin) / cmax;
            float rgb[3];
            ImGui::ColorConvertHSVtoRGB(nh, sat, cmax <= 0.f ? 1.f : cmax, rgb[0], rgb[1], rgb[2]);
            color4[0] = rgb[0]; color4[1] = rgb[1]; color4[2] = rgb[2];
        }
    }

    // Marker
    float s = 0.f, v = 0.f;
    { float r_, g_, b_; r_ = cur.x; g_ = cur.y; b_ = cur.z;
      const float cmax = r_ > g_ ? (r_ > b_ ? r_ : b_) : (g_ > b_ ? g_ : b_);
      const float cmin = r_ < g_ ? (r_ < b_ ? r_ : b_) : (g_ < b_ ? g_ : b_);
      v = cmax; s = cmax <= 0.f ? 0.f : (cmax - cmin) / cmax; }
    dl->AddCircle(ImVec2(fmin.x + s * (fmax.x - fmin.x), fmin.y + (1.f - v) * (fmax.y - fmin.y)),
                  Px(4.f), IM_COL32(255, 255, 255, 255), 0, Px(1.5f));
    dl->AddRectFilled(ImVec2(smin.x + hue * (smax.x - smin.x) - Px(1.5f), smin.y - Px(2.f)),
                      ImVec2(smin.x + hue * (smax.x - smin.x) + Px(1.5f), smax.y + Px(2.f)),
                      IM_COL32(255, 255, 255, 255), Px(1.5f));

    // Klicks auf darunterliegende Rows schlucken, solange offen
    SetPickerBlock(pmin, pmax, true);
}

// ── Sidebar-Item ─────────────────────────────────────────────────────────────
bool SideItem(ImDrawList* dl, const char* id, const ImVec2& pos, float width,
              const char* icon, const char* label, bool active, float dt) {
    const ImGuiID iid = ImGui::GetID(id);
    const float h = Px(layout::SideItemH);
    WRect r{ pos, ImVec2(pos.x + width, pos.y + h) };

    float hover = GetAnim(iid, "hov", 0.f);
    hover = HoverTowards(hover, r.Contains(ImGui::GetIO().MousePos) ? 1.f : 0.f, dt);
    SetAnim(iid, "hov", hover);

    float act = GetAnim(iid, "act", active ? 1.f : 0.f);
    act = HoverTowards(act, active ? 1.f : 0.f, dt);
    SetAnim(iid, "act", act);

    const Palette& pal = Pal();
    if (act > 0.01f)
        dl->AddRectFilled(pos, r.mx, ColU32(ImVec4(pal.sidebarActive.x, pal.sidebarActive.y, pal.sidebarActive.z, pal.sidebarActive.w * act)), Px(12.f));
    else if (hover > 0.01f)
        dl->AddRectFilled(pos, r.mx, ColU32(ImVec4(pal.cardHover.x, pal.cardHover.y, pal.cardHover.z, pal.cardHover.w * hover)), Px(12.f));

    // Icon-Glyph (Icon-Font) links
    if (g_fontIcon) {
        ImU32 iconCol = ColU32(pal.textDim);
        if (act > 0.01f)      iconCol = ColU32(Mix(pal.textDim, pal.accent, act));
        else if (hover > 0.01f) iconCol = ColU32(Mix(pal.textDim, pal.textBright, hover));
        dl->AddText(g_fontIcon, Px(16.f),
                    ImVec2(pos.x + Px(12.f), pos.y + (h - Px(16.f)) * 0.5f - Px(2.f)),
                    iconCol, icon);
    }
    ImU32 textCol = ColU32(pal.textDim);
    if (act > 0.01f)      textCol = ColU32(Mix(pal.textDim, pal.textBright, act));
    else if (hover > 0.01f) textCol = ColU32(Mix(pal.textDim, pal.textBright, hover));
    dl->AddText(g_fontBody, Px(14.f), ImVec2(pos.x + Px(40.f), pos.y + (h - Px(14.f)) * 0.5f - Px(2.f)),
                textCol, label);

    return Clicked(r, id);
}

// ── Modul-Karte ──────────────────────────────────────────────────────────────
bool ModuleCard(ImDrawList* dl, const char* id, const ImVec2& pos,
                float width, float height, const char* name,
                const char* desc, bool* enabled, float dt, bool selected) {
    const ImGuiID iid = ImGui::GetID(id);
    WRect r{ pos, ImVec2(pos.x + width, pos.y + height) };

    float hover = GetAnim(iid, "hov", 0.f);
    hover = HoverTowards(hover, r.Contains(ImGui::GetIO().MousePos) ? 1.f : 0.f, dt);
    SetAnim(iid, "hov", hover);
    const float t = EaseOutCubic(hover);
    const float lift = Px(2.f) * t;
    const ImVec2 cmin(pos.x, pos.y - lift);
    const ImVec2 cmax(pos.x + width, pos.y + height - lift);

    // AN/AUS-Rampe: treibt Border, Tint, Titel und Status-Punkt.
    float enA = GetAnim(iid, "ena", *enabled ? 1.f : 0.f);
    enA = HoverTowards(enA, *enabled ? 1.f : 0.f, dt, 180.f);
    SetAnim(iid, "ena", enA);
    const float e = EaseOutCubic(enA);

    const Palette& pal = Pal();

    // Basis-Karte (Hover hebt leicht an)
    Surface(dl, cmin, cmax, Px(layout::CardRadius), Mix(pal.card, pal.cardHover, t));
    // Eingeschaltet: Akzent-Tint drueber (accentSoft, anteilig) + Akzent-Leiste oben
    if (e > 0.01f) {
        ImVec4 tint = pal.accentSoft;
        tint.w *= e;
        dl->AddRectFilled(cmin, cmax, ColU32(tint), Px(layout::CardRadius));
        const float stripH = Px(3.f);
        dl->AddRectFilled(ImVec2(cmin.x + Px(layout::CardRadius), cmin.y),
                          ImVec2(cmax.x - Px(layout::CardRadius), cmin.y + stripH),
                          ColU32(ImVec4(pal.accent.x, pal.accent.y, pal.accent.z, e)),
                          stripH * 0.5f);
    }
    // Rand: aus = hauchfein (Hover: accentDim), an = Akzent, Panel offen = dicker
    ImVec4 borderCol = Mix(Mix(pal.border, pal.accentDim, t), pal.accent, e);
    dl->AddRect(cmin, cmax, ColU32(borderCol), Px(layout::CardRadius), 0, Px(1.f));
    if (selected)
        dl->AddRect(ImVec2(cmin.x - Px(1.f), cmin.y - Px(1.f)),
                    ImVec2(cmax.x + Px(1.f), cmax.y + Px(1.f)),
                    ColU32(pal.accent), Px(layout::CardRadius) + Px(1.f), 0, Px(1.5f));

    // Titel: an = hell, aus = gedimmt — sofort erkennbar
    dl->AddText(g_fontBody, Px(16.f), ImVec2(cmin.x + Px(20.f), cmin.y + Px(20.f)),
                ColU32(Mix(pal.textDim, pal.textBright, 0.25f + 0.75f * e)), name);
    char elided[192];
    ElideText(elided, sizeof(elided), desc, width - Px(20.f) - Px(56.f), 13.f);
    dl->AddText(g_fontBody, Px(13.f), ImVec2(cmin.x + Px(20.f), cmin.y + Px(46.f)),
                ColU32(e > 0.5f ? pal.textDim : pal.textFaint), elided);

    // Status-Zeile unten links: Punkt + AN/AUS
    {
        const float sy = cmax.y - Px(28.f);
        const ImVec2 dotC(cmin.x + Px(26.f), sy + Px(7.f));
        dl->AddCircleFilled(dotC, Px(5.f), ColU32(Mix(pal.textFaint, pal.positive, e)), 16);
        dl->AddText(g_fontBody, Px(13.f), ImVec2(cmin.x + Px(38.f), sy),
                    ColU32(Mix(pal.textFaint, pal.positive, e)),
                    *enabled ? "AN" : "AUS");
        // Rechts unten: Hinweis, dass Rechtsklick das Setup oeffnet
        const char* hint = selected ? "OFFEN »" : "Setup »";
        ImFont* f = g_fontBody ? g_fontBody : ImGui::GetFont();
        ImVec2 ts = f->CalcTextSizeA(Px(12.f), FLT_MAX, 0.f, hint);
        dl->AddText(f, Px(12.f),
                    ImVec2(cmax.x - Px(16.f) - ts.x, sy + Px(1.f)),
                    ColU32(selected ? pal.accent : pal.textFaint), hint);
    }

    // Toggle oben rechts (eigenes Hit-Rect, weisser Thumb)
    const ImVec2 tpos(cmax.x - Px(56.f), cmin.y + Px(20.f));
    WRect tr{ tpos, ImVec2(tpos.x + Px(layout::ToggleW), tpos.y + Px(layout::ToggleH)) };
    if (tr.Contains(ImGui::GetIO().MousePos)) {
        const bool leftT = Clicked(tr, id);
        const bool rightT = RightClicked(tr, id);
        if (leftT) { *enabled = !*enabled; }
        float anim = GetAnim(iid, "tgl", *enabled ? 1.f : 0.f);
        anim = HoverTowards(anim, *enabled ? 1.f : 0.f, dt, 200.f);
        SetAnim(iid, "tgl", anim);
        const float te = EaseOutCubic(anim);
        dl->AddRectFilled(tpos, tr.mx, ColU32(Mix(pal.track, pal.accent, te)), Px(layout::ToggleH) * 0.5f);
        const float thumbD = Px(layout::ThumbD);
        const float pad = Px(layout::TogglePad);
        const float travel = Px(layout::ToggleW) - thumbD - pad * 2.f;
        const float cx = tpos.x + pad + thumbD * 0.5f + travel * EaseOutBack(te);
        const float cy = tpos.y + Px(layout::ToggleH) * 0.5f;
        dl->AddCircleFilled(ImVec2(cx, cy), thumbD * 0.5f, ColU32(pal.textBright), 20);
        // Toggle-Links klick ist verbraucht (kein Karten-Toggle),
        // Rechtsklick auf dem Toggle oeffnet trotzdem das Panel.
        return rightT;
    }

    // Rechtsklick auf der Karte → Settings-Panel (wird vom Aufrufer getoggelt)
    if (RightClicked(r, id)) return true;
    // Linksklick auf der Karte → Modul direkt an/aus, kein Panel
    if (Clicked(r, id)) { *enabled = !*enabled; }
    return false;
}

// ── Pill-Button ──────────────────────────────────────────────────────────────
bool PillButton(ImDrawList* dl, const char* id, const ImVec2& pos,
                float width, float height, const char* label, float dt) {
    const ImGuiID iid = ImGui::GetID(id);
    WRect r{ pos, ImVec2(pos.x + width, pos.y + height) };
    float hover = GetAnim(iid, "hov", 0.f);
    hover = HoverTowards(hover, r.Contains(ImGui::GetIO().MousePos) ? 1.f : 0.f, dt);
    SetAnim(iid, "hov", hover);

    const Palette& pal = Pal();
    dl->AddRectFilled(pos, r.mx, ColU32(Mix(pal.button, pal.buttonHover, hover)), Px(8.f));
    dl->AddRect(pos, r.mx, ColU32(Mix(pal.border, pal.accentDim, hover)), Px(8.f), 0, Px(1.f));
    DrawTextCentered(dl, ImVec2((pos.x + r.mx.x) * 0.5f, (pos.y + r.mx.y) * 0.5f), label,
                     14.f, ColU32(pal.text));
    return Clicked(r, id);
}

void LabeledCard(ImDrawList* dl, const char* id, const ImVec2& pos,
                 float width, const char* label, float dt) {
    const ImGuiID iid = ImGui::GetID(id);
    WRect r{ pos, ImVec2(pos.x + width, pos.y + Px(layout::SettingH)) };
    float hover = GetAnim(iid, "hov", 0.f);
    hover = HoverTowards(hover, r.Contains(ImGui::GetIO().MousePos) ? 1.f : 0.f, dt);
    SetAnim(iid, "hov", hover);
    const Palette& pal = Pal();
    Surface(dl, pos, r.mx, Px(layout::SettingRad), Mix(pal.setting, pal.settingHover, hover));
    dl->AddRect(pos, r.mx, ColU32(Mix(pal.border, pal.accentDim, hover)), Px(layout::SettingRad), 0, Px(1.f));
    dl->AddText(g_fontBody, Px(14.f), ImVec2(pos.x + Px(14.f), pos.y + Px(8.f)), ColU32(pal.text), label);
}

}  // namespace ui
