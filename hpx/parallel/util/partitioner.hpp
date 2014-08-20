//  Copyright (c) 2007-2014 Hartmut Kaiser
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#if !defined(HPX_PARALLEL_UTIL_PARTITIONER_MAY_27_2014_1040PM)
#define HPX_PARALLEL_UTIL_PARTITIONER_MAY_27_2014_1040PM

#include <hpx/hpx_fwd.hpp>
#include <hpx/async.hpp>
#include <hpx/exception_list.hpp>
#include <hpx/lcos/wait_all.hpp>
#include <hpx/lcos/local/dataflow.hpp>
#include <hpx/util/bind.hpp>
#include <hpx/parallel/execution_policy.hpp>
#include <hpx/parallel/detail/algorithm_result.hpp>
#include <hpx/util/decay.hpp>

#include <boost/multiprecision/cpp_int.hpp>
using boost::multiprecision::uint128_t;

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace parallel { namespace util
{
    struct static_partitioner_tag {};
    struct auto_partitioner_tag {};
    struct default_partitioner_tag {};
}}}

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace parallel { namespace traits
{
    template <typename ExPolicy, typename Enable = void>
    struct extract_partitioner
    {
        typedef parallel::util::default_partitioner_tag type;
    };
}}}

///////////////////////////////////////////////////////////////////////////////
namespace hpx { namespace parallel { namespace util
{
    namespace detail
    {
        ///////////////////////////////////////////////////////////////////////
        template <typename ExPolicy>
        struct handle_local_exceptions
        {
            // std::bad_alloc has to be handled separately
            static void call(boost::exception_ptr const& e,
                std::list<boost::exception_ptr>& errors)
            {
                try {
                    boost::rethrow_exception(e);
                }
                catch (std::bad_alloc const& ba) {
                    boost::throw_exception(ba);
                }
                catch (...) {
                    errors.push_back(e);
                }
            }

            template <typename T>
            static void call(std::vector<hpx::future<T> > const& workitems,
                std::list<boost::exception_ptr>& errors)
            {
                for (hpx::future<T> const& f: workitems)
                {
                    if (f.has_exception())
                        call(f.get_exception_ptr(), errors);
                }

                if (!errors.empty())
                    boost::throw_exception(exception_list(std::move(errors)));
            }
        };

        ///////////////////////////////////////////////////////////////////////
        template <>
        struct handle_local_exceptions<parallel_vector_execution_policy>
        {
            static void call(boost::exception_ptr const&,
                std::list<boost::exception_ptr>&)
            {
                std::terminate();
            }

            template <typename T>
            static void call(std::vector<hpx::future<T> > const& workitems,
                std::list<boost::exception_ptr>&)
            {
                for (hpx::future<T> const& f: workitems)
                {
                    if (f.has_exception())
                        hpx::terminate();
                }
            }
        };

        ///////////////////////////////////////////////////////////////////////
        template <typename R, typename F, typename FwdIter>
        boost::uint64_t add_ready_future(std::vector<hpx::future<R> >& workitems,
            F && f, FwdIter first, std::size_t count)
        {
            boost::uint64_t t = hpx::util::high_resolution_clock::now();
            R ret = f(first, count);
            t = (hpx::util::high_resolution_clock::now() - t);
            workitems.push_back(hpx::make_ready_future(ret));
            return t;
        }

        template <typename F, typename FwdIter>
        boost::uint64_t add_ready_future(std::vector<hpx::future<void> >&,
            F && f, FwdIter first, std::size_t count)
        {
            boost::uint64_t t = hpx::util::high_resolution_clock::now();
            f(first, count);
            t = (hpx::util::high_resolution_clock::now() - t);
            return t;
        }

