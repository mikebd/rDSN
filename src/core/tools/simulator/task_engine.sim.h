/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 * 
 * -=- Robust Distributed System Nucleus (rDSN) -=- 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Description:
 *     What is this file about?
 *
 * Revision history:
 *     xxxx-xx-xx, author, first version
 *     xxxx-xx-xx, author, fix bug about xxx
 */

#pragma once

#include <dsn/tool_api.h>
#include <dsn/internal/priority_queue.h>

namespace dsn { namespace tools {

class sim_timer_service : public timer_service
{
public:
    sim_timer_service(service_node* node, timer_service* inner_provider)
        : timer_service(node, inner_provider)
    {
    }

    // after milliseconds, the provider should call task->enqueue()        
    virtual void add_timer(task* task) override;

    virtual void start(io_modifer& ctx) override {}
};

class sim_task_queue : public task_queue
{
public:
    sim_task_queue(task_worker_pool* pool, int index, task_queue* inner_provider);

    virtual void     enqueue(task* task);
    virtual task*    dequeue();

private:
    std::map<uint32_t, task*> _tasks;
};

struct sim_worker_state;
class sim_semaphore_provider : public semaphore_provider
{
public:  
    sim_semaphore_provider(int initial_count, semaphore_provider *inner_provider)
        : semaphore_provider(initial_count, inner_provider), _count(initial_count)
    {
    }

public:
    virtual void signal(int count);
    virtual bool wait(int timeout_milliseconds);

private:
    int _count;
    std::list<sim_worker_state*> _wait_threads;
};

class sim_lock_provider : public lock_provider
{
public:
    sim_lock_provider(lock_provider* inner_provider);
    virtual ~sim_lock_provider();

    virtual void lock();
    virtual bool try_lock();
    virtual void unlock();

private:
    int                    _lock_depth; // 0 for not locked;
    int                    _current_holder; // -1 for invalid
    sim_semaphore_provider _sema;
};

class sim_lock_nr_provider : public lock_nr_provider
{
public:
    sim_lock_nr_provider(lock_nr_provider* inner_provider);
    virtual ~sim_lock_nr_provider();

    virtual void lock();
    virtual bool try_lock();
    virtual void unlock();

private:
    int                    _lock_depth; // 0 for not locked;
    int                    _current_holder; // -1 for invalid
    sim_semaphore_provider _sema;
};

}} // end namespace
