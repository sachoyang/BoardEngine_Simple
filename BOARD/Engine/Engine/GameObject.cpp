// GameObject 구현 — 컴포넌트 소유권 관리
#include "GameObject.h"

GameObject::~GameObject()
{
    for (auto* c : m_components)
        delete c;
}

void GameObject::AddComponent(Component* comp)
{
    comp->gameObject = this;
    m_components.push_back(comp);
    comp->OnAttach();
}
