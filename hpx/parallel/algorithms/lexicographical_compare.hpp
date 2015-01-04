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
        struct lexicographical_compare : public detail::algorithm<lexicographical_compare, bool>
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
                return std::lexicographical_compare(first1, last1, first2, last2, pred);
            }

            template <typename ExPolicy, typename InIter1, typename InIter2,
                typename Pred>
            static typename detail::algorithm_result<ExPolicy, bool>::type
            parallel(ExPolicy const& policy, InIter1 first1, InIter1 last1, InIter2 first2,
                InIter2 last2, Pred && pred)
            {
                typedef hpx::util::zip_iterator<InIter1, InIter2> zip_iterator;
                typedef typename zip_iterator::reference reference;

                std::size_t count1 = std::distance(first1, last1);
                std::size_t count2 = std::distance(first2, last2);
                std::size_t count = count1 > count2 ? count2 : count1;

                // An empty range is lexicographically less than any non-empty range
                if(count1 == 0)
                    return detail::algorithm_result<ExPolicy, bool>::get(true);
                if(count2 == 0)
                    return detail::algorithm_result<ExPolicy, bool>::get(false);

                util::cancellation_token<std::size_t> tok(count);

                using hpx::util::make_zip_iterator;
                return util::partitioner<ExPolicy, bool, bool>::
                    call_with_index(
                        policy, make_zip_iterator(first1, first2), count,
                        [pred, tok](std::size_t base_idx, zip_iterator part_begin,
                            std::size_t part_size) -> bool
                        {
                            
                            bool res;
                            util::loop_idx_n(
                                base_idx, part_begin, part_size, tok,
                                [&tok, &res](reference v, std::size_t i)
                                {
                        
                                });
                            return res;
                        },
                        [](std::vector<hpx::future<bool> > && rr) -> bool
                        {
                            return true;
                        });
            }
        };
        /// \endcond
    }

    template <typename ExPolicy, typename InIter1, typename InIter2>
    inline typename boost::enable_if<
        is_execution_policy<ExPolicy>,
        typename detail::algorithm_result<ExPolicy, bool>::type
    >::type
    lexicographical_compare(ExPolicy && policy, InIter1 first1, InIter1 last1,
        InIter2 first2, InIter2 last2)
    {
        typedef typename std::iterator_traits<InIter1>::iterator_category
            iterator_category1;
        typedef typename std::iterator_traits<InIter2>::iterator_category
            iterator_category2;

        BOOST_STATIC_ASSERT_MSG(
            (boost::is_base_of<
                std::input_iterator_tag, iterator_category1
            >::value),
            "Requires at least input iterator.");

        BOOST_STATIC_ASSERT_MSG(
            (boost::is_base_of<
                std::input_iterator_tag, iterator_category2
            >::value),
            "Requires at least input iterator.");

        typedef typename boost::mpl::or_<
            is_sequential_execution_policy<ExPolicy>,
            boost::is_same<std::input_iterator_tag, iterator_category1>,
            boost::is_same<std::input_iterator_tag, iterator_category2>
        >::type is_seq;

        return detail::lexicographical_compare().call(
            std::forward<ExPolicy>(policy),
            first1, last1, first2, last2, detail::equal_to(),
            is_seq());
    }

    template <typename ExPolicy, typename InIter1, typename InIter2, typename Pred>
    inline typename boost::enable_if<
        is_execution_policy<ExPolicy>,
        typename detail::algorithm_result<ExPolicy, bool>::type
    >::type
    lexicographical_compare(ExPolicy && policy, InIter1 first1, InIter1 last1,
        InIter2 first2, InIter2 last2, Pred && pred)
    {
       typedef typename std::iterator_traits<InIter1>::iterator_category
            iterator_category1;
        typedef typename std::iterator_traits<InIter2>::iterator_category
            iterator_category2;

        BOOST_STATIC_ASSERT_MSG(
            (boost::is_base_of<
                std::input_iterator_tag, iterator_category1
            >::value),
            "Requires at least input iterator.");

        BOOST_STATIC_ASSERT_MSG(
            (boost::is_base_of<
                std::input_iterator_tag, iterator_category2
            >::value),
            "Requires at least input iterator.");

        typedef typename boost::mpl::or_<
            is_sequential_execution_policy<ExPolicy>,
            boost::is_same<std::input_iterator_tag, iterator_category>
        >::type is_seq;

        return detail::lexicographical_compare().call(
            std::forward<ExPolicy>(policy),
            first1, last1, first2, last2, 
            std::forward<Pred>(pred),
            is_seq());
    }
}}}

#endif