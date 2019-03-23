// The types and functions of this file is only going to be available from C++17 onwards.

// @rksht - Replace scaffold's allocators completely with these ones. Right now, only the scratch allocator is
// somewhat usable and makes sense.

#pragma once

#include <scaffold/memory.h>
#include <string>
#include <vector>

#if __has_include(<memory_resource>)

#include <memory_resource>

namespace pmr = std::pmr;

// #pragma message("stdlib has <memory_resource>")
#else

#include <halpern_pmr/polymorphic_allocator.h>

namespace pmr = halpern::pmr;

#endif

namespace eng {

template <typename T> class FoPmrWrapper : public pmr::memory_resource {
  public:
    static_assert(std::is_base_of<fo::Allocator, T>::value || std::is_same<fo::Allocator, T>::value, "");

    FoPmrWrapper(T &allocator)
        : _allocator(&allocator) {}

    FoPmrWrapper(const FoPmrWrapper &o)
        : _allocator(o._allocator) {}

    FoPmrWrapper(FoPmrWrapper &&o)
        : _allocator(o._allocator) {
        o._allocator = nullptr;
    }

    FoPmrWrapper &operator=(const FoPmrWrapper &o) {
        _allocator = o._allocator;
        return *this;
    }

    FoPmrWrapper &operator=(FoPmrWrapper &&o) {
        _allocator = o._allocator;
        o._allocator = nullptr;
    }

  protected:
    virtual void *do_allocate(std::size_t bytes, std::size_t alignment) override {
        return _allocator->allocate(bytes, alignment);
    }

    virtual void do_deallocate(void *p, std::size_t bytes, std::size_t alignment) override {
        (void)bytes;
        (void)alignment;
        _allocator->deallocate(p);
    }

    virtual bool do_is_equal(const pmr::memory_resource &other) const noexcept override {
        auto o = dynamic_cast<const FoPmrWrapper<T> *>(&other);
        if (o != nullptr) {
            return o->_allocator == _allocator;
        }
        return false;
    }

  private:
    T *_allocator = nullptr;
};

template <typename T> inline FoPmrWrapper<T> make_pmr_wrapper(T &allocator) {
    static_assert(std::is_base_of<fo::Allocator, T>::value, "");
    return FoPmrWrapper<T>(allocator);
}

void init_pmr_globals();
void shutdown_pmr_globals();

FoPmrWrapper<fo::Allocator> &pmr_default_resource();
FoPmrWrapper<fo::Allocator> &pmr_default_scratch_resource();

inline auto scratch_string(const char *init = nullptr) {
    using alloc_type = pmr::polymorphic_allocator<char>;
    using string_type = std::basic_string<char, std::char_traits<char>, alloc_type>;

    alloc_type a(&pmr_default_scratch_resource());

    if (init) {
        return string_type(init, a);
    } else {
        return string_type(a);
    }
}

template <typename T>
inline std::vector<T, pmr::polymorphic_allocator<T>> scratch_vector(std::initializer_list<T> init = {}) {
    using alloc_type = pmr::polymorphic_allocator<T>;
    using vec_type = std::vector<T, alloc_type>;
    alloc_type a(&pmr_default_scratch_resource());
    return vec_type(init, a);
}

#define PMR_ALLOC(type) pmr::polymorphic_allocator<type>

// Creates an std Allocator from given memory_resource by wrapping it in a polymorphic_allocator
template <typename ObjectType> auto make_std_alloc(pmr::memory_resource &memory_resource) {
    return pmr::polymorphic_allocator<ObjectType>(&memory_resource);
}

} // namespace eng

// Helper macro that declares a pmr vector member with an member-initializer
#define DECL_PMR_VECTOR_MEMBER(T, variable_name, ref_memory_resource)                                        \
    pmr::vector<T> variable_name { make_std_alloc<T>(ref_memory_resource) }
