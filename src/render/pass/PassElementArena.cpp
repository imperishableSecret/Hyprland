#include "PassElementArena.hpp"

#include "../../macros.hpp"

using namespace Render;

CPassElementArena::CHandle::CHandle(CPassElementArena* arena, IPassElement* element, CDestroyFn destroy) : m_arena(arena), m_element(element), m_destroy(destroy) {}

CPassElementArena::CHandle::CHandle(CHandle&& other) noexcept {
    std::swap(m_arena, other.m_arena);
    std::swap(m_element, other.m_element);
    std::swap(m_destroy, other.m_destroy);
}

CPassElementArena::CHandle& CPassElementArena::CHandle::operator=(CHandle&& other) noexcept {
    if (this == &other)
        return *this;

    reset();
    std::swap(m_arena, other.m_arena);
    std::swap(m_element, other.m_element);
    std::swap(m_destroy, other.m_destroy);
    return *this;
}

CPassElementArena::CHandle::~CHandle() {
    reset();
}

IPassElement* CPassElementArena::CHandle::get() const {
    return m_element;
}

void CPassElementArena::CHandle::reset() {
    if (m_arena)
        m_arena->destroy(*this);
}

CPassElementArena::CHandle::operator bool() const {
    return m_element;
}

CPassElementArena::CPassElementArena(std::pmr::memory_resource* upstream) : m_resource(m_storage.data(), m_storage.size(), upstream) {}

CPassElementArena::~CPassElementArena() {
    release();
}

void CPassElementArena::release() {
    RASSERT(m_liveElements == 0, "Pass element arena released with {} live elements", m_liveElements);
    m_resource.release();
}

void CPassElementArena::destroy(CHandle& handle) {
    RASSERT(handle.m_arena == this && handle.m_element && handle.m_destroy, "Invalid pass element arena handle");

    handle.m_destroy(handle.m_element);
    --m_liveElements;

    handle.m_arena   = nullptr;
    handle.m_element = nullptr;
    handle.m_destroy = nullptr;
}
