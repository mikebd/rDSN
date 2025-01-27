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

# include <dsn/service_api_c.h>
# include <dsn/ports.h>
# include <atomic>

namespace dsn { namespace service {

class zlock
{
public:
    zlock(bool recursive = false) { _h = dsn_exlock_create(recursive); }
    ~zlock() { dsn_exlock_destroy(_h); }

    void lock() { dsn_exlock_lock(_h); }
    bool try_lock() { return dsn_exlock_try_lock(_h); }
    void unlock() { dsn_exlock_unlock(_h); }

private:
    dsn_handle_t _h;

private:
    // no assignment operator
    zlock& operator=(const zlock& source);
    zlock(const zlock& source);
};

class zrwlock_nr
{
public:
    zrwlock_nr() { _h = dsn_rwlock_nr_create(); }
    ~zrwlock_nr() { dsn_rwlock_nr_destroy(_h); }

    void lock_read() { dsn_rwlock_nr_lock_read(_h);  }
    void unlock_read() { dsn_rwlock_nr_unlock_read(_h); }

    void lock_write() { dsn_rwlock_nr_lock_write(_h); }
    void unlock_write() { dsn_rwlock_nr_unlock_write(_h); }

private:
    dsn_handle_t _h;

private:
    // no assignment operator
    zrwlock_nr& operator=(const zrwlock_nr& source);
    zrwlock_nr(const zrwlock_nr& source);
};

class zsemaphore
{
public:  
    zsemaphore(int initial_count = 0) { _h = dsn_semaphore_create(initial_count); }
    ~zsemaphore() { dsn_semaphore_destroy(_h); }

public:
    virtual void signal(int count = 1) { dsn_semaphore_signal(_h, count); }

    virtual bool wait(int timeout_milliseconds = TIME_MS_MAX) 
    {
        if (timeout_milliseconds == TIME_MS_MAX)
        {
            dsn_semaphore_wait(_h);
            return true;
        }
        else
        {
            return dsn_semaphore_wait_timeout(_h, timeout_milliseconds);
        }
    }

private:
    dsn_handle_t _h;

private:
    // no assignment operator
    zsemaphore& operator=(const zsemaphore& source);
    zsemaphore(const zsemaphore& source);
};

class zevent
{
public:
    zevent(bool manualReset, bool initState = false);
    ~zevent();

public:
    void set();
    void reset();
    bool wait(int timeout_milliseconds = TIME_MS_MAX);

private:
    zsemaphore        _sema;
    std::atomic<bool> _signaled;
    bool              _manualReset;

private:
    // no assignment operator
    zevent& operator=(const zevent& source);
    zevent(const zevent& source);
};

class zauto_lock
{
public:
    zauto_lock (zlock & lock) : _lock(&lock) { _lock->lock(); }
    ~zauto_lock() { _lock->unlock(); }

private:
    zlock * _lock; 
};

class zauto_read_lock
{
public:
    zauto_read_lock (zrwlock_nr & lock) : _lock(&lock) { _lock->lock_read(); }
    ~zauto_read_lock() { _lock->unlock_read(); }

private:
    zrwlock_nr * _lock; 
};

class zauto_write_lock
{
public:
    zauto_write_lock (zrwlock_nr & lock) : _lock(&lock) { _lock->lock_write(); }
    ~zauto_write_lock() { _lock->unlock_write(); }

private:
    zrwlock_nr * _lock; 
};

}} // end namespace dsn::service
