#include "recomp/ui/dialog_layout.h"

#include <algorithm>

#include <imgui.h>

namespace recomp {

namespace {

constexpr ImGuiWindowFlags kFixedPanelFlags =
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoSavedSettings;

}

void DialogLayout::DrawBackdrop(const char* id, const ImGuiIO& io, float opacity) {
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(io.DisplaySize);
  ImGui::SetNextWindowBgAlpha(opacity);
  ImGui::Begin(id, nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus |
                   ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav);
  ImGui::End();
}

void DialogLayout::BeginCenteredPanel(const char* title, const ImGuiIO& io, float max_width, bool* open) {
  ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f), ImGuiCond_Always,
                          ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(std::min(max_width, io.DisplaySize.x - 40.0f), 0.0f));
  ImGui::Begin(title, open, kFixedPanelFlags);
}

void DialogLayout::BeginSidePanel(const char* id, const ImGuiIO& io, float max_width) {
  ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowSize(ImVec2(std::min(max_width, io.DisplaySize.x * 0.8f), io.DisplaySize.y));
  ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | kFixedPanelFlags);
}

bool DialogLayout::BackPressed() {
  return ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Escape, false);
}

}
