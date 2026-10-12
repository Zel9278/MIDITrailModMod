#pragma once

#include <imgui.h>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11SamplerState;
struct ID3D11Buffer;

IMGUI_IMPL_API bool ImGui_ImplDX11_Init(ID3D11Device*, ID3D11DeviceContext*);
IMGUI_IMPL_API void ImGui_ImplDX11_Shutdown();
IMGUI_IMPL_API void ImGui_ImplDX11_NewFrame();
IMGUI_IMPL_API void ImGui_ImplDX11_RenderDrawData(ImDrawData*);
IMGUI_IMPL_API bool ImGui_ImplDX11_CreateDeviceObjects();
IMGUI_IMPL_API void ImGui_ImplDX11_InvalidateDeviceObjects();
IMGUI_IMPL_API void ImGui_ImplDX11_UpdateTexture(ImTextureData* texture);

struct ImGui_ImplDX11_RenderState {
    ID3D11Device* Device;
    ID3D11DeviceContext* DeviceContext;
    ID3D11SamplerState* SamplerLinear;
    ID3D11SamplerState* SamplerNearest;
    ID3D11Buffer* VertexConstantBuffer;
};
