#include "ScreenshareDamage.hpp"

using namespace Screenshare;

bool Screenshare::captureNeedsFullDamage(bool isFirst, bool overlayCursor, bool monitorCapture) {
    return isFirst || overlayCursor || !monitorCapture;
}

SFrozenCaptureDamage Screenshare::freezeCaptureDamage(Monitor::SMirrorDamageSnapshot snapshot, const Vector2D& bufferSize, const Vector2D& transformedSize,
                                                      wl_output_transform transform, bool fullDamage) {
    fullDamage = fullDamage || snapshot.fullDamage;

    SFrozenCaptureDamage frozen{
        .generation = snapshot.generation,
        .full       = fullDamage,
    };

    if (fullDamage) {
        frozen.bufferDamage  = CRegion{0, 0, bufferSize.x, bufferSize.y};
        frozen.monitorDamage = CRegion{0, 0, transformedSize.x, transformedSize.y};
        return frozen;
    }

    frozen.monitorDamage = snapshot.damage.copy();
    frozen.bufferDamage  = std::move(snapshot.damage);
    frozen.bufferDamage.transform(Math::wlTransformToHyprutils(Math::invertTransform(transform)), transformedSize.x, transformedSize.y);
    frozen.bufferDamage.intersect(0, 0, bufferSize.x, bufferSize.y);
    return frozen;
}

uint64_t Screenshare::consumedCaptureGeneration(bool fullDamage, uint64_t frozenGeneration, uint64_t copyGeneration) {
    return fullDamage ? copyGeneration : frozenGeneration;
}
