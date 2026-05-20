// 컴포넌트들을 소유하고 관리하는 씬 오브젝트 컨테이너
#pragma once
#include "Component.h"
#include <vector>
#include <string>

class GameObject
{
public:
    std::string name   = "GameObject";

    // 기물 종류 식별자. "Pawn", "Knight", "Rook" 등 게임에 맞게 정의해 사용한다.
    std::string tag    = "Untagged";

    // 팀/소속 식별자. 0 = 중립(타일), 1 = Player 1, 2 = Player 2.
    int         teamID = 0;

    ~GameObject();

    // comp의 소유권을 넘겨받고 gameObject 포인터를 설정한다.
    void AddComponent(Component* comp);

    // T 타입 컴포넌트를 dynamic_cast로 찾아 반환한다. 없으면 nullptr.
    template<typename T>
    T* GetComponent() const
    {
        for (auto* c : m_components)
            if (auto* t = dynamic_cast<T*>(c))
                return t;
        return nullptr;
    }

    const std::vector<Component*>& GetComponents() const { return m_components; }

private:
    std::vector<Component*> m_components;
};
