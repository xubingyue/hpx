//  Copyright (c) 2014 Grant Mercer
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <hpx/hpx_init.hpp>
#include <hpx/hpx.hpp>
#include <hpx/include/parallel_lexicographical_compare.hpp>
#include <hpx/util/lightweight_test.hpp>

#include "test_utils.hpp"

////////////////////////////////////////////////////////////////////////////
template <typename ExPolicy, typename IteratorTag>
void test_lexicographical_compare1(ExPolicy const& policy, IteratorTag)
{

}

template <typename ExPolicy, typename IteratorTag>
void test_lexicographical_compare(ExPolicy const& p, IteratorTag)
{

}

template <typename IteratorTag>
void test_lexicographical_compare1()
{
    using namespace hpx::parallel;
    test_lexicographical_compare1(seq, IteratorTag());
    test_lexicographical_compare1(par, IteratorTag());
    test_lexicographical_compare1(par_vec, IteratorTag());

    test_lexicographical_compare1_async(seq(task), IteratorTag());
    test_lexicographical_compare1_async(par(task), IteratorTag());


    test_lexicographical_compare1(execution_policy(seq), IteratorTag());
    test_lexicographical_compare1(execution_policy(par), IteratorTag());
    test_lexicographical_compare1(execution_policy(par_vec), IteratorTag());
    test_lexicographical_compare1(execution_policy(seq(task)), IteratorTag());
    test_lexicographical_compare1(execution_policy(par(task)), IteratorTag());
}

void lexicographical_compare_test1()
{
    test_lexicographical_compare1<std::random_access_iterator_tag>();
    test_lexicographical_compare1<std::forward_iterator_tag>();
}

///////////////////////////////////////////////////////////////////////////////
template <typename ExPolicy, typename IteratorTag>
void test_lexicographical_compare_exception(ExPolicy const& policy, IteratorTag)
{

}

template <typename ExPolicy, typename IteratorTag>
void test_lexicographical_compare_async_exception(ExPolicy const& p, IteratorTag)
{

}

template <typename IteratorTag>
void test_lexicographical_compare_exception()
{
    using namespace hpx::parallel;
    //If the execution policy object is of type vector_execution_policy,
    //  std::terminate shall be called. therefore we do not test exceptions
    //  with a vector execution policy
    test_lexicographical_compare_exception(seq, IteratorTag());
    test_lexicographical_compare_exception(par, IteratorTag());

    test_lexicographical_compare_async_exception(seq(task), IteratorTag());
    test_lexicographical_compare_async_exception(par(task), IteratorTag());

    test_lexicographical_compare_exception(execution_policy(par), IteratorTag());
    test_lexicographical_compare_exception(execution_policy(seq(task)), IteratorTag());
    test_lexicographical_compare_exception(execution_policy(par(task)), IteratorTag());
}

void lexicographical_compare_exception_test()
{
    test_lexicographical_compare_exception<std::random_access_iterator_tag>();
    test_lexicographical_compare_exception<std::forward_iterator_tag>();
}

//////////////////////////////////////////////////////////////////////////////
template <typename ExPolicy, typename IteratorTag>
void test_lexicographical_compare_bad_alloc(ExPolicy const& policy, IteratorTag)
{

}

template <typename ExPolicy, typename IteratorTag>
void test_lexicographical_compare_async_bad_alloc(ExPolicy const& p, IteratorTag)
{

}

template <typename IteratorTag>
void test_lexicographical_compare_bad_alloc()
{
    using namespace hpx::parallel;

    // If the execution policy object is of type vector_execution_policy,
    // std::terminate shall be called. therefore we do not test exceptions
    // with a vector execution policy
    test_lexicographical_compare_bad_alloc(par, IteratorTag());
    test_lexicographical_compare_bad_alloc(seq, IteratorTag());

    test_lexicographical_compare_async_bad_alloc(seq(task), IteratorTag());
    test_lexicographical_compare_async_bad_alloc(par(task), IteratorTag());

    test_lexicographical_compare_bad_alloc(execution_policy(par), IteratorTag());
    test_lexicographical_compare_bad_alloc(execution_policy(seq), IteratorTag());
    test_lexicographical_compare_bad_alloc(execution_policy(seq(task)), IteratorTag());
    test_lexicographical_compare_bad_alloc(execution_policy(par(task)), IteratorTag());
}

void lexicographical_compare_bad_alloc_test()
{
    test_lexicographical_compare_bad_alloc<std::random_access_iterator_tag>();
    test_lexicographical_compare_bad_alloc<std::forward_iterator_tag>();
}

int hpx_main(boost::program_options::variables_map& vm)
{

    unsigned int seed = (unsigned int)std::time(0);
    if(vm.count("seed"))
        seed = vm["seed"].as<unsigned int>();

    std::cout << "using seed: " << seed << std::endl;
    std::srand(seed);

    lexicographical_compare_test1();
    lexicographical_compare_test2();
    lexicographical_compare_test3();
    lexicographical_compare_test4();
    lexicographical_compare_exception_test();
    lexicographical_compare_bad_alloc_test();
    return hpx::finalize();
}

int main(int argc, char* argv[])
{
    using namespace boost::program_options;
    options_description desc_commandline(
        "Usage: " HPX_APPLICATION_STRING " [options]");

    desc_commandline.add_options()
        ("seed,s", value<unsigned int>(),
        "the random number generator seed to use for this run")
        ;

    std::vector<std::string> cfg;
    cfg.push_back("hpx.os_threads=" +
        boost::lexical_cast<std::string>(hpx::threads::hardware_concurrency()));

    HPX_TEST_EQ_MSG(hpx::init(desc_commandline, argc, argv, cfg), 0,
        "HPX main exited with non-zero status");

    return hpx::util::report_errors();
}
