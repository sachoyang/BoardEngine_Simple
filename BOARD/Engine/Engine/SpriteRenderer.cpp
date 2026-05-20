// SpriteRenderer 컴포넌트 구현 — 텍스처 설정 및 ImGui 속성 편집
#include "SpriteRenderer.h"
#include "ResourceManager.h"
#include "imgui.h"

void SpriteRenderer::SetTexture(const std::string& path)
{
    m_texturePath    = path;
    m_textureHandle  = ResourceManager::Instance().LoadTexture(path);
}

void SpriteRenderer::ImGuiRender()
{
    ImGui::Checkbox("Visible", &visible);
    if (!m_texturePath.empty())
        ImGui::Text("Texture: %s", m_texturePath.c_str());
    ImGui::ColorEdit4("Color Tint", &colorTint.x);
}

nlohmann::json SpriteRenderer::Serialize() const
{
    return {
        {"type",    GetName()},
        {"visible", visible},
        {"texture", m_texturePath},
        {"color",   {colorTint.x, colorTint.y, colorTint.z, colorTint.w}}
    };
}

void SpriteRenderer::Deserialize(const nlohmann::json& j)
{
    visible = j.value("visible", true);
    std::string path = j.value("texture", std::string{});
    if (!path.empty())
        SetTexture(path);
    auto c = j.value("color", std::vector<float>{1,1,1,1});
    if (c.size() >= 4)
        colorTint = { c[0], c[1], c[2], c[3] };
}