        // estimate a chunk size based on number of cores used
        template <typename ExPolicy, typename Result, typename F1, typename FwdIter>
        std::size_t auto_chunk_size(
            ExPolicy const& policy,
            std::vector<hpx::future<Result> >& workitems,
            F1 && f1, FwdIter& first, std::size_t& count)
        {
            std::size_t startup_size = 1; // one startup iteration
            std::size_t test_chunk_size = 1; //(std::max)(count / 1000, (std::size_t)1);

            // get executor
            threads::executor exec = policy.get_executor();

            // get number of cores available
            std::size_t const cores = hpx::get_os_thread_count(exec);

            // get target chunk time
            boost::chrono::nanoseconds desired_chunktime_ns = policy.get_chunk_time();

            // If no chunktime is supplied, fall back to 64us * cores
            if(desired_chunktime_ns.count() == 0)
                desired_chunktime_ns = boost::chrono::nanoseconds(512000);

            // make sure we have enough work left to actually run the benchmark
            if(count < test_chunk_size + startup_size) return 0;

            // add startup iteration(s), as in some cases the first iteration(s)
            // are slower. (cache effects and stuff)
            if(startup_size > 0)
            {
                add_ready_future(workitems, f1, first, startup_size);
                std::advance(first, startup_size);
                count -= startup_size;
            }

            // run the benchmark iteration
            boost::uint64_t t = add_ready_future(workitems, f1, first, test_chunk_size);

            // mark benchmarked items as processed
            std::advance(first, test_chunk_size);
            count -= test_chunk_size;

            // get the timer step size
            boost::uint64_t t_min = hpx::util::high_resolution_clock::min();

            // if time was smaller than being able to measure, consider it to be
            // the smallest possible amount of time. this will get important,
            // and is an approximation necessary to prevent the creation of too
            // many asyncs.
            if(t <= t_min) t = t_min;

            // calculate number of chunks, round
            std::size_t num_chunks_dividend = static_cast<std::size_t>
                                (t * count);
            std::size_t num_chunks_divisor  = static_cast<std::size_t>
                                (test_chunk_size * desired_chunktime_ns.count());
            std::size_t num_chunks =
                                (num_chunks_dividend + num_chunks_divisor / 2) 
                                                            / num_chunks_divisor;

            // if benchmark returned smallest amount of time, prevent creation
            // of too many asyncs and do normal geometric distribution
            if(t == t_min && num_chunks > cores)
                num_chunks = cores;

            // prevent 0 chunks
            if(num_chunks == 0) num_chunks = 1;

            // calculate desired chunksize from number of chunks, ceil
            std::size_t chunksize = (count + num_chunks - 1) / num_chunks; 

            return chunksize;
        }

        template <typename ExPolicy, typename Result, typename F1,
            typename FwdIter>
        std::size_t get_static_chunk_size(ExPolicy const& policy,
            std::vector<hpx::future<Result> >& workitems,
            F1 && f1, FwdIter& first, std::size_t& count,
            std::size_t chunk_size)
        {
            threads::executor exec = policy.get_executor();
            if (chunk_size == 0)
            {
                chunk_size = policy.get_chunk_size();
                if (chunk_size == 0)
                {
                    std::size_t const cores = hpx::get_os_thread_count(exec);
                    if(count >= 100*cores)
                        chunk_size = auto_chunk_size(policy, workitems, f1,
                                                     first, count);

                    if (chunk_size == 0)
                        chunk_size = (count + cores - 1) / cores;
                }
            }
            return chunk_size;
        }

        ///////////////////////////////////////////////////////////////////////
        // The static partitioner simply spawns one chunk of iterations for
        // each available core.
        struct work_distribution
        {
            work_distribution(std::size_t count,
                              std::size_t cores,
                              std::size_t chunk_size)
            {
                // try to split the work by the number of cores.
                // this will be the size of the smaller cores.
                workitems_per_core_small = count / cores;

                // large cores will have one work item more.
                workitems_per_core_large = workitems_per_core_small + 1;

                // get number of leftover packets
                // this equals the number of large workers.
                // every leftover packet will be added to one of the
                // small workers, creating a large worker.
                num_large_workers = count % cores;

                // get number of chunks for cores with less work
                chunks_per_core_small = workitems_per_core_small / chunk_size;

                // add one (smaller) chunk if it can't be evenly divided
                if(workitems_per_core_small % chunk_size)
                    chunks_per_core_small++;

                // get number of chunks for cores with more work
                chunks_per_core_large = workitems_per_core_large / chunk_size;

                // add one (smaller) chunk if it can't be evenly divided
                if(workitems_per_core_large % chunk_size)
                    chunks_per_core_large++;

                // calculate total number of chunks
                num_chunks_total =
                    num_large_workers * chunks_per_core_large +
                    (cores - num_large_workers) * chunks_per_core_small;
            }

            size_t num_chunks_total;
            size_t workitems_per_core_small;
            size_t workitems_per_core_large;
            size_t num_large_workers;
            size_t chunks_per_core_small;
            size_t chunks_per_core_large;
        };

        template <typename ExPolicy, typename Result = void>
        struct foreach_n_static_partitioner
        {
        private:
            typedef hpx::util::tuple<
                std::size_t, // count
                std::size_t, // chunk_size
                std::size_t, // offset
                std::vector<hpx::future<Result> >& // workitems
            > arguments_type;

