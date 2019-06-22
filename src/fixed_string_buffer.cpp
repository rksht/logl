#include <learnogl/fixed_string_buffer.h>
#include <learnogl/kitchen_sink.h>
#include <loguru.hpp>
#include <scaffold/const_log.h>

FixedStringBuffer::Index FixedStringBuffer::add(const char *s, u32 length) {
    if (length == 0) {
        length = (u32)strlen(s);
    }

    if (length == 0) {
        return Index{ 0u };
    }

    u32 bytes_before_adding = fo::size(_concat_of_strings);
    u32 first_time_adding = bytes_before_adding == 0? 1 : 0;

    bytes_before_adding += first_time_adding;

    fo::resize(_concat_of_strings, bytes_before_adding + length + 1);

    memcpy(fo::data(_concat_of_strings) + bytes_before_adding, s, length);
    _concat_of_strings[fo::size(_concat_of_strings) - 1] = char(0);

    fo::push_back(_start_and_length, StartAndLength{ bytes_before_adding, length });

    return Index{ fo::size(_start_and_length) - 1 };
}

const char *FixedStringBuffer::get(Index index) const {
    if (index._i == 0) {
        return "";
    }

    DCHECK_LT_F(index._i, fo::size(_start_and_length));
    auto sl = _start_and_length[index._i];
    return fo::data(_concat_of_strings) + sl.start_byte;
}

u32 FixedStringBuffer::length(Index index) const {
    if (index._i == 0) {
        return 0;
    }

    DCHECK_LT_F(index._i, fo::size(_start_and_length));
    auto sl = _start_and_length[index._i];
    return sl.length;
}

void FixedStringBuffer::reserve(u32 num_strings, u32 max_length) {
    fo::reserve(_concat_of_strings,
                fo::size(_concat_of_strings) + num_strings * (max_length + 1)); // + 1 for nul
    fo::reserve(_start_and_length, fo::size(_start_and_length) + num_strings);
}
