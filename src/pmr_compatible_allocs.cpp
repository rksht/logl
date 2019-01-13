#include <learnogl/pmr_compatible_allocs.h>

#include <new>

namespace eng {

static std::aligned_storage_t<sizeof(FoPmrWrapper<fo::Allocator>)> scratch_alloc_storage[1];

static std::aligned_storage_t<sizeof(FoPmrWrapper<fo::Allocator>)> malloc_alloc_storage[1];

FoPmrWrapper<fo::Allocator> &pmr_default_scratch_resource() {
    return *reinterpret_cast<FoPmrWrapper<fo::Allocator> *>(scratch_alloc_storage);
}

FoPmrWrapper<fo::Allocator> &pmr_default_resource() {
    return *reinterpret_cast<FoPmrWrapper<fo::Allocator> *>(malloc_alloc_storage);
}

void init_pmr_globals() {
    new (malloc_alloc_storage) FoPmrWrapper<fo::Allocator>(fo::memory_globals::default_allocator());
    new (scratch_alloc_storage) FoPmrWrapper<fo::Allocator>(fo::memory_globals::default_scratch_allocator());
}

void shutdown_pmr_globals() {
    pmr_default_scratch_resource().~FoPmrWrapper<fo::Allocator>();
    pmr_default_resource().~FoPmrWrapper<fo::Allocator>();
}

} // namespace eng
