//  Copyright (c) 2014 Grant Mercer
//
//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <hpx/hpx_init.hpp>
#include <hpx/hpx.hpp>
#include <hpx/util/high_resolution_clock.hpp>
#include <hpx/include/algorithm.hpp>
#include <hpx/include/iostreams.hpp>
#include "worker_timed.hpp"

#include <stdexcept>

#include <boost/format.hpp>
#include <boost/cstdint.hpp>

///////////////////////////////////////////////////////////////////////////////
int delay = 1000;
int test_count = 100;
int chunk_size = 0;
int num_overlapping_loops = 0;
boost::chrono::nanoseconds chunk_time;

///////////////////////////////////////////////////////////////////////////////

template<typename T>
T median(std::vector<T> vals)
{
    size_t size = vals.size();

    std::sort(vals.begin(), vals.end());

    if(size % 2 == 0)
    {
        return (vals[size/2 - 1] + vals[size/2]) / 2;
    }
    else
    {
        return vals[(size-1)/2];
    }
}

void measure_sequential_foreach(std::size_t size)
{
    std::vector<std::size_t> data_representation(size);
    std::iota(boost::begin(data_representation),
        boost::end(data_representation),
        std::rand());

    // invoke sequential for_each
    hpx::parallel::for_each(hpx::parallel::seq,
        boost::begin(data_representation),
        boost::end(data_representation),
        [](std::size_t) {
            worker_timed(delay);
        });
}

void measure_parallel_foreach(std::size_t size)
{
    std::vector<std::size_t> data_representation(size);
    std::iota(boost::begin(data_representation),
        boost::end(data_representation),
        std::rand());

    // invoke parallel for_each
    if(chunk_time.count() != 0)
    {
        hpx::parallel::for_each(hpx::parallel::par(chunk_time),
            boost::begin(data_representation),
            boost::end(data_representation),
            [](std::size_t) {
                worker_timed(delay);
            });
    }
    else
    {
        hpx::parallel::for_each(hpx::parallel::par(chunk_size),
            boost::begin(data_representation),
            boost::end(data_representation),
            [](std::size_t) {
                worker_timed(delay);
            });
    }
}

hpx::future<void> measure_task_foreach(std::size_t size)
{
    boost::shared_ptr<std::vector<std::size_t> > data_representation(
        boost::make_shared<std::vector<std::size_t> >(size));
    std::iota(boost::begin(*data_representation),
        boost::end(*data_representation),
        std::rand());
    
    // invoke parallel for_each
    if(chunk_time.count() != 0)
    {
        return
            hpx::parallel::for_each(hpx::parallel::task(chunk_time),
                boost::begin(*data_representation),
                boost::end(*data_representation),
                [](std::size_t) {
                    worker_timed(delay);
                }
            ).then(
                [data_representation](hpx::future<void>) {}
            );
    }
    else
    {
        return
            hpx::parallel::for_each(hpx::parallel::task(chunk_size),
                boost::begin(*data_representation),
                boost::end(*data_representation),
                [](std::size_t) {
                    worker_timed(delay);
                }
            ).then(
                [data_representation](hpx::future<void>) {}
            );
    }
}

boost::uint64_t average_out_parallel(std::size_t vector_size)
{
    std::vector<uint64_t> results;

    // average out 100 executions to avoid varying results
    for(auto i = 0; i < test_count; i++){
        boost::uint64_t t = hpx::util::high_resolution_clock::now();
        measure_parallel_foreach(vector_size);
        t = (hpx::util::high_resolution_clock::now() - t);
        results.push_back(t);
    }

    return median(results);
}

boost::uint64_t average_out_task(std::size_t vector_size)
{
    if (num_overlapping_loops <= 0)
    {
        std::vector<uint64_t> results;
    
        // average out 100 executions to avoid varying results
        for(auto i = 0; i < test_count; i++){
            boost::uint64_t t = hpx::util::high_resolution_clock::now();
            measure_task_foreach(vector_size).wait();
            t = (hpx::util::high_resolution_clock::now() - t);
            results.push_back(t);
        }
    
        return median(results);
    }

    std::vector<hpx::future<void> > tests;
    tests.resize(num_overlapping_loops);

    std::vector<uint64_t> results;
    for(auto i = 0; i < test_count/num_overlapping_loops; i++)
    {

        boost::uint64_t t = hpx::util::high_resolution_clock::now();
        for(auto j = 0; j < num_overlapping_loops; j++)
        {
            tests[j] = measure_task_foreach(vector_size);
        }
        hpx::wait_all(tests);
        t = (hpx::util::high_resolution_clock::now() - t);
        results.push_back(t);
    }

    return median(results) / 4;
}

