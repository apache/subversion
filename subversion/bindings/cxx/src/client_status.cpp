/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 */

#include "svnxx/client/status.hpp"

#include "aprwrap.hpp"
#include "private.hpp"

#include "svn_client.h"

namespace apache {
namespace subversion {
namespace svnxx {
namespace client {

namespace {
struct status_func
{
  status_callback& proxy;
  static svn_error_t* callback(void* baton,
                               const char *path,
                               const svn_client_status_t* /*status*/,
                               apr_pool_t* /*scratch_pool*/)
    {
      try
        {
          const auto self = static_cast<status_func*>(baton);
          self->proxy(path, status_notification{});
        }
      catch (const stop_iteration&)
        {
          return impl::iteration_stopped();
        }
      return SVN_NO_ERROR;
    }
};
} // anonymous namespace

revision::number
status(context& ctx_, const char* path,
       revision rev_, depth depth, status_flags flags,
       status_callback callback_)
{
  const auto& ctx = impl::unwrap(ctx_);
  const auto rev = impl::convert(rev_);
  const apr::pool scratch_pool(&ctx.get_pool());
  status_func callback{callback_};
  svn_revnum_t result;

  impl::checked_call(
      svn_client_status6(&result, ctx.get(), path, &rev,
                         impl::convert(depth),
                         bool(flags & status_flags::get_all),
                         bool(flags & status_flags::check_out_of_date),
                         bool(flags & status_flags::check_working_copy),
                         bool(flags & status_flags::no_ignore),
                         bool(flags & status_flags::ignore_externals),
                         bool(flags & status_flags::depth_as_sticky),
                         nullptr, // TODO: changelists,
                         status_func::callback, &callback,
                         scratch_pool.get()
                         ));
  return revision::number(result);
}

} // namespace client
} // namespace svnxx
} // namespace subversion
} // namespace apache
