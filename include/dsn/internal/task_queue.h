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

# pragma once

# include <dsn/internal/task.h>

namespace dsn {

class task_worker;
class task_worker_pool;
class admission_controller;

class task_queue
{
public:
    template <typename T> static task_queue* create(task_worker_pool* pool, int index, task_queue* inner_provider)
    {
        return new T(pool, index, inner_provider);
    }

public:
    task_queue(task_worker_pool* pool, int index, task_queue* inner_provider); 
    ~task_queue() {}
    
    virtual void     enqueue(task* task) = 0;
    virtual task*    dequeue() = 0;
    
    int               approx_count() const { return _appro_count; }
    int               decrease_count() { return --_appro_count; }
    void              increase_count() { ++_appro_count; }
    void              reset_count() { _appro_count = 0; }
    const std::string & get_name() { return _name; }    
    task_worker_pool* pool() const { return _pool; }
    bool              is_shared() const { return _worker_count > 1; }
    int               worker_count() const { return _worker_count; }
    task_worker*      owner_worker() const { return _owner_worker; } // when not is_shared()
    int               index() const { return _index; }
    admission_controller* controller() const { return _controller; }
    void set_controller(admission_controller* controller) { _controller = controller; }

private:
    friend class task_worker_pool;
    void set_owner_worker(task_worker* worker) { _owner_worker = worker; }

private:
    task_worker_pool*      _pool;
    task_worker*           _owner_worker;
    std::string            _name;
    int                    _index;
    admission_controller*  _controller;
    int                    _worker_count;
    int                    _appro_count;
};

} // end namespace