            struct call_parallel_sub
            {
                template <typename FwdIter, typename F1>
                void operator()(ExPolicy const& policy, FwdIter first,
                    F1 && f1, arguments_type args) const
                {
                    threads::executor exec = policy.get_executor();

                    std::size_t count       = hpx::util::get<0>(args);
                    std::size_t chunk_size  = hpx::util::get<1>(args);
                    std::size_t offset      = hpx::util::get<2>(args);
                    std::vector<hpx::future<Result> >& workitems =
                                                            hpx::util::get<3>(args);

                    while(count > chunk_size)
                    {
                        if(exec)
                        {
                            workitems[offset] = hpx::async(exec, f1, first, chunk_size);
                        }
                        else
                        {
                            workitems[offset] = hpx::async(hpx::launch::fork,
                                                            f1, first, chunk_size);
                        }
                        count -= chunk_size;
                        std::advance(first, chunk_size);
                        offset++;
                    }

                    // execute last chunk directly
                    if(count != 0)
                    {
                         workitems[offset] = hpx::async(hpx::launch::sync,
                                                         f1, first, count);
                         std::advance(first, count);
                    }
                }
            };

        public:
            template <typename FwdIter, typename F1>
            static FwdIter call_nonparallel(ExPolicy const& policy,
                std::vector<hpx::future<Result> > && workitems, FwdIter first,
                std::size_t count, F1 && f1, std::size_t chunk_size)
            {
                std::list<boost::exception_ptr> errors;

                try {
                    // get number of chunks
                    std::size_t num_chunks = (count + chunk_size - 1) / chunk_size;

                    workitems.reserve(workitems.size() + num_chunks);

                    // get the executor
                    threads::executor exec = policy.get_executor();

                    while (count > chunk_size)
                    {
                        if (exec)
                        {
                            workitems.push_back(hpx::async(exec, f1, first,
                                chunk_size));
                        }
                        else
                        {
                            workitems.push_back(hpx::async(hpx::launch::fork,
                                f1, first, chunk_size));
                        }
                        count -= chunk_size;
                        std::advance(first, chunk_size);
                    }

                    if (count != 0)
                    {
                        workitems.push_back(hpx::async(hpx::launch::sync,
                            f1, first, chunk_size));
                    }
                }
                catch (...) {
                    detail::handle_local_exceptions<ExPolicy>::call(
                        boost::current_exception(), errors);
                }

                // wait for all tasks to finish
                hpx::wait_all(workitems);
                detail::handle_local_exceptions<ExPolicy>::call(
                        workitems, errors);

                return first;

            }

