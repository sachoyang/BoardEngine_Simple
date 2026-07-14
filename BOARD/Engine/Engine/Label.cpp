// Label 컴포넌트 구현 — Inspector 편집 + JSON 직렬화
// 실제 화면 렌더는 Engine::RenderObjectLabels() 가 담당한다(카메라 투영이 필요하므로).
#include "Label.h"
#include "imgui.h"
#include <cstring>

void Label::ImGuiRender()
{
    char buf[256] = {};
    strncpy_s(buf, text.c_str(), sizeof(buf) - 1);
    if (ImGui::InputText("Text", buf, sizeof(buf)))
        text = buf;
    ImGui::ColorEdit4("Text Color", &color.x);
    ImGui::DragFloat("Text Size", &size, 0.5f, 4.0f, 200.0f);
}

nlohmann::json Label::Serialize() const
{
    // text 가 비어 있으면 저장할 의미가 없으므로 빈 JSON 반환 → SaveScene 에서 자동 제외.
    if (text.empty())
        return {};

    return {
        {"type",  GetName()},
        {"text",  text},
        {"color", { color.x, color.y, color.z, color.w }},
        {"size",  size}
    };
}

void Label::Deserialize(const nlohmann::json& j)
{
    text = j.value("text", std::string());
    size = j.value("size", 24.0f);
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4)
    {
        color.x = j["color"][0].get<float>();
        color.y = j["color"][1].get<float>();
        color.z = j["color"][2].get<float>();
        color.w = j["color"][3].get<float>();
    }
}
