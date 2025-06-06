/*
 * Copyright (c) 2019-2022 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#include "pas_config.h"

#if LIBPAS_ENABLED

#include "pas_scavenger.h"

#include <math.h>
#include "pas_all_heaps.h"
#include "pas_baseline_allocator_table.h"
#include "pas_compact_expendable_memory.h"
#include "pas_deferred_decommit_log.h"
#include "pas_dyld_state.h"
#include "pas_epoch.h"
#include "pas_heap_lock.h"
#include "pas_immortal_heap.h"
#include "pas_large_expendable_memory.h"
#include "pas_lock.h"
#include "pas_page_sharing_pool.h"
#include "pas_status_reporter.h"
#include "pas_thread_local_cache.h"
#include "pas_utility_heap.h"
#include "pas_utils.h"
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

static const bool verbose = false;
static bool is_shut_down_enabled = true;

bool pas_scavenger_is_enabled = true;
bool pas_scavenger_eligibility_notification_has_been_deferred = false;
pas_scavenger_state pas_scavenger_current_state = pas_scavenger_state_no_thread;
unsigned pas_scavenger_should_suspend_count = 0;
pas_scavenger_data* pas_scavenger_data_instance = NULL;

double pas_scavenger_deep_sleep_timeout_in_milliseconds = 10. * 1000.;
#ifdef PAS_LIBMALLOC
double pas_scavenger_period_in_milliseconds = 10.;
uint64_t pas_scavenger_max_epoch_delta = 10ll * 1000ll * 1000ll;
#else
#if PAS_OS(DARWIN) && PAS_X86_64
double pas_scavenger_period_in_milliseconds = 125.;
#else
double pas_scavenger_period_in_milliseconds = 100.;
#endif
uint64_t pas_scavenger_max_epoch_delta = 300ll * 1000ll * 1000ll;
#endif

static uint32_t pas_scavenger_tick_count = 0;
/* Run thread-local-cache decommit once a N. It should be power of two. */
#define PAS_THREAD_LOCAL_CACHE_DECOMMIT_PERIOD_COUNT 128 /* Roughly speaking, it runs once per 13 seconds. */

#if PAS_OS(DARWIN)
static _Atomic qos_class_t pas_scavenger_requested_qos_class = QOS_CLASS_USER_INITIATED;

void pas_scavenger_set_requested_qos_class(qos_class_t qos_class)
{
    pas_scavenger_requested_qos_class = qos_class;
}
#endif

pas_scavenger_activity_callback pas_scavenger_did_start_callback = NULL;
pas_scavenger_activity_callback pas_scavenger_completion_callback = NULL;
pas_scavenger_activity_callback pas_scavenger_will_shut_down_callback = NULL;

static pas_scavenger_data* ensure_data_instance(pas_lock_hold_mode heap_lock_hold_mode)
{
    pas_scavenger_data* instance;
    
    instance = pas_scavenger_data_instance;

    pas_compiler_fence();

    if (instance)
        return instance;
    
    pas_heap_lock_lock_conditionally(heap_lock_hold_mode);
    instance = pas_scavenger_data_instance;
    if (!instance) {
        instance = pas_immortal_heap_allocate(
            sizeof(pas_scavenger_data),
            "pas_scavenger_data",
            pas_object_allocation);
        
        pthread_mutex_init(&instance->lock, NULL);
        pthread_cond_init(&instance->cond, NULL);
        pthread_mutex_init(&instance->foreign_work.lock, NULL);

        pas_fence();
        
        pas_scavenger_data_instance = instance;
    }
    pas_heap_lock_unlock_conditionally(heap_lock_hold_mode);
    
    return instance;
}

static double get_time_in_milliseconds(void)
{
    struct timeval current_time;
    
    gettimeofday(&current_time, NULL);
    
    return current_time.tv_sec * 1000. + current_time.tv_usec / 1000.;
}

static void timed_wait(pthread_cond_t* cond, pthread_mutex_t* mutex,
                       double absolute_timeout_in_milliseconds)
{
    struct timespec time_to_wake_up;
    
    time_to_wake_up.tv_sec = (unsigned)(
        absolute_timeout_in_milliseconds / 1000.);
    time_to_wake_up.tv_nsec = (unsigned)(
        (uint64_t)(absolute_timeout_in_milliseconds * 1000. * 1000.) %
        (uint64_t)(1000. * 1000. * 1000.));
    
    if (verbose) {
        pas_log("Doing timed wait with target wake up at %.2lf.\n",
               absolute_timeout_in_milliseconds);
    }
    pthread_cond_timedwait(cond, mutex, &time_to_wake_up);
    if (verbose)
        pas_log("Woke up from timed wait at %.2lf.\n", get_time_in_milliseconds());
}

