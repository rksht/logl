#pragma once

#include <learnogl/gl_binding_state.h>
#include <learnogl/gl_misc.h>

#include <array>

struct BitonicSorterU32 {
    std::array<eng::BufferDeleter, 2> _elements_ssbo;
    GETONLY(u32, num_elements) = 0;

    GLuint _cs_sort_shader = 0;
    GLuint _cs_sort_prog = 0;
    GLuint _cs_xpose_shader = 0;
    GLuint _cs_xpose_prog = 0;
    eng::BufferDeleter _ub_sort_params;
    GLuint _sort_params_binding = 0;

    u32 buffer_bytes() const { return _num_elements * sizeof(u32); }
    u32 elements_per_group() const { return _num_elements / LOGL_MAX_THREADS_PER_GROUP; }
    u32 bytes_per_group() const { return sizeof(u32) * elements_per_group(); }

    static constexpr u32 _SOURCE = 0;
    static constexpr u32 _DEST = 1;

    static constexpr u32 _TRANSPOSE_BLOCK_SIZE = 16;
    static constexpr u32 _NUM_ELEMENTS_PER_GROUP = 512;

    static constexpr u32 _MIN_ELEMENTS = _TRANSPOSE_BLOCK_SIZE * _NUM_ELEMENTS_PER_GROUP; // 16 * 512 = 8192
    static constexpr u32 _MAX_ELEMENTS =
        _NUM_ELEMENTS_PER_GROUP * _NUM_ELEMENTS_PER_GROUP; // 512 * 512 = 262144

    enum SetElementsResult {
        OK,
        TOO_MANY_ELEMENTS,
        TOO_FEW_ELEMENTS,
        NOT_POWER_OF_TWO,
        NEEDS_PADDING_ELEMENTS,
        U32_TOO_BIG_FOR_GROUP, // Should never happen, even on d3d10 hardware
    };

    struct SortParamsUB {
        u32 level;
        u32 level_mask;
        u32 matrix_width;
        u32 matrix_height;
    };

    // before anything, call this
    void load_shader(eng::BindingState &bs) {
        GLuint ub;
        glGenBuffers(1, &ub);
        glBindBuffer(GL_UNIFORM_BUFFER, ub);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(SortParamsUB), nullptr, GL_DYNAMIC_DRAW);

        GLuint sort_params_binding = bs.bind_unique(eng::gl_desc::UniformBuffer(ub, 0, sizeof(SortParamsUB)));
        _ub_sort_params.set(ub);

        eng::ShaderDefines defs;
        defs.add("NUM_ELEMENTS_PER_GROUP", (i32)_NUM_ELEMENTS_PER_GROUP)
            .add("TRANSPOSE_BLOCK_SIZE", (i32)_TRANSPOSE_BLOCK_SIZE)
            .add("SORT_PARAMS_BINDING", (i32)sort_params_binding);

        _cs_sort_shader = eng::create_shader_object(make_path(LOGL_SHADERS_DIR, "cs_bitonic_sort.comp"),
                                                    eng::COMPUTE_SHADER,
                                                    defs,
                                                    "cs_bitonic_sort");

        _cs_xpose_shader = eng::create_shader_object(make_path(LOGL_SHADERS_DIR, "cs_bitonic_transpose.comp"),
                                                     eng::COMPUTE_SHADER,
                                                     defs,
                                                     "cs_bitonic_transpose");

