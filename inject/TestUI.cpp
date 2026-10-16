#include <windows.h>
#include <cstdio>
#include "../imgui/imgui.h"
#include "TestUI.h"
#include "Chams.h"
#include "ModelChams.h"
#include "OverlayESP.h"
#include "AimAssist.h"
#include "Bhop.h"

InjectTestState g_test;

void DrawInjectTestUI() {
    if (!g_test.showMenu)
        return;

    ImGui::SetNextWindowSize(ImVec2(420, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("OwOEsp Inject — Test UI", &g_test.showMenu)) {
        ImGui::Text("Build: %s", kBuildTag);
        ImGui::Text("FPS: %.1f", g_test.fps);
        ImGui::Separator();

        ImGui::Checkbox("ImGui Demo", &g_test.showDemo);
        ImGui::SliderFloat("Test-Slider", &g_test.testSlider, 0.0f, 100.0f);
        ImGui::ColorEdit3("Test-Farbe", g_test.testColor);

        const char* items[] = { "Option A", "Option B", "Option C" };
        ImGui::Combo("Test-Combo", &g_test.testCombo, items, 3);
        ImGui::InputText("Test-Text", g_test.testText, sizeof(g_test.testText));

        if (ImGui::Button("Test-Button")) {
            char buf[128];
            sprintf_s(buf, "[inject-test] Klick! slider=%.1f opt=%d txt='%s'\n",
                      g_test.testSlider, g_test.testCombo, g_test.testText);
            OutputDebugStringA(buf);
        }
        ImGui::SameLine();
        if (ImGui::Button("Unload (END)")) {
            // Signalisiert DllMain-Thread zum Entladen (siehe DllMain.cpp).
            extern volatile bool g_requestUnload;
            g_requestUnload = true;
        }

        ImGui::Separator();
        ImGui::TextWrapped("Tasten: INSERT = Menue an/aus, END = DLL entladen.");
        ImGui::TextWrapped("Naechster Schritt: Features aus features/ hier intern verdrahten.");
    }
    ImGui::End();

    if (g_test.showDemo)
        ImGui::ShowDemoWindow(&g_test.showDemo);

    // ── Chams ─────────────────────────────────────────────────────────────
    ImGui::SetNextWindowSize(ImVec2(360, 340), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("OwOEsp Inject — Chams")) {
        ImGui::Checkbox("Chams aktivieren", &g_chams.glowEnable);
        ImGui::Checkbox("Gegner", &g_chams.glowEnemies);
        ImGui::SameLine();
        ImGui::Checkbox("Mates", &g_chams.glowTeammates);
        ImGui::ColorEdit3("Gegner-Farbe", g_chams.enemyColor);
        ImGui::ColorEdit3("Mate-Farbe", g_chams.teamColor);
        const char* mats[] = { "Flat (deckend)", "Glass (transparent)", "Rainbow (animiert)",
                               "Pulse", "Health (nach HP)", "Ghost" };
        int m = g_chams.material;
        if (ImGui::Combo("Material", &m, mats, 6))
            g_chams.material = m;
        if (g_chams.material == 1 || g_chams.material == 5)
            ImGui::SliderInt("Glass-Alpha", &g_chams.glassAlpha, 0, 255);
        ImGui::Separator();
        ImGui::Checkbox("XQZ-Pass (durch Waende, Stride)", &g_chams.xqzEnable);
        ImGui::SliderInt("XQZ-Stride", &g_chams.xqzStride, 8, 64);
        ImGui::SliderInt("XQZ-IdxMin", &g_chams.xqzIdxMin, 0, 5000);
        ImGui::SliderInt("XQZ-IdxMax", &g_chams.xqzIdxMax, 0, 20000);
        ImGui::Text("XQZ-Draws: %ld", g_mchams.xqzDraws);
        ImGui::Separator();
        ImGui::Checkbox("ESP-Boxen (Overlay, robust)", &g_esp.enable);
        const char* eyes[] = { "Winkel: Auto", "Winkel: 0x35F0", "Winkel: 0x3600" };
        ImGui::Combo("##eyesrc", &g_esp.eyeSrc, eyes, 3);
        ImGui::Text("Boxen: %d", g_esp.drawn);
        ImGui::Separator();
        ImGui::Text("Hook: %s", g_mchams.hooked ? "AKTIV" : "aus");
        ImGui::Text("Draws: %ld  Spieler: %ld", g_mchams.drawsSeen, g_mchams.playersColored);
        ImGui::Text("Reverts: DI %ld / Draw %ld", g_mchams.diReverts, g_mchams.drawReverts);
        ImGui::Text("DI-Recon: %s (%ld draws)", g_mchams.diHooked ? "an" : "aus", g_mchams.diDraws);
        ImGui::Text("Deferred: %d ctx, %ld draws", g_mchams.defCtx, g_mchams.defDraws);
        ImGui::Text("Offsets OK: %s", g_chams.offsetsOk ? "ja" : "nein");
        ImGui::Text("Pawns aktiv: %d", g_chams.applied);
        if (!g_mchams.hooked && g_chams.glowEnable)
            ImGui::TextWrapped("Hook fehlt? Sig in ModelChams.cpp nach Update prüfen (Log!).");
        if (!g_chams.offsetsOk)
            ImGui::TextWrapped("Kein EntitySystem? dwEntityList in Chams.cpp nachziehen.");
    }
    ImGui::End();

    // ── Aim Assist ────────────────────────────────────────────────────────
    ImGui::SetNextWindowSize(ImVec2(340, 220), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("OwOEsp Inject — Aim")) {
        ImGui::Checkbox("Aim aktivieren (ALT halten)", &g_aim.enable);
        const char* modes[] = { "Modus: Maus (extern-Stil)", "Modus: Engine (ViewAngles)" };
        ImGui::Combo("##aimmode", &g_aim.mode, modes, 2);
        ImGui::SliderFloat("Smooth", &g_aim.smooth, 1.f, 30.f);
        ImGui::SliderFloat("FOV (px)", &g_aim.fovPx, 30.f, 400.f);
        ImGui::Checkbox("Auch Mates", &g_aim.mates);
        ImGui::Separator();
        ImGui::Text("Ziele im FOV: %d", g_aim.targets);
        ImGui::Text("Status: %s", g_aim.locking ? "LOCK" : "sucht ...");
        ImGui::TextWrapped("Screen-Aim per Maus (wie externer Legit-Pfad).");
    }
    ImGui::End();

    // ── Bunny Hop ─────────────────────────────────────────────────────────
    ImGui::SetNextWindowSize(ImVec2(340, 180), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("OwOEsp Inject — Bhop")) {
        ImGui::Checkbox("Bhop aktivieren (SPACE halten)", &g_bhop.enable);
        ImGui::Checkbox("Air-Strafe-Helfer", &g_bhop.strafe);
        ImGui::Separator();
        ImGui::Text("Status: %s", g_bhop.jumping ? "SPRUNG" : (g_bhop.air ? "Luft" : "bereit"));
        ImGui::TextWrapped("Springt bei Landung, strafet in der Luft (A/D).");
    }
    ImGui::End();
}
