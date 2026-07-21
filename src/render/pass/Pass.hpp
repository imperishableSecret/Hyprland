#pragma once

#include "../../defines.hpp"
#include "PassElementArena.hpp"
#include "PassElement.hpp"

class CGradientValueData;

namespace Render {
    class ITexture;

    class CRenderPass {
      public:
        CRenderPass();
        explicit CRenderPass(CRenderPass& parent);
        ~CRenderPass();

        bool empty() const;
        bool single() const;

        void add(UP<IPassElement>&& elem);
        template <typename T, typename... Args>
        T& emplace(Args&&... args) {
            auto        handle  = m_elementArena->emplace<T>(std::forward<Args>(args)...);
            auto* const ELEMENT = sc<T*>(handle.get());
            m_passElements.emplace_back(SPassElementData{.elementDamage = CRegion{}, .arenaElement = std::move(handle)});
            return *ELEMENT;
        }
        void    clear();
        void    removeAllOfType(const std::string& type);

        CRegion render(const CRegion& damage_);

      private:
        CRegion              m_damage;
        std::vector<CRegion> m_occludedRegions;
        CRegion              m_totalLiveBlurRegion;

        struct SPassElementData {
            CRegion                    elementDamage;
            CPassElementArena::CHandle arenaElement;
            UP<IPassElement>           ownedElement;
            bool                       discard = false;

            IPassElement*              element() const;
            void                       reset();
        };

        std::vector<SPassElementData> m_passElements;
        UP<CPassElementArena>         m_ownedElementArena;
        CPassElementArena*            m_elementArena = nullptr;

        void                          simplify(bool willBlur, const CRegion& liveBlurRegion);
        float                         oneBlurRadius();
        void                          renderDebugData();

        struct {
            bool         present = false;
            SP<ITexture> keyboardFocusText, pointerFocusText, lastWindowText;
        } m_debugData;

        friend class CHyprOpenGLImpl;
    };
}
