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

#include "meta_service.h"
#include "server_state.h"
#include "load_balancer.h"
#include "meta_server_failure_detector.h"
#include <sys/stat.h>

# ifdef __TITLE__
# undef __TITLE__
# endif
# define __TITLE__ "meta.service"

meta_service::meta_service(server_state* state)
: _state(state), serverlet("meta_service")
{
    _balancer = nullptr;
    _failure_detector = nullptr;
    _log = static_cast<dsn_handle_t>(0);
    _offset = 0;
    _data_dir = ".";
    _started = false;

    _opts.initialize();
}

meta_service::~meta_service(void)
{
}

void meta_service::start(const char* data_dir, bool clean_state)
{
    dassert(!_started, "meta service is already started");

    _data_dir = data_dir;
	std::string checkpoint_path = _data_dir + "/checkpoint";
	std::string oplog_path = _data_dir + "/oplog";


    if (clean_state)
    {
        try {
			if (!dsn::utils::filesystem::remove_path(checkpoint_path))
			{
				dassert(false, "Fail to remove file %s.", checkpoint_path.c_str());
			}

			if (!dsn::utils::filesystem::remove_path(oplog_path))
			{
				dassert(false, "Fail to remove file %s.", oplog_path.c_str());
			}
        }
        catch (std::exception& ex)
        {
            ex;
        }
    }
    else
    {
		if (!dsn::utils::filesystem::create_directory(_data_dir))
		{
			dassert(false, "Fail to create directory %s.", _data_dir.c_str());
		}

        if (dsn::utils::filesystem::file_exists(checkpoint_path))
        {
            _state->load(checkpoint_path.c_str());
        }

        if (dsn::utils::filesystem::file_exists(oplog_path))
        {
            replay_log(oplog_path.c_str());
            _state->save(checkpoint_path.c_str());
			if (!dsn::utils::filesystem::remove_path(oplog_path))
			{
				dassert(false, "Fail to remove file %s.", oplog_path.c_str());
			}
        }
    }

    _log = dsn_file_open((_data_dir + "/oplog").c_str(), O_RDWR | O_CREAT, 0666);

    _balancer = new load_balancer(_state);            
    _failure_detector = new meta_server_failure_detector(_state, this);
    
    // TODO: use zookeeper for leader election
    _failure_detector->set_primary(primary_address());

    // make sure the delay is larger than fd.grace to ensure 
    // all machines are in the correct state (assuming connected initially)
    tasking::enqueue(LPC_LBM_START, this, &meta_service::on_load_balance_start, 0, 
        _opts.fd_grace_seconds * 1000);

    auto err = _failure_detector->start(
        _opts.fd_check_interval_seconds,
        _opts.fd_beacon_interval_seconds,
        _opts.fd_lease_seconds,
        _opts.fd_grace_seconds,
        false
        );

    dassert(err == ERR_OK, "FD start failed, err = %s", err.to_string());

    register_rpc_handler(
        RPC_CM_QUERY_NODE_PARTITIONS,
        "RPC_CM_QUERY_NODE_PARTITIONS",
        &meta_service::on_query_configuration_by_node
        );

    register_rpc_handler(
        RPC_CM_QUERY_PARTITION_CONFIG_BY_INDEX,
        "RPC_CM_QUERY_PARTITION_CONFIG_BY_INDEX",
        &meta_service::on_query_configuration_by_index
        );

    register_rpc_handler(
        RPC_CM_UPDATE_PARTITION_CONFIGURATION,
        "RPC_CM_UPDATE_PARTITION_CONFIGURATION",
        &meta_service::on_update_configuration
        );
}

bool meta_service::stop()
{
    if (!_started || _balancer_timer == nullptr) return false;

    _started = false;
    _failure_detector->stop();
    delete _failure_detector;
    _failure_detector = nullptr;

    if (_balancer_timer == nullptr)
    {
        _balancer_timer->cancel(true);
    }
    
    unregister_rpc_handler(RPC_CM_QUERY_NODE_PARTITIONS);
    unregister_rpc_handler(RPC_CM_QUERY_PARTITION_CONFIG_BY_INDEX);
    unregister_rpc_handler(RPC_CM_UPDATE_PARTITION_CONFIGURATION);

    delete _balancer;
    _balancer = nullptr;
    return true;
}

