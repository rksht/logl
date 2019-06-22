#pragma once

#include <learnogl/math_ops.h>
#include <learnogl/type_utils.h>

#include <scaffold/array.h>
#include <scaffold/debug.h>
#include <scaffold/memory.h>
#include <scaffold/pod_hash.h>
#include <scaffold/string_stream.h>
#include <scaffold/vector.h>

#include <loguru.hpp>

#include <array>
#include <chrono>
#include <functional>
#include <iterator>
#include <map>
#include <vector>

// filesystem namespace
#ifdef _MSC_VER
#    include <filesystem>
namespace fs = std::experimental::filesystem;
#elif __cplusplus <= 201402 && __has_include(<experimental/filesystem>)
#    include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#elif __cplusplus >= 201703L
#    include <filesystem>
namespace fs = std::filesystem;
#elif __has_include(<experimental/filesystem>)
#    include <experimental/filesystem>
namespace fs = std::experimental::filesystem;
#else
#    error "Didn't find filesystem namespace"
#endif

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof(arr[0]))
#define ARRAY_BEGIN(arr) (arr)
#define ARRAY_END(arr) ((arr) + ARRAY_SIZE(arr))
#define CSTR(STRING_LITERAL) (const char *)(STRING_LITERAL)

#define IMPLIES(a, b) (!(a) || (b))

constexpr u64 SECONDS_NS = 1000000000;
constexpr u64 MILLISECONDS_NS = SECONDS_NS / 1000;
constexpr u64 MICROSECONDS_NS = MILLISECONDS_NS / 1000;

#define TOKENPASTE(x, y) x##y
#define TOKENPASTE2(x, y) TOKENPASTE(x, y)

