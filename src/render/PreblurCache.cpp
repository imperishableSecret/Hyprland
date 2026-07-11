#include "PreblurCache.hpp"

bool Render::preblurNeedsRebuild(bool cacheValid, int remainingFullFrames) {
    return !cacheValid || remainingFullFrames > 0;
}

void Render::CPreblurCacheState::invalidate() {
    m_validKey.reset();
}

bool Render::CPreblurCacheState::markValid(const SPreblurCacheKey& key, uint64_t currentGeneration) {
    if (key.generation != currentGeneration) {
        invalidate();
        return false;
    }

    m_validKey = key;
    return true;
}

bool Render::CPreblurCacheState::validFor(const SPreblurCacheKey& key) const {
    return m_validKey && *m_validKey == key;
}
