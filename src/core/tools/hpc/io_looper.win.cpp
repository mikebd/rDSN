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


# ifdef _WIN32

# include "io_looper.h"

# define NON_IO_TASK_NOTIFICATION_KEY 2

namespace dsn
{
    namespace tools
    {        
        io_looper::io_looper()
        {
            _io_queue = 0;
        }

        io_looper::~io_looper(void)
        {
            stop();
        }

        error_code io_looper::bind_io_handle(
            dsn_handle_t handle,
            io_loop_callback* cb,
            unsigned int events,
            ref_counter* ctx
            )
        {
            events; // not used on windows
            ctx; // not used on windows
            if (NULL == ::CreateIoCompletionPort((HANDLE)handle, _io_queue, (ULONG_PTR)cb, 0))
            {
                derror("bind io handler to completion port failed, err = %d", ::GetLastError());
                return ERR_BIND_IOCP_FAILED;
            }
            else
                return ERR_OK;
        }

        error_code io_looper::unbind_io_handle(dsn_handle_t handle, io_loop_callback* cb)
        {
            // nothing to do
            return ERR_OK;
        }

        void io_looper::notify_local_execution()
        {
            if (!::PostQueuedCompletionStatus(_io_queue, 0, NON_IO_TASK_NOTIFICATION_KEY, NULL))
            {
                dassert(false, "PostQueuedCompletionStatus failed, err = %d", ::GetLastError());
            }
        }

        void io_looper::create_completion_queue()
        {
            _io_queue = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        }

		void io_looper::close_completion_queue()
		{
			if (_io_queue != 0)
			{
				::CloseHandle(_io_queue);
				_io_queue = 0;
			}
		}

        void io_looper::start(service_node* node, int worker_count)
        {
            create_completion_queue();
            for (int i = 0; i < worker_count; i++)
            {
                std::thread* thr = new std::thread([this, node, i]()
                {
                    task::set_tls_dsn_context(node, nullptr, nullptr);

                    const char* name = node ? ::dsn::tools::get_service_node_name(node) : "glb";
                    char buffer[128];
                    sprintf(buffer, "%s.io-loop.%d", name, i);
                    task_worker::set_name(buffer);
                    
                    this->loop_worker(); 
                });
                _workers.push_back(thr);
            }
        }

        void io_looper::stop()
        {
            close_completion_queue();

            if (_workers.size() > 0)
            {
                for (auto thr : _workers)
                {
                    thr->join();
                    delete thr;
                }
                _workers.clear();
            }
        }

        void io_looper::loop_worker()
        {
            DWORD io_size;
            uintptr_t completion_key;
            LPOVERLAPPED lolp;
            DWORD error;

            while (true)
            {
                BOOL r = ::GetQueuedCompletionStatus(_io_queue, &io_size, &completion_key, &lolp, 1); // 1ms timeout for timers

                // everything goes fine
                if (r)
                {
                    error = ERROR_SUCCESS;
                }

                // failed or timeout
                else
                {
                    error = ::GetLastError();
                    if (error == ERROR_ABANDONED_WAIT_0)
                    {
                        derror("completion port loop exits");
                        break;
                    }

                    // only possible for timeout
                    if (NULL == lolp)
                    {
                        handle_local_queues();
                        continue;
                    }

                    dinfo("io operation failed in iocp, err = 0x%x", error);
                }

                if (NON_IO_TASK_NOTIFICATION_KEY == completion_key)
                {
                    handle_local_queues();
                }
                else
                {
                    io_loop_callback* cb = (io_loop_callback*)completion_key;
                    (*cb)((int)error, io_size, (uintptr_t)lolp);
                }
            }
        }
    }
}

# endif
