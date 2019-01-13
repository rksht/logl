#pragma once

// Template based programming does have its cons. But it is what it is.

#include <algorithm>
#include <limits>
#include <stdint.h>
#include <string>
#include <type_traits>

template <typename Int, Int _value> struct IntegerConstant {
    static constexpr Int value = _value;
    using type = Int;

    static std::string to_string() { return std::to_string(value); }
};

template <long n> using ConsInt = IntegerConstant<long, n>;
template <unsigned long n> using ConsUint = IntegerConstant<unsigned long, n>;

#define STRING_CONSTANT_FOR_CONS(struct_name, string)                                                        \
    struct struct_name {                                                                                     \
        static constexpr auto value = string;                                                                \
    };

struct Null {
    static std::string to_string() { return "_"; }
};

template <typename _Car, typename _Cdr> struct Cons {
    using car = _Car;
    using cdr = _Cdr;

    static std::string to_string() {
        std::string s;
        s += car::to_string();
        s += ", ";
        s += cdr::to_string();
        return s;
    };
};

template <typename List> struct Car;

template <typename _Car, typename _Cdr> struct Car<Cons<_Car, _Cdr>> { using type = _Car; };

template <typename List> struct Cdr;

template <typename _Car, typename _Cdr> struct Cdr<Cons<_Car, _Cdr>> { using type = _Cdr; };

template <typename List> using car_t = typename Car<List>::type;

template <typename List> using cdr_t = typename Cdr<List>::type;

template <typename List, size_t n> struct Nth { using type = typename Nth<cdr_t<List>, n - 1>::type; };

template <typename List> struct Nth<List, 0> { using type = car_t<List>; };

template <typename List, size_t n> using nth_t = typename Nth<List, n>::type;

template <typename List> struct Len {
    template <typename List_, size_t n> struct Len_ {
        static constexpr size_t value = Len_<cdr_t<List_>, n + 1>::value;
    };

    template <size_t n> struct Len_<Null, n> { static constexpr size_t value = n; };

    static constexpr size_t value = Len_<List, 0>::value;
};

template <typename List> constexpr size_t len_v = Len<List>::value;

template <typename... Elements> struct Unpack;

template <> struct Unpack<> { using type = Null; };

template <typename H, typename... T> struct Unpack<H, T...> {
    using type = Cons<H, typename Unpack<T...>::type>;
};

template <typename... Elements> using unpack_t = typename Unpack<Elements...>::type;

#if 0
template <template <class...> class Holder, typename List> struct Pack {
    using type = Holder<nth_t<List, std::make_index_sequence<len_v<List>>...>>;
};

template <template <class...> class Holder, typename List> using pack_t = typename<Holder, List>::type;

template <typename...> struct DefaultHolder {};

#endif

template <typename List, size_t n> struct LeftSplit {
    static_assert(len_v<List> >= n, "Length of list shorter than index to split at.");

    template <typename L, size_t remaining> struct Left {
        using type = Cons<car_t<L>, typename Left<cdr_t<L>, remaining - 1>::type>;
    };

    template <typename CurrentCons> struct Left<CurrentCons, 0> { using type = Null; };

    using type = typename Left<List, n>::type;
};

template <typename List, size_t n> using left_split_t = typename LeftSplit<List, n>::type;

template <typename List, size_t n> struct RightSplit {
    static_assert(len_v<List> >= n, "Length of list shorter than index to split at.");

    template <typename L, size_t remaining> struct Right {
        using type = typename Right<cdr_t<L>, remaining - 1>::type;
    };

    template <typename CurrentCons> struct Right<CurrentCons, 0> { using type = CurrentCons; };

    using type = typename Right<List, n>::type;
};

template <typename List, size_t n> using right_split_t = typename RightSplit<List, n>::type;

template <typename List1, typename List2> struct Concat {
    using type = Cons<car_t<List1>, typename Concat<cdr_t<List1>, List2>::type>;
};

// Special case when reached the last node (the one before Null)
template <typename CarType, typename List2> struct Concat<Cons<CarType, Null>, List2> {
    using type = Cons<CarType, List2>;
};

template <typename List1, typename List2> using concat_t = typename Concat<List1, List2>::type;