#define GETSET(type, variable_name)                                                                          \
    const type &variable_name() const { return _##variable_name; };                                          \
    type &variable_name() { return _##variable_name; };                                                      \
    void set_##variable_name(const type &v) { _##variable_name = v; }                                        \
    type _##variable_name

#define GETONLY(type, variable_name)                                                                         \
    const type &variable_name() const { return _##variable_name; };                                          \
    const type &variable_name() { return _##variable_name; };                                                \
    type _##variable_name

/// Shorthand for the fo::string_stream namespace
namespace fo_ss = fo::string_stream;

/// Why not
#define fn_ auto
#define local_fn_ const auto
#define LETREF const auto &
#define TU_LOCAL static
#define GLOBAL_LAMBDA []
#define const_ const auto
#define var_ auto

#define DONT_KEEP_INLINED inline
#define FUNC_PTR(function_name) std::add_pointer_t<decltype(function_name)>
#define CTOR_INIT_FIELD(field_name, ...) field_name(__VA_ARGS__)
#define DEFAULT(v) (v{})
#define SELF (*this)
#define self_ (*this)
#define UNUSED(v) (static_cast<void>(v))
#define REINPCAST(t, ...) reinterpret_cast<t>(__VA_ARGS__)
#define CALL_OPERATOR operator()
#define STD_BEGIN_END(v) std::begin(v), std::end(v)

#define reallyconst static constexpr auto

#define GLOBAL_STORAGE(type, name)                                                                           \
    std::aligned_storage_t<sizeof(type), alignof(type)> global_storage_for_##name[1]

#define GLOBAL_ACCESSOR_FN(type, name, funcname)                                                             \
    type &funcname() { return *reinterpret_cast<type *>(global_storage_for_##name); }

#if defined(WIN32)

#    if __has_include(<wrl/client.h>)

#        include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#    endif

#endif

// Cast reference to constant pointer
template <typename T> const T *constptr(T &p) { return static_cast<const T *>(&p); }

template <typename T> fs::path generic_path(T source) {
    static_assert(std::is_constructible<fs::path, T>::value, "");
    return fs::path(std::move(source));
}

// Just a pinch of autism
using Vec4 = fo::Vector4;
using Vec3 = fo::Vector3;
using Vec2 = fo::Vector2;
using Mat3 = fo::Matrix3x3;
using Mat4 = fo::Matrix4x4;

/* Here's a convention. Always wrap integer sequence enums in a struct like this
-

        struct TopThreeBands {
                enum E : u32 {
                        ALICE_IN_CHAINS = 0,
                        NIRVANA = 1,
                        NINE_INCH_NAILS = 2,

                        COUNT // <---- Always put this at the end.
                };

                DECL_BITS_CHECK(type);
                // ^----- This will declare a static constexpr variable `numbits` of
given `type` which gives the
                // number of bits needed to represent any of the enum values.
        };

These enums are what I call 'natural enums'.

*/

#define DECL_BITS(type) static constexpr type numbits = log2_ceil(E::COUNT)
#define DECL_BITS_CHECK(type)                                                                                \
    static constexpr type numbits = (type)log2_ceil(E::COUNT);                                               \
    static_assert(numbits < 8 * sizeof(type), "")

// Should be used on enums that have a `COUNT` as its last member, and is a
// contiguous range of integers. You know what I'm talking about.
#define ENUM_BITS(enum_struct) log2_ceil(enum_struct::E::COUNT)

// Call this to create a mask for encoding different enums into a single
// integer. Need DECL_BITS or DECL_BITS_CHECK declared before this. An `end`
// static const is also defined. See below.
#define ENUM_MASK(type, start_bit)                                                                           \
    using mask = IntMask<start_bit, numbits, type>;                                                          \
    static constexpr auto end_bit = mask::END
#define ENUM_MASK16(start_bit)                                                                               \
    using mask = Mask16<start_bit, numbits>;                                                                 \
    static constexpr auto end_bit = mask::END
#define ENUM_MASK32(start_bit)                                                                               \
    using mask = Mask32<start_bit, numbits>;                                                                 \
    static constexpr auto end_bit = mask::END
#define ENUM_MASK64(start_bit)                                                                               \
    using mask = Mask64<start_bit, numbits>;                                                                 \
    static constexpr auto end_bit = mask::END

/// Prints a matrix in usual math-book format, i.e. transformation of i-th input
/// basis vector is the i-th *column*.
void print_matrix_classic(const char *name, const fo::Matrix4x4 &m);

/// Prints the matrix into the string_stream buffer. If column-major is given
/// false then prints the matrix as it would appear in math books, i.e the basis
/// vectors are aligned to columns.
void str_of_matrix(const fo::Matrix4x4 &mat, fo_ss::Buffer &b, bool column_major = true);

fo_ss::Buffer
str_of_matrix(const fo::Matrix4x4 &m, float significands = 5, const char *name = "", unsigned column_gap = 5);

/// Interprets the given file as a binary file containing structs of type `T`
/// and creates a vector and returns it.
template <typename T> std::vector<T> read_structs_into_vector(const fs::path &file, const char *struct_name);

/// Reads a file into the given array. By default `make_cstring` is true, and a
/// null terminator is appended after the file is read.
void read_file(const fs::path &path, fo::Array<u8> &arr, bool make_cstring = false);
void read_file(const fs::path &path, fo::Array<i8> &arr, bool make_cstring = true);
void read_file(const fs::path &path, fo::Array<char> &arr,
               bool make_cstring = true); // char != signed char

char *read_file(const fs::path &path, fo::Allocator &a, u32 &size_out, bool make_cstring = false);

/// Reads a file and returns an std::string
std::string read_file_stdstring(const fs::path &path);

/// Writes the data to the given file
void write_file(const fs::path &path, const uint8_t *data, uint32_t size);

/// Given `o` is an std::optional. Aborts if optional does not contain a value.
/// Otherwise returns that value.
#define MUST_OPTIONAL(o) must_optional(o, __FILE__, __LINE__)

template <typename T> T must_optional(optional<T> o, const char *file, int line) {
    if (!bool(o)) {
        log_err("Must optional failed - %s:%i", file, line);
        abort();
    }
    return o.value();
}

inline const char *_screaming(int line) {
    const char *s[] = {
        "UGH!!#$#*$$@!",
        "HECKING WRONG!&^@#)@*#!11#$#)(*&",
        "OH SHIT WHY!!#@$#$?????",
        "REEEEEEEEEEEEEE",
    };

    return s[line % ARRAY_SIZE(s)];
}

#define SCREAM _screaming(__LINE__)

inline fs::path make_path(fs::path path) { return path; }

template <typename T, typename... Rest> fs::path make_path(fs::path path, T first, Rest... args) {
    path /= first;
    return make_path(std::move(path), std::forward<Rest>(args)...);
}

inline fo_ss::Buffer &operator<<(fo_ss::Buffer &ss, const std::string &s) {
    using namespace fo::string_stream;
    ss << s.c_str();
    return ss;
}

/// Flips a 2D array (must be tightly packed) vertically.
void flip_2d_array_vertically(uint8_t *data, uint32_t element_size, uint32_t w, uint32_t h);

template <typename T> struct Index2D {
    static_assert(std::is_integral<T>::value, "Must be an integral type");

    T minors_per_major;

    constexpr Index2D(T minors_per_major)
        : minors_per_major(minors_per_major) {}

    constexpr T operator()(T major, T minor) const { return major * minors_per_major + minor; }
};

template <typename T> struct IntMaxMin {
    static_assert(std::is_integral<T>::value, "");
    static constexpr auto max = std::numeric_limits<T>::max();
    static constexpr auto min = std::numeric_limits<T>::min();
};

template <typename T> static constexpr auto imax_v = IntMaxMin<T>::template max;

template <typename T> static constexpr auto imin_v = IntMaxMin<T>::template min;

// Change the file name in the given path. Concatenates the given string
// `concat` into the file name and changes the extension to the new extension.
// By default, it is null, and denotes that original extension is kept.
void change_filename(fs::path &path, const char *concat, const char *new_extension = nullptr);

template <typename T> size_t vec_bytes(const std::vector<T> &v) { return v.size() * sizeof(T); }

template <typename T> size_t vec_bytes(const fo::Array<T> &v) { return fo::size(v) * sizeof(T); }

template <typename T, size_t N> size_t vec_bytes(const std::array<T, N> &v) { return v.size() * sizeof(T); }

template <typename T> size_t vec_bytes(const fo::Vector<T> &v) { return fo::size(v) * sizeof(T); }

// Push an element to the given array and return a reference to the just pushed element.
template <typename T> T &push_back_get(fo::Array<T> &v, T &&e) {
    fo::push_back(v, std::forward<T>(e));
    return fo::back(v);
}

template <typename T> T &push_back_get(std::vector<T> &v, T &&e) {
    v.push_back(std::forward<T>(e));
    return v.back();
}

template <typename T> T &push_back_get(fo::Vector<T> &v, T &&e) {
    return fo::push_back(v, std::forward<T>(e));
}

template <typename T, typename Element> void fill_array(T &array, const Element &elem) {
    std::fill(begin(array), end(array), elem);
}

template <typename InputIterator, typename T>
bool contains(InputIterator begin, InputIterator end, const T &v) {
    return std::any_of(begin, end, [&v](const auto &element) { return element == v; });
}

template <typename FoundIterator, typename EndIterator, bool is_map = false> struct FindWithEnd {
    FoundIterator res_it;
    FoundIterator end_it;

    auto &keyvalue() const { return *res_it; }

    auto &keyvalue_must(const char *msg = "") const {
        CHECK_F(found(), msg);
        return *res_it;
    }

    bool not_found() const { return res_it == end_it; }
    bool found() const { return !not_found(); }

    auto &key() const {
        if constexpr (is_map) {
            return keyvalue().first;
        } else {
            return keyvalue().first();
        }
    }

    auto &value() const {
        if constexpr (is_map) {
            return keyvalue().second;
        } else {
            return keyvalue().second();
        }
    }

    template <typename Callable> void if_found(Callable c) const {
        static_assert(
            std::is_invocable<Callable, decltype(keyvalue().first()), decltype(keyvalue().second())>::value,
            "Given callable is incompatible with lookup key and value types");

        if (found()) {
            std::invoke(c, key(), value());
        }
    }

    template <typename Callable> void if_not_found(Callable c) const {
        static_assert(std::is_invocable<Callable>::value, "Given callable should take no arguments");
        if (!found()) {
            std::invoke(c);
        }
    }

    operator bool() const { return found(); }
};

template <typename FoundIterator, typename EndIterator>
const auto make_find_with_end(FoundIterator f, EndIterator e) {
    return FindWithEnd<FoundIterator, EndIterator>{ f, e };
}

// Finds the key in the map and returns the resulting iterator and the end
// iterator. Works with both std::map and fo::PodHash.
template <typename K, typename V, typename... Whatever>
auto find_with_end(fo::PodHash<K, V, Whatever...> &map, const K &key) {
    return make_find_with_end(fo::get(map, key), fo::end(map));
}

// Finds the key in the map and returns the resulting iterator and the end
// iterator. Works with both std::map and fo::PodHash.
template <typename K, typename V, typename... Whatever>
auto find_with_end(std::map<K, V, Whatever...> &map, const K &key) {
    return make_find_with_end(map.find(key), map.end());
}

// Const-ref version of the above two
template <typename K, typename V, typename... Whatever>
auto find_with_end(const fo::PodHash<K, V, Whatever...> &map, const K &key) {
    return make_find_with_end(fo::get(map, key), fo::end(map));
}
template <typename K, typename V, typename... Whatever>
auto find_with_end(const std::map<K, V, Whatever...> &map, const K &key) {
    return make_find_with_end(map.find(key), map.end());
}

template <typename K, typename V> auto find_with_end(fo::OrderedMap<K, V> &map, const K &key) {
    return make_find_with_end(fo::get(map, key), fo::end(map));
}

template <typename K, typename V> auto find_with_end(const fo::OrderedMap<K, V> &map, const K &key) {
    return make_find_with_end(fo::get(map, key), fo::end(map));
}

u32 split_string(const char *in,
                 char split_char,
                 fo::Vector<fo_ss::Buffer> &out,
                 fo::Allocator &sub_string_allocator = fo::memory_globals::default_allocator());

template <typename T, typename... Args> T *fo_alloc_init(Args &&... args) {
    return fo::make_new<T>(fo::memory_globals::default_allocator(), std::forward<Args>(args)...);
}

template <typename T, typename... Args> T *fo_delete(T *p) {
    return fo::make_delete(fo::memory_globals::default_allocator(), p);
}

constexpr static inline fo::Vector4 normalize_rgba_u8(uint32_t rgba) {
    const uint32_t r = (rgba & 0xff000000u) >> 24;
    const uint32_t g = (rgba & 0x00ff0000u) >> 16;
    const uint32_t b = (rgba & 0x0000ff00u) >> 8;
    const uint32_t a = (rgba & 0x000000ffu);
    return fo::Vector4{ float(r) / 255, float(g) / 255, float(b) / 255, float(a) / 255 };
}

constexpr static inline fo::Vector3 normalize_rgb_u8(uint32_t rgba) {
    const uint32_t r = (rgba & 0x00ff0000u) >> 16;
    const uint32_t g = (rgba & 0x0000ff00u) >> 8;
    const uint32_t b = (rgba & 0x000000ffu);
    return fo::Vector3{ float(r) / 255, float(g) / 255, float(b) / 255 };
}

template <typename T> std::vector<T> read_structs_into_vector(const fs::path &file, const char *struct_name) {
    DLOG_F(INFO, "Reading structs from file %s", file.u8string().c_str());

    static_assert(std::is_trivially_copyable<T>::value, "");
    static_assert(std::is_default_constructible<T>::value, "");

    using namespace fo;

    size_t struct_count = ceil_div((u32)fs::file_size(file), (u32)sizeof(T));

    Array<uint8_t> raw(memory_globals::default_allocator());
    read_file(file, raw, false);

    std::vector<T> structs;
    structs.reserve(struct_count);
    T *p = reinterpret_cast<T *>(data(raw));
    while (p < reinterpret_cast<T *>(data(raw)) + struct_count) {
        T t;
        memcpy(&t, p, sizeof(T));
        structs.push_back(t);
        ++p;
    }

    return structs;
}

template <typename StructInFile, typename OutType, typename TransformFn>
void read_structs_into_array(const fs::path &file,
                             TransformFn fn,
                             fo::Array<OutType> &out_array,
                             const char *struct_name = "<not_given>") {

    const auto file_path = file.generic_u8string();

    DLOG_F(INFO, "Reading structs from file %s", file_path.c_str());

    const u32 initial_size = size(out_array);

    const u32 file_size = (u32)fs::file_size(file);

    LOG_IF_F(WARNING,
             file_size % sizeof(StructInFile) != 0,
             "File size of %s is not a multiple of struct size. Reading upto "
             "last multiple.",
             file_path.c_str());

    const u32 struct_count = file_size / (u32)sizeof(StructInFile);

    fo::Array<uint8_t> raw(fo::memory_globals::default_allocator());
    read_file(file, raw, false);

    resize(out_array, size(out_array) + struct_count);

    const StructInFile *p = reinterpret_cast<const StructInFile *>(data(raw));

    for (u32 i = initial_size; i < initial_size + struct_count; ++i, ++p) {
        StructInFile t;
        memcpy(&t, p, sizeof(StructInFile));
        out_array[i] = fn(t);
    }
}

struct RGBA8 {
    uint8_t r, g, b, a;

    constexpr uint32_t little_endian_u32() const {
        return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    }

    constexpr uint32_t big_endian_u32() const {
        return uint32_t(a) | (uint32_t(b) << 8) | (uint32_t(g) << 16) | (uint32_t(r) << 24);
    }

    static constexpr RGBA8 from_hex_alpha(uint32_t hex) {
        return RGBA8{ uint8_t((hex & 0xff000000) >> 24),
                      uint8_t((hex & 0x00ff0000) >> 16),
                      uint8_t((hex & 0x0000ff00) >> 8),
                      uint8_t(hex & 0x000000ff) };
    }

    static constexpr RGBA8 from_hex(uint32_t hex) {
        return RGBA8{
            uint8_t((hex & 0xff0000) >> 16), uint8_t((hex & 0x00ff00) >> 8), uint8_t(hex & 0x0000ff), 255
        };
    }
};

static_assert(sizeof(RGBA8) == 4, "Need this");

inline constexpr bool operator==(const RGBA8 &c1, const RGBA8 &c2) {
    return c1.r == c2.r && c1.g == c2.g && c1.b == c2.b && c1.a == c2.a;
}

// Translates an html string to RGBA8 struct
inline RGBA8 html_color(const char *color) {
    color = &color[1];
    auto intcolor = (uint32_t)std::stoi(color, nullptr, 16);
    return RGBA8::from_hex(intcolor);
}

// Convenience type that denotes a 2D rectangle with some padding.
struct PaddedRect {
    fo::Vector2 lowest;           // The inner box's top left corner
    fo::Vector2 width_and_height; // The inner box's width
    f32 padding;

    // `wh` is the total width and height of the box, inclusive of the padding.
    void set_topleft_including_padding(const fo::Vector2 &p, const fo::Vector2 &wh, f32 inner_padding) {
        using namespace eng::math;
        fo::Vector2 pad = { inner_padding, inner_padding };
        padding = inner_padding;
        lowest = p + pad;
        width_and_height = wh - 2.0f * pad;
    }

    void get_inner_min_and_max(fo::Vector2 &min, fo::Vector2 &max) const {
        using namespace eng::math;
        min = lowest;
        max = min + width_and_height;
    }

    void get_outer_min_and_max(fo::Vector2 &min, fo::Vector2 &max) const {
        using namespace eng::math;
        const fo::Vector2 pad = { padding, padding };
        min = lowest - pad;
        max = lowest + width_and_height + pad;
    }
};

// Any chrono duration to seconds count in double precision
template <typename Scalar, std::intmax_t num, std::intmax_t den>
inline double seconds(const std::chrono::duration<Scalar, std::ratio<num, den>> &dura) {
    return std::chrono::duration<double, std::ratio<1, 1>>(dura).count();
}

inline u64 kilobytes(u64 n) { return n << 10; }
inline u64 megabytes(u64 n) { return n << 20; }
inline u64 gigabytes(u64 n) { return n << 30; }

inline f32 kilometer(f32 n) { return n * 1000.0f; }
inline f32 millimeter(f32 n) { return n * 0.001f; }
inline f32 centimeters(f32 n) { return n * 0.01f; }

// An object that calls a given function when it's destroyed.
template <typename Cleanup> struct Defer {
    Cleanup _dtor_func;

    Defer(Cleanup dtor_func)
        : _dtor_func(std::move(dtor_func)) {}

    Defer(Defer &&) = default;
    Defer(const Defer &) = default;

    ~Defer() { _dtor_func(); }
};

template <typename Cleanup> inline Defer<Cleanup> make_deferred(Cleanup dtor_func) {
    return Defer<Cleanup>(dtor_func);
}

// Convenience macro, since we don't care about the variable name. But we *do*
// need to give a unique id, just given a number like the macro __LINE__.
#define DEFER(C) auto TOKENPASTE2(deferred_statement_, __LINE__) = make_deferred(C)

// Another convenience macro. Wraps the given statement in a lambda.
#define DEFERSTAT(statement) DEFER([&]() { statement; });

// Convenience macro. Execute a block of code in a function only once (per thread). No thread safety.
#define ONCE_BLOCK(...)                                                                                      \
    static bool TOKENPASTE2(_did_call_, __LINE__) = false;                                                   \
    if (!TOKENPASTE2(_did_call_, __LINE__)) {                                                                \
        TOKENPASTE2(_did_call_, __LINE__) = true;                                                            \
        [&]() __VA_ARGS__();                                                                                 \
    }

// end is exclusive.
template <uint32_t start, typename ConstantType, ConstantType... constants> struct UintSequenceSwitch {
    static constexpr ConstantType _array[] = { constants... };

    static ConstantType get(uint32_t i, const char *error_msg) {
#if !defined(NDEBUG)
        int32_t index = int32_t(i) - int32_t(start);
        if (index < 0 || index >= (int32_t)sizeof...(constants)) {
            fprintf(stderr,
                    "Error - %s -- \n\t%s - Index %u (indexing %i into _array) is "
                    "out of range\n",
                    error_msg,
                    __PRETTY_FUNCTION__,
                    i,
                    index);
            assert(false);
        }
#endif
        return _array[i];
    }
};

// Since C++20.
template <class T> struct remove_cvref { typedef std::remove_cv_t<std::remove_reference_t<T>> type; };

template <typename T, typename... TypeList> struct OneOfType {
    static constexpr bool value = std::disjunction_v<std::is_same<T, TypeList>...>;
};

template <typename T, typename... TypeList>
inline constexpr bool one_of_type_v = OneOfType<T, TypeList...>::value;

template <typename T, template <class> class SingleParamTemplate> struct IsInstanceOfTemplate {
    static constexpr bool value = false;
};

template <typename ParamType, template <class> class SingleParamTemplate>
struct IsInstanceOfTemplate<SingleParamTemplate<ParamType>, SingleParamTemplate> {
    static constexpr bool value = true;
};

// Customary non-copyable base class.
class NonCopyable {
  public:
    NonCopyable() = default;

    NonCopyable(const NonCopyable &) = delete;
    NonCopyable &operator=(const NonCopyable &) = delete;

    NonCopyable(NonCopyable &&o) = default;
    NonCopyable &operator=(NonCopyable &&o) = default;
};

// Partition the values of an enum into either true or false with this wrapper.
template <typename Enum, Enum... true_values> struct EnumBool {
    GETSET(Enum, value);

    constexpr EnumBool(Enum value)
        : _value(value) {}

    template <Enum... _true_values> struct IsTrue {
        constexpr bool operator()(Enum v) const { return false; }
    };

    template <Enum first, Enum... rest> struct IsTrue<first, rest...> {
        constexpr bool operator()(Enum v) const { return v == first || IsTrue<rest...>()(v); }
    };

    constexpr operator bool() const { return IsTrue<true_values...>()(_value); }

    constexpr bool operator==(const EnumBool &o) const { return _value == o._value; }
    constexpr bool operator!=(const EnumBool &o) const { return *this != o; }
};

#define ENUMSTRUCT struct

template <auto... values> struct OneOf;

template <auto v0, auto... v_rest> struct OneOf<v0, v_rest...> {
    using T = decltype(v0);

    T _v = v0;

    OneOf(T v)
        : _v(v) {}
    OneOf<v0, v_rest...> &operator=(T v) {
        _v = v;
        return *this;
    }
};

// std::invoke implementation. I don't care about being noexcept.

#if __cplusplus <= 201402

namespace invoke_internal {

template <typename T> struct is_reference_wrapper : std::false_type {};

template <typename U> struct is_reference_wrapper<std::reference_wrapper<U>> : std::true_type {};

template <typename T> constexpr bool is_reference_wrapper_v = is_reference_wrapper<T>::value;

template <class T, class Type, class T1, class... Args> auto INVOKE(Type T::*f, T1 &&t1, Args &&... args) {
    if (std::is_member_function_pointer<decltype(f)>::value) {
        if (std::is_base_of<T, std::decay_t<T1>>::value) {
            return (std::forward<T1>(t1).*f)(std::forward<Args>(args)...);
        } else if (is_reference_wrapper_v<std::decay_t<T1>>) {
            return (t1.get().*f)(std::forward<Args>(args)...);
        } else {
            return ((*std::forward<T1>(t1)).*f)(std::forward<Args>(args)...);
        }
    } else {
        static_assert(std::is_member_object_pointer<decltype(f)>::value, "");
        static_assert(sizeof...(args) == 0, "");
        if (std::is_base_of<T, std::decay_t<T1>>::value) {
            return std::forward<T1>(t1).*f;
        } else if (is_reference_wrapper_v<std::decay_t<T1>>) {
            return t1.get().*f;
        } else {
            return (*std::forward<T1>(t1)).*f;
        }
    }
}

template <class F, class... Args> auto INVOKE(F &&f, Args &&... args) {
    return std::forward<F>(f)(std::forward<Args>(args)...);
}

} // namespace invoke_internal

template <typename F, typename... Args> auto invoke(F &&f, Args &&... args) {
    return invoke_internal::INVOKE(std::forward<F>(f), std::forward<Args>(args)...);
}

#else

template <typename F, typename... Args> REALLY_INLINE auto invoke(F &&f, Args &&... args) {
    return std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
}

#endif

template <typename T, bool is_const>
struct StridedIterator : std::iterator<std::bidirectional_iterator_tag, T> {
    using Ptr = std::conditional_t<is_const, const u8, u8>;

    Ptr *_p;
    size_t _stride;

    StridedIterator(Ptr *p, size_t stride)
        : _p(p)
        , _stride(stride) {}

    StridedIterator(const StridedIterator &) = default;

    StridedIterator &operator++() {
        _p += _stride;
        return *this;
    }

    StridedIterator operator++(int) {
        auto copy = *this;
        this->operator++();
        return copy;
    }

    StridedIterator &operator--() {
        _p -= _stride;
        return *this;
    }

    StridedIterator operator--(int) {
        auto copy = *this;
        this->operator--();
        return copy;
    }

    ptrdiff_t operator-(const StridedIterator &other) const {
        return reinterpret_cast<const T *>(_p) - reinterpret_cast<const T *>(other._p);
    }

    T *operator->() { return reinterpret_cast<T *>(_p); }

    T &operator*() { return *reinterpret_cast<T *>(_p); }

    T &operator[](int i) { return reinterpret_cast<uint8_t *>(_p + i * _stride); }

    bool operator==(const StridedIterator &o) const { return _p == o._p && _stride == o._stride; }

    bool operator!=(const StridedIterator &o) const { return !(*this == o); }
};

// A pointer that does not do anything special other than setting self to
// nullptr on destruction.
template <typename T> struct NulledOnDtorPtr {
    T *_p = nullptr;
    NulledOnDtorPtr() = default;
    NulledOnDtorPtr(T *p)
        : _p(p) {}
    NulledOnDtorPtr(const NulledOnDtorPtr &) = default;
    NulledOnDtorPtr(NulledOnDtorPtr &&o)
        : _p(o._p) {
        o._p = nullptr;
    }
    NulledOnDtorPtr &operator=(const NulledOnDtorPtr &o) = default;
    NulledOnDtorPtr &operator=(NulledOnDtorPtr &&o) {
        if (&o != this) {
            _p = o._p;
            o._p = nullptr;
        }
    }

    ~NulledOnDtorPtr() { _p = nullptr; }

    T *operator->() { return _p; }

    T *operator->() const { return _p; }

    operator bool() const { return _p != nullptr; }
};

template <typename ObjectType> void scratch_allocator_deleter(ObjectType *p) {
    fo::make_delete(fo::memory_globals::default_scratch_allocator(), p);
}

template <typename ObjectType> void default_allocator_deleter(ObjectType *p) {
    fo::make_delete(fo::memory_globals::default_allocator(), p);
}

template <typename ObjectType>
using ScratchUniquePtr = std::unique_ptr<ObjectType, FUNC_PTR(scratch_allocator_deleter<ObjectType>)>;

// Construct an object using the memory_globals::scratch_allocator and return a unique_ptr to it which deletes
// it appropriately on destruction.
template <typename ObjectType, typename... Args>
ScratchUniquePtr<ObjectType> scratch_unique(Args &&... ctor_args) {
    return ScratchUniquePtr<ObjectType>(
        fo::make_new<ObjectType>(fo::memory_globals::default_scratch_allocator(),
                                 std::forward<Args>(ctor_args)...),
        &scratch_allocator_deleter<ObjectType>);
}

template <typename ObjectType> ScratchUniquePtr<ObjectType> null_scratch_unique() {
    return ScratchUniquePtr<ObjectType>(nullptr, &scratch_allocator_deleter<ObjectType>);
}

// Construct an object using memory_globals::default_allocator

template <typename ObjectType>
using DefaultUniquePtr = std::unique_ptr<ObjectType, FUNC_PTR(default_allocator_deleter<ObjectType>)>;

template <typename ObjectType, typename... Args>
DefaultUniquePtr<ObjectType> default_unique(Args &&... ctor_args) {
    return DefaultUniquePtr<ObjectType>(
        fo::make_new<ObjectType>(fo::memory_globals::default_allocator(), std::forward<Args>(ctor_args)...),
        &scratch_allocator_deleter<ObjectType>);
}

// Basic static vector
template <typename T, size_t full_capacity> struct StaticVector : std::array<T, full_capacity> {
    using Base = std::array<T, full_capacity>;

    size_t _current_count = 0;

    StaticVector() = default;

    StaticVector(std::initializer_list<T> initial_elements) {
        _current_count = initial_elements.size();
        CHECK_LE_F(initial_elements.size(), full_capacity);
        std::copy(STD_BEGIN_END(initial_elements), std::begin(*this));
    }

    size_t filled_size() const { return (size_t)_current_count; }

    // Not usually required.
    size_t capacity() const { return full_capacity; }

    T &operator[](size_t i) {
        DCHECK_LT_F(i, _current_count);
        return Base::operator[](i);
    }

    T &unchecked_at(size_t i) { return Base::operator[](i); }

    const T &operator[](size_t i) const {
        DCHECK_LT_F(i, _current_count);
        return Base::operator[](i);
    }

    const T &unchecked_at(size_t i) const { return Base::operator[](i); }

    T &push_back(T &&item) {
        DCHECK_NE_F(_current_count, Base::size());
        Base::operator[](_current_count++) = std::forward<T>(item);
        return Base::operator[](_current_count - 1);
    }

    T &push_back(const T &item) {
        DCHECK_NE_F(_current_count, Base::size());
        Base::operator[](_current_count++) = item;
        return Base::operator[](_current_count - 1);
    }

    void set_empty() { _current_count = 0; }

    T &back() { return Base::operator[](_current_count - 1); }

    // Pop-back is nop if current count is 0
    void pop_back() {
        if (_current_count > 0) {
            --_current_count;
        }
    }

    void fill_backing_array(const T &v) { std::fill(Base::begin(), Base::end(), v); }

    auto begin() { return Base::begin(); }
    auto end() { return Base::begin() + _current_count; }

    auto begin() const { return Base::begin(); }
    auto end() const { return Base::begin() + _current_count; }
};

struct Str {
    virtual ~Str() {}

    virtual const char *str() const = 0;
};
