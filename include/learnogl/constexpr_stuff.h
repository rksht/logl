#pragma once

#include <learnogl/kitchen_sink.h>

/// A constexpr linear probed hash table. @T is the value type. It comes before the @KeyType because this table
/// is usually being used with @KeyType = u32. @MaxKeys is the maximum number of keys the table can hold.
/// @KeyType is the type of keys, usually an integral type. @nil is the type used to denote the "empty" slot
/// in the hash table. A table can be created at compile-time. At runtime, try to simply read from it, don't
/// modify it.
template <typename T, size_t MaxKeys = 64, typename KeyType = u32, KeyType nil = KeyType(0)>
struct CexprSparseArray {
    static constexpr size_t HASH_SLOTS = clip_to_pow2(MaxKeys * 2);

    static_assert(std::is_integral<KeyType>::value || std::is_enum<KeyType>::value,
                  "SparseArray dude. Not SparseHash. Should be an integer type");

    using index_type = KeyType;

    using value_type = T;

    T nil_value;

    struct Item {
        KeyType index = nil;
        T value = {};
    };

    Item items[HASH_SLOTS];

    constexpr CexprSparseArray(T nil_value = T{})
        : nil_value(nil_value)
        , items{} {
        if (nil != KeyType(0)) {
            for (size_t i = 0; i < HASH_SLOTS; ++i) {
                items[i].index = nil;
                items[i].value = {};
            }
        }
    }

  private:
    // A way to notify of errors during constexpr computation. Call this with `something != 0`, and you get an
    // error.
    template <typename ReturnType = int> static constexpr inline ReturnType _cexpr_error(int something = 1) {
        ReturnType a[1] = {};
        return a[something];
    }

  public:
    /// Associate given key and values
    constexpr int set(KeyType i, T value) {
        auto index = static_cast<size_t>(i);
        // Hash
        size_t hash = index % HASH_SLOTS;
        if (items[hash].index == index) {
            items[hash].index = i;
            items[hash].value = value;
            return true;
        }

        size_t seen = 1;

        while (seen < HASH_SLOTS && items[hash].index != nil) {
            ++seen;
            hash = (hash + 1) % HASH_SLOTS;
        }

        if (seen == HASH_SLOTS) {
            return _cexpr_error((int)seen);
        }
        items[hash].index = i;
        items[hash].value = value;
        return _cexpr_error(0);
    }

  private:
    /// If key is present, returns asssociated value. If not, and called at compile time then -
    /// returns_nil_on_fail => returns nil, and !returns_nil_on_fail => triggers a compile time error. At
    /// runtime, we always call with return_nil_on_fail == true.
    constexpr T get(KeyType i, bool return_nil_on_fail = false) const {
        auto index = static_cast<u32>(i);
        // Hash
        size_t hash = index % HASH_SLOTS;
        if (items[hash].index == i) {
            return items[hash].value;
        }

        size_t seen = 1;

        while (seen < HASH_SLOTS && items[hash].index != nil) {
            hash = (hash + 1) % HASH_SLOTS;

            if (items[hash].index == i) {
                break;
            }

            ++seen;
        }

        if (seen == HASH_SLOTS) {
            if (return_nil_on_fail) {
                return nil_value;
            }

            return _cexpr_error<T>((int)seen);
        }

        return items[hash].value;
    }

  public:
    /// Fails at compile time if key doesn't exist. Should use in constexpr expressions only.
    constexpr T must_get(KeyType i) const { return get(i, false); }

    /// Returns the nil value if given key `i` is not present.
    constexpr T get_maybe_nil(KeyType i) const { return get(i, true); }

    /// Operator[] can only be used to access a member's value, not modify it. Use `set` instead.
    constexpr T operator[](KeyType i) const { return get_maybe_nil(i); }
};

namespace cexpr_internal {

template <typename T, size_t MaxKeys, typename KeyType, KeyType nil, typename... Args>
constexpr void add_cexpr_sparse_array(CexprSparseArray<T, MaxKeys, KeyType, nil> &table) {
    UNUSED(table);
}

template <typename T, size_t MaxKeys, typename KeyType, KeyType nil, typename... Args>
constexpr void add_cexpr_sparse_array(CexprSparseArray<T, MaxKeys, KeyType, nil> &table,
                                      KeyType key,
                                      T value,
                                      Args... args) {
    table.set(key, value);
    add_cexpr_sparse_array(table, args...);
}

} // namespace cexpr_internal

/// Create a CexprSparseArray from given keys and values. The 0 value is used as the nil. Convenience
/// function. This allows you to create a CexprSparseArray as a constexpr value at global scope.
template <typename FirstKey, typename FirstValue, typename... Rest>
constexpr auto gen_cexpr_sparse_array(FirstKey key, FirstValue value, Rest... args) {
    static_assert(sizeof...(Rest) % 2 == 0, "Keys and values");

    CexprSparseArray<FirstValue, sizeof...(Rest) + 1, FirstKey, FirstKey(0)> table;
    table.set(key, value);
    cexpr_internal::add_cexpr_sparse_array(table, args...);
    return table;
}
