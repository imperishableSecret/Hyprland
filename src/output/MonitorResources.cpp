#include "MonitorResources.hpp"
#include "../managers/screenshare/ScreenshareManager.hpp"
#include "../helpers/cm/ColorManagement.hpp"
#include "../render/Renderer.hpp"
#include <algorithm>
#include <format>

using namespace Monitor;
using namespace NColorManagement;

static const int MAX_WORK_BUFFERS   = 8;
static const int MAX_UNUSED_SECONDS = 5;

CMonitorResources::CMonitorResources(WP<CMonitor> monitor, DRMFormat format, Vector2D size, NColorManagement::PImageDescription imageDescription) :
    m_stencilTex(g_pHyprRenderer->createStencilTexture(monitor->m_pixelSize.x, monitor->m_pixelSize.y)),
    m_blurFB(g_pHyprRenderer->createFB(std::format("Monitor {} blur FB", monitor->m_name))), m_monitor(monitor), m_drmFormat(format), m_size(size),
    m_imageDescription(imageDescription) {
    initFB(m_blurFB);
    monitor->m_blurFBDirty = true;
}

void CMonitorResources::initFB(SP<Render::IFramebuffer> fb) {
    fb->addStencil(m_stencilTex);
    fb->alloc(m_size.x, m_size.y, m_drmFormat);
    fb->setImageDescription(m_imageDescription);
}

void CMonitorResources::setImageDescription(NColorManagement::PImageDescription imageDescription) {
    if (m_imageDescription == imageDescription)
        return;

    m_imageDescription = imageDescription;
    ++m_imageDescriptionGeneration;
    invalidatePreblurCache();
    m_monitor->m_blurFBDirty = true;
    // m_blurFB describes completed cache contents and is relabelled only after a matching rebuild.
    for (const auto& res : m_workBuffers)
        res.buffer->setImageDescription(imageDescription);
    if (m_monitorMirrorFB)
        m_monitorMirrorFB->setImageDescription(getMirrorTexImageDescription());
    if (m_mirrorTex)
        m_mirrorTex->m_imageDescription = getMirrorTexImageDescription();
    invalidateMirrorFB();
}

Render::SPreblurCacheKey CMonitorResources::preblurCacheKey(NColorManagement::PImageDescription sourceDescription, NColorManagement::PImageDescription outputDescription) const {
    return {
        .generation          = m_imageDescriptionGeneration,
        .sourceDescriptionId = sourceDescription ? sourceDescription->id() : 0,
        .outputDescriptionId = outputDescription ? outputDescription->id() : 0,
    };
}

void CMonitorResources::invalidatePreblurCache() {
    m_preblurCacheState.invalidate();
}

bool CMonitorResources::markPreblurCacheValid(const Render::SPreblurCacheKey& key) {
    return m_preblurCacheState.markValid(key, m_imageDescriptionGeneration);
}

bool CMonitorResources::preblurCacheValid(const Render::SPreblurCacheKey& key) const {
    return m_preblurCacheState.validFor(key);
}

SP<Render::IFramebuffer> CMonitorResources::getUnusedWorkBuffer() {
    std::erase_if(m_workBuffers, [](const auto& res) { return res.lastUsed.getSeconds() >= MAX_UNUSED_SECONDS; });

    auto found = std::ranges::find_if(m_workBuffers, [](const auto& res) { return res.buffer.strongRef() < 2; });
    if (found != m_workBuffers.end()) {
        found->lastUsed.reset();
        return found->buffer;
    }
    if (m_workBuffers.size() >= MAX_WORK_BUFFERS)
        return nullptr;

    auto& res = m_workBuffers.emplace_back(g_pHyprRenderer->createFB(std::format("Monitor {} workbuffer", m_monitor->m_name)));
    initFB(res.buffer);
    res.lastUsed.reset();
    return res.buffer;
}

void CMonitorResources::forEachUnusedFB(std::function<void(SP<Render::IFramebuffer>)> callback, bool includeNamed) {
    for (const auto& res : m_workBuffers) {
        if (res.buffer.strongRef() > 1)
            continue;

        callback(res.buffer);
    }
    if (includeNamed) {
        if (m_blurFB && m_blurFB->isAllocated() && m_blurFB.strongRef() < 2)
            callback(m_blurFB);
        if (hasMirrorFB() && m_monitorMirrorFB.strongRef() < 2)
            callback(m_monitorMirrorFB);
    }
}

bool CMonitorResources::hasMirrorFB() const {
    return m_monitorMirrorFB && m_monitorMirrorFB->isAllocated();
}

bool CMonitorResources::shouldKeepMirrorFB() const {
    return !m_monitor->m_mirrors.empty() || Screenshare::mgr()->isOutputBeingSSd(m_monitor.lock());
}

void CMonitorResources::releaseMirrorFB() {
    if (m_monitorMirrorFB)
        m_monitorMirrorFB->release();

    invalidateMirrorFB();
}

void CMonitorResources::invalidateMirrorFB() {
    m_mirrorFBValid            = false;
    m_mirrorFBNeedsFullRefresh = true;
    m_mirrorFBStaleDamage.clear();
}