boost::uint64_t average_out_sequential(std::size_t vector_size)
{
    std::vector<uint64_t> results;

    // average out 100 executions to avoid varying results
    for(auto i = 0; i < test_count; i++){
        boost::uint64_t t = hpx::util::high_resolution_clock::now();
        measure_sequential_foreach(vector_size);
        t = (hpx::util::high_resolution_clock::now() - t);
        results.push_back(t);
    }

    return median(results);
}

///////////////////////////////////////////////////////////////////////////////
int hpx_main(boost::program_options::variables_map& vm)
{
    //pull values from cmd
    std::size_t vector_size = vm["vector_size"].as<std::size_t>();
    bool csvoutput = vm["csv_output"].as<int>() ?true : false;
    delay = vm["work_delay"].as<int>();
    test_count = vm["test_count"].as<int>();
    chunk_size = vm["chunk_size"].as<int>();
    chunk_time = boost::chrono::nanoseconds(vm["chunk_time"].as<unsigned long>());
    num_overlapping_loops = vm["overlapping_loops"].as<int>();

    //verify that input is within domain of program
    if(test_count == 0 || test_count < 0) {
        hpx::cout << "test_count cannot be zero or negative...\n" << hpx::flush;
    } else if (delay < 0) {
        hpx::cout << "delay cannot be a negative number...\n" << hpx::flush;
    } else {

        //results
        boost::uint64_t par_time = average_out_parallel(vector_size);
        boost::uint64_t task_time = average_out_task(vector_size);
        boost::uint64_t seq_time = average_out_sequential(vector_size);

        if(csvoutput) {
            hpx::cout << "," << seq_time/1e9
                      << "," << par_time/1e9
                      << "," << task_time/1e9 << "\n" << hpx::flush;
        }
        else {
        // print results(Formatted). Setw(x) assures that all output is right justified
            hpx::cout << std::left << "----------------Parameters-----------------\n"
                << std::left << "Vector size: " << std::right
                             << std::setw(30) << vector_size << "\n"
                << std::left << "Number of tests" << std::right
                             << std::setw(28) << test_count << "\n"
                << std::left << "Delay per iteration(nanoseconds)"
                             << std::right << std::setw(11) << delay << "\n"
                << std::left << "Display time in: "
                << std::right << std::setw(27) << "Seconds\n" << hpx::flush;

            hpx::cout << "------------------Average------------------\n"
                << std::left << "Average parallel execution time  : "
                             << std::right << std::setw(8) << par_time/1e9 << "\n"
                << std::left << "Average task execution time      : "
                             << std::right << std::setw(8) << task_time/1e9 << "\n"
                << std::left << "Average sequential execution time: "
                             << std::right << std::setw(8) << seq_time/1e9 << "\n" << hpx::flush;

            hpx::cout << "---------Execution Time Difference---------\n"
                << std::left << "Parallel Scale: " << std::right  << std::setw(27)
                             << (double(seq_time) / par_time) << "\n"
                << std::left << "Task Scale    : " << std::right  << std::setw(27)
                             << (double(seq_time) / task_time) << "\n" << hpx::flush;
        }
    }

    return hpx::finalize();
}

///////////////////////////////////////////////////////////////////////////////
int main(int argc, char* argv[])
{
    //initialize program
    std::vector<std::string> cfg;
    cfg.push_back("hpx.os_threads=" +
        boost::lexical_cast<std::string>(hpx::threads::hardware_concurrency()));
    boost::program_options::options_description cmdline(
        "usage: " HPX_APPLICATION_STRING " [options]");

    cmdline.add_options()
        ( "vector_size"
        , boost::program_options::value<std::size_t>()->default_value(1000)
        , "size of vector")

        ("work_delay"
        , boost::program_options::value<int>()->default_value(1)
        , "loop delay per element in nanoseconds")

        ("test_count"
        , boost::program_options::value<int>()->default_value(100)
        , "number of tests to be averaged")

        ("chunk_size"
        , boost::program_options::value<int>()->default_value(0)
        , "number of iterations to combine while parallelization")

        ("chunk_time"
        , boost::program_options::value<unsigned long>()->default_value(0)
        , "target execution time of each chunk in nanoseconds")

        ("overlapping_loops"
        , boost::program_options::value<int>()->default_value(0)
        , "number of overlapping task loops")

        ("csv_output"
        , boost::program_options::value<int>()->default_value(0)
        ,"print results in csv format")
        ;

    return hpx::init(cmdline, argc, argv, cfg);
}