static bool handle_expendable_memory(pas_expendable_memory_scavenge_kind kind)
{
    bool should_go_again = false;
    pas_heap_lock_lock();
    should_go_again |= pas_compact_expendable_memory_scavenge(kind);
    should_go_again |= pas_large_expendable_memory_scavenge(kind);
    pas_heap_lock_unlock();
    return should_go_again;
}

static void* scavenger_thread_main(void* arg)
{
    pas_scavenger_data* data;
    pas_scavenger_activity_callback did_start_callback;
#if PAS_OS(DARWIN)
    qos_class_t configured_qos_class;
#endif
    
    PAS_UNUSED_PARAM(arg);
    
    PAS_ASSERT(pas_scavenger_current_state == pas_scavenger_state_polling);

    if (verbose)
        pas_log("Scavenger is running in thread %p\n", (void*)pthread_self());

#if PAS_OS(DARWIN) || PAS_PLATFORM(PLAYSTATION)
#if PAS_BMALLOC
    pthread_setname_np("JavaScriptCore libpas scavenger");
#else
    pthread_setname_np("libpas scavenger");
#endif
#endif

    did_start_callback = pas_scavenger_did_start_callback;
    if (did_start_callback)
        did_start_callback();
    
    data = ensure_data_instance(pas_lock_is_not_held);
    
#if PAS_OS(DARWIN)
    configured_qos_class = pas_scavenger_requested_qos_class;
    pthread_set_qos_class_self_np(configured_qos_class, 0);
#endif

    PAS_PROFILE(SCAVENGER_THREAD_MAIN, data);

    for (;;) {
        pas_page_sharing_pool_scavenge_result scavenge_result;
        bool should_shut_down;
        double time_in_milliseconds;
        double absolute_timeout_in_milliseconds_for_period_sleep;
        pas_scavenger_activity_callback completion_callback;
        pas_thread_local_cache_decommit_action thread_local_cache_decommit_action;
        bool should_go_again;
        uint64_t epoch;
        uint64_t delta;
        uint64_t max_epoch;
        int installed_foreign_work_descriptors;
        bool did_overflow;
#if PAS_OS(DARWIN)
        qos_class_t current_qos_class;
#endif

#if PAS_OS(DARWIN)
        current_qos_class = pas_scavenger_requested_qos_class;
        if (configured_qos_class != current_qos_class) {
            configured_qos_class = current_qos_class;
            pthread_set_qos_class_self_np(configured_qos_class, 0);
        }
#endif
        ++pas_scavenger_tick_count;

        should_go_again = false;
        
        if (verbose)
            pas_log("Scavenger is running.\n");

#if PAS_LOCAL_ALLOCATOR_MEASURE_REFILL_EFFICIENCY
        pas_local_allocator_refill_efficiency_lock_lock();
        pas_log("%d: Refill efficiency: %lf\n",
                getpid(),
                pas_local_allocator_refill_efficiency_sum / pas_local_allocator_refill_efficiency_n);
        pas_local_allocator_refill_efficiency_lock_unlock();
#endif /* PAS_LOCAL_ALLOCATOR_MEASURE_REFILL_EFFICIENCY */
        
        should_go_again |=
            pas_baseline_allocator_table_for_all(pas_allocator_scavenge_request_stop_action);

        should_go_again |=
            pas_utility_heap_for_all_allocators(pas_allocator_scavenge_request_stop_action,
                                                pas_lock_is_not_held);
        
        thread_local_cache_decommit_action = pas_thread_local_cache_decommit_no_action;
        if ((pas_scavenger_tick_count % PAS_THREAD_LOCAL_CACHE_DECOMMIT_PERIOD_COUNT) == 0) {
            if (verbose)
                pas_log("Attempt to decommit unused TLC\n");
            thread_local_cache_decommit_action = pas_thread_local_cache_decommit_if_possible_action;
        }
        should_go_again |=
            pas_thread_local_cache_for_all(pas_allocator_scavenge_request_stop_action,
                                           pas_deallocator_scavenge_flush_log_if_clean_action,
                                           thread_local_cache_decommit_action);

        should_go_again |= handle_expendable_memory(pas_expendable_memory_scavenge_periodic);

        /* For the purposes of performance tuning, as well as some of the scavenger tests, the epoch
           is time in nanoseconds.
           
           But for some tests, including some scavenger tests, the epoch is just a counter.
           
           This code is engineered to kind of limp along when the epoch is a counter, but it doesn't
           actually achieve its full purpose unless the epoch really is time. */
        epoch = pas_get_epoch();
        delta = pas_scavenger_max_epoch_delta;

        did_overflow = __builtin_sub_overflow(epoch, (uint64_t)delta, &max_epoch);
        if (did_overflow)
            max_epoch = PAS_EPOCH_MIN;

        if (verbose)
            pas_log("epoch = %llu, delta = %llu, max_epoch = %llu\n", (unsigned long long)epoch, (unsigned long long)delta, (unsigned long long)max_epoch);

        scavenge_result = pas_physical_page_sharing_pool_scavenge(max_epoch);

        switch (scavenge_result.take_result) {
        case pas_page_sharing_pool_take_none_available:
            break;
            
        case pas_page_sharing_pool_take_none_within_max_epoch:
            should_go_again = true;
            break;
            
        case pas_page_sharing_pool_take_success: {
            PAS_ASSERT(!"Should not see pas_page_sharing_pool_take_success.");
            break;
        }

        case pas_page_sharing_pool_take_locks_unavailable: {
            PAS_ASSERT(!"Should not see pas_page_sharing_pool_take_locks_unavailable.");
            break;
        } }


        installed_foreign_work_descriptors = data->foreign_work.next_open_descriptor;
        PAS_ASSERT(installed_foreign_work_descriptors <= PAS_SCAVENGER_MAX_FOREIGN_WORK_DESCRIPTORS);
        pas_fence();
        for (int i = 0; i < installed_foreign_work_descriptors; i++) {
            void* userdata = data->foreign_work.descriptors[i].userdata;
            uint32_t requested_period_ticks = 1 << data->foreign_work.descriptors[i].period_log2_ticks;
            if (!requested_period_ticks || pas_scavenger_tick_count % requested_period_ticks == 0)
                should_go_again |= data->foreign_work.descriptors[i].func(userdata);
        }

        if (verbose) {
            pas_log("%d: %.0lf: scavenger freed %zu bytes (%s, should_go_again = %s).\n",
                    getpid(), get_time_in_milliseconds(), scavenge_result.total_bytes,
                    pas_page_sharing_pool_take_result_get_string(scavenge_result.take_result),
                    should_go_again ? "yes" : "no");
        }

        completion_callback = pas_scavenger_completion_callback;
        if (completion_callback)
            completion_callback();
        
        should_shut_down = false;
        
        pthread_mutex_lock(&data->lock);
        
        PAS_ASSERT(pas_scavenger_current_state == pas_scavenger_state_polling ||
                   pas_scavenger_current_state == pas_scavenger_state_deep_sleep);
        
        time_in_milliseconds = get_time_in_milliseconds();
        
        if (verbose)
            pas_log("Finished a round of scavenging at %.2lf.\n", time_in_milliseconds);
        
        /* By default we need to sleep for a short while and then try again. */
        absolute_timeout_in_milliseconds_for_period_sleep =
            time_in_milliseconds + pas_scavenger_period_in_milliseconds;

        if (should_go_again) {
            if (verbose)
                pas_log("Waiting for a period.\n");

            /* This field is accessed a lot by other threads, so don't write to it if we don't
               have to. */
            if (pas_scavenger_current_state != pas_scavenger_state_polling)
                pas_scavenger_current_state = pas_scavenger_state_polling;
        } else if (PAS_LIKELY(is_shut_down_enabled)) {
            double absolute_timeout_in_milliseconds_for_deep_pre_sleep;
            
            if (pas_scavenger_current_state == pas_scavenger_state_polling) {
                if (verbose)
                    pas_log("Will consider deep sleep.\n");
                
                /* do one more round of polling but this time indicating that it's the last
                   chance. */
                pas_scavenger_current_state = pas_scavenger_state_deep_sleep;
            } else {
                if (verbose)
                    pas_log("Considering deep sleep.\n");
                
                PAS_ASSERT(pas_scavenger_current_state == pas_scavenger_state_deep_sleep);
                
                absolute_timeout_in_milliseconds_for_deep_pre_sleep =
                    time_in_milliseconds + pas_scavenger_deep_sleep_timeout_in_milliseconds;
                
                /* need to deep sleep and then shut down. */
                while (get_time_in_milliseconds() < absolute_timeout_in_milliseconds_for_deep_pre_sleep
                       && !pas_scavenger_should_suspend_count
                       && pas_scavenger_current_state == pas_scavenger_state_deep_sleep) {
                    timed_wait(&data->cond, &data->lock,
                               absolute_timeout_in_milliseconds_for_deep_pre_sleep);
                }
                
                if (pas_scavenger_current_state == pas_scavenger_state_deep_sleep)
                    should_shut_down = true;
            }
        }
        
        if (PAS_LIKELY(is_shut_down_enabled)) {
            while (get_time_in_milliseconds() < absolute_timeout_in_milliseconds_for_period_sleep
                   && !pas_scavenger_should_suspend_count) {
                timed_wait(&data->cond, &data->lock,
                           absolute_timeout_in_milliseconds_for_period_sleep);
            }

            should_shut_down |= !!pas_scavenger_should_suspend_count;

            if (should_shut_down) {
                pas_scavenger_current_state = pas_scavenger_state_no_thread;
                pthread_cond_broadcast(&data->cond);
            }
        }

        pthread_mutex_unlock(&data->lock);
        
        if (should_shut_down) {
            pas_scavenger_activity_callback shut_down_callback;

            shut_down_callback = pas_scavenger_will_shut_down_callback;
            if (shut_down_callback)
                shut_down_callback();
            
            if (verbose)
                pas_log("Killing the scavenger.\n");
            return NULL;
        }
    }

    PAS_ASSERT(!"Should not be reached");
    return NULL;
}

