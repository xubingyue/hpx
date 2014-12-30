//  Copyright (c) 2014 Grant Mercer
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

/// \file parallel/algorithms/lexicographical_compare.hpp

#if !defined(HPX_PARALLEL_DETAIL_LEXI_COMPARE_DEC_30_2014_0312PM)
#define HPX_PARALLEL_DETAIL_LEXI_COMPARE_DEC_30_2014_0312PM

#include <hpx/hpx_fwd.hpp>
#include <hpx/parallel/execution_policy.hpp>
#include <hpx/parallel/algorithms/detail/algorithm_result.hpp>
#include <hpx/parallel/algorithms/detail/predicates.hpp>
#include <hpx/parallel/algorithms/detail/dispatch.hpp>
#include <hpx/parallel/algorithms/for_each.hpp>
#include <hpx/parallel/util/partitioner.hpp>
#include <hpx/parallel/util/loop.hpp>
#include <hpx/parallel/util/zip_iterator.hpp>

#include <algorithm>
#include <iterator>

#include <boost/static_assert.hpp>
#include <boost/utility/enable_if.hpp>
#include <boost/type_traits/is_base_of.hpp>

namespace hpx { namespace parallel { HPX_INLINE_NAMESPACE(v1)
{    
    ///////////////////////////////////////////////////////////////////////////
    // lexicographical_compare
    namespace detail 
    {
        /// \cond NOINTERNAL
        struct lexicographical_compare : public detail::algorithm<lexicographical_compare>
        {
            lexicographical_compare()
                : lexicographical_compare::algorithm("lexicographical_compare")
            {}

            template <typename ExPolicy, typename InIter1, typename InIter2,
                typename Pred>
           static bool
           sequential(ExPolicy const&, InIter1 first1, InIter1 last1, InIter2 first2,
                InIter2 last2, Pred && pred)
            {
                return std::lexicographical_cast(first1, last1, first2, last2, pred);
            }

            template <typename ExPolicy, typename InIter1, typename InIter2,
                typename Pred>
            static typename detail::algorithm_result<ExPolicy>::type
            parallel(ExPolicy const& policy, InIter1 first1, InIter1 last1, InIer2 first2,
                InIter2 last2, Pred && pred)
            {
            
            }
        };
    }


}}}

#endif