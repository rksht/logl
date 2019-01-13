#pragma once

#include <learnogl/cons.h>

#include <stdint.h>
#include <vector>

namespace cppreflection {

struct RuntimeFieldInfo {
    const char *name;
    size_t offset;
    size_t size;
};

template <typename T> struct FieldNameWrapper {
    using FieldType = T;

    const char *name;
    size_t offset;
    size_t size;
};

// Going to great lengths to not require me giving the name of the struct inside which the field wrapper will
// be generated. To do that, we will actually define non-static member functions which will let us use the
// `this` pointer, and thus obtain decltype(*this).

#define DECL_FIELD_WRAPPER(name)                                                                             \
    REALLY_INLINE inline constexpr auto field_info_##name() const {                                          \
        using ObjectType = std::remove_cv_t<std::remove_pointer_t<decltype(this)>>;                          \
        using FieldType = decltype(reinterpret_cast<ObjectType *>(0)->name);                                 \
        size_t field_size = sizeof(reinterpret_cast<ObjectType *>(0)->name);                                 \
        size_t field_offset = offsetof(ObjectType, name);                                                    \
        return cppreflection::FieldNameWrapper<FieldType>{#name, field_offset, field_size};                  \
    }

#define GET_FIELD_INFO(struct_name, field_name) reinterpret_cast<struct_name *>(0)->field_info_##field_name()

// How can I automatically sort the struct members given their names only?

template <typename Name, typename... FieldNames> struct StructDescription {
    using T_Name = Name;
    using T_FieldsList = unpack_t<FieldNames...>;

    // *--- Collect infos and create a vector of FieldInfo structs ---*

    /// Get the FieldInfo at runtime in an std::vector
    static std::vector<RuntimeFieldInfo> get_field_infos() {
        std::vector<RuntimeFieldInfo> list;
        return list;
    }

    template <typename CurrentList> struct CollectFieldInfos {
        static void push_field_info(std::vector<RuntimeFieldInfo> &list) {
            using Item = car_t<CurrentList>;
            list.push_back(RuntimeFieldInfo{Item::name, Item::offset, Item::size});
            CollectFieldInfos<cdr_t<CurrentList>>::push_field_info(list);
        }
    };

    template <> struct CollectFieldInfos<Null> {
        static void push_field_info(std::vector<RuntimeFieldInfo> &) {}
    };
};

// Convenience macro to specialize the StructDescription template for a particular struct.
#define CREATE_STRUCT_DESCRIPTION(name, ...)                                                                 \
    using StructDescription_##name = StructDescription<name, ##__VA_ARGS__>

} // namespace cppreflection
