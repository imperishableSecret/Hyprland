#pragma once

#include <cstdint>
#include <optional>

namespace Render {
    bool preblurNeedsRebuild(bool cacheValid, int remainingFullFrames);

    struct SPreblurCacheKey {
        uint64_t generation          = 0;
        uint64_t sourceDescriptionId = 0;
        uint64_t outputDescriptionId = 0;

        bool     operator==(const SPreblurCacheKey&) const = default;
    };

    class CPreblurCacheState {
      public:
        void invalidate();
        bool markValid(const SPreblurCacheKey& key, uint64_t currentGeneration);
        bool validFor(const SPreblurCacheKey& key) const;

      private:
        std::optional<SPreblurCacheKey> m_validKey;
    };
}
