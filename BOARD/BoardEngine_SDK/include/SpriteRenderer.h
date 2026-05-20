// 스프라이트(텍스처 사각형)를 화면에 그리는 SpriteRenderer 컴포넌트
#pragma once
#include "Component.h"
#include <string>
#include <DirectXMath.h>

struct TextureHandle;  // ResourceManager.h 의 전방 선언 — SpriteRenderer.h 를 가볍게 유지

class SpriteRenderer : public Component
{
public:
    bool visible   = true;

    // 마우스가 오브젝트 위에 올라가 있으면 true. 매 프레임 Engine 이 갱신한다.
    // Render() 에서 colorTint 에 +0.3 보정을 더해 시각적 하이라이트를 표현한다.
    bool isHovered = false;

    // 텍스처 픽셀에 곱해지는 색상 배율. 기본값 흰색(1,1,1,1) = 원본 그대로.
    // Play Mode 에서 GameLogic 이 이 값을 바꿔 타일 색을 변경한다.
    DirectX::XMFLOAT4 colorTint = { 1.0f, 1.0f, 1.0f, 1.0f };

    // ResourceManager::LoadTexture 를 호출하고 핸들을 캐싱한다.
    void SetTexture(const std::string& path);

    // Engine 의 렌더 루프가 이 핸들로 SetGraphicsRootDescriptorTable 을 호출한다.
    const TextureHandle* GetTextureHandle() const { return m_textureHandle; }

    // DLL 스크립트가 동일 텍스처로 새 SpriteRenderer 를 만들 때 사용한다.
    const std::string& GetTexturePath() const { return m_texturePath; }

    void ImGuiRender() override;
    const char* GetName() const override { return "SpriteRenderer"; }

    nlohmann::json Serialize()                    const override;
    void           Deserialize(const nlohmann::json& j) override;

private:
    const TextureHandle* m_textureHandle = nullptr;
    std::string          m_texturePath;
};
