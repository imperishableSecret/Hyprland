#pragma once

#include "Monitor.hpp"
#include "MirrorDamageJournal.hpp"
#include "../helpers/Format.hpp"
#include "../helpers/time/Timer.hpp"
#include "../render/Framebuffer.hpp"
#include "../render/PreblurCache.hpp"
#include <hyprutils/math/Vector2D.hpp>
#include <vector>

namespace Monitor {
    class CMonitorResources {
      public:
        CMonitorResources(WP<CMonitor> monitor, DRMFormat format, Vector2D size, NColorManagement::PImageDescription imageDescription);

        SP<Render::IFramebuffer> getUnusedWorkBuffer();
        Render::SPreblurCacheKey preblurCacheKey(NColorManagement::PImageDescription sourceDescription, NColorManagement::PImageDescription outputDescription) const;
        void                     invalidatePreblurCache();
        bool                     markPreblurCacheValid(const Render::SPreblurCacheKey& key);
        bool                     preblurCacheValid(const Render::SPreblurCacheKey& key) const;
        void                     forEachUnusedFB(std::function<void(SP<Render::IFramebuffer>)> callback, bool includeNamed = false);
        bool                     hasMirrorFB() const;
        bool                     shouldKeepMirrorFB() const;
        void                     releaseMirrorFB();
        void                     invalidateMirrorFB();
        void                     markMirrorFBStale(const CRegion& damage);
        void                     markMirrorFBStale();
        void                     markMirrorFBUpdated(const CRegion& damage);
        void                     markMirrorSourceDamage(const CRegion& damage);
        CRegion                  pendingMirrorFBDamage() const;
        uint64_t                 mirrorDamageGeneration() const;
        SMirrorDamageSnapshot    mirrorDamageSince(uint64_t generation) const;
        void                     enableMirror();
        void                     disableMirror();
        SP<Render::IFramebuffer> mirrorFB();
        SP<Render::ITexture>     getMirrorTexture();
        SP<Render::ITexture>     m_mirrorTex;

        SP<Render::ITexture>     m_stencilTex; // TODO fix blur ignore alpha and remove
        SP<Render::IFramebuffer> m_blurFB;

      private:
        void                                initFB(SP<Render::IFramebuffer> fb);
        void                                setImageDescription(NColorManagement::PImageDescription imageDescription);
        NColorManagement::PImageDescription getMirrorTexImageDescription();
        Vector2D                            mirrorFBDamageSize() const;

        struct SResource {
            SP<Render::IFramebuffer> buffer;
            CTimer                   lastUsed;
        };

        SP<Render::IFramebuffer>            m_monitorMirrorFB;
        CRegion                             m_mirrorFBStaleDamage;
        CRegion                             m_mirrorSourceDamage;
        CMirrorDamageJournal                m_mirrorDamageJournal;
        WP<CMonitor>                        m_monitor;
        DRMFormat                           m_drmFormat;
        Vector2D                            m_size;
        NColorManagement::PImageDescription m_imageDescription;
        bool                                m_mirrorFBValid              = false;
        bool                                m_mirrorFBNeedsFullRefresh   = true;
        uint64_t                            m_imageDescriptionGeneration = 1;
        Render::CPreblurCacheState          m_preblurCacheState;

        std::vector<SResource>              m_workBuffers;

        friend class CMonitor;
    };
}
