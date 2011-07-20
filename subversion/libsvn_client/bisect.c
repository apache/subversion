/*
 * bisect.c:  bisect functionality
 *
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
 */

/* ==================================================================== */


/*** Includes. ***/

#include "svn_pools.h"
#include "svn_wc.h"
#include "svn_client.h"
#include "svn_ra.h"
#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_io.h"
#include "svn_opt.h"
#include "svn_time.h"
#include "client.h"

#include "private/svn_wc_private.h"

#include "svn_private_config.h"

/*** Public Interfaces. ***/

svn_error_t*
svn_client_bisect(const char *target,
                  const char *option,
                  svn_client_ctx_t *ctx,
                  apr_pool_t *pool)
{

  printf("Reached libsvn_client. option = %s , target = %s\n" , option, target);
  return SVN_NO_ERROR;

}
