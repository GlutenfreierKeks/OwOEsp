#include "pch.h"
#include "MenuAssets.h"
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#ifdef _WIN32
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#else
#define STB_IMAGE_IMPLEMENTATION
#include "../platform/stb_image.h"
#include <GL/gl.h>
#endif

OwO_Tex g_texLogo = nullptr;
OwO_Tex g_texFoto = nullptr;
#ifdef _WIN32
ID3D11ShaderResourceView* g_texBg = nullptr;
#endif

#ifdef _WIN32
namespace {

OwO_Tex LoadPng(const wchar_t* path, ID3D11Device* device) {
    if (!device || !path || !*path) return nullptr;

    HRESULT hr;
    ID3D11ShaderResourceView* srv = nullptr;
    IWICImagingFactory* factory = nullptr;
    IWICBitmapDecoder*  decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter*  conv  = nullptr;

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                          IID_PPV_ARGS(&factory));
    if (FAILED(hr)) return nullptr;

    hr = factory->CreateDecoderFromFilename(path, nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnLoad, &decoder);
    if (FAILED(hr)) goto cleanup;

    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) goto cleanup;

    hr = factory->CreateFormatConverter(&conv);
    if (FAILED(hr)) goto cleanup;

    hr = conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                          WICBitmapDitherTypeNone, nullptr, 0.f,
                          WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) goto cleanup;

    {
        UINT w = 0, h = 0;
        conv->GetSize(&w, &h);
        if (w == 0 || h == 0) goto cleanup;

        std::vector<uint8_t> pixels((size_t)w * h * 4);
        conv->CopyPixels(nullptr, w * 4, (UINT)pixels.size(), pixels.data());

        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd{};
        sd.pSysMem = pixels.data();
        sd.SysMemPitch = w * 4;

        ID3D11Texture2D* tex = nullptr;
        hr = device->CreateTexture2D(&td, &sd, &tex);
        if (SUCCEEDED(hr) && tex) {
            device->CreateShaderResourceView(tex, nullptr, &srv);
            tex->Release();
        }
    }

cleanup:
    if (conv)    conv->Release();
    if (frame)   frame->Release();
    if (decoder) decoder->Release();
    if (factory) factory->Release();
    return srv;
}

std::wstring ExeDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring path(buf);
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) path.resize(slash);
    return path;
}

} // namespace

void LoadMenuAssets(ID3D11Device* device) {
    std::wstring dir = ExeDir() + L"\\assets\\";
    g_texLogo = LoadPng((dir + L"logo.png").c_str(), device);
    g_texFoto = LoadPng((dir + L"avatar.png").c_str(), device);
}

void ReleaseMenuAssets() {
    if (g_texLogo) { g_texLogo->Release(); g_texLogo = nullptr; }
    if (g_texFoto) { g_texFoto->Release(); g_texFoto = nullptr; }
}
#else // ── Linux: stb_image + OpenGL (Kontext muss current sein) ──────────────
namespace {

OwO_Tex LoadPng(const char* path) {
    if (!path || !*path) return nullptr;
    int w = 0, h = 0, n = 0;
    // 4 Kanäle erzwingen (RGBA, wie WIC-BGRA-Pfad optisch äquivalent nach Swizzle:
    // stb liefert RGBA; GL_RGBA-Upload 1:1).
    unsigned char* px = stbi_load(path, &w, &h, &n, 4);
    if (!px || w <= 0 || h <= 0) {
        if (px) stbi_image_free(px);
        printf("[assets] PNG fehlt/ungültig: %s\n", path);
        return nullptr;
    }
    unsigned int tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(px);
    return (OwO_Tex)(uintptr_t)tex;
}

} // namespace

void LoadMenuAssets() {
    std::string dir = OwO_ExeDir() + "assets/";
    g_texLogo = LoadPng((dir + "logo.png").c_str());
    g_texFoto = LoadPng((dir + "avatar.png").c_str());
}

void ReleaseMenuAssets() {
    if (g_texLogo) {
        unsigned int tex = (unsigned int)(uintptr_t)g_texLogo;
        glDeleteTextures(1, &tex);
        g_texLogo = nullptr;
    }
    if (g_texFoto) {
        unsigned int tex = (unsigned int)(uintptr_t)g_texFoto;
        glDeleteTextures(1, &tex);
        g_texFoto = nullptr;
    }
}
#endif
