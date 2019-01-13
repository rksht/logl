#ifndef INCLUDED_PMR_MAP_DOT_H
#define INCLUDED_PMR_MAP_DOT_H

#include <map>
#include <halpern_pmr/polymorphic_allocator.h>

namespace halpern {
namespace pmr {

    // C++17 vector container that uses a polymorphic allocator
    template <class Key, class T, class Compare = std::less<Key>>
    using map = std::map<Key, T, polymorphic_allocator<std::pair<const Key, T>>>;

}
}

#endif // ! defined(INCLUDED_PMR_MAP_DOT_H)
