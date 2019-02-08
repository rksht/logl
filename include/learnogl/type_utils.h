#pragma once

#include <learnogl/cons.h>
#include <utility>

// TODO: Use std::variant in C++17 mode

// optional template
#ifdef _MSC_VER
#    include <optional>
template <typename T> using optional = std::optional<T>;
constexpr auto nullopt = std::nullopt;

#elif __has_include(<optional>) && __cplusplus >= 201703L
#    include <optional>
template <typename T> using optional = std::optional<T>;
constexpr auto nullopt = std::nullopt;

#elif __has_include(<experimental/optional>)
#    include <experimental/optional>
template <typename T> using optional = std::experimental::optional<T>;
constexpr auto nullopt = std::experimental::nullopt;

#else
#    include <mapbox/optional.hpp>
#    define USING_MAPBOX_OPTIONAL
template <typename T> using optional = mapbox::util::optional<T>;
constexpr auto nullopt = mapbox::optional();

#endif

#if defined(_MSC_VER)
#    define NOT_CONSTEXPR_IN_MSVC
#elif defined(__CLANG__) || defined(__GNUG__)
#    define NOT_CONSTEXPR_IN_MSVC constexpr
#endif

// variant template

#if __cplusplus >= 201703L
#    include <variant>
template <typename... Types> using variant = std::variant<Types...>;
#elif __cplusplus <= 201402 && __has_include(<experimental/variant>)
#    include <experimental/variant>
template <typename... Types> using variant = std::experimental::variant<Types...>;
#else
#    include <mapbox/variant.hpp>
template <typename... Types> using variant = mapbox::util::variant<Types...>;
#    define USING_MAPBOX_VARIANT
#endif

template <typename... Types> inline int type_index(const variant<Types...> &v) {
#if defined(USING_MAPBOX_VARIANT)
    return v.which();
#else
    return v.index();
#endif
}

template <typename T, typename... Types> auto &get_value(variant<Types...> &v) {
#if defined(USING_MAPBOX_VARIANT)
    return mapbox::util::get<T>(v);
#else
    return std::get<T>(v);
#endif
}

template <typename T, typename... Types> auto &get_value(const variant<Types...> &v) {
#if defined(USING_MAPBOX_VARIANT)
    return mapbox::util::get<T>(v);
#else
    return std::get<T>(v);
#endif
}

template <size_t index, typename... Types> auto &get_value(variant<Types...> &v) {
#if defined(USING_MAPBOX_VARIANT)
    return mapbox::util::get<index>(v);
#else
    return std::get<index>(v);
#endif
}

template <size_t index, typename... Types> auto &get_value(const variant<Types...> &v) {
#if defined(USING_MAPBOX_VARIANT)
    return mapbox::util::get<index>(v);
#else
    return std::get<index>(v);
#endif
}

template <typename T> T optional_value(const ::optional<T> &o, T default_value = {}) {
    return bool(o) ? o.value() : std::move(default_value);
}

template <typename T> using Maybe = ::optional<T>;

inline constexpr auto MAYBE_NONE = ::nullopt;

