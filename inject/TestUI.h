#pragma once
// inject/TestUI.h — simples Test-Menü für die Inject-Version (v1).
// Absichtlich klein: beweist Hook + Render + Input, ohne Feature-Logik.

struct InjectTestState {
    bool showMenu = true;
    bool showDemo = false;
    float testSlider = 50.0f;
    float testColor[3] = { 0.65f, 0.35f, 1.0f };
    int testCombo = 0;
    char testText[64] = { 0 };
    double fps = 0.0;
};

extern InjectTestState g_test;
extern const char* kBuildTag;

// Zeichnet das Test-Fenster. Muss zwischen ImGui::NewFrame() und Render() laufen.
void DrawInjectTestUI();
