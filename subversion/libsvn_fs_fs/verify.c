/* verify.c --- verification of FSFS filesystems
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

#include "verify.h"
#include "fs_fs.h"

#include "cached_data.h"
#include "rep-cache.h"
#include "util.h"

#include "../libsvn_fs/fs-loader.h"

#include "svn_private_config.h"


/** Verifying. **/

/* Baton type expected by verify_walker().  The purpose is to reuse open
 * rev / pack file handles between calls.  Its contents need to be cleaned
 * periodically to limit resource usage.
 */
typedef struct verify_walker_baton_t
{
  /* number of calls to verify_walker() since the last clean */
  int iteration_count;

  /* number of files opened since the last clean */
  int file_count;

  /* progress notification callback to invoke periodically (may be NULL) */
  svn_fs_progress_notify_func_t notify_func;

  /* baton to use with NOTIFY_FUNC */
  void *notify_baton;

  /* remember the last revision for which we called notify_func */
  svn_revnum_t last_notified_revision;

  /* cached hint for successive calls to svn_fs_fs__check_rep() */
  void *hint;

  /* pool to use for the file handles etc. */
  apr_pool_t *pool;
} verify_walker_baton_t;

/* Used by svn_fs_fs__verify().
   Implements svn_fs_fs__walk_rep_reference().walker.  */
static svn_error_t *
verify_walker(representation_t *rep,
              void *baton,
              svn_fs_t *fs,
              apr_pool_t *scratch_pool)
{
  if (baton)
    {
      verify_walker_baton_t *walker_baton = baton;
      void *previous_file;

      /* notify and free resources periodically */
      if (   walker_baton->iteration_count > 1000
          || walker_baton->file_count > 16)
        {
          if (   walker_baton->notify_func
              && rep->revision != walker_baton->last_notified_revision)
            {
              walker_baton->notify_func(rep->revision,
                                        walker_baton->notify_baton,
                                        scratch_pool);
              walker_baton->last_notified_revision = rep->revision;
            }

          svn_pool_clear(walker_baton->pool);

          walker_baton->iteration_count = 0;
          walker_baton->file_count = 0;
          walker_baton->hint = NULL;
        }

      /* access the repo data */
      previous_file = walker_baton->hint;
      SVN_ERR(svn_fs_fs__check_rep(rep, fs, &walker_baton->hint,
                                   walker_baton->pool));

      /* update resource usage counters */
      walker_baton->iteration_count++;
      if (previous_file != walker_baton->hint)
        walker_baton->file_count++;
    }
  else
    {
      /* ### Should this be using read_rep_line() directly? */
      SVN_ERR(svn_fs_fs__check_rep(rep, fs, NULL, scratch_pool));
    }

  return SVN_NO_ERROR;
}

/* Verify the rep cache DB's consistency with our rev / pack data.
 * The function signature is similar to svn_fs_fs__verify.
 * The values of START and END have already been auto-selected and
 * verified.
 */
static svn_error_t *
verify_rep_cache(svn_fs_t *fs,
                 svn_revnum_t start,
                 svn_revnum_t end,
                 svn_fs_progress_notify_func_t notify_func,
                 void *notify_baton,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *pool)
{
  svn_boolean_t exists;

  /* rep-cache verification. */
  SVN_ERR(svn_fs_fs__exists_rep_cache(&exists, fs, pool));
  if (exists)
    {
      /* provide a baton to allow the reuse of open file handles between
         iterations (saves 2/3 of OS level file operations). */
      verify_walker_baton_t *baton = apr_pcalloc(pool, sizeof(*baton));
      baton->pool = svn_pool_create(pool);
      baton->last_notified_revision = SVN_INVALID_REVNUM;
      baton->notify_func = notify_func;
      baton->notify_baton = notify_baton;

      /* tell the user that we are now ready to do *something* */
      if (notify_func)
        notify_func(SVN_INVALID_REVNUM, notify_baton, baton->pool);

      /* Do not attempt to walk the rep-cache database if its file does
         not exist,  since doing so would create it --- which may confuse
         the administrator.   Don't take any lock. */
      SVN_ERR(svn_fs_fs__walk_rep_reference(fs, start, end,
                                            verify_walker, baton,
                                            cancel_func, cancel_baton,
                                            pool));

      /* walker resource cleanup */
      svn_pool_destroy(baton->pool);
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_fs_fs__verify(svn_fs_t *fs,
                  svn_revnum_t start,
                  svn_revnum_t end,
                  svn_fs_progress_notify_func_t notify_func,
                  void *notify_baton,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *pool)
{
  fs_fs_data_t *ffd = fs->fsap_data;
  svn_revnum_t youngest = ffd->youngest_rev_cache; /* cache is current */

  /* Input validation. */
  if (! SVN_IS_VALID_REVNUM(start))
    start = 0;
  if (! SVN_IS_VALID_REVNUM(end))
    end = youngest;
  SVN_ERR(svn_fs_fs__ensure_revision_exists(start, fs, pool));
  SVN_ERR(svn_fs_fs__ensure_revision_exists(end, fs, pool));

  /* rep cache consistency */
  if (ffd->format >= SVN_FS_FS__MIN_REP_SHARING_FORMAT)
    SVN_ERR(verify_rep_cache(fs, start, end, notify_func, notify_baton,
                             cancel_func, cancel_baton, pool));

  return SVN_NO_ERROR;
}