bool pas_scavenger_try_install_foreign_work_callback(
    pas_scavenger_foreign_work_callback callback,
    uint32_t period_log2_ms,
    void* userdata)
{
    pas_scavenger_data* data;

    PAS_ASSERT(callback);

    data = ensure_data_instance(pas_lock_is_not_held);
    pthread_mutex_lock(&data->foreign_work.lock);

    int slot = data->foreign_work.next_open_descriptor;
    if (slot >= PAS_SCAVENGER_MAX_FOREIGN_WORK_DESCRIPTORS)
        return false;

    double requested_period_ms = pow(2.0, period_log2_ms);
    uint32_t requested_ticks = (uint32_t)(requested_period_ms / pas_scavenger_period_in_milliseconds);

    data->foreign_work.descriptors[slot].period_log2_ticks = pas_log2(requested_ticks);
    data->foreign_work.descriptors[slot].func = callback;
    data->foreign_work.descriptors[slot].userdata = userdata;
    pas_store_store_fence();
    data->foreign_work.next_open_descriptor = slot + 1;

    pthread_mutex_unlock(&data->foreign_work.lock);

    return true;
}

bool pas_scavenger_did_create_eligible(void)
{
    if (pas_scavenger_current_state == pas_scavenger_state_polling)
        return false;
    
    if (!pas_scavenger_is_enabled)
        return false;
    
    if (pas_scavenger_eligibility_notification_has_been_deferred)
        return true;
    
    pas_fence();
    
    pas_scavenger_eligibility_notification_has_been_deferred = true;
    return true;
}

