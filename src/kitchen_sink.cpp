#include <learnogl/callstack.h>
#include <learnogl/kitchen_sink.h>
#include <scaffold/array.h>
#include <scaffold/debug.h>

#include <fstream>
#include <sstream>

using namespace fo;
using namespace eng::math;

void print_matrix_classic(const char *name, const fo::Matrix4x4 &m) {
    string_stream::Buffer b(memory_globals::default_allocator());
    str_of_matrix(m, b, false);
    log_info("Matrix %s = \n%s\n-----\n", name, string_stream::c_str(b));
}

template <typename T> void read_file_T(const fs::path &path, fo::Array<T> &arr, bool make_cstring) {
    static_assert(std::is_same_v<T, u8> || std::is_same_v<T, i8> || std::is_same_v<T, char>, "");

    auto path_str8 = path.generic_u8string();

    if (!fs::exists(path)) {
        eng::print_callstack();
        CHECK_F(fs::exists(path), "File: %s does not exist", path_str8.c_str());
    }

    FILE *f = fopen(path_str8.c_str(), u8"rb");
    CHECK_NE_F(f, nullptr, "Unexpected error - could not open file %s", path.c_str());

    const size_t size = fs::file_size(path);

    clear(arr);
    resize(arr, size);

    size_t read_count = fread(data(arr), 1, size, f);
    CHECK_EQ_F(read_count, size, "Failed to read full file: %s", path_str8.c_str());

    fclose(f);

    if (make_cstring) {
        push_back(arr, T('\0'));
    }
}

void read_file(const fs::path &path, fo::Array<u8> &arr, bool make_cstring) {
    read_file_T(path, arr, make_cstring);
}

void read_file(const fs::path &path, fo::Array<i8> &arr, bool make_cstring) {
    read_file_T(path, arr, make_cstring);
}

void read_file(const fs::path &path, fo::Array<char> &arr, bool make_cstring) {
    read_file_T(path, arr, make_cstring);
}

char *read_file(const fs::path &path, fo::Allocator &a, u32 &size_out, bool make_cstring) {
    CHECK_F(fs::exists(path));

    std::string path_str = path.generic_u8string();

    FILE *f = fopen(path_str.c_str(), u8"rb");
    CHECK_NE_F(f, nullptr, "Unexpected error - could not open file %s", path.c_str());

    const size_t size = fs::file_size(path);
    const size_t alloc_size = make_cstring ? size + 1 : size;

    char *data = (char *)a.allocate(size);
    size_t read_count = fread(data, 1, size, f);

    if (make_cstring) {
        data[size] = '\0';
    }

    CHECK_EQ_F(read_count, size, "Failed to read full file: %s", path_str.c_str());

    size_out = alloc_size;

    fclose(f);
    return data;
}

std::string read_file_stdstring(const fs::path &path) {
    auto path_str8 = path.generic_u8string();
    log_assert(fs::exists(path), "File: %s does not exist", path_str8.c_str());
    std::ifstream t(path_str8);
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

void write_file(const fs::path &path, const u8 *data, u32 size) {
    auto path_str8 = path.generic_u8string();

    FILE *f = fopen(path_str8.c_str(), "wb");

    CHECK_NE_F(f, nullptr, "Unexpected error - could not create/open file %s", path.c_str());

    size_t write_count = fwrite(data, 1, size, f);

    CHECK_EQ_F(write_count, size, "Failed to read full file: %s", path_str8.c_str());

    fclose(f);
}

void change_filename(fs::path &path, const char *concat, const char *new_extension) {
    std::string filename = path.filename().generic_u8string();

    std::string old_extension = "";

    // Remove last dot onward
    auto lastdot = filename.find_last_of('.');
    if (lastdot != std::string::npos && lastdot != filename.length() - 1) {
        old_extension = filename.substr(lastdot + 1, filename.length() - lastdot);
        filename.erase(filename.begin() + lastdot, filename.end());
    }

    filename += concat;
    filename += ".";
    filename += new_extension ? new_extension : old_extension;

    path.remove_filename();
    path /= filename;
}

void flip_2d_array_vertically(u8 *data, u32 element_size, u32 w, u32 h) {
    assert(w > 0);
    assert(h > 0);

    u32 row_size = element_size * w;
    fo::Array<u8> tmp(fo::memory_globals::default_allocator(), row_size);
    auto p = fo::data(tmp);

    for (u32 r = 0; r < h / 2; ++r) {
        u32 opposite = h - r - 1;
        u8 *r1 = data + row_size * r;
        u8 *r2 = data + row_size * opposite;
        memcpy(p, r1, row_size);
        memcpy(r1, r2, row_size);
        memcpy(r2, p, row_size);
    }
}

void str_of_matrix(const fo::Matrix4x4 &mat, fo::string_stream::Buffer &b, bool column_major) {
    using namespace fo;
    if (column_major) {
        // clang-format off
        string_stream::printf(b, "[[%f,    %f,    %f,    %f],\n [%f,    %f,    %f,    %f],\n [%f,    %f,    %f,    %f],\n [%f,    %f,    %f,    %f]]",
            mat.x.x, mat.x.y, mat.x.z, mat.x.w,
            mat.y.x, mat.y.y, mat.y.z, mat.y.w,
            mat.z.x, mat.z.y, mat.z.z, mat.z.w,
            mat.t.x, mat.t.y, mat.t.z, mat.t.w);
        // clang-format on
    } else {
        // clang-format off
        string_stream::printf(b, "[[%f,    %f,    %f,    %f],\n [%f,    %f,    %f,    %f],\n [%f,    %f,    %f,    %f],\n [%f,    %f,    %f,    %f]]",
            mat.x.x, mat.y.x, mat.z.x, mat.t.x,
            mat.x.y, mat.y.y, mat.z.y, mat.t.y,
            mat.x.z, mat.y.z, mat.z.z, mat.t.z,
            mat.x.w, mat.y.w, mat.z.w, mat.t.w);
        // clang-format on
    }
}

u32 split_string(const char *in, char split_char, Vector<string_stream::Buffer> &out, fo::Allocator &sub_string_allocator) {
    using namespace string_stream;

    u32 i = 0;

    u32 seen = 0;

    do {
        u32 start = i;
        while (in[i] && in[i] != split_char) {
            ++i;
        }
        ++seen;

        Buffer ss(sub_string_allocator);
        for (u32 j = start; j < i; ++j) {
            ss << in[j];
        }

        push_back(out, std::move(ss));

        if (in[i]) {
            ++i;
        }
    } while (in[i]);

    return seen;
}