            template <typename FwdIter, typename F1>
            static FwdIter call_parallel(ExPolicy const& policy,
                std::vector<hpx::future<Result> > && workitems, FwdIter first,
                std::size_t count, F1 && f1, std::size_t chunk_size)
            {
                std::vector<hpx::future<void> > workers;
                std::list<boost::exception_ptr> errors;

                try {
                    // get the executor
                    threads::executor exec = policy.get_executor();

                    // get the number of cores
                    std::size_t const cores = hpx::get_os_thread_count(exec);

                    // calculate the work distribution
                    work_distribution work_dist(count, cores, chunk_size);

                    // resize the array to hold the workitems
                    std::size_t workitems_offset = workitems.size();
                    workitems.resize(workitems_offset + work_dist.num_chunks_total);

                    // create an array to hold the sub-threads
                    workers.reserve(cores);

                    // start all workers
                    std::size_t workitems_of_worker = 0;
                    for(std::size_t i = 0; i < cores - 1; i++)
                    {
                        // if we have x leftover workitems, the workers 0 to x-1
                        // have to process one extra work item
                        if(i < work_dist.num_large_workers)
                        {
                            workitems_of_worker = work_dist.workitems_per_core_large;
                        }
                        else
                        {
                            workitems_of_worker = work_dist.workitems_per_core_small;
                        }

                        if(exec)
                        {
                            workers.push_back(hpx::async(exec,
                                 call_parallel_sub(),
                                 boost::ref(policy), first, f1,
                                 hpx::util::make_tuple(
                                    workitems_of_worker,
                                    chunk_size,
                                    workitems_offset,
                                    boost::ref(workitems)
                                 )));
                        }
                        else
                        {
                            workers.push_back(hpx::async(hpx::launch::fork,
                                 call_parallel_sub(),
                                 boost::ref(policy), first, f1,
                                 hpx::util::make_tuple(
                                    workitems_of_worker,
                                    chunk_size,
                                    workitems_offset,
                                    boost::ref(workitems)
                                 )));
                        }

                        // move to work of next worker
                        std::advance(first, workitems_of_worker);
                        count -= workitems_of_worker;

                        // move forward in result array. again, workers that
                        // have the extra work item could have a different
                        // amount of chunks
                        if(i < work_dist.num_large_workers)
                        {
                            workitems_offset += work_dist.chunks_per_core_large;
                        }
                        else
                        {
                            workitems_offset += work_dist.chunks_per_core_small;
                        }
                    }

                    // execute the last one on current thread

                    // the last worker is always a small one.
                    // (if it would be a large worker, all the workers would be
                    //  the same size, and therefore small workers)
                    workitems_of_worker = work_dist.workitems_per_core_small;

                    workers.push_back(hpx::async(hpx::launch::sync,
                        call_parallel_sub(),
                        boost::ref(policy), first, f1,
                        hpx::util::make_tuple(
                           workitems_of_worker,
                           chunk_size,
                           workitems_offset,
                           boost::ref(workitems)
                        )));

                    std::advance(first, workitems_of_worker);
                    count -= workitems_of_worker;

                    // make sure that we processed all the items
                    HPX_ASSERT(count == 0);
                }
                catch (...) {
                    detail::handle_local_exceptions<ExPolicy>::call(
                        boost::current_exception(), errors);
                }

                // wait for all workers to finish
                hpx::wait_all(workers);
                detail::handle_local_exceptions<ExPolicy>::call(
                    workers, errors);

                // wait for all tasks to finish
                hpx::wait_all(workitems);
                detail::handle_local_exceptions<ExPolicy>::call(
                    workitems, errors);

                return first;
            }
            
            template <typename FwdIter, typename F1>
            static FwdIter call(ExPolicy const& policy, FwdIter first,
                std::size_t count, F1 && f1, std::size_t chunk_size)
            {
                std::vector<hpx::future<Result> > workitems;

                // estimate a chunk size based on number of cores used
                chunk_size = get_static_chunk_size(policy, workitems, f1,
                    first, count, chunk_size);

                // get the executor
                threads::executor exec = policy.get_executor();

                // get the number of cores
                std::size_t const cores = hpx::get_os_thread_count(exec);

                // get number of chunks
                std::size_t num_chunks = (count + chunk_size - 1) / chunk_size;

                // decide which algorithm to use
                if(num_chunks <= cores)
                {
                    return call_nonparallel(policy, std::move(workitems),
                                first, count, f1, chunk_size);
                }
                else
                {
                    return call_parallel(policy, std::move(workitems),
                                first, count, f1, chunk_size);
                }
            }
        };

        ///////////////////////////////////////////////////////////////////////
        template <typename Result>
        struct foreach_n_static_partitioner<task_execution_policy, Result>
        {
        private:
            typedef hpx::util::tuple<
                std::size_t, // count
                std::size_t, // chunk_size
                std::size_t, // offset
                std::vector<hpx::future<Result> >& // workitems
            > arguments_type;

            struct call_parallel
            {
                template <typename FwdIter, typename F1>
                void operator()(task_execution_policy const& policy,
                    FwdIter first, F1 && f1, arguments_type args) const
                {
                    threads::executor exec = policy.get_executor();

                    std::size_t count       = hpx::util::get<0>(args);
                    std::size_t chunk_size  = hpx::util::get<1>(args);
                    std::size_t offset      = hpx::util::get<2>(args);
                    std::vector<hpx::future<Result> >& workitems =
                                                            hpx::util::get<3>(args);

                    while(count > chunk_size)
                    {
                        if(exec)
                        {
                            workitems[offset] = hpx::async(exec, f1, first, chunk_size);
                        }
                        else
                        {
                            workitems[offset] = hpx::async(hpx::launch::fork,
                                                            f1, first, chunk_size);
                        }
                        count -= chunk_size;
                        std::advance(first, chunk_size);
                        offset++;
                    }

                    // execute last chunk directly
                    if(count != 0)
                    {
                         workitems[offset] = hpx::async(hpx::launch::sync,
                                                         f1, first, count);
                         std::advance(first, count);
                    }
                }
            };

