#pragma once

#include "PassElement.hpp"

#include <array>
#include <cstddef>
#include <concepts>
#include <memory>
#include <memory_resource>
#include <utility>

namespace Render {
    class CPassElementArena {
      public:
        static constexpr size_t STORAGE_SIZE = 64 * 1024;

        class CHandle {
          public:
            CHandle() = default;
            CHandle(CHandle&& other) noexcept;
            CHandle& operator=(CHandle&& other) noexcept;
            CHandle(const CHandle&)            = delete;
            CHandle& operator=(const CHandle&) = delete;
            ~CHandle();

            IPassElement* get() const;
            void          reset();
            explicit      operator bool() const;

          private:
            using CDestroyFn = void (*)(IPassElement*);

            CHandle(CPassElementArena* arena, IPassElement* element, CDestroyFn destroy);

            CPassElementArena* m_arena   = nullptr;
            IPassElement*      m_element = nullptr;
            CDestroyFn         m_destroy = nullptr;

            friend class CPassElementArena;
        };

        explicit CPassElementArena(std::pmr::memory_resource* upstream = std::pmr::get_default_resource());
        CPassElementArena(const CPassElementArena&)            = delete;
        CPassElementArena& operator=(const CPassElementArena&) = delete;
        ~CPassElementArena();

        template <typename T, typename... Args>
        CHandle emplace(Args&&... args) {
            static_assert(std::derived_from<T, IPassElement>);

            auto* const ELEMENT = std::construct_at(sc<T*>(m_resource.allocate(sizeof(T), alignof(T))), std::forward<Args>(args)...);
            ++m_liveElements;
            return CHandle{this, ELEMENT, [](IPassElement* element) { std::destroy_at(sc<T*>(element)); }};
        }

        void release();

      private:
        void                                destroy(CHandle& handle);

        std::array<std::byte, STORAGE_SIZE> m_storage;
        std::pmr::monotonic_buffer_resource m_resource;
        size_t                              m_liveElements = 0;

        friend class CHandle;
    };
}
