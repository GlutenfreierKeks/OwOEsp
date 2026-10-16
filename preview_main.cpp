// Standalone UI preview â€” shows the modular menu (ui/) without CS2.
// Run OwOEspPreview.exe -> menu opens on INSERT, ESC exits.
#include "pch.h"
#include "ui/Menu.h"
#include "ui/MenuShared.h"
#include "ui/MenuAssets.h"

#ifdef _WIN32

// â”€â”€ D3D11 plumbing â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
static ID3D11Device*           g_dev    = nullptr;
static ID3D11DeviceContext*    g_ctx    = nullptr;
static IDXGISwapChain*         g_swap   = nullptr;
static ID3D11RenderTargetView* g_rtv    = nullptr;
static HWND                    g_hwnd   = nullptr;
static int                     g_prevW   = 1440;
static int                     g_prevH   = 960;

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return 0;
    switch (msg) {
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static bool CreateDeviceD3D() {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = g_prevW;
    sd.BufferDesc.Height = g_prevH;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
        D3D11_SDK_VERSION, &sd, &g_swap, &g_dev, nullptr, &g_ctx);
    if (FAILED(hr)) return false;

    ID3D11Texture2D* back = nullptr;
    g_swap->GetBuffer(0, IID_PPV_ARGS(&back));
    g_dev->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
    return g_rtv != nullptr;
}

static void CleanupDeviceD3D() {
    if (g_rtv)  { g_rtv->Release();  g_rtv  = nullptr; }
    if (g_swap) { g_swap->Release(); g_swap = nullptr; }
    if (g_ctx)  { g_ctx->Release();  g_ctx  = nullptr; }
    if (g_dev)  { g_dev->Release();  g_dev  = nullptr; }
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
int APIENTRY wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    g_menuOpen = true; // Preview startet mit offenem Menue

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"OwOEspPreview";
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"OwOEsp - UI Preview (INSERT = Menue, ESC = Ende)",
                             WS_OVERLAPPEDWINDOW, 200, 100, g_prevW, g_prevH, nullptr, nullptr, inst, nullptr);
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);

    if (!CreateDeviceD3D()) { MessageBoxA(g_hwnd, "D3D11 Fehler!", "Preview", MB_ICONERROR); return 1; }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    InitMenu(g_dev);                 // style + fonts + assets (before backend init)
    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_dev, g_ctx);

    LoadConfig();
    printf("[*] Preview: INSERT = Menue offen/zu, ESC = Ende\n");

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) running = false;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!running) break;

        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();

        if (ImGui::IsKeyPressed(ImGuiKey_Insert)) g_menuOpen = !g_menuOpen;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) running = false;

        if (g_menuOpen) RenderMenu();

        ImGui::Render();
        float clear[4] = { 0.10f, 0.10f, 0.12f, 1.f };
        g_ctx->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_ctx->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swap->Present(1, 0);
    }

    SaveConfig();
    ReleaseMenuAssets();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    CoUninitialize();
    return 0;
}

#else // ── Linux: GLFW + OpenGL3 ──────────────────────────────────────────────
#include <GLFW/glfw3.h>
#include <GL/gl.h>
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"

int main() {
    g_menuOpen = true;

    if (!glfwInit()) {
        fprintf(stderr, "[Preview] glfwInit fehlgeschlagen\n");
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* win = glfwCreateWindow(1440, 960,
        "OwOEsp - UI Preview (INSERT = Menue, ESC = Ende)", nullptr, nullptr);
    if (!win) {
        fprintf(stderr, "[Preview] Fenster fehlgeschlagen\n");
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 130");
    InitMenu(); // style + fonts + assets (Kontext ist current)

    LoadConfig();
    printf("[*] Preview: INSERT = Menue offen/zu, ESC = Ende\n");

    bool running = true;
    while (running) {
        if (glfwWindowShouldClose(win)) running = false;
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        if (ImGui::IsKeyPressed(ImGuiKey_Insert)) g_menuOpen = !g_menuOpen;
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) running = false;

        if (g_menuOpen) RenderMenu();

        ImGui::Render();
        int w = 0, h = 0;
        glfwGetFramebufferSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.10f, 0.10f, 0.12f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(win);
    }

    SaveConfig();
    ReleaseMenuAssets();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
#endif // _WIN32

