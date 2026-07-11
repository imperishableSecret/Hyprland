#include <render/pass/Pass.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory_resource>

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

    void do_deallocate(void* p, size_t bytes, size_t alignment) override {
        ++deallocations;
        outstandingBytes -= bytes;
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
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

class alignas(64) CPathologicalPassElement : public CTestPassElement {
  public:
    explicit CPathologicalPassElement(size_t& destructions) : CTestPassElement(destructions) {}

  private:
    std::array<std::byte, 4096> m_payload;
};

TEST(PassElementArena, reusesRetainedStorageAndRunsDestructors) {
    CCountingMemoryResource upstream;
    CPassElementArena       arena{4096, &upstream};
    size_t                  destructions = 0;
    IPassElement*           firstAddress = nullptr;

    for (size_t frame = 0; frame < 3; ++frame) {
        std::vector<CPassElementArena::CHandle> elements;
        for (size_t i = 0; i < 8; ++i)
            elements.emplace_back(arena.emplace<CTestPassElement>(destructions));

        if (frame == 0)
            firstAddress = elements.front().get();
        else
            EXPECT_EQ(elements.front().get(), firstAddress);

        elements.clear();
        arena.release();
    }

    EXPECT_EQ(destructions, 24U);
    EXPECT_EQ(upstream.allocations, 0U);
}

TEST(PassElementArena, releasesPathologicalOverflow) {
    CCountingMemoryResource upstream;
    CPassElementArena       arena{64, &upstream};
    size_t                  destructions = 0;

    auto                    element = arena.emplace<CPathologicalPassElement>(destructions);
    EXPECT_GT(upstream.allocations, 0U);
    EXPECT_GT(upstream.outstandingBytes, 0U);

    element.reset();
    arena.release();

    EXPECT_EQ(destructions, 1U);
    EXPECT_EQ(upstream.outstandingBytes, 0U);
    EXPECT_EQ(upstream.allocations, upstream.deallocations);
}

TEST(PassElementArena, temporaryWeakPointersDoNotFreeArenaStorage) {
    CCountingMemoryResource upstream;
    CPassElementArena       arena{4096, &upstream};
    size_t                  destructions = 0;

    auto                    element = arena.emplace<CTestPassElement>(destructions);
    {
        auto weak = element.weak();
        EXPECT_EQ(weak.get(), element.get());

        auto weakCopy = weak;
        EXPECT_EQ(weakCopy.get(), element.get());
    }

    EXPECT_NE(element.get(), nullptr);
    EXPECT_EQ(destructions, 0U);

    element.reset();
    arena.release();

    EXPECT_EQ(destructions, 1U);
    EXPECT_EQ(upstream.allocations, 0U);
}

TEST(PassElementArena, nestedArenaKeepsParentStorageValid) {
    CCountingMemoryResource upstream;
    CPassElementArena       parent{4096, &upstream};
    CPassElementArena       nested{parent};
    size_t                  destructions = 0;

    auto                    parentElement = parent.emplace<CTestPassElement>(destructions);
    auto                    nestedElement = nested.emplace<CTestPassElement>(destructions);

    nestedElement.reset();
    nested.release();
    EXPECT_NE(parentElement.get(), nullptr);
    EXPECT_EQ(destructions, 1U);

    parentElement.reset();
    parent.release();
    EXPECT_EQ(destructions, 2U);
    EXPECT_EQ(upstream.allocations, 0U);
}

TEST(PassElementArena, pluginOwnedElementsKeepIndependentOwnership) {
    CRenderPass pass;
    size_t      destructions = 0;

    pass.add(makeUnique<CTestPassElement>(destructions));
    EXPECT_EQ(destructions, 0U);

    pass.clear();
    EXPECT_EQ(destructions, 1U);
}
