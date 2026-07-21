#include <render/pass/Pass.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory_resource>
#include <stdexcept>

using namespace Render;

class CCountingMemoryResource : public std::pmr::memory_resource {
  public:
    size_t allocations      = 0;
    size_t deallocations    = 0;
    size_t outstandingBytes = 0;

  private:
    void* do_allocate(size_t bytes, size_t alignment) override {
        ++allocations;
        outstandingBytes += bytes;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void* pointer, size_t bytes, size_t alignment) override {
        ++deallocations;
        outstandingBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
        return this == &other;
    }
};

class CTestPassElement : public IPassElement {
  public:
    explicit CTestPassElement(size_t& destructions) : m_destructions(destructions) {}
    ~CTestPassElement() override {
        ++m_destructions;
    }

    bool needsLiveBlur() override {
        return false;
    }

    bool needsPrecomputeBlur() override {
        return false;
    }

    const char* passName() override {
        return "CTestPassElement";
    }

    ePassElementType type() override {
        return EK_CUSTOM;
    }

  private:
    size_t& m_destructions;
};

class alignas(64) CAlignedPassElement : public CTestPassElement {
  public:
    explicit CAlignedPassElement(size_t& destructions) : CTestPassElement(destructions) {}
};

class COverflowPassElement : public CTestPassElement {
  public:
    explicit COverflowPassElement(size_t& destructions) : CTestPassElement(destructions) {}

  private:
    std::array<std::byte, CPassElementArena::STORAGE_SIZE + 1> m_payload;
};

class CThrowingPassElement : public CTestPassElement {
  public:
    explicit CThrowingPassElement(size_t& destructions) : CTestPassElement(destructions) {
        throw std::runtime_error("constructor failure");
    }
};

TEST(PassElementArena, destroysExactlyOnceAndReusesStorage) {
    CCountingMemoryResource upstream;
    CPassElementArena       arena{&upstream};
    size_t                  destructions = 0;
    IPassElement*           firstAddress = nullptr;

    for (size_t frame = 0; frame < 3; ++frame) {
        std::vector<CPassElementArena::CHandle> elements;
        for (size_t i = 0; i < 64; ++i)
            elements.emplace_back(arena.emplace<CTestPassElement>(destructions));

        if (frame == 0)
            firstAddress = elements.front().get();
        else
            EXPECT_EQ(elements.front().get(), firstAddress);

        elements.clear();
        arena.release();
    }

    EXPECT_EQ(destructions, 192U);
    EXPECT_EQ(upstream.allocations, 0U);
}

TEST(PassElementArena, respectsElementAlignment) {
    CPassElementArena arena;
    size_t            destructions = 0;
    auto              element      = arena.emplace<CAlignedPassElement>(destructions);

    EXPECT_EQ(rc<uintptr_t>(element.get()) % alignof(CAlignedPassElement), 0U);

    element.reset();
    arena.release();
    EXPECT_EQ(destructions, 1U);
}

TEST(PassElementArena, releasesOverflowOnClear) {
    CCountingMemoryResource upstream;
    CPassElementArena       arena{&upstream};
    size_t                  destructions = 0;
    auto                    element      = arena.emplace<COverflowPassElement>(destructions);

    EXPECT_GT(upstream.allocations, 0U);
    EXPECT_GT(upstream.outstandingBytes, 0U);

    element.reset();
    arena.release();

    EXPECT_EQ(destructions, 1U);
    EXPECT_EQ(upstream.outstandingBytes, 0U);
    EXPECT_EQ(upstream.allocations, upstream.deallocations);
}

TEST(PassElementArena, nestedPassSharesRootLifetime) {
    CRenderPass root;
    CRenderPass nested{root};
    size_t      destructions = 0;

    root.emplace<CTestPassElement>(destructions);
    nested.emplace<CTestPassElement>(destructions);

    nested.clear();
    EXPECT_EQ(destructions, 1U);
    EXPECT_FALSE(root.empty());

    root.clear();
    EXPECT_EQ(destructions, 2U);
}

TEST(PassElementArena, pluginOwnedElementKeepsIndependentOwnership) {
    CRenderPass pass;
    size_t      destructions = 0;

    pass.add(makeUnique<CTestPassElement>(destructions));
    EXPECT_FALSE(pass.empty());
    EXPECT_EQ(destructions, 0U);

    pass.clear();
    EXPECT_TRUE(pass.empty());
    EXPECT_EQ(destructions, 1U);
}

TEST(PassElementArena, throwingConstructorDoesNotLeaveLiveElement) {
    CRenderPass pass;
    size_t      destructions = 0;

    EXPECT_THROW(pass.emplace<CThrowingPassElement>(destructions), std::runtime_error);
    pass.clear();
    EXPECT_EQ(destructions, 1U);

    pass.emplace<CTestPassElement>(destructions);
    pass.clear();
    EXPECT_EQ(destructions, 2U);
}

TEST(PassElementArena, repeatedClearReusesRootArena) {
    CRenderPass pass;
    size_t      destructions = 0;

    for (size_t frame = 0; frame < 100; ++frame) {
        for (size_t i = 0; i < 32; ++i)
            pass.emplace<CTestPassElement>(destructions);
        pass.clear();
        EXPECT_TRUE(pass.empty());
    }

    EXPECT_EQ(destructions, 3200U);
}
