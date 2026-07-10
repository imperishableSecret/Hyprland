#include <render/pass/Pass.hpp>
#include <render/pass/PreBlurElement.hpp>
#include <render/pass/TexPassElement.hpp>

#include <gtest/gtest.h>

using namespace Render;

class CRequirementPassElement : public IPassElement {
  public:
    CRequirementPassElement(bool liveBlur, bool precomputedBlur, ePassElementType type = EK_RECT) : m_liveBlur(liveBlur), m_precomputedBlur(precomputedBlur), m_type(type) {}

    bool needsLiveBlur() override {
        return m_liveBlur;
    }

    bool needsPrecomputeBlur() override {
        return m_precomputedBlur;
    }

    const char* passName() override {
        return "CRequirementPassElement";
    }

    ePassElementType type() override {
        return m_type;
    }

  private:
    bool             m_liveBlur        = false;
    bool             m_precomputedBlur = false;
    ePassElementType m_type            = EK_UNKNOWN;
};

TEST(PassRequirements, eligiblePassHasNoReasons) {
    CRenderPass pass;
    pass.add(makeUnique<CRequirementPassElement>(false, false));

    const auto REQUIREMENTS = pass.requirements();
    EXPECT_FALSE(REQUIREMENTS.requiresOffscreen());
    EXPECT_EQ(REQUIREMENTS.reasons(), RPR_NONE);
}

TEST(PassRequirements, reportsBlurAndTransformerReasons) {
    CRenderPass pass;
    pass.add(makeUnique<CRequirementPassElement>(true, false));
    pass.add(makeUnique<CRequirementPassElement>(false, true));
    pass.add(makeUnique<CRequirementPassElement>(false, false, EK_TRANSFORMED_WINDOW));
    pass.add(makeUnique<CPreBlurElement>());
    pass.add(makeUnique<CTexPassElement>(CTexPassElement::SRenderData{.blur = true}));

    const auto REQUIREMENTS = pass.requirements();
    EXPECT_TRUE(REQUIREMENTS.has(RPR_LIVE_BLUR));
    EXPECT_TRUE(REQUIREMENTS.has(RPR_PRECOMPUTED_BLUR));
    EXPECT_TRUE(REQUIREMENTS.has(RPR_WINDOW_TRANSFORMER));
}

TEST(PassRequirements, reportsEveryExternalReason) {
    CRenderPass pass;
    const auto  REQUIREMENTS = pass.requirements({
        .outputCopy        = true,
        .screenShader      = true,
        .colorConversion   = true,
        .zoom              = true,
        .outputTransform   = true,
        .backendConstraint = true,
    });

    EXPECT_TRUE(REQUIREMENTS.has(RPR_OUTPUT_COPY));
    EXPECT_TRUE(REQUIREMENTS.has(RPR_SCREEN_SHADER));
    EXPECT_TRUE(REQUIREMENTS.has(RPR_COLOR_CONVERSION));
    EXPECT_TRUE(REQUIREMENTS.has(RPR_ZOOM));
    EXPECT_TRUE(REQUIREMENTS.has(RPR_OUTPUT_TRANSFORM));
    EXPECT_TRUE(REQUIREMENTS.has(RPR_BACKEND_CONSTRAINT));
}

TEST(PassRequirements, uncertainElementTypesRequireOffscreen) {
    for (const auto TYPE : {EK_UNKNOWN, EK_FRAMEBUFFER, EK_TEXTURE_MATTE, EK_CUSTOM}) {
        CRenderPass pass;
        pass.add(makeUnique<CRequirementPassElement>(false, false, TYPE));

        EXPECT_TRUE(pass.requirements().has(RPR_BACKEND_CONSTRAINT));
    }
}
