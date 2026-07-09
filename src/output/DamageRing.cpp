#include "DamageRing.hpp"

#include <cstdint>
#include <vector>

namespace {
    constexpr int     MAX_DAMAGE_RECTS_WITHOUT_AREA_CHECK = 8;
    constexpr int     MAX_SPARSE_DAMAGE_RECTS             = 32;
    constexpr int64_t COALESCE_AREA_RATIO_NUMERATOR       = 3;
    constexpr int64_t COALESCE_AREA_RATIO_DENOMINATOR     = 2;

    int64_t           boxArea(const pixman_box32_t& box) {
        if (box.x2 <= box.x1 || box.y2 <= box.y1)
            return 0;

        return static_cast<int64_t>(box.x2 - box.x1) * static_cast<int64_t>(box.y2 - box.y1);
    }

    int64_t rectsArea(const std::vector<pixman_box32_t>& rects) {
        int64_t area = 0;

        for (const auto& rect : rects) {
            area += boxArea(rect);
        }

        return area;
    }

    bool shouldCoalesceDamage(const CRegion& damage, const std::vector<pixman_box32_t>& rects) {
        const int64_t EXACT_AREA = rectsArea(rects);
        if (EXACT_AREA <= 0)
            return false;

        const int64_t EXTENTS_AREA = boxArea(damage.pixman()->extents);

        return EXTENTS_AREA * COALESCE_AREA_RATIO_DENOMINATOR <= EXACT_AREA * COALESCE_AREA_RATIO_NUMERATOR;
    }
}

using namespace Monitor;

void CDamageRing::setSize(const Vector2D& size_) {
    if (size_ == m_size)
        return;

    m_size = size_;

    damageEntire();
}

bool CDamageRing::damage(const CBox& box) {
    if (m_size.x <= 0 || m_size.y <= 0 || box.w <= 0 || box.h <= 0)
        return false;

    return damage(CRegion{box});
}

bool CDamageRing::damage(const CRegion& rg) {
    if (m_size.x <= 0 || m_size.y <= 0)
        return false;

    CRegion clipped = rg.copy().intersect(CBox{{}, m_size});
    if (clipped.empty())
        return false;

    m_current.add(clipped);
    m_lastDamageTime = hrc::now();
    return true;
}

void CDamageRing::damageEntire() {
    damage(CBox{{}, m_size});
}

void CDamageRing::rotate() {
    m_previousIdx = (m_previousIdx + DAMAGE_RING_PREVIOUS_LEN - 1) % DAMAGE_RING_PREVIOUS_LEN;

    m_previous[m_previousIdx] = m_current;
    m_current.clear();
    m_lastRotationTime = hrc::now();
}

CRegion CDamageRing::getBufferDamage(int age) {
    if (m_size.x <= 0 || m_size.y <= 0)
        return {};

    if (age <= 0 || age > DAMAGE_RING_PREVIOUS_LEN + 1)
        return CBox{{}, m_size};

    CRegion damage = m_current;

    for (int i = 0; i < age - 1; ++i) {
        int j = (m_previousIdx + i) % DAMAGE_RING_PREVIOUS_LEN;
        damage.add(m_previous.at(j));
    }

    // Don't return a ludicrous amount of rects, but avoid collapsing sparse
    // damage into a much larger extents redraw.
    const int RECT_COUNT = pixman_region32_n_rects(damage.pixman());
    if (RECT_COUNT <= MAX_DAMAGE_RECTS_WITHOUT_AREA_CHECK)
        return damage;

    if (RECT_COUNT > MAX_SPARSE_DAMAGE_RECTS)
        return damage.getExtents();

    const auto RECTS = damage.getRects();
    if (shouldCoalesceDamage(damage, RECTS))
        return damage.getExtents();

    return damage;
}

bool CDamageRing::hasChanged() {
    return !m_current.empty();
}

CDamageRing::hrc::time_point CDamageRing::lastDamageTime() const {
    return m_lastDamageTime;
}

CDamageRing::hrc::time_point CDamageRing::lastRotationTime() const {
    return m_lastRotationTime;
}