void meta_service::on_load_balance_start()
{
    dassert(_balancer_timer == nullptr, "");

    _state->unfree_if_possible_on_start();
    _balancer_timer = tasking::enqueue(LPC_LBM_RUN, this, &meta_service::on_load_balance_timer, 
        0,
        1,
        10000
        );

    _started = true;
}

//void meta_service::on_request(dsn_message_t msg)
//{
//    meta_request_header hdr;
//    ::unmarshall(msg, hdr);
//        
//    meta_response_header rhdr;
//    rhdr.err = ERR_OK;
//    _failure_detector->get_primary(rhdr.primary_address);
//
//    bool is_primary = _failure_detector->is_primary();
//    if (!is_primary)
//    {
//        dsn_rpc_forward(msg, rhdr.primary_address.c_addr_ptr());
//        return;
//    }
//
//    ::dsn::rpc_address faddr;
//    dsn_msg_from_address(msg, faddr.c_addr_ptr());
//    dinfo("recv meta request %s from %s", 
//        dsn_task_code_to_string(hdr.rpc_tag),
//        faddr.name(),
//        faddr.port()
//        );
//
//    dsn_message_t resp = dsn_msg_create_response(msg);
//    if (!_started)
//    {
//        rhdr.err = ERR_SERVICE_NOT_ACTIVE;
//        ::marshall(resp, rhdr);
//    }
//    else if (hdr.rpc_tag == RPC_CM_QUERY_NODE_PARTITIONS)
//    {
//        configuration_query_by_node_request request;
//        configuration_query_by_node_response response;
//        ::unmarshall(msg, request);
//
//        query_configuration_by_node(request, response);
//
//        ::marshall(resp, rhdr);
//        ::marshall(resp, response);
//    }
//
//    else if (hdr.rpc_tag == RPC_CM_QUERY_PARTITION_CONFIG_BY_INDEX)
//    {
//        configuration_query_by_index_request request;
//        configuration_query_by_index_response response;
//        unmarshall(msg, request);
//
//        query_configuration_by_index(request, response);
//        
//        ::marshall(resp, rhdr);
//        ::marshall(resp, response);
//    }
//
//    else  if (hdr.rpc_tag == RPC_CM_UPDATE_PARTITION_CONFIGURATION)
//    {
//        update_configuration(msg, resp);
//        rhdr.err.end_tracking();
//        return;
//    }
//    
//    else
//    {
//        dassert(false, "unknown rpc tag 0x%x (%s)", hdr.rpc_tag, dsn_task_code_to_string(hdr.rpc_tag));
//    }
//
//    dsn_rpc_reply(resp);
//}

// partition server & client => meta server
void meta_service::on_query_configuration_by_node(dsn_message_t msg)
{
    if (!_started)
    {
        configuration_query_by_node_response response;
        response.err = ERR_SERVICE_NOT_ACTIVE;
        reply(msg, response);
        return;
    }

    if (!_failure_detector->is_primary())
    {
        dsn_rpc_forward(msg, _failure_detector->get_primary().c_addr());
        return;
    }

    configuration_query_by_node_response response;
    configuration_query_by_node_request request;
    ::unmarshall(msg, request);
    _state->query_configuration_by_node(request, response);
    reply(msg, response);    
}

void meta_service::on_query_configuration_by_index(dsn_message_t msg)
{
    if (!_started)
    {
        configuration_query_by_index_response response;
        response.err = ERR_SERVICE_NOT_ACTIVE;
        reply(msg, response);
        return;
    }

    if (!_failure_detector->is_primary())
    {
        dsn_rpc_forward(msg, _failure_detector->get_primary().c_addr());
        return;
    }
        
    configuration_query_by_index_response response;
    configuration_query_by_index_request request;
    ::unmarshall(msg, request);
    _state->query_configuration_by_index(request, response);
    reply(msg, response);
}