namespace cons_internal {

template <typename List, template <class> class Pred> struct Filter;

template <bool pred_result, typename CheckedElement, typename RemainingList, template <class> class Pred>
struct Filter_;

template <typename CheckedElement, typename RemainingList, template <class> class Pred>
struct Filter_<true, CheckedElement, RemainingList, Pred> {
    using type = Cons<CheckedElement, typename Filter<RemainingList, Pred>::type>;
};

template <typename _, typename RemainingList, template <class> class Pred>
struct Filter_<false, _, RemainingList, Pred> {
    using type = typename Filter<RemainingList, Pred>::type;
};

template <template <class> class Pred> struct Filter<Null, Pred> { using type = Null; };

template <typename List, template <class> class Pred> struct Filter {
    using type = typename Filter_<Pred<car_t<List>>::value, car_t<List>, cdr_t<List>, Pred>::type;
};

} // namespace cons_internal

// Apply a predicate `Pred` on each element of given `List`
template <typename List, template <class> class Pred> using Filter = cons_internal::Filter<List, Pred>;

template <typename List, template <class> class Pred> using filter_t = typename Filter<List, Pred>::type;

template <typename List, template <class> class Func> struct Map {
    using type = Cons<Func<car_t<List>>, typename Map<cdr_t<List>, Func>::type>;
};

template <template <class> class Func> struct Map<Null, Func> { using type = Null; };

// --- Sort

// Merges two sorted lists
template <typename LeftList, typename RightList> struct Merge {
#if 0
    // Extremely inefficient. Both branches get evaluated. Order of 2^N.
    using type = std::conditional_t<(car_t<LeftList>::value <= car_t<RightList>::value),
                                    Cons<car_t<LeftList>, typename Merge<cdr_t<LeftList>, RightList>::type>,
                                    Cons<car_t<RightList>, typename Merge<LeftList, cdr_t<RightList>>::type>>;
#else

    template <bool left_le, typename NextLeft, typename NextRight> struct Choose;

    template <typename NextLeft, typename NextRight> struct Choose<true, NextLeft, NextRight> {
        using type = typename Merge<cdr_t<NextLeft>, NextRight>::type;
    };

    template <typename NextLeft, typename NextRight> struct Choose<false, NextLeft, NextRight> {
        using type = typename Merge<NextLeft, cdr_t<NextRight>>::type;
    };

    static constexpr bool left_le = car_t<LeftList>::value <= car_t<RightList>::value;

    using head = std::conditional_t<left_le, car_t<LeftList>, car_t<RightList>>;

    using type = Cons<head, typename Choose<left_le, LeftList, RightList>::type>;
#endif
};

// Case when at least one of the lists is empty
template <typename LeftList> struct Merge<LeftList, Null> { using type = LeftList; };
template <typename RightList> struct Merge<Null, RightList> { using type = RightList; };
template <> struct Merge<Null, Null> { using type = Null; };

template <typename List, size_t my_size> struct Sort_ {
    static constexpr size_t left_size = my_size / 2;
    static constexpr size_t right_size = my_size - my_size / 2;

    // Split into left and right

    using LeftList = typename Sort_<left_split_t<List, left_size>, left_size>::type;
    using RightList = typename Sort_<right_split_t<List, left_size>, right_size>::type;

    // Merge

    using type = typename Merge<LeftList, RightList>::type;
};

// No element, means 0
template <> struct Sort_<Null, 0> { using type = Null; };

// One element
template <typename List> struct Sort_<List, 1> { using type = List; };

template <typename List> struct Sort { using type = typename Sort_<List, len_v<List>>::type; };

template <typename List> using sort_t = typename Sort<List>::type;

// Associative array (aka dict, map)
struct AssocArray {
    using type = Null;
};

template <typename KeyType, typename ValueType> struct AssocArrayElement {
    using key_type = KeyType;
    using value_type = ValueType;
};