void pas_scavenger_notify_eligibility_if_needed(void)
{
    pas_scavenger_data* data;
    
    if (!pas_scavenger_is_enabled)
        return;
    
    if (!pas_scavenger_eligibility_notification_has_been_deferred)
        return;
    
    if (pas_scavenger_should_suspend_count)
        return;

    if (!pas_dyld_is_libsystem_initialized())
        return;
    
    pas_fence();
    
    pas_scavenger_eligibility_notification_has_been_deferred = false;
    
    pas_fence();
    
    if (pas_scavenger_current_state == pas_scavenger_state_polling)
        return;
    
    if (verbose)
        pas_log("It's not polling so need to do something.\n");
    
    data = ensure_data_instance(pas_lock_is_not_held);
    pthread_mutex_lock(&data->lock);
    
    if (pas_scavenger_should_suspend_count)
        goto done;
    
    if (pas_scavenger_current_state == pas_scavenger_state_no_thread) {
        pthread_t thread;
        int result;
        pas_scavenger_current_state = pas_scavenger_state_polling;
        result = pthread_create(&thread, NULL, scavenger_thread_main, NULL);
        PAS_ASSERT(!result);
        pthread_detach(thread);
    }
    
    if (pas_scavenger_current_state == pas_scavenger_state_deep_sleep) {
        pas_scavenger_current_state = pas_scavenger_state_polling;
        pthread_cond_broadcast(&data->cond);
    }
    
done:
    pthread_mutex_unlock(&data->lock);

    pas_status_reporter_start_if_necessary();
}