        public:
            template <typename FwdIter, typename F1>
            static hpx::future<FwdIter> call(
                task_execution_policy const& policy,
                FwdIter first, std::size_t count, F1 && f1,
                std::size_t chunk_size)
            {
                boost::shared_ptr<std::vector<hpx::future<Result> > > workitems
                    = boost::make_shared<std::vector<hpx::future<Result> > >();
                std::vector<hpx::future<void> > workers;
                std::list<boost::exception_ptr> errors;

                try {
                    // estimate a chunk size based on number of cores used
                    chunk_size = get_static_chunk_size(policy, *workitems, f1,
                        first, count, chunk_size);

                    // get the executor
                    threads::executor exec = policy.get_executor();

                    // get the number of cores
                    std::size_t const cores = hpx::get_os_thread_count(exec);

                    // calculate the work distribution
                    work_distribution work_dist(count, cores, chunk_size);

                    // resize the array to hold the workitems
                    std::size_t workitems_offset = workitems->size();
                    workitems->resize(workitems_offset + work_dist.num_chunks_total);

                    // create an array to hold the sub-threads
                    workers.reserve(cores);

                    // start all workers
                    std::size_t workitems_of_worker = 0;
                    for(std::size_t i = 0; i < cores; i++)
                    {
                        // if we have x leftover workitems, the workers 0 to x-1
                        // have to process one extra work item
                        if(i < work_dist.num_large_workers)
                        {
                            workitems_of_worker = work_dist.workitems_per_core_large;
                        }
                        else
                        {
                            workitems_of_worker = work_dist.workitems_per_core_small;
                        }

                        if(exec)
                        {
                            workers.push_back(hpx::async(exec,
                                 call_parallel(),
                                 boost::ref(policy), first, f1,
                                 hpx::util::make_tuple(
                                    workitems_of_worker,
                                    chunk_size,
                                    workitems_offset,
                                    boost::ref(*workitems)
                                 )));
                        }
                        else
                        {
                            workers.push_back(hpx::async(hpx::launch::fork,
                                 call_parallel(),
                                 boost::ref(policy), first, f1,
                                 hpx::util::make_tuple(
                                    workitems_of_worker,
                                    chunk_size,
                                    workitems_offset,
                                    boost::ref(*workitems)
                                 )));
                        }

                        // move to work of next worker
                        std::advance(first, workitems_of_worker);
                        count -= workitems_of_worker;

                        // move forward in result array. again, workers that
                        // have the extra work item could have a different
                        // amount of chunks
                        if(i < work_dist.num_large_workers)
                        {
                            workitems_offset += work_dist.chunks_per_core_large;
                        }
                        else
                        {
                            workitems_offset += work_dist.chunks_per_core_small;
                        }
                    }

                    // make sure that we processed all the items
                    HPX_ASSERT(count == 0);
                }
                catch (...) {
                    detail::handle_local_exceptions<task_execution_policy>::call(
                        boost::current_exception(), errors);
                }

                // wait for all workers, then wait for all tasks
                return hpx::when_all(workers).then(
                    [first, errors, workitems]
                    (hpx::future<std::vector<hpx::future<void> > > && workers) mutable
                    {
                        detail::handle_local_exceptions<task_execution_policy>
                            ::call(workers.get(), errors);

                        hpx::wait_all(*workitems);
                        detail::handle_local_exceptions<task_execution_policy>
                            ::call(*workitems, errors);

                        return first;
                    });
            }
        };

        ///////////////////////////////////////////////////////////////////////
        // ExPolicy: execution policy
        // Result:   intermediate result type of first step (default: void)
        // PartTag:  select appropriate partitioner
        template <typename ExPolicy, typename Result, typename PartTag>
        struct foreach_n_partitioner;

        ///////////////////////////////////////////////////////////////////////
        template <typename ExPolicy, typename Result>
        struct foreach_n_partitioner<ExPolicy, Result, static_partitioner_tag>
        {
            template <typename FwdIter, typename F1>
            static FwdIter call(ExPolicy const& policy, FwdIter first,
                std::size_t count, F1 && f1)
            {
                return foreach_n_static_partitioner<ExPolicy, Result>::call(
                    policy, first, count, std::forward<F1>(f1), 0);
            }
        };

        template <typename Result>
        struct foreach_n_partitioner<
            task_execution_policy, Result, static_partitioner_tag>
        {
            template <typename FwdIter, typename F1>
            static hpx::future<FwdIter> call(
                task_execution_policy const& policy,
                FwdIter first, std::size_t count, F1 && f1)
            {
                return foreach_n_static_partitioner<
                        task_execution_policy, Result
                    >::call(policy, first, count, std::forward<F1>(f1), 0);
            }
        };

