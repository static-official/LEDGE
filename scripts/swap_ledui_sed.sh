#!/bin/bash
# One-shot ImGui → ledui conversion across LEDGE sources.
set -e
cd /home/z/my-project
FILES=$(grep -rl "ImFont\|ImVec\|ImGui\|ImDraw" ledgevr/native ledgevr-ios/native_ios ledgevr-ios/shim 2>/dev/null)

for f in $FILES; do
  sed -i \
    -e 's/#include <imgui.h>/#include "ledui\/ledui.h"/' \
    -e 's/ImGui_ImplNewFrame/uiNewFrame/g' \
    -e 's/ImGuiWindowFlags_/ledui::WinFlags::/g' \
    -e 's/ImGuiCol_/ledui::Col_/g' \
    -e 's/ImDrawCornerFlags_/ledui::Corner::/g' \
    -e 's/ImGui::/ledui::/g' \
    -e 's/ImFontAtlas/ledui::FontAtlas/g' \
    -e 's/ImFontConfig/ledui::FontConfig/g' \
    -e 's/ImFontGlyph/ledui::Glyph/g' \
    -e 's/\bImFont\b/ledui::Font/g' \
    -e 's/ImDrawVert/ledui::DrawVert/g' \
    -e 's/ImDrawIdx/ledui::DrawIdx/g' \
    -e 's/ImDrawList/ledui::DrawList/g' \
    -e 's/ImDrawCmd/ledui::DrawCmd/g' \
    -e 's/ImDrawData/ledui::DrawData/g' \
    -e 's/ImTextureID/ledui::TextureID/g' \
    -e 's/\bImWchar\b/unsigned int/g' \
    -e 's/\bImVec2\b/ledui::Vec2/g' \
    -e 's/\bImVec4\b/ledui::Vec4/g' \
    -e 's/g->AdvanceX/g->XAdvance/g' \
    -e 's/if (!g || g->Visible == 0) continue;/if (!g) continue;/' \
    "$f"
  echo "swapped: $f"
done

# renderer.cpp: glyph loop starts at 32 up to 0x2FFF — cap at 127 for ledui's ASCII atlas
sed -i 's/for (int cp = 32; cp < 0x2FFF; cp++)/for (int cp = 32; cp <= 127; cp++)/' ledgevr/native/renderer.cpp
echo "renderer glyph range capped"