void CMonitorResources::markMirrorFBStale(const CRegion& damage) {
    if (damage.empty() || !hasMirrorFB() || !m_mirrorFBValid)
        return;

    auto staleDamage = damage.copy().intersect(CBox{{}, mirrorFBDamageSize()});
    m_mirrorFBStaleDamage.add(staleDamage);

    auto sourceDamage = staleDamage.copy().intersect(m_mirrorSourceDamage);
    m_mirrorDamageJournal.record(sourceDamage);
    m_mirrorSourceDamage.subtract(staleDamage);
}

void CMonitorResources::markMirrorFBStale() {
    if (!hasMirrorFB() || !m_mirrorFBValid)
        return;

    m_mirrorDamageJournal.record(m_mirrorSourceDamage);
    m_mirrorSourceDamage.clear();
    m_mirrorFBNeedsFullRefresh = true;
    m_mirrorFBStaleDamage.clear();
}

void CMonitorResources::markMirrorFBUpdated(const CRegion& damage) {
    auto updatedDamage = damage.copy().intersect(CBox{{}, mirrorFBDamageSize()});
    auto sourceDamage  = updatedDamage.copy().intersect(m_mirrorSourceDamage);
    m_mirrorDamageJournal.record(sourceDamage);
    m_mirrorSourceDamage.subtract(updatedDamage);

    m_mirrorFBValid            = true;
    m_mirrorFBNeedsFullRefresh = false;
    m_mirrorFBStaleDamage.clear();
}

void CMonitorResources::markMirrorSourceDamage(const CRegion& damage) {
    m_mirrorSourceDamage.add(damage).intersect(CBox{{}, mirrorFBDamageSize()});
}

CRegion CMonitorResources::pendingMirrorFBDamage() const {
    const auto DAMAGE_SIZE = mirrorFBDamageSize();
    if (!hasMirrorFB() || !m_mirrorFBValid || m_mirrorFBNeedsFullRefresh)
        return CRegion{0, 0, DAMAGE_SIZE.x, DAMAGE_SIZE.y};

    return m_mirrorFBStaleDamage.copy();
}

uint64_t CMonitorResources::mirrorDamageGeneration() const {
    return m_mirrorDamageJournal.generation();
}

SMirrorDamageSnapshot CMonitorResources::mirrorDamageSince(uint64_t generation) const {
    const auto DAMAGE_SIZE = mirrorFBDamageSize();
    if (!hasMirrorFB() || !m_mirrorFBValid || m_mirrorFBNeedsFullRefresh)
        return {
            .generation = m_mirrorDamageJournal.generation(),
            .damage     = CRegion{0, 0, DAMAGE_SIZE.x, DAMAGE_SIZE.y},
            .fullDamage = true,
        };

    return m_mirrorDamageJournal.damageSince(generation, CRegion{0, 0, DAMAGE_SIZE.x, DAMAGE_SIZE.y});
}

SP<Render::IFramebuffer> CMonitorResources::mirrorFB() {
    if (!m_monitorMirrorFB)
        m_monitorMirrorFB = g_pHyprRenderer->createFB(std::format("Monitor {} mirror FB", m_monitor->m_name));

    if (!m_monitorMirrorFB->isAllocated()) {
        m_monitorMirrorFB->alloc(m_size.x, m_size.y, m_monitor->m_activeMonitorRule.m_enable10bit ? DRM_FORMAT_XRGB2101010 : DRM_FORMAT_XRGB8888);
        m_monitorMirrorFB->setImageDescription(getMirrorTexImageDescription());
    }

    return m_monitorMirrorFB;
}

SP<Render::ITexture> CMonitorResources::getMirrorTexture() {
    return hasMirrorFB() ? mirrorFB()->getTexture() : nullptr;
}

NColorManagement::PImageDescription CMonitorResources::getMirrorTexImageDescription() {
    const auto TF = m_imageDescription->value().transferFunction;
    if (TF == CM_TRANSFER_FUNCTION_GAMMA22 || TF == CM_TRANSFER_FUNCTION_SRGB)
        return m_imageDescription;

    return DEFAULT_SRGB_IMAGE_DESCRIPTION;
}

Vector2D CMonitorResources::mirrorFBDamageSize() const {
    return m_monitor->m_transformedSize;
}

void CMonitorResources::enableMirror() {
    if (m_mirrorTex)
        return;
    m_mirrorTex = g_pHyprRenderer->createTexture();
    m_mirrorTex->allocate({m_size.x, m_size.y}, m_monitor->m_activeMonitorRule.m_enable10bit ? DRM_FORMAT_XRGB2101010 : DRM_FORMAT_XRGB8888);
    m_mirrorTex->m_imageDescription = getMirrorTexImageDescription();
    m_monitor->m_blurFBDirty        = true;
}

void CMonitorResources::disableMirror() {
    if (m_mirrorTex)
        m_monitor->m_blurFBDirty = true;
    m_mirrorTex.reset();
}
