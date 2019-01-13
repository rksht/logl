/* pmr_vector.h                  -*-C++-*-
 *
 *            Copyright 2017 Pablo Halpern.
 * Distributed under the Boost Software License, Version 1.0.
 *    (See accompanying file LICENSE_1_0.txt or copy at
 *          http://www.boost.org/LICENSE_1_0.txt)
 */

#ifndef INCLUDED_PMR_VECTOR_DOT_H
#define INCLUDED_PMR_VECTOR_DOT_H

#include <vector>
#include <halpern_pmr/polymorphic_allocator.h>

namespace halpern {
namespace pmr {

    // C++17 vector container that uses a polymorphic allocator
    template <class Tp>
    using vector = std::vector<Tp, polymorphic_allocator<Tp>>;

}
}

#endif // ! defined(INCLUDED_PMR_VECTOR_DOT_H)
