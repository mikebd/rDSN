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
# include "echo.client.h"
# include "echo.client.perf.h"
# include "echo.server.h"

namespace dsn { namespace example { 
// server app example
class echo_server_app : 
    public ::dsn::service_app
{
public:
    echo_server_app()
    {}

    virtual ::dsn::error_code start(int argc, char** argv)
    {
        _echo_svc.open_service();
        return ::dsn::ERR_OK;
    }

    virtual void stop(bool cleanup = false)
    {
        _echo_svc.close_service();
    }

private:
    echo_service _echo_svc;
};

// client app example
class echo_client_app : 
    public ::dsn::service_app, 
    public virtual ::dsn::clientlet
{
public:
    echo_client_app() 
    {
        _echo_client = nullptr;
    }
    
    ~echo_client_app() 
    {
        stop();
    }

    virtual ::dsn::error_code start(int argc, char** argv)
    {
        if (argc < 3)
            return ::dsn::ERR_INVALID_PARAMETERS;

        _server.assign_ipv4(argv[1], (uint16_t)atoi(argv[2]));
        _echo_client = new echo_client(_server);
        _timer = ::dsn::tasking::enqueue(LPC_ECHO_TEST_TIMER, this, &echo_client_app::on_test_timer, 0, 0, 1000);
        return ::dsn::ERR_OK;
    }

    virtual void stop(bool cleanup = false)
    {
        _timer->cancel(true);
 
        if (_echo_client != nullptr)
        {
            delete _echo_client;
            _echo_client = nullptr;
        }
    }

    void on_test_timer()
    {
        // test for service 'echo'
        {
            std::string req;
            //sync:
            std::string resp;
            auto err = _echo_client->ping(req, resp);
            std::cout << "call RPC_ECHO_ECHO_PING end, return " << err.to_string() << std::endl;
            //async: 
            //_echo_client->begin_ping(req);
           
        }
    }

private:
    ::dsn::task_ptr _timer;
    ::dsn::rpc_address _server;
    
    echo_client *_echo_client;
};

class echo_perf_test_client_app :
    public ::dsn::service_app, 
    public virtual ::dsn::clientlet
{
public:
    echo_perf_test_client_app()
    {
        _echo_client = nullptr;
    }

    ~echo_perf_test_client_app()
    {
        stop();
    }

    virtual ::dsn::error_code start(int argc, char** argv)
    {
        if (argc < 2)
            return ::dsn::ERR_INVALID_PARAMETERS;

        _server.assign_ipv4(argv[1], (uint16_t)atoi(argv[2]));

        _echo_client = new echo_perf_test_client(_server);
        _echo_client->start_test();
        return ::dsn::ERR_OK;
    }

    virtual void stop(bool cleanup = false)
    {
        if (_echo_client != nullptr)
        {
            delete _echo_client;
            _echo_client = nullptr;
        }
    }
    
private:
    echo_perf_test_client *_echo_client;
    ::dsn::rpc_address _server;
};

} } 