template <typename List, typename KeyToFind> struct AssocArrayGet {
    template <typename CurrentList> struct Get {
        static_assert(std::is_same<CurrentList, Null>::value, "Not a valid list, end doesn't have Null");
        using type = Null;
    };

    template <typename KeyType, typename ValueType, typename Tail>
    struct Get<Cons<AssocArrayElement<KeyType, ValueType>, Tail>> {
        using type = typename Get<Tail>::type;
    };

    template <typename ValueType, typename Tail>
    struct Get<Cons<AssocArrayElement<KeyToFind, ValueType>, Tail>> {
        using type = typename AssocArrayElement<KeyToFind, ValueType>::value_type;
    };

    using type = typename Get<List>::type;
};

template <typename List, typename KeyToFind>
using assoc_array_get_t = typename AssocArrayGet<List, KeyToFind>::type;

template <typename List, typename KeyToFind, typename NewValue, bool add_if_not_present = true>
struct AssocArraySet {
    template <typename CurrentList> struct Set {
        static_assert(std::is_same<CurrentList, Null>::value, "Not a valid list, end doesn't have Null");
        using type = typename std::
            conditional<add_if_not_present, Cons<AssocArrayElement<KeyToFind, NewValue>, Null>, Null>::type;
    };

    template <typename CurrentValue, typename Tail>
    struct Set<Cons<AssocArrayElement<KeyToFind, CurrentValue>, Tail>> {
        using type = Cons<AssocArrayElement<KeyToFind, NewValue>, typename Set<Tail>::type>;
    };

    template <typename NotSameKey, typename CurrentValue, typename Tail>
    struct Set<Cons<AssocArrayElement<NotSameKey, CurrentValue>, Tail>> {
        using type = Cons<AssocArrayElement<NotSameKey, CurrentValue>, typename Set<Tail>::type>;
    };

    using type = typename Set<List>::type;
};

template <typename List, typename KeyToFind, typename NewValue>
using assoc_array_set_t = typename AssocArraySet<List, KeyToFind, NewValue>::type;

template <typename... KeysAndValues> struct MakeAssocArray {
    static_assert(sizeof...(KeysAndValues) % 2 == 0, "Must be key-value pairs, so multiple of 2");

    template <typename... Default> struct Make { using type = Null; };

    template <typename KeyType, typename ValueType, typename... Rest>
    struct Make<KeyType, ValueType, Rest...> {
        using type = Cons<AssocArrayElement<KeyType, ValueType>, typename Make<Rest...>::type>;
    };

    using type = typename Make<KeysAndValues...>::type;
};

template <typename... KeysAndValues>
using make_assoc_array_t = typename MakeAssocArray<KeysAndValues...>::type;

// xyspoon - Making assoc array of integers easier to create/use.

#if defined(CONS_TEST)

#include <iostream>

// Example

template <typename T> struct IsOdd { static constexpr bool value = T::value % 2 != 0; };

using my_list = unpack_t<ConsInt<9>,
                         ConsInt<8>,
                         ConsInt<7>,
                         ConsInt<6>,
                         ConsInt<5>,
                         ConsInt<4>,
                         ConsInt<3>,
                         ConsInt<2>,
                         ConsInt<1>>;
using left_half = left_split_t<my_list, 4>;
using right_half = right_split_t<my_list, 4>;
using joined = concat_t<left_half, right_half>;
using odds = filter_t<my_list, IsOdd>;

using sorted = sort_t<my_list>;

STRING_CONSTANT_FOR_CONS(LanaDelRay, "Lana Del Ray");
STRING_CONSTANT_FOR_CONS(StVincent, "St. Vincent");
STRING_CONSTANT_FOR_CONS(TaylorSwift, "Taylor Swift");

using player_xp =
    make_assoc_array_t<LanaDelRay, ConsInt<90>, StVincent, ConsInt<95>, TaylorSwift, ConsInt<100>>;

int main() {
    std::cout << my_list::to_string() << "\n";
    std::cout << "Left half = " << left_half::to_string() << "\n";
    std::cout << "Right half = " << right_half::to_string() << "\n";
    std::cout << "Concat = " << joined::to_string() << "\n";
    std::cout << "Odds = " << odds::to_string() << "\n";
    std::cout << "Sorted = " << sorted::to_string() << "\n";

    std::cout << "XP of Taylor Swift = " << assoc_array_get_t<player_xp, TaylorSwift>::value << "\n";
}

#endif
