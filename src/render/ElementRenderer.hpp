#pragma once

#include "./pass/BorderPassElement.hpp"
#include "./pass/ClearPassElement.hpp"
#include "./pass/FramebufferElement.hpp"
#include "./pass/PreBlurElement.hpp"
#include "./pass/RectPassElement.hpp"
#include "./pass/RendererHintsPassElement.hpp"
#include "./pass/ShadowPassElement.hpp"
#include "./pass/SurfacePassElement.hpp"
#include "./pass/TexPassElement.hpp"
#include "./pass/TextureMatteElement.hpp"
#include "./pass/InnerGlowPassElement.hpp"
#include "./pass/TransformedWindowPassElement.hpp"
#include <hyprutils/math/Region.hpp>

namespace Render {
    class IElementRenderer {
      public:
        IElementRenderer()          = default;
        virtual ~IElementRenderer() = default;

        void drawElement(IPassElement& element, const CRegion& damage);
        void drawElement(WP<IPassElement> element, const CRegion& damage);

      protected:
        virtual void draw(CBorderPassElement& element, const CRegion& damage)    = 0;
        virtual void draw(CClearPassElement& element, const CRegion& damage)     = 0;
        virtual void draw(CFramebufferElement& element, const CRegion& damage)   = 0;
        virtual void draw(CPreBlurElement& element, const CRegion& damage)       = 0;
        virtual void draw(CRectPassElement& element, const CRegion& damage)      = 0;
        virtual void draw(CShadowPassElement& element, const CRegion& damage)    = 0;
        virtual void draw(CInnerGlowPassElement& element, const CRegion& damage) = 0;
        virtual void draw(CTexPassElement& element, const CRegion& damage)       = 0;
        virtual void draw(CTextureMatteElement& element, const CRegion& damage)  = 0;

      private:
        void calculateUVForSurface(PHLWINDOW, SP<CWLSurfaceResource>, PHLMONITOR pMonitor, bool main = false, const Vector2D& projSize = {}, const Vector2D& projSizeUnscaled = {},
                                   bool fixMisalignedFSV1 = false);

        void drawRect(CRectPassElement& element, const CRegion& damage);
        void drawHints(CRendererHintsPassElement& element, const CRegion& damage);
        void drawPreBlur(CPreBlurElement& element, const CRegion& damage);
        void drawClear(CClearPassElement& element, const CRegion& damage);
        void drawSurface(CSurfacePassElement& element, const CRegion& damage);
        void preDrawSurface(CSurfacePassElement& element, const CRegion& damage);
        void drawTex(CTexPassElement& element, const CRegion& damage);
        void drawTexMatte(CTextureMatteElement& element, const CRegion& damage);
        void drawTransformedWindow(CTransformedWindowPassElement& element, const CRegion& damage);
        void drawCustom(IPassElement& element, const CRegion& damage);
    };
}
