#pragma once

#include "PassElement.hpp"

#include <cstddef>
#include <concepts>
#include <memory>
#include <memory_resource>
#include <utility>
#include <vector>

namespace Render {
    class CPassElementArena {
      public:
        class CHandle {
          public:
            CHandle() = default;
            CHandle(CHandle&& other) noexcept;
            CHandle& operator=(CHandle&& other) noexcept;
            CHandle(const CHandle&)            = delete;
            CHandle& operator=(const CHandle&) = delete;
            ~CHandle();

            IPassElement*    get() const;
            WP<IPassElement> weak() const;
            void             reset();
            explicit         operator bool() const;

          private:
            using CDestroyFn = void (*)(IPassElement*);

            CHandle(CPassElementArena* arena, IPassElement* element, Hyprutils::Memory::Impl_::impl_base* control, CDestroyFn destroy);

            CPassElementArena*                   m_arena   = nullptr;
            IPassElement*                        m_element = nullptr;
            Hyprutils::Memory::Impl_::impl_base* m_control = nullptr;
            CDestroyFn                           m_destroy = nullptr;

            friend class CPassElementArena;
        };

        explicit CPassElementArena(size_t retainedBytes = 64 * 1024, std::pmr::memory_resource* upstream = std::pmr::get_default_resource());
        explicit CPassElementArena(CPassElementArena& parent);
        CPassElementArena(const CPassElementArena&)            = delete;
        CPassElementArena& operator=(const CPassElementArena&) = delete;
        ~CPassElementArena();

        template <typename T, typename... Args>
        CHandle emplace(Args&&... args) {
            static_assert(std::derived_from<T, IPassElement>);

            auto* const                          ELEMENT = std::construct_at(sc<T*>(m_resource->allocate(sizeof(T), alignof(T))), std::forward<Args>(args)...);
            Hyprutils::Memory::Impl_::impl_base* control = nullptr;

            try {
                control = std::construct_at(
                    sc<Hyprutils::Memory::Impl_::impl_base*>(m_resource->allocate(sizeof(Hyprutils::Memory::Impl_::impl_base), alignof(Hyprutils::Memory::Impl_::impl_base))),
                    ELEMENT, [](void*) { ; }, false);
                // CWeakPointer deletes its control block when the last weak reference is
                // released. Keep one arena-owned sentinel reference so temporary weak
                // pointers cannot call delete on control blocks stored in this arena.
                control->incWeak();
            } catch (...) {
                std::destroy_at(ELEMENT);
                throw;
            }

            ++m_liveElements;
            return CHandle{this, ELEMENT, control, [](IPassElement* element) { std::destroy_at(sc<T*>(element)); }};
        }

        void release();

      private:
        void                                    destroy(CHandle& handle);

        std::vector<std::byte>                  m_retainedStorage;
        UP<std::pmr::monotonic_buffer_resource> m_ownedResource;
        std::pmr::memory_resource*              m_resource     = nullptr;
        size_t                                  m_liveElements = 0;

        friend class CHandle;
    };
}
