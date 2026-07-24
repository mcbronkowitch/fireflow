#pragma once

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace bench {

// Bench setup/process rows run strictly serially. This arena makes that
// lifetime fact explicit: constructing the next row group destroys the old
// group first, then reuses one max-sized, max-aligned AXI .bss allocation.
template <class... Groups>
class SerialArena {
  public:
    static_assert(sizeof...(Groups) > 0, "SerialArena needs at least one group");

    static constexpr std::size_t capacity = [] {
        std::size_t value = 0;
        ((value = value < sizeof(Groups) ? sizeof(Groups) : value), ...);
        return value;
    }();

    static constexpr std::size_t alignment = [] {
        std::size_t value = 0;
        ((value = value < alignof(Groups) ? alignof(Groups) : value), ...);
        return value;
    }();

    SerialArena() = default;
    SerialArena(const SerialArena&) = delete;
    SerialArena& operator=(const SerialArena&) = delete;

    ~SerialArena() { reset(); }

    template <class Group, class... Args>
    Group& emplace(Args&&... args)
    {
        static_assert(
            (std::is_same_v<Group, Groups> || ...),
            "Group is not a member of this SerialArena");
        reset();
        Group* group = new (storage_) Group(std::forward<Args>(args)...);
        destroy_ = [](void* address) {
            static_cast<Group*>(address)->~Group();
        };
        return *group;
    }

    template <class Group>
    Group& get()
    {
        static_assert(
            (std::is_same_v<Group, Groups> || ...),
            "Group is not a member of this SerialArena");
        return *std::launder(reinterpret_cast<Group*>(storage_));
    }

    void reset()
    {
        if (destroy_) {
            destroy_(storage_);
            destroy_ = nullptr;
        }
    }

  private:
    alignas(alignment) unsigned char storage_[capacity];
    void (*destroy_)(void*) = nullptr;
};

} // namespace bench
