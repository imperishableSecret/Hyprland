#include "PassElementArena.hpp"

#include "../../macros.hpp"

using namespace Render;

CPassElementArena::CHandle::CHandle(CPassElementArena* arena, IPassElement* element, Hyprutils::Memory::Impl_::impl_base* control, CDestroyFn destroy) :
    m_arena(arena), m_element(element), m_control(control), m_destroy(destroy) {}

CPassElementArena::CHandle::CHandle(CHandle&& other) noexcept {
    std::swap(m_arena, other.m_arena);
    std::swap(m_element, other.m_element);
    std::swap(m_control, other.m_control);
    std::swap(m_destroy, other.m_destroy);
}

CPassElementArena::CHandle& CPassElementArena::CHandle::operator=(CHandle&& other) noexcept {
    if (this == &other)
        return *this;

    reset();
    std::swap(m_arena, other.m_arena);
    std::swap(m_element, other.m_element);
    std::swap(m_control, other.m_control);
    std::swap(m_destroy, other.m_destroy);
    return *this;
}

CPassElementArena::CHandle::~CHandle() {
    reset();
}

IPassElement* CPassElementArena::CHandle::get() const {
    return m_element;
}

WP<IPassElement> CPassElementArena::CHandle::weak() const {
    return {m_control, m_element};
}

void CPassElementArena::CHandle::reset() {
    if (m_arena)
        m_arena->destroy(*this);
}

CPassElementArena::CHandle::operator bool() const {
    return m_element;
}

CPassElementArena::CPassElementArena(size_t retainedBytes, std::pmr::memory_resource* upstream) : m_retainedStorage(retainedBytes) {
    m_ownedResource = makeUnique<std::pmr::monotonic_buffer_resource>(m_retainedStorage.data(), m_retainedStorage.size(), upstream);
    m_resource      = m_ownedResource.get();
}

CPassElementArena::CPassElementArena(CPassElementArena& parent) : m_resource(parent.m_resource) {}

CPassElementArena::~CPassElementArena() {
    release();
}

void CPassElementArena::release() {
    RASSERT(m_liveElements == 0, "Pass element arena released with {} live elements", m_liveElements);

    if (m_ownedResource)
        m_ownedResource->release();
}

void CPassElementArena::destroy(CHandle& handle) {
    RASSERT(handle.m_arena == this && handle.m_element && handle.m_control && handle.m_destroy, "Invalid pass element arena handle");
    RASSERT(handle.m_control->wref() == 1 && handle.m_control->ref() == 0, "Arena-backed pass elements cannot outlive their render pass");

    handle.m_destroy(handle.m_element);
    handle.m_control->decWeak();
    std::destroy_at(handle.m_control);
    --m_liveElements;

    handle.m_arena   = nullptr;
    handle.m_element = nullptr;
    handle.m_control = nullptr;
    handle.m_destroy = nullptr;
}