        ///////////////////////////////////////////////////////////////////////
        template <typename ExPolicy, typename Result>
        struct foreach_n_partitioner<ExPolicy, Result, default_partitioner_tag>
          : foreach_n_partitioner<ExPolicy, Result, static_partitioner_tag>
        {};
    }

    ///////////////////////////////////////////////////////////////////////////
    template <typename ExPolicy, typename Result = void,
        typename PartTag = typename parallel::traits::extract_partitioner<
            typename hpx::util::decay<ExPolicy>::type
        >::type>
    struct foreach_n_partitioner
      : detail::foreach_n_partitioner<
            typename hpx::util::decay<ExPolicy>::type, Result, PartTag>
    {};

    ///////////////////////////////////////////////////////////////////////////
    namespace detail
    {
        template <typename R, typename F, typename FwdIter>
        void add_ready_future_idx(std::vector<hpx::future<R> >& workitems,
            F && f, std::size_t base_idx, FwdIter first, std::size_t count)
        {
            workitems.push_back(
                hpx::make_ready_future(f(base_idx, first, count)));
        }

        template <typename F, typename FwdIter>
        void add_ready_future_idx(std::vector<hpx::future<void> >&,
            F && f, std::size_t base_idx, FwdIter first, std::size_t count)
        {
            f(base_idx, first, count);
        }

        // estimate a chunk size based on number of cores used, take into
        // account base index
        template <typename Result, typename F1, typename FwdIter>
        std::size_t auto_chunk_size_idx(
            std::vector<hpx::future<Result> >& workitems, F1 && f1,
            std::size_t& base_idx, FwdIter& first, std::size_t& count)
        {
            std::size_t test_chunk_size = count / 100;
            if (0 == test_chunk_size) return 0;

            boost::uint64_t t = hpx::util::high_resolution_clock::now();
            add_ready_future_idx(workitems, f1, base_idx, first, test_chunk_size);

            t = (hpx::util::high_resolution_clock::now() - t) / test_chunk_size;

            base_idx += test_chunk_size;
            std::advance(first, test_chunk_size);
            count -= test_chunk_size;

            // return chunk size which will create 80 microseconds of work
            return t == 0 ? 0 : (std::min)(count, 80000 / t);
        }

        template <typename ExPolicy, typename Result, typename F1,
            typename FwdIter>
        std::size_t get_static_chunk_size_idx(ExPolicy const& policy,
            std::vector<hpx::future<Result> >& workitems,
            F1 && f1, std::size_t& base_idx, FwdIter& first,
            std::size_t& count, std::size_t chunk_size)
        {
            threads::executor exec = policy.get_executor();
            if (chunk_size == 0)
            {
                chunk_size = policy.get_chunk_size();
                if (chunk_size == 0)
                {
                    std::size_t const cores = hpx::get_os_thread_count(exec);
                    if (count > 100*cores)
                        chunk_size = auto_chunk_size_idx(workitems, f1,
                            base_idx, first, count);

                    if (chunk_size == 0)
                        chunk_size = (count + cores - 1) / cores;
                }
            }
            return chunk_size;
        }

        ///////////////////////////////////////////////////////////////////////
        // The static partitioner simply spawns one chunk of iterations for
        // each available core.
        template <typename ExPolicy, typename R, typename Result = void>
        struct static_partitioner
        {
            template <typename FwdIter, typename F1, typename F2>
            static R call(ExPolicy const& policy, FwdIter first,
                std::size_t count, F1 && f1, F2 && f2, std::size_t chunk_size)
            {
                std::vector<hpx::future<Result> > workitems;
                std::list<boost::exception_ptr> errors;

                try {
                    // estimate a chunk size based on number of cores used
                    chunk_size = get_static_chunk_size(policy, workitems, f1,
                        first, count, chunk_size);

                    // schedule every chunk on a separate thread
                    workitems.reserve(count / chunk_size + 1);

                    threads::executor exec = policy.get_executor();
                    while (count > chunk_size)
                    {
                        if (exec)
                        {
                            workitems.push_back(hpx::async(exec, f1, first,
                                chunk_size));
                        }
                        else
                        {
                            workitems.push_back(hpx::async(hpx::launch::fork, f1,
                                first, chunk_size));
                        }
                        count -= chunk_size;
                        std::advance(first, chunk_size);
                    }

                    // execute last chunk directly
                    if (count != 0)
                    {
                        workitems.push_back(hpx::async(hpx::launch::sync,
                            std::forward<F1>(f1), first, count));
                        std::advance(first, count);
                    }
                }
                catch (...) {
                    detail::handle_local_exceptions<ExPolicy>::call(
                        boost::current_exception(), errors);
                }

                // wait for all tasks to finish
                hpx::wait_all(workitems);
                detail::handle_local_exceptions<ExPolicy>::call(
                    workitems, errors);

                return f2(std::move(workitems));
            }

