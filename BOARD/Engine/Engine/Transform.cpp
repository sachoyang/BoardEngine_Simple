// Transform 컴포넌트 구현 — Model 행렬 계산 및 ImGui 속성 편집
#include "Transform.h"
#include "imgui.h"

using namespace DirectX;

XMMATRIX Transform::GetModelMatrix() const
{
    return XMMatrixScaling(width, height, 1.0f)
         * XMMatrixRotationZ(rotation)
         * XMMatrixTranslation(x, y, 0.0f);
}

XMMATRIX Transform::GetMVP(const XMMATRIX& viewProj) const
{
    return GetModelMatrix() * viewProj;
}

void Transform::ImGuiRender()
{
    ImGui::DragFloat("X",        &x,        1.0f);
    ImGui::DragFloat("Y",        &y,        1.0f);
    ImGui::DragFloat("Width",    &width,    1.0f, 1.0f, 2000.0f);
    ImGui::DragFloat("Height",   &height,   1.0f, 1.0f, 2000.0f);
    ImGui::DragFloat("Rotation", &rotation, 0.01f);
}

nlohmann::json Transform::Serialize() const
{
    return {
        {"type",     GetName()},
        {"x",        x},
        {"y",        y},
        {"width",    width},
        {"height",   height},
        {"rotation", rotation}
    };
}

void Transform::Deserialize(const nlohmann::json& j)
{
    x        = j.value("x",        0.0f);
    y        = j.value("y",        0.0f);
    width    = j.value("width",  100.0f);
    height   = j.value("height", 100.0f);
    rotation = j.value("rotation", 0.0f);
}
