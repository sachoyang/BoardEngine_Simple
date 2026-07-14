// 오브젝트 위치에 텍스트를 겹쳐 그리는 Label 컴포넌트
// 엔진이 ImGui 배경 드로우리스트(폰트 아틀라스 재사용)로 렌더하며 카메라 이동/줌을 따라간다.
// 스프라이트만으로 종류 구분이 어려운 게임(체스의 P·N·B·R·Q·K 등)에서
// SpriteRenderer 위에 글자를 얹어 종류를 표시할 때 사용한다.
// D3D12 파이프라인을 건드리지 않으므로 렌더 비용·리스크가 낮다.
#pragma once
#include "Component.h"
#include <string>
#include <DirectXMath.h>

class Label : public Component
{
public:
    std::string       text;                       // 표시할 문자열 (비어 있으면 그리지 않음)
    DirectX::XMFLOAT4 color = { 1, 1, 1, 1 };      // 글자 색 (RGBA, 0~1)
    float             size  = 24.0f;              // zoom 1.0 기준 글자 크기(픽셀)

    void ImGuiRender() override;
    const char* GetName() const override { return "Label"; }

    nlohmann::json Serialize()                    const override;
    void           Deserialize(const nlohmann::json& j) override;
};
