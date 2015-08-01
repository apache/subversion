/*
 * lock-helper.c :  create locks with an expiry date
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

#include <stdlib.h>

#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_fs.h"

int
main(int argc, const char *argv[])
{
  apr_pool_t *pool;
  const char *fs_path, *file_path, *username;
  apr_int64_t seconds;
  apr_time_t expiration_date;
  svn_fs_t *fs;
  svn_fs_access_t *access;
  svn_lock_t *lock;

  if (argc != 5)
    {
      fprintf(stderr, "usage: lock-helper repo_path file_path user seconds\n");
      exit(1);
    }

  if (apr_initialize() != APR_SUCCESS)
    {
      fprintf(stderr, "apr_initialize() failed.\n");
      exit(1);
    }

  pool = svn_pool_create(NULL);

  fs_path = svn_dirent_internal_style(argv[1], pool);
  fs_path = svn_dirent_join(fs_path, "db", pool);
  file_path = svn_dirent_canonicalize(argv[2], pool);
  username = argv[3];
  SVN_INT_ERR(svn_cstring_atoi64(&seconds, argv[4]));

  SVN_INT_ERR(svn_fs_open2(&fs, fs_path, NULL, pool, pool));
  SVN_INT_ERR(svn_fs_create_access(&access, username, pool));
  SVN_INT_ERR(svn_fs_set_access(fs, access));

  expiration_date = apr_time_now() + apr_time_from_sec(seconds);

  SVN_INT_ERR(svn_fs_lock(&lock, fs, file_path, NULL, "created by lock-helper",
                          FALSE, expiration_date, SVN_INVALID_REVNUM, FALSE,
                          pool));

  svn_pool_destroy(pool);
  apr_terminate();

  return EXIT_SUCCESS;
}