            template <typename FwdIter, typename F1, typename F2>
            static R call_with_index(ExPolicy const& policy, FwdIter first,
                std::size_t count, F1 && f1, F2 && f2, std::size_t chunk_size)
            {
                std::vector<hpx::future<Result> > workitems;
                std::list<boost::exception_ptr> errors;

                try {
                    // estimate a chunk size based on number of cores used
                    std::size_t base_idx = 0;
                    chunk_size = get_static_chunk_size_idx(policy, workitems,
                        f1, base_idx, first, count, chunk_size);

                    // schedule every chunk on a separate thread
                    workitems.reserve(count / chunk_size + 1);

                    threads::executor exec = policy.get_executor();
                    while (count > chunk_size)
                    {
                        if (exec)
                        {
                            workitems.push_back(hpx::async(exec, f1, base_idx,
                                first, chunk_size));
                        }
                        else
                        {
                            workitems.push_back(hpx::async(hpx::launch::fork,
                                f1, base_idx, first, chunk_size));
                        }
                        count -= chunk_size;
                        std::advance(first, chunk_size);
                        base_idx += chunk_size;
                    }

                    // execute last chunk directly
                    if (count != 0)
                    {
                        workitems.push_back(hpx::async(hpx::launch::sync,
                            std::forward<F1>(f1), base_idx, first, count));
                        std::advance(first, count);
                    }
                }
                catch (...) {
                    detail::handle_local_exceptions<ExPolicy>::call(
                        boost::current_exception(), errors);
                }

                // wait for all tasks to finish
                hpx::wait_all(workitems);
                detail::handle_local_exceptions<ExPolicy>::call(
                    workitems, errors);

                return f2(std::move(workitems));
            }
        };

        template <typename R, typename Result>
        struct static_partitioner<task_execution_policy, R, Result>
        {
            template <typename FwdIter, typename F1, typename F2>
            static hpx::future<R> call(task_execution_policy const& policy,
                FwdIter first, std::size_t count, F1 && f1, F2 && f2,
                std::size_t chunk_size)
            {
                std::vector<hpx::future<Result> > workitems;
                std::list<boost::exception_ptr> errors;

                try {
                    // estimate a chunk size based on number of cores used
                    chunk_size = get_static_chunk_size(policy, workitems, f1,
                        first, count, chunk_size);

                    // schedule every chunk on a separate thread
                    workitems.reserve(count / chunk_size + 1);

                    threads::executor exec = policy.get_executor();
                    while (count > chunk_size)
                    {
                        if (exec)
                        {
                            workitems.push_back(hpx::async(exec, f1, first,
                                chunk_size));
                        }
                        else
                        {
                            workitems.push_back(hpx::async(hpx::launch::fork,
                                f1, first, chunk_size));
                        }
                        count -= chunk_size;
                        std::advance(first, chunk_size);
                    }

                    // add last chunk
                    if (count != 0)
                    {
                        if (exec)
                        {
                            workitems.push_back(hpx::async(exec, f1, first, count));
                        }
                        else
                        {
                            workitems.push_back(hpx::async(hpx::launch::fork,
                                f1, first, count));
                        }
                        std::advance(first, count);
                    }
                }
                catch (...) {
                    detail::handle_local_exceptions<task_execution_policy>::call(
                        boost::current_exception(), errors);
                }

                // wait for all tasks to finish
                return hpx::lcos::local::dataflow(
                    [f2, errors](std::vector<hpx::future<Result> > && r) mutable
                    {
                        detail::handle_local_exceptions<task_execution_policy>
                            ::call(r, errors);
                        return f2(std::move(r));
                    },
                    std::move(workitems));
            }