        _cs_sort_prog = eng::create_compute_program(_cs_sort_shader);
        _cs_xpose_prog = eng::create_compute_program(_cs_xpose_shader);
    }

    EnumBool<SetElementsResult, OK> can_set(u32 num_elements) const {
        if (!is_power_of_2(num_elements)) {
            return NOT_POWER_OF_TWO;
        }

        if (bytes_per_group() > eng::caps().max_compute_shared_mem_size) {
            return U32_TOO_BIG_FOR_GROUP;
        }

        // If number of elements are less than this, just use CPU.
        if (num_elements < _NUM_ELEMENTS_PER_GROUP) {
            return TOO_FEW_ELEMENTS;
        }

        if (num_elements < _MIN_ELEMENTS) {
            return NEEDS_PADDING_ELEMENTS;
        }

        if (num_elements > _MAX_ELEMENTS) {
            return TOO_MANY_ELEMENTS;
        }

        return OK;
    }

    EnumBool<SetElementsResult, OK> set_elements(u32 *elements, u32 num_elements) {
        auto res = can_set(num_elements);
        if (!bool(res)) {
            LOG_F(ERROR, "Cannot set elements - result = %u", res.value());
            return res;
        }

        bool same_size_buffer = _num_elements == num_elements;

        if (_elements_ssbo[0].is_created() && !same_size_buffer) {
            _elements_ssbo[0].destroy();
            _elements_ssbo[1].destroy();
        }

        _num_elements = num_elements;

        // allocate new buffers if not same size as previous
        if (!same_size_buffer) {
            GLuint b[2];
            glCreateBuffers(2, b);
            glNamedBufferStorage(b[0], buffer_bytes(), elements, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
            glNamedBufferStorage(b[1], buffer_bytes(), nullptr, GL_MAP_READ_BIT | GL_MAP_WRITE_BIT);
            _elements_ssbo[_SOURCE].set(b[0]);
            _elements_ssbo[_DEST].set(b[1]);
        }

        // copy into buffer 0
        u32 *mem_source = (u32 *)glMapNamedBufferRange(
            _elements_ssbo[_SOURCE].handle(), 0, buffer_bytes(), GL_MAP_WRITE_BIT);
        std::copy(elements, elements + num_elements, mem_source);
        glUnmapNamedBuffer(_elements_ssbo[_SOURCE].handle());

        return res;
    }

    // Call after sorting, or before sorting. Don't forget to call unmap
    u32 *map_readable() {
        return (u32 *)glMapNamedBufferRange(
            _elements_ssbo[_SOURCE].handle(), 0, buffer_bytes(), GL_MAP_READ_BIT);
    }

    // Must call after you're done with the mapped range.
    void unmap() { glUnmapNamedBuffer(_elements_ssbo[_SOURCE].handle()); }

    void _set_uniforms(const SortParamsUB &u) {
        glInvalidateBufferData(_ub_sort_params.handle());
        glNamedBufferSubData(_ub_sort_params.handle(), 0, sizeof(u), &u);
    };

    // Do the sort. This will block until the whole sort is done. Until I make a "gl work system" this will
    // have to suffice.
    void compute_sort() {
        const u32 matrix_width = _NUM_ELEMENTS_PER_GROUP;
        const u32 matrix_height = _num_elements / _NUM_ELEMENTS_PER_GROUP;

        glUseProgram(_cs_sort_prog);

        // Sort each subarray of upto length NUM_ELEMENTS_PER_GROUP first
        for (u32 subarray_length = 2; subarray_length <= _NUM_ELEMENTS_PER_GROUP;
             subarray_length = subarray_length * 2) {
            _set_uniforms({ subarray_length, subarray_length, matrix_width, matrix_height });
            glDispatchCompute(_num_elements / _NUM_ELEMENTS_PER_GROUP, 1, 1);

            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }

        // Now the "transpose,sort,transpose,sort" part that will run if num_elements > NUM_ELEMENTS_PER_GROUP

        // Helper function to swap the current source and destination buffer handles after each transpose.
        const auto swap_source_dest = [&]() { std::swap(_elements_ssbo[_SOURCE], _elements_ssbo[_DEST]); };

        // Helper function for binding the source and destination ssbos after each transpose shader is run.
        const auto bind_source_and_dest = [&]() {
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, _elements_ssbo[0].handle(), 0, buffer_bytes());
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 1, _elements_ssbo[1].handle(), 0, buffer_bytes());
        };

        for (u32 subarray_length = _NUM_ELEMENTS_PER_GROUP * 2; subarray_length <= _num_elements;
             subarray_length *= 2) {
            _set_uniforms({ subarray_length / _NUM_ELEMENTS_PER_GROUP,
                            (subarray_length & ~_num_elements) / _NUM_ELEMENTS_PER_GROUP,
                            matrix_width,
                            matrix_height });

            // Transpose data from source buffer to dest buffer.
            glUseProgram(_cs_xpose_prog);
            bind_source_and_dest();
            glDispatchCompute(matrix_width / _TRANSPOSE_BLOCK_SIZE, matrix_height / _TRANSPOSE_BLOCK_SIZE, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // Sort the transposed data. Need to first bind the dest buffer to point 0 (the one which the
            // sorting shader modifies)
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, _elements_ssbo[_DEST].handle(), 0, buffer_bytes());

            glUseProgram(_cs_sort_prog);
            glDispatchCompute(_num_elements / _NUM_ELEMENTS_PER_GROUP, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // Transpose data back from the pong buffer to the ping buffer
            _set_uniforms({ _NUM_ELEMENTS_PER_GROUP, subarray_length, matrix_height, matrix_width });
            glUseProgram(_cs_xpose_prog);
            swap_source_dest();
            bind_source_and_dest();
            glDispatchCompute(matrix_height / _TRANSPOSE_BLOCK_SIZE, matrix_width / _TRANSPOSE_BLOCK_SIZE, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            // Sort the transposed data.
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, _elements_ssbo[_DEST].handle(), 0, buffer_bytes());
            glUseProgram(_cs_sort_prog);
            glDispatchCompute(_num_elements / _NUM_ELEMENTS_PER_GROUP, 1, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

            swap_source_dest();
        }
    }
};