// A mask for specifying a contiguous range of bits in an integer
template <unsigned start_bit, unsigned size, typename Int = uint64_t> struct IntMask {
    static_assert(std::is_integral<Int>::value, "Must be integer");

    static_assert(start_bit + size <= sizeof(Int) * 8,
                  "Bit range overflows the number of bits IntType can hold");

    using IntType = Int;

    static constexpr unsigned START = start_bit;
    static constexpr unsigned SIZE = size;
    static constexpr unsigned END = START + SIZE;

    static constexpr unsigned value = START; // For sorting

    // Just use `extract`
    static constexpr IntType and_mask() {
        IntType n = std::numeric_limits<IntType>::max();
        n = n >> (sizeof(IntType) * 8 - SIZE);
        n = n << START;
        return n;
    }

    // Just use `extract`
    static constexpr IntType rshift_by() { return START; }

    // Extracts the bits from the integer
    static constexpr IntType extract(IntType n) { return (n & and_mask()) >> rshift_by(); }

    // Maximum value of the mask
    static constexpr IntType max() { return extract(~IntType(0)); }

    static constexpr IntType shift(IntType n) { return n << START; }

    static std::string to_string() {
        std::string s;
        IntType n = and_mask();
        while (n) {
            s += (n & IntType(1)) ? '1' : '0';
            n = n >> 1;
        }

        while (s.size() < sizeof(IntType) * 8) {
            s += '0';
        }
        std::reverse(s.begin(), s.end());
        return s;
    }

    static constexpr IntType declare(IntType n) {
        assert(n <= max());
        return n;
    }

    template <IntType n> static constexpr IntType declare_static() {
        static_assert(n <= max(), "");
        return n;
    }

    static constexpr IntType set(IntType dest, IntType integer) {
        return (dest & ~and_mask()) | (integer << START);
    }

    // Can also instantiate an integer-like value from this

    IntType _n;

    constexpr IntMask(IntType n = 0)
        : _n(n) {}

    operator IntType() const { return _n; }

    IntType set_to(IntType dest) const { return (dest & ~and_mask()) | (_n << START); }
};

template <size_t start_bit, size_t size> using Mask32 = IntMask<start_bit, size, uint32_t>;

template <size_t start_bit, size_t size> using Mask64 = IntMask<start_bit, size, uint64_t>;

template <size_t start_bit, size_t size> using Mask16 = IntMask<start_bit, size, uint16_t>;

template <size_t start_bit, size_t size> using Mask8 = IntMask<start_bit, size, uint8_t>;

#if !defined(LEAN_MISC)

// This template enables using a syntax like MaskDeclaration<EntityMask, CounterMask, ActiveMask>::type, where
// each argument is a Mask. This will check for errors like overlapping ranges. Keep the checked type in a
// single .cpp file to avoid doing the compile-time checks like this everytime you compile a translation unit.
template <typename... Masks> struct MaskDeclaration {
    using mask_list = unpack_t<Masks...>;

    using sorted_by_start = sort_t<mask_list>;

    // Check that ranges don't overlap
    template <size_t end_of_prev, typename CurrentList> struct Check {
        using Head = car_t<CurrentList>;
        static constexpr bool value = Head::START >= end_of_prev;

        static_assert(value, "Overlapped ranges. See the type error");
        static_assert(Check<Head::START + Head::SIZE, cdr_t<CurrentList>>::value, "");
    };

    template <size_t end_of_prev> struct Check<end_of_prev, Null> { static constexpr bool value = true; };

    static_assert(Check<0, sorted_by_start>::value, "");

    using type = typename car_t<mask_list>::IntType;
};

// Short-hand for the above.
template <typename... Masks> using mask_t = typename MaskDeclaration<Masks...>::type;

template <typename FirstMask, typename... RestMasks>
inline constexpr auto set_masks_one_by_one(FirstMask first, RestMasks... rest) {
    return first.set_to(0) | set_masks_one_by_one(rest...);
}

template <typename FirstMask> inline constexpr auto set_masks_one_by_one(FirstMask first) {
    return first.set_to(0);
}

template <typename... Masks> inline constexpr auto set_masks(Masks... mask_values) {
    MaskDeclaration<Masks...> _;
    UNUSED(_);
    return set_masks_one_by_one(mask_values...);
}

#endif

#if !defined(LEAN_MISC)

