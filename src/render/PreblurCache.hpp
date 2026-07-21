#pragma once

#include <cstdint>
#include <optional>

namespace Render {
    struct SPreblurCacheKey {
        uint64_t sourceDescriptionId = 0;
        uint64_t outputDescriptionId = 0;

        bool     operator==(const SPreblurCacheKey&) const = default;
    };

    class CPreblurCacheState {
      public:
        void invalidate();
        void markValid(const SPreblurCacheKey& key);
        bool validFor(const SPreblurCacheKey& key) const;

      private:
        std::optional<SPreblurCacheKey> m_validKey;
    };
}
