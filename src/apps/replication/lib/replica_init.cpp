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

#include "replica.h"
#include "mutation.h"
#include "mutation_log.h"
#include "replica_stub.h"
#include <dsn/internal/factory_store.h>

# ifdef __TITLE__
# undef __TITLE__
# endif
# define __TITLE__ "replica.init"

namespace dsn { namespace replication {

using namespace dsn::service;

error_code replica::initialize_on_new(const char* app_type, global_partition_id gpid)
{
    char buffer[256];
    sprintf(buffer, "%u.%u.%s", gpid.app_id, gpid.pidx, app_type);

    _config.gpid = gpid;
    _dir = _stub->dir() + "/" + buffer;

    if (dsn::utils::filesystem::directory_exists(_dir) &&
        !dsn::utils::filesystem::remove_path(_dir))
    {
        derror("cannot allocate new replica @ %s, as the dir is already exists", _dir.c_str());
        return ERR_PATH_ALREADY_EXIST;
    }

	if (!dsn::utils::filesystem::create_directory(_dir))
	{
		dassert(false, "Fail to create directory %s.", _dir.c_str());
		return ERR_FILE_OPERATION_FAILED;
	}

    error_code err = init_app_and_prepare_list(app_type, true);
    dassert (err == ERR_OK, "");
    return err;
}

/*static*/ replica* replica::newr(replica_stub* stub, const char* app_type, global_partition_id gpid)
{
    replica* rep = new replica(stub, gpid, app_type);
    if (rep->initialize_on_new(app_type, gpid) == ERR_OK)
        return rep;
    else
    {
        rep->close();
        delete rep;
        return nullptr;
    }
}

error_code replica::initialize_on_load(const char* dir, bool rename_dir_on_failure)
{
    std::string dr(dir);
    char splitters[] = { '\\', '/', 0 };
    std::string name = utils::get_last_component(dr, splitters);

    if (name == "")
    {
        derror("invalid replica dir %s", dir);
        return ERR_PATH_NOT_FOUND;
    }

    char app_type[128];
    global_partition_id gpid;
    if (3 != sscanf(name.c_str(), "%u.%u.%s", &gpid.app_id, &gpid.pidx, app_type))
    {
        derror( "invalid replica dir %s", dir);
        return ERR_PATH_NOT_FOUND;
    }
    
    _config.gpid = gpid;
    _dir = dr;

    error_code err = init_app_and_prepare_list(app_type, false);

    if (err != ERR_OK && rename_dir_on_failure)
    {
        // GCed later
        char newPath[256];
        sprintf(newPath, "%s.%x.err", dir, random32(0, (uint32_t)-1));  
		if (dsn::utils::filesystem::rename_path(dir, newPath, true))
		{
			derror("move bad replica from '%s' to '%s'", dir, newPath);
		}
		else
		{
            err = ERR_FILE_OPERATION_FAILED;
		}
    }
    return err;
}


/*static*/ replica* replica::load(replica_stub* stub, const char* dir, bool rename_dir_on_failure)
{
    replica* rep = new replica(stub, dir);
    error_code err = rep->initialize_on_load(dir, rename_dir_on_failure);
    if (err != ERR_OK)
    {
        rep->close();
        delete rep;
        return nullptr;
    }
    else
    {
        return rep;
    }
}

error_code replica::init_app_and_prepare_list(const char* app_type, bool create_new)
{
    dassert(nullptr == _app, "");

    sprintf(_name, "%u.%u @ %s", _config.gpid.app_id, _config.gpid.pidx, primary_address().to_string());

    _app = ::dsn::utils::factory_store<replication_app_base>::create(app_type, PROVIDER_TYPE_MAIN, this);
    if (nullptr == _app)
    {
        return ERR_OBJECT_NOT_FOUND;
    }

    error_code err = _app->open_internal(
        this,
        create_new
        );

    if (err == ERR_OK)
    {
        _prepare_list->reset(_app->last_committed_decree());
        
        if (_options->log_enable_private_prepare
            || !_app->is_delta_state_learning_supported())
        {
            dassert(nullptr == _private_log, "private log must not be initialized yet");

            std::string log_dir = utils::filesystem::path_combine(dir(), "log");

            _private_log = new mutation_log(
                log_dir,
                true,
                _options->log_batch_buffer_MB,
                _options->log_file_size_mb
                );
        }

        // sync vaid start log offset between app and logs
        if (create_new)
        {
            err = _app->update_log_info(
                this,
                _stub->_log->on_partition_reset(get_gpid(), _app->last_committed_decree()),
                _private_log ? _private_log->on_partition_reset(get_gpid(), _app->last_committed_decree()) : 0
                );
        }
        else
        {
            _stub->_log->set_valid_log_offset_before_open(get_gpid(), _app->log_info().init_offset_in_shared_log);
            if (_private_log)
                _private_log->set_valid_log_offset_before_open(get_gpid(), _app->log_info().init_offset_in_private_log);
        }

        // replay the logs
        if (nullptr != _private_log)
        {
            err = _private_log->open(
                get_gpid(),
                [this](mutation_ptr& mu)
                {
                    return replay_mutation(mu);
                }
            );

            if (err == ERR_OK)
            {
                derror(
                    "%s: private log initialized, durable = %lld, committed = %lld, maxpd = %llu, ballot = %llu, valid_offset = %lld",
                    name(),
                    _app->last_durable_decree(),
                    _app->last_committed_decree(),
                    max_prepared_decree(),
                    get_ballot(),
                    _app->log_info().init_offset_in_private_log
                    );
                _private_log->check_log_start_offset(get_gpid(), _app->log_info().init_offset_in_private_log);
                set_inactive_state_transient(true);
            }
            else
            {
                derror(
                    "%s: private log initialized with error, durable = %lld, committed = %lld, maxpd = %llu, ballot = %llu, valid_offset = %lld",
                    name(),
                    _app->last_durable_decree(),
                    _app->last_committed_decree(),
                    max_prepared_decree(),
                    get_ballot(),
                    _app->log_info().init_offset_in_private_log
                    );

                set_inactive_state_transient(false);

                _private_log->close();
            }
        }

        if (nullptr == _check_timer && err == ERR_OK)
        {
            _check_timer = tasking::enqueue(
                LPC_PER_REPLICA_CHECK_TIMER,
                this,
                &replica::on_check_timer,
                gpid_to_hash(get_gpid()),
                0,
                5 * 60 * 1000 // check every five mins
                );
        }
    }
    

    if (err != ERR_OK)
    {
        derror( "%s: open replica failed, error = %s", name(), err.to_string());
        _app->close(false);
        delete _app;
        _app = nullptr;
    }
    
    return err;
}

// return false only when the log is invalid
bool replica::replay_mutation(mutation_ptr& mu, bool is_private)
{
    auto d = mu->data.header.decree;
    auto offset = mu->data.header.log_offset;
    if (is_private && offset < _app->log_info().init_offset_in_private_log)
    {
        ddebug(
            "%s: replay mutation skipped1 as offset is invalid, ballot = %llu, decree = %llu, last_committed_decree = %llu, offset = %lld",
            name(),
            mu->data.header.ballot,
            d,
            mu->data.header.last_committed_decree,
            offset
            );
        return false;
    }
    
    if (!is_private && offset < _app->log_info().init_offset_in_shared_log)
    {
        ddebug(
            "%s: replay mutation skipped2 as offset is invalid, ballot = %llu, decree = %llu, last_committed_decree = %llu, offset = %lld",
            name(),
            mu->data.header.ballot,
            d,
            mu->data.header.last_committed_decree,
            offset
            );
        return false;
    }

    if (d <= last_committed_decree())
    {
        ddebug(
            "%s: replay mutation skipped3 as decree is outdated, ballot = %llu, decree = %llu, last_committed_decree = %llu, offset = %lld",
            name(),
            mu->data.header.ballot,
            d,
            mu->data.header.last_committed_decree,
            offset
            );
        return true;
    }   

    auto old = _prepare_list->get_mutation_by_decree(d);
    if (old != nullptr && old->data.header.ballot >= mu->data.header.ballot)
    {
        ddebug(
            "%s: replay mutation skipped4 as ballot is outdated, ballot = %llu, decree = %llu, last_committed_decree = %llu, offset = %lld",
            name(),
            mu->data.header.ballot,
            d,
            mu->data.header.last_committed_decree,
            offset
            );

        return true;
    }
    
    if (mu->data.header.ballot > get_ballot())
    {
        _config.ballot = mu->data.header.ballot;
        update_local_configuration(_config, true);
    }

    ddebug(
        "%s: replay mutation ballot = %llu, decree = %llu, last_committed_decree = %llu",
        name(),
        mu->data.header.ballot,
        d,
        mu->data.header.last_committed_decree
        );

    // prepare
    error_code err = _prepare_list->prepare(mu, PS_INACTIVE);
    dassert (err == ERR_OK, "");

    // fix private log completeness when it is from shared
    if (_private_log && !is_private)
    {
        _private_log->append(mu,
            LPC_WRITE_REPLICATION_LOG,
            this,
            [this](error_code err, size_t size)
        {
            if (err != ERR_OK)
            {
                handle_local_failure(err);
            }
        },
            gpid_to_hash(get_gpid())
            );
    }

    return true;
}

void replica::set_inactive_state_transient(bool t)
{
    if (status() == PS_INACTIVE)
    {
        _inactive_is_transient = t;
    }
}

void replica::reset_prepare_list_after_replay()
{
    // commit prepare list if possible
    _prepare_list->commit(_app->last_committed_decree(), COMMIT_TO_DECREE_SOFT);

    // align the prepare list and the app
    _prepare_list->truncate(_app->last_committed_decree());
}

}} // namespace
