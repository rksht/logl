#pragma once

#include <learnogl/gl_misc.h>
#include <scaffold/pool_allocator.h>

// Don't need this.

#if 0

// Allocate static VBOs from a global vbo.
struct VBO_Heap {
    // Contains info about a chunk of the underlying buffer that is *free*. The free list is a linked-list of
    // these structures.
    struct Chunk_Info {
        u32 bits;
        u32 offset; // Offset of this chunk in the buffer
        Chunk_Info *next_free;
    };

    using chunk_is_allocated_mask = Mask32<0, 1>;
    using chunk_size_mask = Mask32<1, 30>;

    fo::PoolAllocator _chunk_info_allocator;
    eng::BufferDeleter _vbo_deleter;
    u32 _size = 0;
    u32 _total_bytes_allocated = 0;

    Chunk_Info *_first_free_chunk = nullptr;
    Chunk_Info *_first_allocated_chunk = nullptr;

    VBO_Heap() = delete;

    VBO_Heap(u32 total_size, u32 average_allocation_size)
        : CTOR_INIT_FIELD(_chunk_info_allocator, sizeof(Chunk_Info), clip_to_pow2(total_size) / average_allocation_size) {
        _chunk_info_allocator.set_name("VBO_Heap_Chunk_Info_alloc");

        _size = clip_to_pow2(total_size);
    }

    void allocate_vbo(GLuint usage = GL_STATIC_DRAW) {
        GLuint buffer;
        glGenBuffers(1, &buffer);
        _vbo_deleter.set(buffer);

        glBindBuffer(GL_ARRAY_BUFFER, buffer);
        glBufferData(GL_ARRAY_BUFFER, _size, nullptr, usage);

        Chunk_Info first = {};
        first.bits = set_masks(chunk_is_allocated_mask(0), chunk_size_mask(_size));
        first.next_free = nullptr;

        _first_free_chunk = fo::make_new<Chunk_Info>(_chunk_info_allocator, first);
    }

    struct Alloc_Result {
        GLuint vbo = 0;
        u32 offset = 0;
        u32 size = 0;
        u32 alloc_size = 0;

        bool invalid() const { return vbo == 0; }
    };

    // Allocate a vertex buffer region. Returns an Alloc_Result. The allocator does not itself store the
    // offset of the returned region. But I will always be needing it on the command submit side anyway so I
    // can store that myself.
    Alloc_Result allocate_vbo_region(u32 size) {
        u32 alloc_size = size;
        {
            u32 mod = size % 64;
            if (mod) {
                alloc_size = size + (64 - mod);
            }
        }

        Chunk_Info *chunk = _first_free_chunk;
        Chunk_Info *prev_chunk = nullptr;

        while (chunk != nullptr) {
            u32 chunk_size = chunk_size_mask::extract(chunk->bits);
            if (chunk_size >= size) {
                break;
            }
            chunk = chunk->next_free;
        }

        if (chunk == nullptr) {
            LOG_F(ERROR, "Could not allocate a vertex buffer of size %u out of this vbo heap", size);
            return Alloc_Result();
        }

        // Split chunk if difference in actual size is >= 2 * size
        _split_chunk(chunk, prev_chunk, alloc_size);

        _total_bytes_allocated += alloc_size;

        Alloc_Result result;
        result.vbo = _vbo_deleter.handle();
        result.offset = chunk->offset;
        result.size = size;
        result.alloc_size = alloc_size;

        // Delete the chunk that just got allocated and possibly split from
        fo::make_delete(_chunk_info_allocator, chunk);

        return result;
    }

    // Splits given chunk if size of chunk > allocation_size. Returns offset of given chunk
    void _split_chunk(Chunk_Info *chunk, Chunk_Info *prev_chunk, u32 allocation_size) {
        u32 chunk_size = chunk_size_mask::extract(chunk->bits);

        if (chunk_size == allocation_size) {
            if (prev_chunk) {
                prev_chunk->next_free = chunk->next_free;
            } else {
                _first_free_chunk = chunk->next_free;
            }
        }

        // So chunk_size > actual_size

        Chunk_Info *free_chunk = fo::make_new<Chunk_Info>(_chunk_info_allocator);
        free_chunk->bits =
            set_masks(chunk_is_allocated_mask(0), chunk_size_mask(allocation_size - chunk_size));
        free_chunk->offset = chunk->offset + allocation_size;
        free_chunk->next_free = chunk->next_free;

        if (prev_chunk) {
            prev_chunk->next_free = free_chunk;
        } else {
            _first_free_chunk = free_chunk;
        }
    }

    // @rksht - Support deallocation and merging of consecutive free chunk
};

#endif
