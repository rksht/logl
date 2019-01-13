#pragma once

#include <learnogl/gl_misc.h>

namespace ringbuffer {

enum class AppenderAndConsumer { UNSPECIFIED = 0, CPU_GPU, GPU_CPU };

struct GPUConsumeState {
    // Number of elements the GPU will consume max
    u32 num_elements_max = 0;

    // The sync object that will get signaled when the consume is done
    GLsync sync = nullptr;

    GPUConsumeState(u32 num_elements_max, GLsync sync)
        : num_elements_max(num_elements_max)
        , sync(sync) {}

    void reset() {
        num_elements_max = 0;
        sync = nullptr;
    }
};

template <typename T> struct PersistentRingBuffer : public NonCopyable {

    static_assert(std::is_trivially_copy_assignable<T>::value,
                  "Only supports trivially copy-assignable elements");

    static_assert(std::is_trivially_destructible<T>::value, "");

    struct ChunkExtent {
        T *first_chunk;
        T *second_chunk;
        GLuint first_chunk_extent;
        GLuint second_chunk_extent;
    };

    // Pointer to mapped memory
    T *_pointer = nullptr;

    // Handle
    GLuint _handle;

    // Current number of items in buffer
    GLuint _size;

    // Current start offset of buffer
    GLuint _offset;

    // Total capacity of the buffer
    GLuint _capacity;

    AppenderAndConsumer _appender_consumer = AppenderAndConsumer::UNSPECIFIED;

    GPUConsumeState _cur_consume_state;

    GLsync _sync_object;

    // A fence to denote that append can continue

    void init(AppenderAndConsumer appender_consumer, GLuint buffer_type, u32 capacity) {
        GLuint flags = GL_MAP_PERSISTENT_BIT | GL_MAP_COHERENT_BIT;

        _appender_consumer = appender_consumer;

        if (_appender_consumer == AppenderAndConsumer::CPU_GPU) {
            flags |= GL_MAP_WRITE_BIT;
        } else if (_appender_consumer == AppenderAndConsumer::GPU_CPU) {
            flags |= GL_MAP_READ_BIT;
        } else {
            flags |= GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
        }

        _size = 0;
        _offset = 0;
        _capacity = capacity;

        _handle = logl_internal::make_buffer(buffer_type, flags, _capacity, nullptr);
        _pointer = glMapBufferRange(buffer_type, 0, _capacity, flags);
    }

    GLuint space() const { return _capacity - _size; }

    ChunkExtent get_extent(GLuint start, GLuint count) const {
        ChunkExtent e;

        uint32_t first_chunk_start = (_offset + start) % _capacity;
        uint32_t first_chunk_end = first_chunk_start + count;

        e.first_chunk = _pointer + first_chunk_start;

        if (first_chunk_end > _capacity) {
            e.first_chunk_size = _capacity - first_chunk_start;
            e.second_chunk = _pointer;
            e.second_chunk_size = count - e.first_chunk_size;
        } else {
            e.first_chunk_size = count;
            e.second_chunk = nullptr;
            e.second_chunk_size = 0;
        }

        return e;
    }

    struct AppendResult {
        u32 elements_given;
        u32 elements_remaining;

        bool all_appended() const { return elements_remaining == 0; }

        u32 elements_appended() const { return elements_given - elements_remaining; }
    };

    AppendResult append(const T *elements, GLuint count, bool wait_on_gpu_completion) {
        assert(_appender_consumer == AppenderAndConsumer::CPU_GPU ||
               _appender_consumer == AppenderAndConsumer::UNSPECIFIED);

        u32 able_to_write = std::min(count, space());
        auto extent = get_extent(_size, able_to_write);

        memcpy(extent.first_chunk_extent, elements, std::min(extent.first_chunk_size, count) * sizeof(T));
        if (extent.second_chunk_extent) {
            memcpy(extent.second_chunk_extent,
                   elements + extent.first_chunk_size,
                   extent.second_chunk_size * sizeof(T));
        }

        if (able_to_write == count) {
            return AppendResult{count, 0};
        }

        u32 remaining_count = count - able_to_write;

        if (!wait_on_gpu_completion) {
            return AppendResult{count, remaining_count};
        }

        LOG_F(WARNING, "A wait on sync event occured");
        CHECK_NE_F(_cur_consume_state.sync, nullptr, "No GPU consume in progress");
        GLenum wait_status = glClientWaitSync(_cur_consume_state.sync, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    }

    void gpu_consume_sync_object(u32 max_elements) {
        assert(_appender_consumer == AppenderAndConsumer::CPU_GPU ||
               _appender_consumer == AppenderAndConsumer::UNSPECIFIED);

        CHECK_F(_cur_consume_state.sync == nullptr,
                "One GPU consume already in progress, sync object is valid");

        _cur_consume_state = GPUConsumeState(max_elements, glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0));
    }
};

} // namespace ringbuffer
