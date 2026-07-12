#include "ScanoutGPUCache.hpp"

using namespace Monitor;

bool SScanoutGPUIdentity::valid() const {
    return allocator != 0 && outputBackend != 0 && compositorBackend != 0 && compositorGeneration != 0 && allocatorFD >= 0 && compositorFD >= 0;
}

eScanoutGPUCacheResult CScanoutGPUCache::lookup(const SScanoutGPUIdentity& identity) const {
    if (!identity.valid())
        return eScanoutGPUCacheResult::MULTI_GPU;

    if (!m_identity || !m_topology || *m_identity != identity)
        return eScanoutGPUCacheResult::UNKNOWN;

    return m_topology->sameGPU ? eScanoutGPUCacheResult::SINGLE_GPU : eScanoutGPUCacheResult::MULTI_GPU;
}

void CScanoutGPUCache::store(const SScanoutGPUIdentity& identity, const SScanoutGPUTopology& topology) {
    if (!identity.valid()) {
        invalidate();
        return;
    }

    m_identity = identity;
    m_topology = topology;
}

void CScanoutGPUCache::invalidate() {
    m_identity.reset();
    m_topology.reset();
}
