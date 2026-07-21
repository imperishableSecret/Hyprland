#pragma once

#include "../ElementRenderer.hpp"

namespace Render::GL {
    class CGLElementRenderer : public Render::IElementRenderer {
      public:
        CGLElementRenderer()  = default;
        ~CGLElementRenderer() = default;

      private:
        void draw(CBorderPassElement& element, const Hyprutils::Math::CRegion& damage) override;
        void draw(CClearPassElement& element, const CRegion& damage) override;
        void draw(CFramebufferElement& element, const CRegion& damage) override;
        void draw(CPreBlurElement& element, const CRegion& damage) override;
        void draw(CRectPassElement& element, const CRegion& damage) override;
        void draw(CShadowPassElement& element, const CRegion& damage) override;
        void draw(CInnerGlowPassElement& element, const CRegion& damage) override;
        void draw(CTexPassElement& element, const CRegion& damage) override;
        void draw(CTextureMatteElement& element, const CRegion& damage) override;
    };
}
