#include "MirrorDamageJournal.hpp"

#include <algorithm>

using namespace Monitor;

CMirrorDamageJournal::CMirrorDamageJournal(size_t historyLimit) : m_historyLimit(std::max<size_t>(historyLimit, 1)) {
    ;
}

uint64_t CMirrorDamageJournal::record(const CRegion& damage) {
    if (damage.empty())
        return m_generation;

    m_history.emplace_back(SEntry{.generation = ++m_generation, .damage = damage.copy()});
    while (m_history.size() > m_historyLimit)
        m_history.pop_front();

    return m_generation;
}

uint64_t CMirrorDamageJournal::generation() const {
    return m_generation;
}

SMirrorDamageSnapshot CMirrorDamageJournal::damageSince(uint64_t generation, const CRegion& fullDamage) const {
    SMirrorDamageSnapshot snapshot{.generation = m_generation};

    if (generation == m_generation)
        return snapshot;

    if (generation > m_generation || m_history.empty() || generation + 1 < m_history.front().generation) {
        snapshot.damage = fullDamage.copy();
        return snapshot;
    }

    for (const auto& entry : m_history) {
        if (entry.generation <= generation)
            continue;

        snapshot.damage.add(entry.damage);
    }

    return snapshot;
}