// Subclass of std::variant (or ::variant) which provides an assoc array from type to index, so you don't have
// to remember it yourself when doing a switch-case. So use this one instead of original variant.
template <typename... Types> struct VariantTable : public ::variant<Types...> {
    template <size_t n, typename... T> struct Rec;

    template <size_t n, typename T> struct Rec<n, T> {
        using assoc_array = make_assoc_array_t<T, ConsUint<n>>;
    };

    template <size_t n, typename T, typename... Rest> struct Rec<n, T, Rest...> {
        using assoc_array = assoc_array_set_t<typename Rec<n + 1, Rest...>::assoc_array, T, ConsUint<n>>;
    };

    using assoc_array = typename Rec<0, Types...>::assoc_array;

    // Static variable that contains the index of T in the variant
    template <typename T> static constexpr size_t index = (size_t)assoc_array_get_t<assoc_array, T>::value;

    // Return reference to inner value
    template <typename T> T &as() { return get_value<T>(*this); }
    template <typename T> const T &as() const { return get_value<T>(*this); }

    template <typename T> bool isa() const { return ::type_index(*this) == index<T>; }

    // Still have to define copy, move, ctor and assign just for the conversion
    // from one of the internal types during assignment. Here goes.
    using Base = ::variant<Types...>;

    VariantTable() = default;

    VariantTable(const VariantTable &) = default;

    VariantTable(VariantTable &&) = default;

    VariantTable &operator=(const VariantTable &other) {
        Base::operator=(static_cast<const Base &>(other));
        return *this;
    }

    VariantTable &operator=(VariantTable &&other) {
        if (this != &other) {
            Base::operator=(std::move(other));
        }
        return *this;
    }

    template <typename T>
    VariantTable(T &&t)
        : Base(std::forward<T>(t)) {}

    template <typename T>
    VariantTable(const T &t)
        : Base(t) {}

    template <typename T> VariantTable &operator=(T &&t) {
        Base::operator=(std::forward<T>(t));
        return *this;
    }

    template <typename T> VariantTable &operator=(const T &t) {
        Base::operator=(t);
        return *this;
    }

    // Returns the type index. Don't use this directly.
    auto type_index() const { return ::type_index(*this); }

    // Returns true if current subtype equals given SubType
    template <typename SubType> bool contains_subtype() const { return index<SubType> == type_index(); }

    // Returns reference to contained subtype if it indeed is containing an object of that subtype. Throws
    // otherwise.
    template <typename SubType> SubType &get_value() { return ::get_value<SubType>(*this); }
    template <typename SubType> const SubType &get_value() const { return ::get_value<SubType>(*this); }
};

template <typename VariantTableInstance, typename SubType>
constexpr size_t vt_index = VariantTableInstance::template index<SubType>;

#    define VT_SWITCH(variant_object) switch (type_index(variant_object))
#    define VT_CASE(variant_object, sub_type_desired)                                                        \
    case vt_index<strip_type_t<decltype(variant_object)>, sub_type_desired>

// Returns index of the sub type, given a variant object. This is a shorthand so that you don't have to use
// decltype, and usually VariantTable type names are long.
#    define VT_INDEX(variant_object, sub_type_desired)                                                       \
        vt_index<strip_type_t<decltype(variant_object)>, sub_type_desired>

#    define VT_TYPE_EQUAL(variant_object, sub_type_desired) (VT_INDEX(variant_object, sub_type_desired) == ::type_index()

#endif

template <typename T> struct StripType { using type = std::remove_cv_t<std::remove_reference_t<T>>; };

template <typename T> using strip_type_t = typename StripType<T>::type;

#define DEFINE_TRIVIAL_PAIR(struct_name, T0, first_name, T1, second_name)                                    \
    struct struct_name {                                                                                     \
        static_assert(std::is_trivially_copyable<T0>::value, "");                                            \
        static_assert(std::is_trivially_copyable<T1>::value, "");                                            \
                                                                                                             \
        T0 first_name;                                                                                       \
        T1 second_name;                                                                                      \
                                                                                                             \
        struct_name() = default;                                                                             \
                                                                                                             \
        struct_name(const T0 &t1, const T1 &t2)                                                              \
            : first_name(t1)                                                                                 \
            , second_name(t2) {}                                                                             \
                                                                                                             \
        struct_name(const struct_name &) = default;                                                          \
    }