            template <typename FwdIter, typename F1, typename F2>
            static hpx::future<R> call_with_index(
                task_execution_policy const& policy,
                FwdIter first, std::size_t count, F1 && f1, F2 && f2,
                std::size_t chunk_size)
            {
                std::vector<hpx::future<Result> > workitems;
                std::list<boost::exception_ptr> errors;

                try {
                    // estimate a chunk size based on number of cores used
                    std::size_t base_idx = 0;
                    chunk_size = get_static_chunk_size_idx(policy, workitems,
                        f1, base_idx, first, count, chunk_size);

                    // schedule every chunk on a separate thread
                    workitems.reserve(count / chunk_size + 1);

                    threads::executor exec = policy.get_executor();
                    while (count > chunk_size)
                    {
                        if (exec)
                        {
                            workitems.push_back(hpx::async(exec, f1, base_idx,
                                first, chunk_size));
                        }
                        else
                        {
                            workitems.push_back(hpx::async(hpx::launch::fork,
                                f1, base_idx, first, chunk_size));
                        }
                        count -= chunk_size;
                        std::advance(first, chunk_size);
                        base_idx += chunk_size;
                    }

                    // add last chunk
                    if (count != 0)
                    {
                        if (exec)
                        {
                            workitems.push_back(hpx::async(exec, f1, base_idx,
                                first, count));
                        }
                        else
                        {
                            workitems.push_back(hpx::async(hpx::launch::fork,
                                f1, base_idx, first, count));
                        }
                        std::advance(first, count);
                    }
                }
                catch (...) {
                    detail::handle_local_exceptions<task_execution_policy>::call(
                        boost::current_exception(), errors);
                }

                // wait for all tasks to finish
                return hpx::lcos::local::dataflow(
                    [f2, errors](std::vector<hpx::future<Result> > && r) mutable
                    {
                        detail::handle_local_exceptions<task_execution_policy>
                            ::call(r, errors);
                        return f2(std::move(r));
                    },
                    std::move(workitems));
            }
        };

        ///////////////////////////////////////////////////////////////////////
        // ExPolicy: execution policy
        // R:        overall result type
        // Result:   intermediate result type of first step
        // PartTag:  select appropriate partitioner
        template <typename ExPolicy, typename R, typename Result, typename PartTag>
        struct partitioner;

        ///////////////////////////////////////////////////////////////////////
        template <typename ExPolicy, typename R, typename Result>
        struct partitioner<ExPolicy, R, Result, static_partitioner_tag>
        {
            template <typename FwdIter, typename F1, typename F2>
            static R call(ExPolicy const& policy, FwdIter first,
                std::size_t count, F1 && f1, F2 && f2)
            {
                return static_partitioner<ExPolicy, R, Result>::call(
                    policy, first, count,
                    std::forward<F1>(f1), std::forward<F2>(f2), 0);
            }

            template <typename FwdIter, typename F1, typename F2>
            static R call_with_index(ExPolicy const& policy, FwdIter first,
                std::size_t count, F1 && f1, F2 && f2)
            {
                return static_partitioner<ExPolicy, R, Result>::call_with_index(
                    policy, first, count,
                    std::forward<F1>(f1), std::forward<F2>(f2), 0);
            }
        };

        template <typename R, typename Result>
        struct partitioner<task_execution_policy, R, Result, static_partitioner_tag>
        {
            template <typename FwdIter, typename F1, typename F2>
            static hpx::future<R> call(task_execution_policy const& policy,
                FwdIter first, std::size_t count, F1 && f1, F2 && f2)
            {
                return static_partitioner<
                        task_execution_policy, R, Result
                    >::call(policy, first, count,
                        std::forward<F1>(f1), std::forward<F2>(f2), 0);
            }

            template <typename FwdIter, typename F1, typename F2>
            static hpx::future<R> call_with_index(
                task_execution_policy const& policy,
                FwdIter first, std::size_t count, F1 && f1, F2 && f2)
            {
                return static_partitioner<
                        task_execution_policy, R, Result
                    >::call_with_index(policy, first, count,
                        std::forward<F1>(f1), std::forward<F2>(f2), 0);
            }
        };

        ///////////////////////////////////////////////////////////////////////
        template <typename ExPolicy, typename R, typename Result>
        struct partitioner<ExPolicy, R, Result, default_partitioner_tag>
          : partitioner<ExPolicy, R, Result, static_partitioner_tag>
        {};
    }

    ///////////////////////////////////////////////////////////////////////////
    template <typename ExPolicy, typename R = void, typename Result = R,
        typename PartTag = typename parallel::traits::extract_partitioner<
            typename hpx::util::decay<ExPolicy>::type
        >::type>
    struct partitioner
      : detail::partitioner<
            typename hpx::util::decay<ExPolicy>::type, R, Result, PartTag>
    {};
}}}

#endif
