//@HEADER
// ************************************************************************
//
//                        Kokkos v. 4.0
//       Copyright (2022) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Part of Kokkos, under the Apache License v2.0 with LLVM Exceptions.
// See https://kokkos.org/LICENSE for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//@HEADER

#include <Kokkos_Core.hpp>
#include <Kokkos_Random.hpp>
#include <Kokkos_DualView.hpp>
#include <Kokkos_Timer.hpp>
#include <cstdlib>
#include <iostream>

// cuda
using DeviceType = Kokkos::Device<Kokkos::Cuda,Kokkos::CudaSpace>;
using ViewType = Kokkos::View<double**, DeviceType>;
using ExecutionSpace = Kokkos::Cuda;

// openmp
// using DeviceType = Kokkos::Device<Kokkos::OpenMP,Kokkos::HostSpace>;
// using ViewType = Kokkos::View<double**, DeviceType>;
// using ExecutionSpace = Kokkos::OpenMP;

// Kokkos provides two different random number generators with a 64 bit and a
// 1024 bit state. These generators are based on Vigna, Sebastiano (2014). "An
// experimental exploration of Marsaglia's xorshift generators, scrambled" See:
// http://arxiv.org/abs/1402.6246 The generators can be used fully independently
// on each thread and have been tested to produce good statistics for both inter
// and intra thread numbers. Note that within a kernel NO random number
// operations are (team) collective operations. Everything can be called within
// branches. This is a difference to the curand library where certain operations
// are required to be called by all threads in a block.
//
// In Kokkos you are required to create a pool of generator states, so that
// threads can grep their own. On CPU architectures the pool size is equal to
// the thread number, on CUDA about 128k states are generated (enough to give
// every potentially simultaneously running thread its own state). With a kernel
// a thread is required to acquire a state from the pool and later return it. On
// CPUs the Random number generator is deterministic if using the same number of
// threads. On GPUs (i.e. using the CUDA backend it is not deterministic because
// threads acquire states via atomics.

// A Functor for generating uint64_t random numbers templated on the
// GeneratorPool type
template <class GeneratorPool>
struct generate_random {
  // Output View for the random numbers
  ViewType vals;

  // The GeneratorPool
  GeneratorPool rand_pool;

  int samples;

  // Initialize all members
  generate_random(ViewType vals_, GeneratorPool rand_pool_,
                  int samples_)
      : vals(vals_), rand_pool(rand_pool_), samples(samples_) {}

  KOKKOS_INLINE_FUNCTION
  void operator()(int i) const {
    // Get a random number state from the pool for the active thread
    typename GeneratorPool::generator_type rand_gen = rand_pool.get_state();

    // Draw samples numbers from the pool as urand64 between 0 and
    // rand_pool.MAX_URAND64 Note there are function calls to get other type of
    // scalars, and also to specify Ranges or get a normal distributed float.
    for (int k = 0; k < samples; k++) vals(i, k) = rand_gen.normal();

    // Give the state back, which will allow another thread to acquire it
    rand_pool.free_state(rand_gen);
  }
};

int main(int argc, char* args[]) {
  Kokkos::initialize(argc, args);
  if (argc != 3) {
    printf("Please pass two integers on the command line\n");
  } else {
    std::cout<<"Exec space: "<<ExecutionSpace{}.name() <<std::endl;

    // Initialize Kokkos
    int size    = std::stoi(args[1]);
    int samples = std::stoi(args[2]);

    // Create two random number generator pools one for 64bit states and one for
    // 1024 bit states Both take an 64 bit unsigned integer seed to initialize a
    // Random_XorShift64 generator which is used to fill the generators of the
    // pool.
    Kokkos::Random_XorShift64_Pool<ExecutionSpace> rand_pool64(5374857);
    Kokkos::Random_XorShift1024_Pool<ExecutionSpace> rand_pool1024(5374857);
    ViewType vals("Vals", size, samples);
    auto policy = Kokkos::RangePolicy<ExecutionSpace>(0, size);

    // Run some performance comparisons
    Kokkos::Timer timer;
    for(int i=0;i<5;i++){
      Kokkos::parallel_for(policy,
                         generate_random<Kokkos::Random_XorShift64_Pool<ExecutionSpace> >(
                             vals, rand_pool64, samples));
      Kokkos::fence();
    }

    timer.reset();
    Kokkos::parallel_for(policy,
                         generate_random<Kokkos::Random_XorShift64_Pool<ExecutionSpace> >(
                             vals, rand_pool64, samples));
    Kokkos::fence();
    double time_64 = timer.seconds();


    Kokkos::parallel_for(policy,
                         generate_random<Kokkos::Random_XorShift1024_Pool<ExecutionSpace> >(
                             vals, rand_pool1024, samples));
    Kokkos::fence();

    timer.reset();
    Kokkos::parallel_for(policy,
                         generate_random<Kokkos::Random_XorShift1024_Pool<ExecutionSpace> >(
                             vals, rand_pool1024, samples));
    Kokkos::fence();
    double time_1024 = timer.seconds();

    std::cout<<"Time 64: "<<time_64<<std::endl;
    std::cout<<"Time 1024: "<<time_1024<<std::endl;

    //Kokkos::deep_copy(vals.h_view, vals.d_view);
  }
  Kokkos::finalize();
  return 0;
}
