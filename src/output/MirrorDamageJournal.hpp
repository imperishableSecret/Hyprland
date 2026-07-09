#pragma once

#include "../helpers/math/Math.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>

namespace Monitor {
    constexpr size_t MIRROR_DAMAGE_HISTORY_LIMIT = 32;

    struct SMirrorDamageSnapshot {
        uint64_t generation = 0;
        CRegion  damage;
        bool     fullDamage = false;
    };

    class CMirrorDamageJournal {
      public:
        explicit CMirrorDamageJournal(size_t historyLimit = MIRROR_DAMAGE_HISTORY_LIMIT);

        uint64_t              record(const CRegion& damage);
        uint64_t              generation() const;
        SMirrorDamageSnapshot damageSince(uint64_t generation, const CRegion& fullDamage) const;

      private:
        struct SEntry {
            uint64_t generation = 0;
            CRegion  damage;
        };

        size_t             m_historyLimit = MIRROR_DAMAGE_HISTORY_LIMIT;
        uint64_t           m_generation   = 0;
        std::deque<SEntry> m_history;
    };
}
