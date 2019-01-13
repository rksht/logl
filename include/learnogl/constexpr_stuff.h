#pragma once

#include <learnogl/kitchen_sink.h>

// A way to notify of errors during constexpr computation. Call this with `something != 0`, and you get an
// error.
template <typename ReturnType = int> static constexpr inline ReturnType cexpr_error(int something = 1) {
    ReturnType a[1] = {};
    return a[something];
}

template <typename T, size_t MaxKeys = 64, typename IndexType = u32, IndexType nil = IndexType(0)>
struct CexprSparseArray {
    static constexpr size_t HASH_SLOTS = clip_to_pow2(MaxKeys * 2);

    static_assert(std::is_integral<IndexType>::value || std::is_enum<IndexType>::value,
                  "SparseArray dude. Not SparseHash. Should be an integer type");

    using index_type = IndexType;

    using value_type = T;

    T nil_value;

    struct Item {
        IndexType index;
        T value;
    };

    Item items[HASH_SLOTS];

    constexpr CexprSparseArray(T nil_value = T{})
        : nil_value(nil_value)
        , items{} {
        if (nil != IndexType(0)) {
            for (size_t i = 0; i < HASH_SLOTS; ++i) {
                items[i].index = nil;
                items[i].value = {};
            }
        }
    }

    constexpr int set(IndexType i, T value) {
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
            return cexpr_error((int)seen);
        }
        items[hash].index = i;
        items[hash].value = value;
        return cexpr_error(0);
    }

    constexpr T get(IndexType i, bool return_nil_on_fail = false) const {
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

            return cexpr_error<T>((int)seen);
        }

        return items[hash].value;
    }

    // Fails at compile time if key doesn't exist. Use in constexpr expressions only.
    constexpr T must_get(IndexType i) const { return get(i, false); }

    // Returns the nil value if given key `i` is not present.
    constexpr T get_maybe_nil(IndexType i) const { return get(i, true); }

    // Operator[] can only be used to access a member's value, not modify it. Use `set` instead.
    constexpr T operator[](IndexType i) const { return get_maybe_nil(i); }
};

template <typename FirstKey, typename FirstValue, typename... Rest>
constexpr auto gen_cexpr_sparse_array(FirstKey key, FirstValue value, Rest... args) {
    static_assert(sizeof...(Rest) % 2 == 0, "Keys and values");

    CexprSparseArray<FirstValue, sizeof...(Rest) + 1, FirstKey, FirstKey(0)> table;
    table.set(key, value);
    add_cexpr_sparse_array(table, args...);
    return table;
}

template <typename T, size_t MaxKeys, typename IndexType, IndexType nil, typename... Args>
constexpr void add_cexpr_sparse_array(CexprSparseArray<T, MaxKeys, IndexType, nil> &table, IndexType key,
                                      T value, Args... args) {
    table.set(key, value);
    add_cexpr_sparse_array(table, args...);
}

template <typename T, size_t MaxKeys, typename IndexType, IndexType nil, typename... Args>
constexpr void add_cexpr_sparse_array(CexprSparseArray<T, MaxKeys, IndexType, nil> &table) {
    UNUSED(table);
}
