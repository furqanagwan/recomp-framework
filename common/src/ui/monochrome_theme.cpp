#include "recomp/ui/monochrome_theme.h"

#include <imgui.h>

#include <rex/ui/style.h>

namespace recomp {

namespace {

constexpr ImVec4 kBlack{0.00f, 0.00f, 0.00f, 1.00f};
constexpr ImVec4 kPanel{0.04f, 0.04f, 0.04f, 1.00f};
constexpr ImVec4 kRaised{0.12f, 0.12f, 0.12f, 1.00f};
constexpr ImVec4 kHovered{0.22f, 0.22f, 0.22f, 1.00f};
constexpr ImVec4 kPressed{0.32f, 0.32f, 0.32f, 1.00f};
constexpr ImVec4 kWhite{1.00f, 1.00f, 1.00f, 1.00f};
constexpr ImVec4 kSoftWhite{0.85f, 0.85f, 0.85f, 1.00f};
constexpr ImVec4 kGrey{0.55f, 0.55f, 0.55f, 1.00f};
constexpr ImVec4 kHairline{1.00f, 1.00f, 1.00f, 0.25f};
constexpr ImVec4 kTransparent{0.00f, 0.00f, 0.00f, 0.00f};

void ApplyColors(ImGuiStyle& style) {
  auto& colors = style.Colors;
  colors[ImGuiCol_Text] = kWhite;
  colors[ImGuiCol_TextDisabled] = kGrey;
  colors[ImGuiCol_WindowBg] = kBlack;
  colors[ImGuiCol_ChildBg] = kBlack;
  colors[ImGuiCol_PopupBg] = kPanel;
  colors[ImGuiCol_Border] = kHairline;
  colors[ImGuiCol_BorderShadow] = kTransparent;
  colors[ImGuiCol_FrameBg] = kRaised;
  colors[ImGuiCol_FrameBgHovered] = kHovered;
  colors[ImGuiCol_FrameBgActive] = kHovered;
  colors[ImGuiCol_TitleBg] = kBlack;
  colors[ImGuiCol_TitleBgActive] = kBlack;
  colors[ImGuiCol_TitleBgCollapsed] = kBlack;
  colors[ImGuiCol_MenuBarBg] = kPanel;
  colors[ImGuiCol_ScrollbarBg] = kBlack;
  colors[ImGuiCol_ScrollbarGrab] = kRaised;
  colors[ImGuiCol_ScrollbarGrabHovered] = kHovered;
  colors[ImGuiCol_ScrollbarGrabActive] = kGrey;
  colors[ImGuiCol_CheckMark] = kWhite;
  colors[ImGuiCol_SliderGrab] = kSoftWhite;
  colors[ImGuiCol_SliderGrabActive] = kWhite;
  colors[ImGuiCol_Button] = kRaised;
  colors[ImGuiCol_ButtonHovered] = kHovered;
  colors[ImGuiCol_ButtonActive] = kPressed;
  colors[ImGuiCol_Header] = kRaised;
  colors[ImGuiCol_HeaderHovered] = kHovered;
  colors[ImGuiCol_HeaderActive] = kHovered;
  colors[ImGuiCol_Separator] = kHairline;
  colors[ImGuiCol_SeparatorHovered] = kSoftWhite;
  colors[ImGuiCol_SeparatorActive] = kWhite;
  colors[ImGuiCol_ResizeGrip] = kRaised;
  colors[ImGuiCol_ResizeGripHovered] = kSoftWhite;
  colors[ImGuiCol_ResizeGripActive] = kWhite;
  colors[ImGuiCol_Tab] = kRaised;
  colors[ImGuiCol_TabHovered] = kHovered;
  colors[ImGuiCol_TabSelected] = kPressed;
  colors[ImGuiCol_TabSelectedOverline] = kWhite;
  colors[ImGuiCol_TabDimmed] = kRaised;
  colors[ImGuiCol_TabDimmedSelected] = kHovered;
  colors[ImGuiCol_TabDimmedSelectedOverline] = kSoftWhite;
  colors[ImGuiCol_PlotLines] = kSoftWhite;
  colors[ImGuiCol_PlotLinesHovered] = kWhite;
  colors[ImGuiCol_PlotHistogram] = kWhite;
  colors[ImGuiCol_PlotHistogramHovered] = kWhite;
  colors[ImGuiCol_TextSelectedBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.30f);
  colors[ImGuiCol_NavCursor] = kWhite;
  colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.90f);
}

void ApplyMetrics(ImGuiStyle& style) {
  style.WindowRounding = 2.0f;
  style.FrameRounding = 2.0f;
  style.PopupRounding = 2.0f;
  style.TabRounding = 2.0f;
  style.WindowBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.WindowPadding = ImVec2(16.0f, 14.0f);
  style.FramePadding = ImVec2(10.0f, 6.0f);
  style.ItemSpacing = ImVec2(10.0f, 8.0f);
}

void ApplyOverlayAccents(rex::ui::Style& overlay) {
  overlay.achievements.header_text = kWhite;
  overlay.achievements.progress_bar = kWhite;
  overlay.achievements.row_unlocked_bg = ImVec4(1.0f, 1.0f, 1.0f, 0.08f);
  overlay.toast.title = kWhite;
  overlay.toast.background_alpha = 1.0f;
  overlay.debug.muted_text = ImVec4(1.0f, 1.0f, 1.0f, 0.55f);
}

}

void MonochromeTheme::Apply(ImGuiStyle& imgui_style, rex::ui::Style& overlay_style) {
  ImGui::StyleColorsDark(&imgui_style);
  ApplyColors(imgui_style);
  ApplyMetrics(imgui_style);
  ApplyOverlayAccents(overlay_style);
}

}