void meta_service::replay_log(const char* log)
{
    FILE* fp = ::fopen(log, "rb");
    dassert (fp != nullptr, "open operation log %s failed, err = %d", log, errno);

    char buffer[4096]; // enough for holding configuration_update_request
    while (true)
    {
        int32_t len;
        if (1 != ::fread((void*)&len, sizeof(int32_t), 1, fp))
            break;

        dassert(len <= 4096, "");
        auto r = ::fread((void*)buffer, len, 1, fp);
        dassert(r == 1, "log is corrupted");

        blob bb(buffer, 0, len);
        binary_reader reader(bb);

        configuration_update_request request;
        configuration_update_response response;
        unmarshall(reader, request);

        node_states state;
        state.push_back(std::make_pair(request.node, true));

        _state->set_node_state(state, nullptr);
        _state->update_configuration(request, response);
        response.err.end_tracking();
    }

    ::fclose(fp);
}

void meta_service::on_update_configuration(dsn_message_t req)
{
    if (!_started)
    {
        configuration_update_response response;
        response.err = ERR_SERVICE_NOT_ACTIVE;
        reply(req, response);
        return;
    }

    if (!_failure_detector->is_primary())
    {
        dsn_rpc_forward(req, _failure_detector->get_primary().c_addr());
        return;
    }

    if (_state->freezed())
    {
        configuration_update_request request;
        configuration_update_response response;
        
        ::unmarshall(req, request);

        response.err = ERR_STATE_FREEZED;
        _state->query_configuration_by_gpid(request.config.gpid, response.config);

        reply(req, response);
        return;
    }

    void* ptr;
    size_t sz;
    dsn_msg_read_next(req, &ptr, &sz);
    dsn_msg_read_commit(req, 0); // commit 0 so we can read again

    uint64_t offset;
    int len = (int)sz + sizeof(int32_t);
    
    char* buffer = new char[len];
    *(int32_t*)buffer = (int)sz;
    memcpy(buffer + sizeof(int32_t), ptr, sz);

    auto tmp = std::shared_ptr<char>(buffer);
    blob bb2(tmp, 0, len);

    auto request = std::shared_ptr<configuration_update_request>(new configuration_update_request());
    ::unmarshall(req, *request);

    {

        zauto_lock l(_log_lock);
        offset = _offset;
        _offset += len;

        file::write(_log, buffer, len, offset, LPC_CM_LOG_UPDATE, this,
            std::bind(&meta_service::on_log_completed, this, 
            std::placeholders::_1, std::placeholders::_2, bb2, request, dsn_msg_create_response(req)));
    }
}

void meta_service::update_configuration(std::shared_ptr<configuration_update_request>& update)
{
    binary_writer writer;
    int32_t sz = 0;
    marshall(writer, sz);
    marshall(writer, *update);

    blob bb = writer.get_buffer();
    *(int32_t*)bb.data() = bb.length() - sizeof(int32_t);

    {
        zauto_lock l(_log_lock);
        auto offset = _offset;
        _offset += bb.length();

        file::write(_log, bb.data(), bb.length(), offset, LPC_CM_LOG_UPDATE, this,
            std::bind(&meta_service::on_log_completed, this,
            std::placeholders::_1, std::placeholders::_2, bb, update, nullptr));
    }
}

void meta_service::on_log_completed(error_code err, size_t size,
    blob buffer, 
    std::shared_ptr<configuration_update_request> req, dsn_message_t resp)
{
    dassert(err == ERR_OK, "log operation failed, cannot proceed, err = %s", err.to_string());
    dassert(buffer.length() == size, "log size must equal to the specified buffer size");

    configuration_update_response response;    
    update_configuration(*req, response);

    if (resp != nullptr)
    {
        marshall(resp, response);
        dsn_rpc_reply(resp);
    }
    else
    {
        err.end_tracking();
    }
}

void meta_service::update_configuration(const configuration_update_request& request, /*out*/ configuration_update_response& response)
{
    _state->update_configuration(request, response);

    if (_started)
    {
        tasking::enqueue(LPC_LBM_RUN, this, std::bind(&meta_service::on_config_changed, this, request.config.gpid));
    }   
}

// local timers
void meta_service::on_load_balance_timer()
{
    if (_state->freezed())
        return;

    if (_failure_detector->is_primary())
    {
        _balancer->run();
    }
}

void meta_service::on_config_changed(global_partition_id gpid)
{
    if (_failure_detector->is_primary())
    {
        _balancer->run(gpid);
    }
}
