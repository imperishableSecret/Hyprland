#include "PreblurCache.hpp"

void Render::CPreblurCacheState::invalidate() {
    m_validKey.reset();
}

void Render::CPreblurCacheState::markValid(const SPreblurCacheKey& key) {
    m_validKey = key;
}

bool Render::CPreblurCacheState::validFor(const SPreblurCacheKey& key) const {
    return m_validKey && *m_validKey == key;
}
