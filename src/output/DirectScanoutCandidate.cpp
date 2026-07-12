#include "DirectScanoutCandidate.hpp"

bool Monitor::directScanoutSnapshotMatches(const SDirectScanoutSnapshot& expected, const SDirectScanoutSnapshot& current) {
    return current.dmaBuffer && expected == current;
}
