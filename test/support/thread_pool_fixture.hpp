// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: natyamatsya/sigslot26 contributors

#pragma once

/**
 * @file thread_pool_fixture.hpp
 * @brief Shared thread pool for execution tests
 *
 * Provides a thread pool that lives for the duration of the test run,
 * avoiding repeated creation/destruction overhead and resource exhaustion
 * issues when running many test iterations.
 *
 * Configure pool size via CMake: -DSIGSLOT_TEST_POOL_SIZE=N
 * Default: 4 threads
 *
 * Usage:
 *   TEST_CASE("my execution test", "[execution]") {
 *       auto& pool = sigslot::test::get_test_pool();
 *       auto sched = pool.get_scheduler();
 *       // ... use scheduler
 *   }
 */

#if defined(SIGSLOT_HAVE_STDEXEC)

#include <exec/static_thread_pool.hpp>

// Configurable pool size (set via CMake)
#ifndef SIGSLOT_TEST_POOL_SIZE
#define SIGSLOT_TEST_POOL_SIZE 4
#endif

namespace sigslot::test {

/**
 * @brief Get a shared thread pool for tests
 * 
 * The pool is created on first access and lives until program exit.
 * Thread-safe to call from multiple test cases.
 * 
 * @param size Number of threads (default: SIGSLOT_TEST_POOL_SIZE)
 * @note Size is only used on first call; subsequent calls return the same pool
 */
inline exec::static_thread_pool& get_test_pool(std::uint32_t size = SIGSLOT_TEST_POOL_SIZE) {
    static exec::static_thread_pool pool{size};
    return pool;
}

} // namespace sigslot::test

#endif // SIGSLOT_HAVE_STDEXEC