void pas_scavenger_suspend(void)
{
    pas_scavenger_data* data;
    data = ensure_data_instance(pas_lock_is_not_held);
    pthread_mutex_lock(&data->lock);
    
    pas_scavenger_should_suspend_count++;
    PAS_ASSERT(pas_scavenger_should_suspend_count);
    
    while (pas_scavenger_current_state != pas_scavenger_state_no_thread)
        pthread_cond_wait(&data->cond, &data->lock);
    
    pthread_mutex_unlock(&data->lock);
}

void pas_scavenger_resume(void)
{
    pas_scavenger_data* data;
    data = ensure_data_instance(pas_lock_is_not_held);
    pthread_mutex_lock(&data->lock);
    
    PAS_ASSERT(pas_scavenger_should_suspend_count);
    
    pas_scavenger_should_suspend_count--;
    
    pthread_mutex_unlock(&data->lock);
    
    /* Just assume that there are empty pages to be scavenged. We wouldn't have been keeping
       track perfectly while the scavenger was suspended. For example, we would not have
       remembered if there still hadd been empty pages at the time that the scavenger had been
       shut down. */
    pas_scavenger_did_create_eligible();
    
    pas_scavenger_notify_eligibility_if_needed();
}

void pas_scavenger_clear_all_non_tlc_caches(void)
{
    pas_baseline_allocator_table_for_all(pas_allocator_scavenge_force_stop_action);

    pas_utility_heap_for_all_allocators(pas_allocator_scavenge_force_stop_action,
                                        pas_lock_is_not_held);
}

void pas_scavenger_clear_all_caches_except_remote_tlcs(void)
{
    pas_thread_local_cache* cache;
    
    cache = pas_thread_local_cache_try_get();
    if (cache)
        pas_thread_local_cache_shrink(cache, pas_lock_is_not_held);

    pas_scavenger_clear_all_non_tlc_caches();
}

void pas_scavenger_clear_all_caches(void)
{
    pas_scavenger_clear_all_caches_except_remote_tlcs();
    
    pas_thread_local_cache_for_all(pas_allocator_scavenge_force_stop_action,
                                   pas_deallocator_scavenge_flush_log_action,
                                   pas_thread_local_cache_decommit_if_possible_action);
}

void pas_scavenger_decommit_expendable_memory(void)
{
    handle_expendable_memory(pas_expendable_memory_scavenge_forced);
}

void pas_scavenger_fake_decommit_expendable_memory(void)
{
    handle_expendable_memory(pas_expendable_memory_scavenge_forced_fake);
}

size_t pas_scavenger_decommit_free_memory(void)
{
    pas_page_sharing_pool_scavenge_result result;

    result = pas_physical_page_sharing_pool_scavenge(PAS_EPOCH_MAX);
    
    PAS_ASSERT(result.take_result == pas_page_sharing_pool_take_none_available);

    return result.total_bytes;
}

void pas_scavenger_run_synchronously_now(void)
{
    pas_scavenger_clear_all_caches();
    pas_scavenger_decommit_expendable_memory();
    pas_scavenger_decommit_free_memory();
}

void pas_scavenger_perform_synchronous_operation(
    pas_scavenger_synchronous_operation_kind kind)
{
    switch (kind) {
    case pas_scavenger_invalid_synchronous_operation_kind:
        PAS_ASSERT(!"Should not be reached");
        return;
    case pas_scavenger_clear_all_non_tlc_caches_kind:
        pas_scavenger_clear_all_non_tlc_caches();
        return;
    case pas_scavenger_clear_all_caches_except_remote_tlcs_kind:
        pas_scavenger_clear_all_caches_except_remote_tlcs();
        return;
    case pas_scavenger_clear_all_caches_kind:
        pas_scavenger_clear_all_caches();
        return;
    case pas_scavenger_decommit_expendable_memory_kind:
        pas_scavenger_decommit_expendable_memory();
        return;
    case pas_scavenger_decommit_free_memory_kind:
        pas_scavenger_decommit_free_memory();
        return;
    case pas_scavenger_run_synchronously_now_kind:
        pas_scavenger_run_synchronously_now();
        return;
    }
    PAS_ASSERT(!"Should not be reached");
}

void pas_scavenger_disable_shut_down(void)
{
    pas_scavenger_suspend();
    is_shut_down_enabled = false;
    pas_scavenger_resume();
}

#endif /* LIBPAS_ENABLED */
