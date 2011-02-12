/*
 * wc_db.c :  manipulating the administrative database
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

#define SVN_WC__I_AM_WC_DB

#include <assert.h>
#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_wc.h"
#include "svn_checksum.h"
#include "svn_pools.h"

#include "wc.h"
#include "wc_db.h"
#include "adm_files.h"
#include "wc-queries.h"
#include "entries.h"
#include "lock.h"
#include "tree_conflicts.h"
#include "wc_db_private.h"
#include "workqueue.h"

#include "svn_private_config.h"
#include "private/svn_sqlite.h"
#include "private/svn_skel.h"
#include "private/svn_wc_private.h"
#include "private/svn_token.h"


#define NOT_IMPLEMENTED() SVN__NOT_IMPLEMENTED()


/*
 * Some filename constants.
 */
#define SDB_FILE  "wc.db"
#define SDB_FILE_UPGRADE "wc.db.upgrade"

#define WCROOT_TEMPDIR_RELPATH   "tmp"


/*
 * PARAMETER ASSERTIONS
 *
 * Every (semi-)public entrypoint in this file has a set of assertions on
 * the parameters passed into the function. Since this is a brand new API,
 * we want to make sure that everybody calls it properly. The original WC
 * code had years to catch stray bugs, but we do not have that luxury in
 * the wc-nb rewrite. Any extra assurances that we can find will be
 * welcome. The asserts will ensure we have no doubt about the values
 * passed into the function.
 *
 * Some parameters are *not* specifically asserted. Typically, these are
 * params that will be used immediately, so something like a NULL value
 * will be obvious.
 *
 * ### near 1.7 release, it would be a Good Thing to review the assertions
 * ### and decide if any can be removed or switched to assert() in order
 * ### to remove their runtime cost in the production release.
 *
 *
 * DATABASE OPERATIONS
 *
 * Each function should leave the database in a consistent state. If it
 * does *not*, then the implication is some other function needs to be
 * called to restore consistency. Subtle requirements like that are hard
 * to maintain over a long period of time, so this API will not allow it.
 *
 *
 * STANDARD VARIABLE NAMES
 *
 * db     working copy database (this module)
 * sdb    SQLite database (not to be confused with 'db')
 * wc_id  a WCROOT id associated with a node
 */

#define INVALID_REPOS_ID ((apr_int64_t) -1)
#define UNKNOWN_WC_ID ((apr_int64_t) -1)
#define FORMAT_FROM_SDB (-1)

/* This is a character used to escape itself and the globbing character in
   globbing sql expressions below.  See escape_sqlite_like().

   NOTE: this should match the character used within wc-metadata.sql  */
#define LIKE_ESCAPE_CHAR     "#"

/* Calculates the depth of the relpath below "" */
APR_INLINE static int relpath_depth(const char *relpath)
{
  int n = 1;
  if (*relpath == '\0')
    return 0;

  do
  {
    if (*relpath == '/')
      n++;
  }
  while (*(++relpath));

  return n;
}

int svn_wc__db_op_depth_for_upgrade(const char *local_relpath)
{
  return relpath_depth(local_relpath);
}

typedef struct insert_base_baton_t {
  /* common to all insertions into BASE */
  svn_wc__db_status_t status;
  svn_wc__db_kind_t kind;
  apr_int64_t wc_id;
  const char *local_relpath;
  apr_int64_t repos_id;
  const char *repos_relpath;
  svn_revnum_t revision;

  /* common to all "normal" presence insertions */
  const apr_hash_t *props;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  apr_hash_t *dav_cache;

  /* for inserting directories */
  const apr_array_header_t *children;
  svn_depth_t depth;

  /* for inserting files */
  const svn_checksum_t *checksum;
  svn_filesize_t translated_size;

  /* for inserting symlinks */
  const char *target;

  /* may need to insert/update ACTUAL to record a conflict  */
  const svn_skel_t *conflict;

  /* may have work items to queue in this transaction  */
  const svn_skel_t *work_items;

} insert_base_baton_t;


typedef struct insert_working_baton_t {
  /* common to all insertions into WORKING (including NODE_DATA) */
  svn_wc__db_status_t presence;
  svn_wc__db_kind_t kind;
  apr_int64_t wc_id;
  const char *local_relpath;
  apr_int64_t op_depth;

  /* common to all "normal" presence insertions */
  const apr_hash_t *props;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  apr_int64_t original_repos_id;
  const char *original_repos_relpath;
  svn_revnum_t original_revnum;
  svn_boolean_t moved_here;

  /* for inserting directories */
  const apr_array_header_t *children;
  svn_depth_t depth;

  /* for inserting (copied/moved-here) files */
  const svn_checksum_t *checksum;

  /* for inserting symlinks */
  const char *target;

  /* may have work items to queue in this transaction  */
  const svn_skel_t *work_items;

} insert_working_baton_t;


static const svn_token_map_t kind_map[] = {
  { "file", svn_wc__db_kind_file },
  { "dir", svn_wc__db_kind_dir },
  { "symlink", svn_wc__db_kind_symlink },
  { "unknown", svn_wc__db_kind_unknown },
  { NULL }
};

/* Note: we only decode presence values from the database. These are a subset
   of all the status values. */
static const svn_token_map_t presence_map[] = {
  { "normal", svn_wc__db_status_normal },
  { "absent", svn_wc__db_status_absent },
  { "excluded", svn_wc__db_status_excluded },
  { "not-present", svn_wc__db_status_not_present },
  { "incomplete", svn_wc__db_status_incomplete },
  { "base-deleted", svn_wc__db_status_base_deleted },
  { NULL }
};


/* Forward declarations  */
static svn_error_t *
add_work_items(svn_sqlite__db_t *sdb,
               const svn_skel_t *skel,
               apr_pool_t *scratch_pool);

static svn_error_t *
insert_incomplete_children(svn_sqlite__db_t *sdb,
                           apr_int64_t wc_id,
                           const char *local_relpath,
                           apr_int64_t repos_id,
                           const char *repos_relpath,
                           svn_revnum_t revision,
                           const apr_array_header_t *children,
                           apr_int64_t op_depth,
                           apr_pool_t *scratch_pool);

static svn_error_t *
db_read_pristine_props(apr_hash_t **props,
                       svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool);

static svn_error_t *
read_info(svn_wc__db_status_t *status,
          svn_wc__db_kind_t *kind,
          svn_revnum_t *revision,
          const char **repos_relpath,
          apr_int64_t *repos_id,
          svn_revnum_t *changed_rev,
          apr_time_t *changed_date,
          const char **changed_author,
          apr_time_t *last_mod_time,
          svn_depth_t *depth,
          const svn_checksum_t **checksum,
          svn_filesize_t *translated_size,
          const char **target,
          const char **changelist,
          const char **original_repos_relpath,
          apr_int64_t *original_repos_id,
          svn_revnum_t *original_revision,
          svn_boolean_t *props_mod,
          svn_boolean_t *have_base,
          svn_boolean_t *have_work,
          svn_boolean_t *conflicted,
          svn_wc__db_lock_t **lock,
          svn_wc__db_wcroot_t *wcroot,
          const char *local_relpath,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool);

static svn_error_t *
scan_addition(svn_wc__db_status_t *status,
              const char **op_root_relpath,
              const char **repos_relpath,
              apr_int64_t *repos_id,
              const char **original_repos_relpath,
              apr_int64_t *original_repos_id,
              svn_revnum_t *original_revision,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool);

static svn_error_t *
scan_deletion(const char **base_del_relpath,
              const char **moved_to_relpath,
              const char **work_del_relpath,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool);

static svn_error_t *
convert_to_working_status(svn_wc__db_status_t *working_status,
                          svn_wc__db_status_t status);

static svn_error_t *
wclock_owns_lock(svn_boolean_t *own_lock,
                 svn_wc__db_wcroot_t *wcroot,
                 const char *local_relpath,
                 svn_boolean_t exact,
                 apr_pool_t *scratch_pool);


/* Return the absolute path, in local path style, of LOCAL_RELPATH in WCROOT. */
static const char *
path_for_error_message(const svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       apr_pool_t *result_pool)
{
  const char *local_abspath
    = svn_dirent_join(wcroot->abspath, local_relpath, result_pool);

  return svn_dirent_local_style(local_abspath, result_pool);
}


/* Return a file size from column SLOT of the SQLITE statement STMT, or
 * SVN_INVALID_FILESIZE if the column value is NULL. */
static svn_filesize_t
get_translated_size(svn_sqlite__stmt_t *stmt, int slot)
{
  if (svn_sqlite__column_is_null(stmt, slot))
    return SVN_INVALID_FILESIZE;
  return svn_sqlite__column_int64(stmt, slot);
}


/* Return a lock info structure constructed from the given columns of the
 * SQLITE statement STMT, or return NULL if the token column value is null. */
static svn_wc__db_lock_t *
lock_from_columns(svn_sqlite__stmt_t *stmt,
                  int col_token,
                  int col_owner,
                  int col_comment,
                  int col_date,
                  apr_pool_t *result_pool)
{
  svn_wc__db_lock_t *lock;

  if (svn_sqlite__column_is_null(stmt, col_token))
    {
      lock = NULL;
    }
  else
    {
      lock = apr_pcalloc(result_pool, sizeof(svn_wc__db_lock_t));
      lock->token = svn_sqlite__column_text(stmt, col_token, result_pool);
      lock->owner = svn_sqlite__column_text(stmt, col_owner, result_pool);
      lock->comment = svn_sqlite__column_text(stmt, col_comment, result_pool);
      lock->date = svn_sqlite__column_int64(stmt, col_date);
    }
  return lock;
}


/* */
static const char *
escape_sqlite_like(const char * const str, apr_pool_t *result_pool)
{
  char *result;
  const char *old_ptr;
  char *new_ptr;
  int len = 0;

  /* Count the number of extra characters we'll need in the escaped string.
     We could just use the worst case (double) value, but we'd still need to
     iterate over the string to get it's length.  So why not do something
     useful why iterating over it, and save some memory at the same time? */
  for (old_ptr = str; *old_ptr; ++old_ptr)
    {
      len++;
      if (*old_ptr == '%'
            || *old_ptr == '_'
            || *old_ptr == LIKE_ESCAPE_CHAR[0])
        len++;
    }

  result = apr_palloc(result_pool, len + 1);

  /* Now do the escaping. */
  for (old_ptr = str, new_ptr = result; *old_ptr; ++old_ptr, ++new_ptr)
    {
      if (*old_ptr == '%'
            || *old_ptr == '_'
            || *old_ptr == LIKE_ESCAPE_CHAR[0])
        *(new_ptr++) = LIKE_ESCAPE_CHAR[0];
      *new_ptr = *old_ptr;
    }
  *new_ptr = '\0';

  return result;
}

/* Return a string that can be used as the argument to a SQLite 'LIKE'
 * operator, in order to match any path that is a child of LOCAL_RELPATH
 * (at any depth below LOCAL_RELPATH), *excluding* LOCAL_RELPATH itself.
 * LOCAL_RELPATH may be the empty string, in which case the result will
 * match any path except the empty path.
 *
 * Allocate the result either statically or in RESULT_POOL.  */
static const char *construct_like_arg(const char *local_relpath,
                                      apr_pool_t *result_pool)
{
  if (local_relpath[0] == '\0')
    return "_%";

  return apr_pstrcat(result_pool,
                     escape_sqlite_like(local_relpath, result_pool),
                     "/%", (char *)NULL);
}



/* Look up REPOS_ID in SDB and set *REPOS_ROOT_URL and/or *REPOS_UUID to
 * its root URL and UUID respectively.  If REPOS_ID is INVALID_REPOS_ID,
 * use NULL for both URL and UUID.  Either or both output parameters may be
 * NULL if not wanted. */
static svn_error_t *
fetch_repos_info(const char **repos_root_url,
                 const char **repos_uuid,
                 svn_sqlite__db_t *sdb,
                 apr_int64_t repos_id,
                 apr_pool_t *result_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  if (!repos_root_url && !repos_uuid)
    return SVN_NO_ERROR;

  if (repos_id == INVALID_REPOS_ID)
    {
      if (repos_root_url)
        *repos_root_url = NULL;
      if (repos_uuid)
        *repos_uuid = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_SELECT_REPOSITORY_BY_ID));
  SVN_ERR(svn_sqlite__bindf(stmt, "i", repos_id));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_CORRUPT, svn_sqlite__reset(stmt),
                             _("No REPOSITORY table entry for id '%ld'"),
                             (long int)repos_id);

  if (repos_root_url)
    *repos_root_url = svn_sqlite__column_text(stmt, 0, result_pool);
  if (repos_uuid)
    *repos_uuid = svn_sqlite__column_text(stmt, 1, result_pool);

  return svn_error_return(svn_sqlite__reset(stmt));
}

/* Set *REPOS_ID, *REVISION and *REPOS_RELPATH from the
 * given columns of the SQLITE statement STMT, or to NULL if the respective
 * column value is null.  Any of the output parameters may be NULL if not
 * required. */
static svn_error_t *
repos_location_from_columns(apr_int64_t *repos_id,
                            svn_revnum_t *revision,
                            const char **repos_relpath,
                            svn_sqlite__stmt_t *stmt,
                            int col_repos_id,
                            int col_revision,
                            int col_repos_relpath,
                            apr_pool_t *result_pool)
{
  svn_error_t *err = SVN_NO_ERROR;

  if (repos_id)
    {
      /* Fetch repository information via REPOS_ID. */
      if (svn_sqlite__column_is_null(stmt, col_repos_id))
        *repos_id = INVALID_REPOS_ID;
      else
        *repos_id = svn_sqlite__column_int64(stmt, col_repos_id);
    }
  if (revision)
    {
      *revision = svn_sqlite__column_revnum(stmt, col_revision);
    }
  if (repos_relpath)
    {
      *repos_relpath = svn_sqlite__column_text(stmt, col_repos_relpath,
                                               result_pool);
    }

  return err;
}


/* Set *REPOS_ID and *REPOS_RELPATH to the BASE node of LOCAL_RELPATH.
 * Either of REPOS_ID and REPOS_RELPATH may be NULL if not wanted. */
static svn_error_t *
scan_upwards_for_repos(apr_int64_t *repos_id,
                       const char **repos_relpath,
                       const svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(wcroot->sdb != NULL && wcroot->wc_id != UNKNOWN_WC_ID);
  SVN_ERR_ASSERT(repos_id != NULL || repos_relpath != NULL);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    {
      svn_error_t *err = svn_error_createf(
            SVN_ERR_WC_PATH_NOT_FOUND, NULL,
            _("The node '%s' was not found."),
            path_for_error_message(wcroot, local_relpath, scratch_pool));

      return svn_error_compose_create(err, svn_sqlite__reset(stmt));
    }

  SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 0));
  SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 1));

  if (repos_id)
    *repos_id = svn_sqlite__column_int64(stmt, 0);
  if (repos_relpath)
    *repos_relpath = svn_sqlite__column_text(stmt, 1, result_pool);
  return svn_sqlite__reset(stmt);
}


/* Get the statement given by STMT_IDX, and bind the appropriate wc_id and
   local_relpath based upon LOCAL_ABSPATH.  Store it in *STMT, and use
   SCRATCH_POOL for temporary allocations.

   Note: WC_ID and LOCAL_RELPATH must be arguments 1 and 2 in the statement. */
static svn_error_t *
get_statement_for_path(svn_sqlite__stmt_t **stmt,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       int stmt_idx,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(stmt, wcroot->sdb, stmt_idx));
  SVN_ERR(svn_sqlite__bindf(*stmt, "is", wcroot->wc_id, local_relpath));

  return SVN_NO_ERROR;
}


/* For a given REPOS_ROOT_URL/REPOS_UUID pair, set *REPOS_ID to the existing
   REPOS_ID value. If one does not exist, throw an error. */
static svn_error_t *
fetch_repos_id(apr_int64_t *repos_id,
               const char *repos_root_url,
               const char *repos_uuid,
               svn_sqlite__db_t *sdb,
               apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *get_stmt;

  SVN_ERR(svn_sqlite__get_statement(&get_stmt, sdb, STMT_SELECT_REPOSITORY));
  SVN_ERR(svn_sqlite__bindf(get_stmt, "s", repos_root_url));
  SVN_ERR(svn_sqlite__step_row(get_stmt));

  *repos_id = svn_sqlite__column_int64(get_stmt, 0);
  return svn_error_return(svn_sqlite__reset(get_stmt));
}


/* For a given REPOS_ROOT_URL/REPOS_UUID pair, return the existing REPOS_ID
   value. If one does not exist, then create a new one. */
static svn_error_t *
create_repos_id(apr_int64_t *repos_id,
                const char *repos_root_url,
                const char *repos_uuid,
                svn_sqlite__db_t *sdb,
                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *get_stmt;
  svn_sqlite__stmt_t *insert_stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&get_stmt, sdb, STMT_SELECT_REPOSITORY));
  SVN_ERR(svn_sqlite__bindf(get_stmt, "s", repos_root_url));
  SVN_ERR(svn_sqlite__step(&have_row, get_stmt));

  if (have_row)
    {
      *repos_id = svn_sqlite__column_int64(get_stmt, 0);
      return svn_error_return(svn_sqlite__reset(get_stmt));
    }
  SVN_ERR(svn_sqlite__reset(get_stmt));

  /* NOTE: strictly speaking, there is a race condition between the
     above query and the insertion below. We're simply going to ignore
     that, as it means two processes are *modifying* the working copy
     at the same time, *and* new repositores are becoming visible.
     This is rare enough, let alone the miniscule chance of hitting
     this race condition. Further, simply failing out will leave the
     database in a consistent state, and the user can just re-run the
     failed operation. */

  SVN_ERR(svn_sqlite__get_statement(&insert_stmt, sdb,
                                    STMT_INSERT_REPOSITORY));
  SVN_ERR(svn_sqlite__bindf(insert_stmt, "ss", repos_root_url, repos_uuid));
  return svn_error_return(svn_sqlite__insert(repos_id, insert_stmt));
}


/* Initialize the baton with appropriate "blank" values. This allows the
   insertion function to leave certain columns null.  */
static void
blank_ibb(insert_base_baton_t *pibb)
{
  memset(pibb, 0, sizeof(*pibb));
  pibb->revision = SVN_INVALID_REVNUM;
  pibb->changed_rev = SVN_INVALID_REVNUM;
  pibb->depth = svn_depth_infinity;
  pibb->translated_size = SVN_INVALID_FILESIZE;
}


/* Extend any delete of the parent of LOCAL_RELPATH to LOCAL_RELPATH.

   Given a wc:

              0         1         2         3         4
              normal
   A          normal
   A/B        normal              normal
   A/B/C                          not-pres  normal
   A/B/C/D                                            normal

   That is checkout, delete A/B, copy a replacement A/B, delete copied
   child A/B/C, add replacement A/B/C, add A/B/C/D.

   Now an update that adds base nodes for A/B/C, A/B/C/D and A/B/C/D/E
   must extend the A/B deletion:

              0         1         2         3         4
              normal
   A          normal
   A/B        normal              normal
   A/B/C      normal              not-pres  normal
   A/B/C/D    normal              base-del            normal
   A/B/C/D/E  normal              base-del

   When adding a base node if the parent has a working node then the
   parent base is deleted and this must be extended to cover new base
   node.

   In the example above A/B/C/D and A/B/C/D/E are the nodes that get
   the extended delete, A/B/C is already deleted.
 */
static svn_error_t *
extend_parent_delete(svn_sqlite__db_t *sdb,
                     apr_int64_t wc_id,
                     const char *local_relpath,
                     apr_pool_t *scratch_pool)
{
  svn_boolean_t have_row;
  svn_sqlite__stmt_t *stmt;
  apr_int64_t parent_op_depth;
  const char *parent_relpath = svn_relpath_dirname(local_relpath, scratch_pool);

  SVN_ERR_ASSERT(local_relpath[0]);

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_SELECT_LOWEST_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, parent_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    parent_op_depth = svn_sqlite__column_int64(stmt, 0);
  SVN_ERR(svn_sqlite__reset(stmt));
  if (have_row)
    {
      apr_int64_t op_depth;

      SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (have_row)
        op_depth = svn_sqlite__column_int64(stmt, 0);
      SVN_ERR(svn_sqlite__reset(stmt));
      if (!have_row || parent_op_depth < op_depth)
        {
          SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                          STMT_INSERT_WORKING_NODE_FROM_BASE));
          SVN_ERR(svn_sqlite__bindf(stmt, "isit", wc_id,
                                    local_relpath, parent_op_depth,
                                    presence_map,
                                    svn_wc__db_status_base_deleted));
          SVN_ERR(svn_sqlite__update(NULL, stmt));
        }
    }

  return SVN_NO_ERROR;
}

/* This is the reverse of extend_parent_delete.

   When removing a base node if the parent has a working node then the
   parent base and this node are both deleted and so the delete of
   this node must be removed.
 */
static svn_error_t *
retract_parent_delete(svn_sqlite__db_t *sdb,
                      apr_int64_t wc_id,
                      const char *local_relpath,
                      apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_DELETE_LOWEST_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}



/* */
static svn_error_t *
insert_base_node(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  const insert_base_baton_t *pibb = baton;
  svn_sqlite__stmt_t *stmt;
  /* The directory at the WCROOT has a NULL parent_relpath. Otherwise,
     bind the appropriate parent_relpath. */
  const char *parent_relpath =
    (*pibb->local_relpath == '\0') ? NULL
    : svn_relpath_dirname(pibb->local_relpath, scratch_pool);

  SVN_ERR_ASSERT(pibb->repos_id != INVALID_REPOS_ID);
  SVN_ERR_ASSERT(pibb->repos_relpath != NULL);

  /* ### we can't handle this right now  */
  SVN_ERR_ASSERT(pibb->conflict == NULL);

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isisisr"
                            "tstr"               /* 8 - 11 */
                            "isnnnnns",          /* 12 - 19 */
                            pibb->wc_id,         /* 1 */
                            pibb->local_relpath, /* 2 */
                            (apr_int64_t)0, /* op_depth is 0 for base */
                            parent_relpath,      /* 4 */
                            pibb->repos_id,
                            pibb->repos_relpath,
                            pibb->revision,
                            presence_map, pibb->status, /* 8 */
                            (pibb->kind == svn_wc__db_kind_dir) ? /* 9 */
                               svn_depth_to_word(pibb->depth) : NULL,
                            kind_map, pibb->kind, /* 10 */
                            pibb->changed_rev,    /* 11 */
                            pibb->changed_date,   /* 12 */
                            pibb->changed_author, /* 13 */
                            (pibb->kind == svn_wc__db_kind_symlink) ?
                                pibb->target : NULL)); /* 19 */

  if (pibb->kind == svn_wc__db_kind_file)
    {
      SVN_ERR(svn_sqlite__bind_checksum(stmt, 14, pibb->checksum, scratch_pool));
      if (pibb->translated_size != SVN_INVALID_FILESIZE)
        SVN_ERR(svn_sqlite__bind_int64(stmt, 16, pibb->translated_size));
    }

  SVN_ERR(svn_sqlite__bind_properties(stmt, 15, pibb->props,
                                      scratch_pool));
  if (pibb->dav_cache)
    SVN_ERR(svn_sqlite__bind_properties(stmt, 18, pibb->dav_cache,
                                        scratch_pool));

  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  if (pibb->kind == svn_wc__db_kind_dir && pibb->children)
    SVN_ERR(insert_incomplete_children(sdb, pibb->wc_id,
                                       pibb->local_relpath,
                                       pibb->repos_id,
                                       pibb->repos_relpath,
                                       pibb->revision,
                                       pibb->children,
                                       0 /* BASE */,
                                       scratch_pool));

  if (parent_relpath)
    SVN_ERR(extend_parent_delete(sdb, pibb->wc_id, pibb->local_relpath,
                                 scratch_pool));

  SVN_ERR(add_work_items(sdb, pibb->work_items, scratch_pool));

  return SVN_NO_ERROR;
}


static void
blank_iwb(insert_working_baton_t *piwb)
{
  memset(piwb, 0, sizeof(*piwb));
  piwb->changed_rev = SVN_INVALID_REVNUM;
  piwb->depth = svn_depth_infinity;

  /* ORIGINAL_REPOS_ID and ORIGINAL_REVNUM could use some kind of "nil"
     value, but... meh. We'll avoid them if ORIGINAL_REPOS_RELPATH==NULL.  */
}


/* Insert a row in NODES for each (const char *) child name in CHILDREN,
   whose parent directory is LOCAL_RELPATH, at op_depth=OP_DEPTH.  Set each
   child's presence to 'incomplete', kind to 'unknown', repos_id to REPOS_ID,
   repos_path by appending the child name to REPOS_PATH, and revision to
   REVISION (which should match the parent's revision).

   If REPOS_ID is INVALID_REPOS_ID, set each child's repos_id to null. */
static svn_error_t *
insert_incomplete_children(svn_sqlite__db_t *sdb,
                           apr_int64_t wc_id,
                           const char *local_relpath,
                           apr_int64_t repos_id,
                           const char *repos_path,
                           svn_revnum_t revision,
                           const apr_array_header_t *children,
                           apr_int64_t op_depth,
                           apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int i;

  SVN_ERR_ASSERT(repos_path != NULL || op_depth > 0);
  SVN_ERR_ASSERT((repos_id != INVALID_REPOS_ID)
                 == (repos_path != NULL));

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_NODE));

  for (i = children->nelts; i--; )
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);

      SVN_ERR(svn_sqlite__bindf(stmt, "isisnnrsns",
                                wc_id,
                                svn_relpath_join(local_relpath, name,
                                                 scratch_pool),
                                op_depth,
                                local_relpath,
                                revision,
                                "incomplete", /* 8, presence */
                                "unknown"));  /* 10, kind */

      if (repos_id != INVALID_REPOS_ID)
        {
          SVN_ERR(svn_sqlite__bind_int64(stmt, 5, repos_id));
          SVN_ERR(svn_sqlite__bind_text(stmt, 6,
                                        svn_relpath_join(repos_path, name,
                                                         scratch_pool)));
        }

      SVN_ERR(svn_sqlite__insert(NULL, stmt));
    }

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
insert_working_node(void *baton,
                    svn_sqlite__db_t *sdb,
                    apr_pool_t *scratch_pool)
{
  const insert_working_baton_t *piwb = baton;
  const char *parent_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(piwb->op_depth > 0);

  /* We cannot insert a WORKING_NODE row at the wcroot.  */
  SVN_ERR_ASSERT(*piwb->local_relpath != '\0');
  parent_relpath = svn_relpath_dirname(piwb->local_relpath, scratch_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isisnnntstrisn"
                "nnnn" /* properties translated_size last_mod_time dav_cache */
                "s",
                piwb->wc_id, piwb->local_relpath,
                piwb->op_depth,
                parent_relpath,
                presence_map, piwb->presence,
                (piwb->kind == svn_wc__db_kind_dir)
                            ? svn_depth_to_word(piwb->depth) : NULL,
                kind_map, piwb->kind,
                piwb->changed_rev,
                piwb->changed_date,
                piwb->changed_author,
                (piwb->kind == svn_wc__db_kind_symlink)
                            ? piwb->target : NULL));


  if (piwb->kind == svn_wc__db_kind_file)
    {
      SVN_ERR(svn_sqlite__bind_checksum(stmt, 14, piwb->checksum,
                                        scratch_pool));
    }
  else if (piwb->kind == svn_wc__db_kind_symlink)
    {
      /* Note: incomplete nodes may have a NULL target.  */
      if (piwb->target)
        SVN_ERR(svn_sqlite__bind_text(stmt, 19, piwb->target));
    }

  if (piwb->original_repos_relpath != NULL)
    {
      SVN_ERR(svn_sqlite__bind_int64(stmt, 5, piwb->original_repos_id));
      SVN_ERR(svn_sqlite__bind_text(stmt, 6, piwb->original_repos_relpath));
      SVN_ERR(svn_sqlite__bind_int64(stmt, 7, piwb->original_revnum));
    }


  SVN_ERR(svn_sqlite__bind_properties(stmt, 15, piwb->props, scratch_pool));

  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  /* Insert incomplete children, if specified.
     The children are part of the same op and so have the same op_depth.
     (The only time we'd want a different depth is during a recursive
     simple add, but we never insert children here during a simple add.) */
  if (piwb->kind == svn_wc__db_kind_dir && piwb->children)
    SVN_ERR(insert_incomplete_children(sdb, piwb->wc_id,
                                       piwb->local_relpath,
                                       INVALID_REPOS_ID /* inherit repos_id */,
                                       NULL /* inherit repos_path */,
                                       piwb->original_revnum,
                                       piwb->children,
                                       piwb->op_depth,
                                       scratch_pool));

  SVN_ERR(add_work_items(sdb, piwb->work_items, scratch_pool));

  return SVN_NO_ERROR;
}


/* Each name is allocated in RESULT_POOL and stored into CHILDREN as a key
   pointed to the same name.  */
static svn_error_t *
add_children_to_hash(apr_hash_t *children,
                     int stmt_idx,
                     svn_sqlite__db_t *sdb,
                     apr_int64_t wc_id,
                     const char *parent_relpath,
                     apr_pool_t *result_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, stmt_idx));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, parent_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      const char *name = svn_relpath_basename(child_relpath, result_pool);

      apr_hash_set(children, name, APR_HASH_KEY_STRING, name);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  return svn_sqlite__reset(stmt);
}


/* Return in *CHILDREN all of the children of the directory LOCAL_RELPATH,
   of any status, in all op-depths in the NODES table. */
static svn_error_t *
gather_children(const apr_array_header_t **children,
                svn_wc__db_wcroot_t *wcroot,
                const char *local_relpath,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  apr_hash_t *names_hash = apr_hash_make(scratch_pool);
  apr_array_header_t *names_array;

  /* All of the names get allocated in RESULT_POOL.  It
     appears to be faster to use the hash to remove duplicates than to
     use DISTINCT in the SQL query. */
  SVN_ERR(add_children_to_hash(names_hash, STMT_SELECT_NODE_CHILDREN,
                               wcroot->sdb, wcroot->wc_id,
                               local_relpath, result_pool));

  SVN_ERR(svn_hash_keys(&names_array, names_hash, result_pool));
  *children = names_array;
  return SVN_NO_ERROR;
}

/* Set *CHILDREN to a new array of (const char *) names of the repository
   children of the directory WCROOT:LOCAL_RELPATH - that is, the children at
   the same op-depth. */
static svn_error_t *
gather_repo_children(const apr_array_header_t **children,
                     svn_wc__db_wcroot_t *wcroot,
                     const char *local_relpath,
                     apr_int64_t op_depth,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  apr_array_header_t *result
    = apr_array_make(result_pool, 0, sizeof(const char *));
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_OP_DEPTH_CHILDREN));
  SVN_ERR(svn_sqlite__bindf(stmt, "isi", wcroot->wc_id, local_relpath,
                            op_depth));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);

      /* Allocate the name in RESULT_POOL so we won't have to copy it. */
      APR_ARRAY_PUSH(result, const char *)
        = svn_relpath_basename(child_relpath, result_pool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  *children = result;
  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
flush_entries(svn_wc__db_t *db,
              const char *local_abspath,
              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_wc_adm_access_t *adm_access;
  const char *parent_abspath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  adm_access = apr_hash_get(wcroot->access_cache, local_abspath,
                            APR_HASH_KEY_STRING);

  if (adm_access)
    svn_wc__adm_access_set_entries(adm_access, NULL);

  /* We're going to be overly aggressive here and just flush the parent
     without doing much checking.  This may hurt performance for
     legacy API consumers, but that's not our problem. :) */
  parent_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
  adm_access = apr_hash_get(wcroot->access_cache, parent_abspath,
                            APR_HASH_KEY_STRING);

  if (adm_access)
    svn_wc__adm_access_set_entries(adm_access, NULL);

  return SVN_NO_ERROR;
}


/* Add a single WORK_ITEM into the given SDB's WORK_QUEUE table. This does
   not perform its work within a transaction, assuming the caller will
   manage that.  */
static svn_error_t *
add_single_work_item(svn_sqlite__db_t *sdb,
                     const svn_skel_t *work_item,
                     apr_pool_t *scratch_pool)
{
  svn_stringbuf_t *serialized;
  svn_sqlite__stmt_t *stmt;

  serialized = svn_skel__unparse(work_item, scratch_pool);
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_WORK_ITEM));
  SVN_ERR(svn_sqlite__bind_blob(stmt, 1, serialized->data, serialized->len));
  return svn_error_return(svn_sqlite__insert(NULL, stmt));
}


/* Add work item(s) to the given SDB. Also see add_one_work_item(). This
   SKEL is usually passed to the various wc_db operation functions. It may
   be NULL, indicating no additional work items are needed, it may be a
   single work item, or it may be a list of work items.  */
static svn_error_t *
add_work_items(svn_sqlite__db_t *sdb,
               const svn_skel_t *skel,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool;

  /* Maybe there are no work items to insert.  */
  if (skel == NULL)
    return SVN_NO_ERROR;

  /* Should have a list.  */
  SVN_ERR_ASSERT(!skel->is_atom);

  /* Is the list a single work item? Or a list of work items?  */
  if (SVN_WC__SINGLE_WORK_ITEM(skel))
    return svn_error_return(add_single_work_item(sdb, skel, scratch_pool));

  /* SKEL is a list-of-lists, aka list of work items.  */

  iterpool = svn_pool_create(scratch_pool);
  for (skel = skel->children; skel; skel = skel->next)
    {
      svn_pool_clear(iterpool);

      SVN_ERR(add_single_work_item(sdb, skel, iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* Determine which trees' nodes exist for a given WC_ID and LOCAL_RELPATH
   in the specified SDB.  */
static svn_error_t *
which_trees_exist(svn_boolean_t *base_exists,
                  svn_boolean_t *working_exists,
                  svn_sqlite__db_t *sdb,
                  apr_int64_t wc_id,
                  const char *local_relpath)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  *base_exists = FALSE;
  *working_exists = FALSE;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_DETERMINE_TREE_FOR_RECORDING));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      int value = svn_sqlite__column_int(stmt, 0);

      if (value)
        *working_exists = TRUE;  /* value == 1  */
      else
        *base_exists = TRUE;  /* value == 0  */

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (have_row)
        {
          /* If both rows, then both tables.  */
          *base_exists = TRUE;
          *working_exists = TRUE;
        }
    }

  return svn_error_return(svn_sqlite__reset(stmt));
}


/* */
static svn_error_t *
create_db(svn_sqlite__db_t **sdb,
          apr_int64_t *repos_id,
          apr_int64_t *wc_id,
          const char *dir_abspath,
          const char *repos_root_url,
          const char *repos_uuid,
          const char *sdb_fname,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_wc__db_util_open_db(sdb, dir_abspath, sdb_fname,
                                  svn_sqlite__mode_rwcreate, result_pool,
                                  scratch_pool));

  /* Create the database's schema.  */
  SVN_ERR(svn_sqlite__exec_statements(*sdb, STMT_CREATE_SCHEMA));
  SVN_ERR(svn_sqlite__exec_statements(*sdb, STMT_CREATE_NODES));
  SVN_ERR(svn_sqlite__exec_statements(*sdb, STMT_CREATE_NODES_TRIGGERS));

  /* Insert the repository. */
  SVN_ERR(create_repos_id(repos_id, repos_root_url, repos_uuid, *sdb,
                          scratch_pool));

  /* Insert the wcroot. */
  /* ### Right now, this just assumes wc metadata is being stored locally. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, *sdb, STMT_INSERT_WCROOT));
  SVN_ERR(svn_sqlite__insert(wc_id, stmt));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_init(svn_wc__db_t *db,
                const char *local_abspath,
                const char *repos_relpath,
                const char *repos_root_url,
                const char *repos_uuid,
                svn_revnum_t initial_rev,
                svn_depth_t depth,
                apr_pool_t *scratch_pool)
{
  svn_sqlite__db_t *sdb;
  apr_int64_t repos_id;
  apr_int64_t wc_id;
  svn_wc__db_pdh_t *pdh;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(depth == svn_depth_empty
                 || depth == svn_depth_files
                 || depth == svn_depth_immediates
                 || depth == svn_depth_infinity);

  /* ### REPOS_ROOT_URL and REPOS_UUID may be NULL. ... more doc: tbd  */

  /* Create the SDB and insert the basic rows.  */
  SVN_ERR(create_db(&sdb, &repos_id, &wc_id, local_abspath, repos_root_url,
                    repos_uuid, SDB_FILE, db->state_pool, scratch_pool));

  /* Begin construction of the PDH.  */
  pdh = apr_pcalloc(db->state_pool, sizeof(*pdh));
  pdh->local_abspath = apr_pstrdup(db->state_pool, local_abspath);

  /* Create the WCROOT for this directory.  */
  SVN_ERR(svn_wc__db_pdh_create_wcroot(&pdh->wcroot, pdh->local_abspath,
                        sdb, wc_id, FORMAT_FROM_SDB,
                        FALSE /* auto-upgrade */,
                        FALSE /* enforce_empty_wq */,
                        db->state_pool, scratch_pool));

  /* The PDH is complete. Stash it into DB.  */
  apr_hash_set(db->dir_data, pdh->local_abspath, APR_HASH_KEY_STRING, pdh);

  blank_ibb(&ibb);

  if (initial_rev > 0)
    ibb.status = svn_wc__db_status_incomplete;
  else
    ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_dir;
  ibb.wc_id = wc_id;
  ibb.local_relpath = "";
  ibb.repos_id = repos_id;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = initial_rev;

  /* ### what about the children?  */
  ibb.children = NULL;
  ibb.depth = depth;

  /* ### no children, conflicts, or work items to install in a txn... */

  return svn_error_return(insert_base_node(&ibb, sdb, scratch_pool));
}


svn_error_t *
svn_wc__db_to_relpath(const char **local_relpath,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, local_relpath, db,
                              local_abspath, svn_sqlite__mode_readonly,
                              result_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_from_relpath(const char **local_abspath,
                        svn_wc__db_t *db,
                        const char *wri_abspath,
                        const char *local_relpath,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *unused_relpath;

#if 0
  SVN_ERR_ASSERT(svn_relpath_is_canonical(local_abspath));
#endif

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &unused_relpath, db,
                              wri_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  *local_abspath = svn_dirent_join(wcroot->abspath,
                                   local_relpath,
                                   result_pool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_get_wcroot(const char **wcroot_abspath,
                      svn_wc__db_t *db,
                      const char *wri_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *unused_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &unused_relpath, db,
                              wri_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

  /* Can't use VERIFY_USABLE_WCROOT, as this should be usable to detect
     where call upgrade */

  if (wcroot == NULL)
    return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                             _("The node '%s' is not in a workingcopy."),
                             svn_dirent_local_style(wri_abspath,
                                                    scratch_pool));

  *wcroot_abspath = apr_pstrdup(result_pool, wcroot->abspath);

  return SVN_NO_ERROR;
}

struct with_sqlite_lock_baton
{
  svn_wc__db_t *db;
  svn_wc__db_sqlite_lock_cb lock_cb;
  void *lock_baton;
};

static svn_error_t *
call_sqlite_lock_cb(void *baton,
                    svn_sqlite__db_t *sdb,
                    apr_pool_t *scratch_pool)
{
  struct with_sqlite_lock_baton *lb = baton;

  return svn_error_return(lb->lock_cb(lb->db, lb->lock_baton, scratch_pool));
}

svn_error_t *
svn_wc__db_with_sqlite_lock(svn_wc__db_t *db,
                            const char *wri_abspath,
                            svn_wc__db_sqlite_lock_cb lock_cb,
                            void *cb_baton,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *unused_relpath;
  struct with_sqlite_lock_baton baton;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &unused_relpath, db,
                              wri_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

  /* Can't use VERIFY_USABLE_WCROOT, as this should be usable to detect
     where call upgrade */

  if (wcroot == NULL || !wcroot->sdb)
    return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                             _("The node '%s' is not in a workingcopy."),
                             svn_dirent_local_style(wri_abspath,
                                                    scratch_pool));

  baton.db = db;
  baton.lock_cb = lock_cb;
  baton.lock_baton = cb_baton;

  return svn_error_return(
            svn_sqlite__with_lock(wcroot->sdb,
                                  call_sqlite_lock_cb,
                                  &baton,
                                  scratch_pool));
}

svn_error_t *
svn_wc__db_base_add_directory(svn_wc__db_t *db,
                              const char *local_abspath,
                              const char *repos_relpath,
                              const char *repos_root_url,
                              const char *repos_uuid,
                              svn_revnum_t revision,
                              const apr_hash_t *props,
                              svn_revnum_t changed_rev,
                              apr_time_t changed_date,
                              const char *changed_author,
                              const apr_array_header_t *children,
                              svn_depth_t depth,
                              apr_hash_t *dav_cache,
                              const svn_skel_t *conflict,
                              const svn_skel_t *work_items,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  apr_int64_t repos_id;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_canonical(repos_root_url, scratch_pool));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
#if 0
  SVN_ERR_ASSERT(children != NULL);
#endif

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          wcroot->sdb, scratch_pool));

  blank_ibb(&ibb);

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_dir;
  ibb.wc_id = wcroot->wc_id;
  ibb.local_relpath = local_relpath;
  ibb.repos_id = repos_id;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  ibb.props = props;
  ibb.changed_rev = changed_rev;
  ibb.changed_date = changed_date;
  ibb.changed_author = changed_author;

  ibb.children = children;
  ibb.depth = depth;

  ibb.dav_cache = dav_cache;
  ibb.conflict = conflict;
  ibb.work_items = work_items;

  /* Insert the directory and all its children transactionally.

     Note: old children can stick around, even if they are no longer present
     in this directory's revision.  */
  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb,
                                       insert_base_node, &ibb,
                                       scratch_pool));

  /* ### worry about flushing child subdirs?  */
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_add_file(svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *repos_relpath,
                         const char *repos_root_url,
                         const char *repos_uuid,
                         svn_revnum_t revision,
                         const apr_hash_t *props,
                         svn_revnum_t changed_rev,
                         apr_time_t changed_date,
                         const char *changed_author,
                         const svn_checksum_t *checksum,
                         svn_filesize_t translated_size,
                         apr_hash_t *dav_cache,
                         const svn_skel_t *conflict,
                         const svn_skel_t *work_items,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  apr_int64_t repos_id;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_canonical(repos_root_url, scratch_pool));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
  SVN_ERR_ASSERT(checksum != NULL);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          wcroot->sdb, scratch_pool));

  blank_ibb(&ibb);

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_file;
  ibb.wc_id = wcroot->wc_id;
  ibb.local_relpath = local_relpath;
  ibb.repos_id = repos_id;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  ibb.props = props;
  ibb.changed_rev = changed_rev;
  ibb.changed_date = changed_date;
  ibb.changed_author = changed_author;

  ibb.checksum = checksum;
  ibb.translated_size = translated_size;

  ibb.dav_cache = dav_cache;
  ibb.conflict = conflict;
  ibb.work_items = work_items;

  /* ### hmm. if this used to be a directory, we should remove children.
     ### or maybe let caller deal with that, if there is a possibility
     ### of a node kind change (rather than eat an extra lookup here).  */

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb,
                                       insert_base_node, &ibb,
                                       scratch_pool));

  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_add_symlink(svn_wc__db_t *db,
                            const char *local_abspath,
                            const char *repos_relpath,
                            const char *repos_root_url,
                            const char *repos_uuid,
                            svn_revnum_t revision,
                            const apr_hash_t *props,
                            svn_revnum_t changed_rev,
                            apr_time_t changed_date,
                            const char *changed_author,
                            const char *target,
                            apr_hash_t *dav_cache,
                            const svn_skel_t *conflict,
                            const svn_skel_t *work_items,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  apr_int64_t repos_id;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_canonical(repos_root_url, scratch_pool));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
  SVN_ERR_ASSERT(target != NULL);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          wcroot->sdb, scratch_pool));

  blank_ibb(&ibb);

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_symlink;
  ibb.wc_id = wcroot->wc_id;
  ibb.local_relpath = local_relpath;
  ibb.repos_id = repos_id;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  ibb.props = props;
  ibb.changed_rev = changed_rev;
  ibb.changed_date = changed_date;
  ibb.changed_author = changed_author;

  ibb.target = target;

  ibb.dav_cache = dav_cache;
  ibb.conflict = conflict;
  ibb.work_items = work_items;

  /* ### hmm. if this used to be a directory, we should remove children.
     ### or maybe let caller deal with that, if there is a possibility
     ### of a node kind change (rather than eat an extra lookup here).  */

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb,
                                       insert_base_node, &ibb,
                                       scratch_pool));

  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));
  return SVN_NO_ERROR;
}


static svn_error_t *
add_absent_excluded_not_present_node(svn_wc__db_t *db,
                                     const char *local_abspath,
                                     const char *repos_relpath,
                                     const char *repos_root_url,
                                     const char *repos_uuid,
                                     svn_revnum_t revision,
                                     svn_wc__db_kind_t kind,
                                     svn_wc__db_status_t status,
                                     const svn_skel_t *conflict,
                                     const svn_skel_t *work_items,
                                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  apr_int64_t repos_id;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_canonical(repos_root_url, scratch_pool));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(status == svn_wc__db_status_absent
                 || status == svn_wc__db_status_excluded
                 || status == svn_wc__db_status_not_present);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          wcroot->sdb, scratch_pool));

  blank_ibb(&ibb);

  ibb.status = status;
  ibb.kind = kind;
  ibb.wc_id = wcroot->wc_id;
  ibb.local_relpath = local_relpath;
  ibb.repos_id = repos_id;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  /* Depending upon KIND, any of these might get used. */
  ibb.children = NULL;
  ibb.depth = svn_depth_unknown;
  ibb.checksum = NULL;
  ibb.translated_size = SVN_INVALID_FILESIZE;
  ibb.target = NULL;

  ibb.conflict = conflict;
  ibb.work_items = work_items;

  /* ### hmm. if this used to be a directory, we should remove children.
     ### or maybe let caller deal with that, if there is a possibility
     ### of a node kind change (rather than eat an extra lookup here).  */

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb,
                                       insert_base_node, &ibb,
                                       scratch_pool));

  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_add_absent_node(svn_wc__db_t *db,
                                const char *local_abspath,
                                const char *repos_relpath,
                                const char *repos_root_url,
                                const char *repos_uuid,
                                svn_revnum_t revision,
                                svn_wc__db_kind_t kind,
                                svn_wc__db_status_t status,
                                const svn_skel_t *conflict,
                                const svn_skel_t *work_items,
                                apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(status == svn_wc__db_status_absent
                 || status == svn_wc__db_status_excluded);

  return add_absent_excluded_not_present_node(
    db, local_abspath, repos_relpath, repos_root_url, repos_uuid, revision,
    kind, status, conflict, work_items, scratch_pool);
}


svn_error_t *
svn_wc__db_base_add_not_present_node(svn_wc__db_t *db,
                                     const char *local_abspath,
                                     const char *repos_relpath,
                                     const char *repos_root_url,
                                     const char *repos_uuid,
                                     svn_revnum_t revision,
                                     svn_wc__db_kind_t kind,
                                     const svn_skel_t *conflict,
                                     const svn_skel_t *work_items,
                                     apr_pool_t *scratch_pool)
{
  return add_absent_excluded_not_present_node(
    db, local_abspath, repos_relpath, repos_root_url, repos_uuid, revision,
    kind, svn_wc__db_status_not_present, conflict, work_items, scratch_pool);
}

struct base_remove_baton {
  const char *local_relpath;
  apr_int64_t wc_id;
};

static svn_error_t *
db_base_remove(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  struct base_remove_baton *brb = baton;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_DELETE_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", brb->wc_id, brb->local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(retract_parent_delete(sdb, brb->wc_id, brb->local_relpath,
                                scratch_pool));

  /* If there is no working node then any actual node must be deleted,
     unless it marks a conflict */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", brb->wc_id, brb->local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));
  if (!have_row)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_DELETE_ACTUAL_NODE_WITHOUT_CONFLICT));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", brb->wc_id, brb->local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_base_remove(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  struct base_remove_baton brb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  brb.local_relpath = local_relpath;
  brb.wc_id = wcroot->wc_id;

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb,
                                       db_base_remove, &brb,
                                       scratch_pool));

  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

/* Like svn_wc__db_base_get_info(), but taking WCROOT+LOCAL_RELPATH instead of
 * DB+LOCAL_ABSPATH and outputting REPOS_ID instead of URL+UUID. */
static svn_error_t *
base_get_info(svn_wc__db_status_t *status,
              svn_wc__db_kind_t *kind,
              svn_revnum_t *revision,
              const char **repos_relpath,
              apr_int64_t *repos_id,
              svn_revnum_t *changed_rev,
              apr_time_t *changed_date,
              const char **changed_author,
              apr_time_t *last_mod_time,
              svn_depth_t *depth,
              const svn_checksum_t **checksum,
              svn_filesize_t *translated_size,
              const char **target,
              svn_wc__db_lock_t **lock,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    lock ? STMT_SELECT_BASE_NODE_WITH_LOCK
                                         : STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      svn_wc__db_kind_t node_kind = svn_sqlite__column_token(stmt, 3,
                                                             kind_map);

      if (kind)
        {
          *kind = node_kind;
        }
      if (status)
        {
          *status = svn_sqlite__column_token(stmt, 2, presence_map);
        }
      err = repos_location_from_columns(repos_id, revision, repos_relpath,
                                        stmt, 0, 4, 1, result_pool);
      SVN_ERR_ASSERT(!repos_id || *repos_id != INVALID_REPOS_ID);
      SVN_ERR_ASSERT(!repos_relpath || *repos_relpath);
      if (lock)
        {
          *lock = lock_from_columns(stmt, 14, 15, 16, 17, result_pool);
        }
      if (changed_rev)
        {
          *changed_rev = svn_sqlite__column_revnum(stmt, 7);
        }
      if (changed_date)
        {
          *changed_date = svn_sqlite__column_int64(stmt, 8);
        }
      if (changed_author)
        {
          /* Result may be NULL. */
          *changed_author = svn_sqlite__column_text(stmt, 9, result_pool);
        }
      if (last_mod_time)
        {
          *last_mod_time = svn_sqlite__column_int64(stmt, 12);
        }
      if (depth)
        {
          if (node_kind != svn_wc__db_kind_dir)
            {
              *depth = svn_depth_unknown;
            }
          else
            {
              const char *depth_str = svn_sqlite__column_text(stmt, 10, NULL);

              if (depth_str == NULL)
                *depth = svn_depth_unknown;
              else
                *depth = svn_depth_from_word(depth_str);
            }
        }
      if (checksum)
        {
          if (node_kind != svn_wc__db_kind_file)
            {
              *checksum = NULL;
            }
          else
            {
              err = svn_sqlite__column_checksum(checksum, stmt, 5,
                                                result_pool);
              if (err != NULL)
                err = svn_error_createf(
                        err->apr_err, err,
                        _("The node '%s' has a corrupt checksum value."),
                        path_for_error_message(wcroot, local_relpath,
                                               scratch_pool));
            }
        }
      if (translated_size)
        {
          *translated_size = get_translated_size(stmt, 6);
        }
      if (target)
        {
          if (node_kind != svn_wc__db_kind_symlink)
            *target = NULL;
          else
            *target = svn_sqlite__column_text(stmt, 11, result_pool);
        }
    }
  else
    {
      err = svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("The node '%s' was not found."),
                              path_for_error_message(wcroot, local_relpath,
                                                     scratch_pool));
    }

  /* Note: given the composition, no need to wrap for tracing.  */
  return svn_error_compose_create(err, svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_base_get_info(svn_wc__db_status_t *status,
                         svn_wc__db_kind_t *kind,
                         svn_revnum_t *revision,
                         const char **repos_relpath,
                         const char **repos_root_url,
                         const char **repos_uuid,
                         svn_revnum_t *changed_rev,
                         apr_time_t *changed_date,
                         const char **changed_author,
                         apr_time_t *last_mod_time,
                         svn_depth_t *depth,
                         const svn_checksum_t **checksum,
                         svn_filesize_t *translated_size,
                         const char **target,
                         svn_wc__db_lock_t **lock,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  apr_int64_t repos_id;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(base_get_info(status, kind, revision, repos_relpath, &repos_id,
                        changed_rev, changed_date, changed_author,
                        last_mod_time, depth, checksum, translated_size,
                        target, lock,
                        wcroot, local_relpath, result_pool, scratch_pool));
  SVN_ERR_ASSERT(repos_id != INVALID_REPOS_ID);
  SVN_ERR(fetch_repos_info(repos_root_url, repos_uuid,
                           wcroot->sdb, repos_id, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_base_get_prop(const svn_string_t **propval,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         const char *propname,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  apr_hash_t *props;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(propname != NULL);

  /* Note: maybe one day, we'll have internal caches of this stuff, but
     for now, we just grab all the props and pick out the requested prop. */

  /* ### should: fetch into scratch_pool, then dup into result_pool.  */
  SVN_ERR(svn_wc__db_base_get_props(&props, db, local_abspath,
                                    result_pool, scratch_pool));

  *propval = apr_hash_get(props, propname, APR_HASH_KEY_STRING);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_get_props(apr_hash_t **props,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err;

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_SELECT_BASE_PROPS, scratch_pool));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    {
      err = svn_sqlite__reset(stmt);
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, err,
                               _("The node '%s' was not found."),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  err = svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                      scratch_pool);
  if (err == NULL && *props == NULL)
    {
      /* ### is this a DB constraint violation? the column "probably" should
         ### never be null.  */
      *props = apr_hash_make(result_pool);
    }

  return svn_error_compose_create(err, svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_base_get_children(const apr_array_header_t **children,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                             local_abspath,
                                             svn_sqlite__mode_readonly,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return gather_repo_children(children, wcroot, local_relpath, 0,
                              result_pool, scratch_pool);
}


svn_error_t *
svn_wc__db_base_set_dav_cache(svn_wc__db_t *db,
                              const char *local_abspath,
                              const apr_hash_t *props,
                              apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_UPDATE_BASE_NODE_DAV_CACHE,
                                 scratch_pool));
  SVN_ERR(svn_sqlite__bind_properties(stmt, 3, props, scratch_pool));

  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows != 1)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_base_get_dav_cache(apr_hash_t **props,
                              svn_wc__db_t *db,
                              const char *local_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_SELECT_BASE_DAV_CACHE, scratch_pool));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                             svn_sqlite__reset(stmt),
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                        scratch_pool));
  return svn_error_return(svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_base_clear_dav_cache_recursive(svn_wc__db_t *db,
                                          const char *local_abspath,
                                          apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *like_arg;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                                             db, local_abspath,
                                             svn_sqlite__mode_readwrite,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  like_arg = construct_like_arg(local_relpath, scratch_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_CLEAR_BASE_NODE_RECURSIVE_DAV_CACHE));
  SVN_ERR(svn_sqlite__bindf(stmt, "iss", wcroot->wc_id, local_relpath,
                            like_arg));

  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}

/* Helper for svn_wc__db_op_copy to handle copying from one db to
   another */
static svn_error_t *
cross_db_copy(svn_wc__db_wcroot_t *src_wcroot,
              const char *src_relpath,
              svn_wc__db_wcroot_t *dst_wcroot,
              const char *dst_relpath,
              svn_wc__db_status_t dst_status,
              apr_int64_t dst_op_depth,
              svn_wc__db_kind_t kind,
              const apr_array_header_t *children,
              apr_int64_t copyfrom_id,
              const char *copyfrom_relpath,
              svn_revnum_t copyfrom_rev,
              apr_pool_t *scratch_pool)
{
  insert_working_baton_t iwb;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  const svn_checksum_t *checksum;
  apr_hash_t *props;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_depth_t depth;

  SVN_ERR_ASSERT(kind == svn_wc__db_kind_file
                 || kind == svn_wc__db_kind_dir
                 );

  SVN_ERR(read_info(NULL /* status */,
                    NULL /* kind */,
                    NULL /* revision */,
                    NULL /* repos_relpath */,
                    NULL /* repos_id */,
                    &changed_rev, &changed_date, &changed_author,
                    NULL /* last_mod_time */,
                    &depth,
                    &checksum,
                    NULL /* translated_size */,
                    NULL /* target */,
                    NULL /* changelist */,
                    NULL /* original_repos_relpath */,
                    NULL /* original_repos_id */,
                    NULL /* original_revision */,
                    NULL /* props_mod */,
                    NULL /* have_base */,
                    NULL /* have_work */,
                    NULL /* conflicted */,
                    NULL /* lock */,
                    src_wcroot, src_relpath, scratch_pool, scratch_pool));

  SVN_ERR(db_read_pristine_props(&props, src_wcroot, src_relpath,
                                 scratch_pool, scratch_pool));

  blank_iwb(&iwb);
  iwb.presence = dst_status;
  iwb.kind = kind;
  iwb.wc_id = dst_wcroot->wc_id;
  iwb.local_relpath = dst_relpath;

  iwb.props = props;
  iwb.changed_rev = changed_rev;
  iwb.changed_date = changed_date;
  iwb.changed_author = changed_author;
  iwb.original_repos_id = copyfrom_id;
  iwb.original_repos_relpath = copyfrom_relpath;
  iwb.original_revnum = copyfrom_rev;
  iwb.moved_here = FALSE;

  iwb.op_depth = dst_op_depth;

  iwb.checksum = checksum;
  iwb.children = children;
  iwb.depth = depth;

  SVN_ERR(insert_working_node(&iwb, dst_wcroot->sdb, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, src_wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", src_wcroot->wc_id, src_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      /* const char *prop_reject = svn_sqlite__column_text(stmt, 0,
                                                           scratch_pool);

         ### STMT_INSERT_ACTUAL_NODE doesn't cover every column, it's
         ### enough for some cases but will probably need to extended. */
      const char *changelist = svn_sqlite__column_text(stmt, 1, scratch_pool);
      const char *conflict_old = svn_sqlite__column_text(stmt, 2, scratch_pool);
      const char *conflict_new = svn_sqlite__column_text(stmt, 3, scratch_pool);
      const char *conflict_working = svn_sqlite__column_text(stmt, 4,
                                                             scratch_pool);
      const char *tree_conflict_data = svn_sqlite__column_text(stmt, 5,
                                                               scratch_pool);
      apr_size_t props_size;

      /* No need to parse the properties when simply copying. */
      const char *properties = svn_sqlite__column_blob(stmt, 6, &props_size,
                                                       scratch_pool);
      SVN_ERR(svn_sqlite__reset(stmt));
      SVN_ERR(svn_sqlite__get_statement(&stmt, dst_wcroot->sdb,
                                        STMT_INSERT_ACTUAL_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "issbsssss",
                                dst_wcroot->wc_id, dst_relpath,
                                svn_relpath_dirname(dst_relpath, scratch_pool),
                                properties, props_size,
                                conflict_old, conflict_new, conflict_working,
                                changelist, tree_conflict_data));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

/* Set *COPYFROM_ID, *COPYFROM_RELPATH, *COPYFROM_REV to the values
   appropriate for the copy. Also return *STATUS, *KIND and *HAVE_WORK
   since they are available.  This is a helper for
   svn_wc__db_op_copy. */
static svn_error_t *
get_info_for_copy(apr_int64_t *copyfrom_id,
                  const char **copyfrom_relpath,
                  svn_revnum_t *copyfrom_rev,
                  svn_wc__db_status_t *status,
                  svn_wc__db_kind_t *kind,
                  svn_boolean_t *have_work,
                  svn_wc__db_wcroot_t *wcroot,
                  const char *local_relpath,
                  apr_pool_t *result_pool,
                  apr_pool_t *scratch_pool)
{
  const char *repos_relpath;
  svn_revnum_t revision;

  SVN_ERR(read_info(status, kind, &revision, &repos_relpath, copyfrom_id,
                    NULL /* changed_rev */,
                    NULL /* changed_date */,
                    NULL /* changed_author */,
                    NULL /* last_mod_time */,
                    NULL /* depth */,
                    NULL /* checksum */,
                    NULL /* translated_size */,
                    NULL /* target */,
                    NULL /* changelist */,
                    NULL /* original_repos_relpath */,
                    NULL /* original_repos_id */,
                    NULL /* original_revision */,
                    NULL /* props_mod */,
                    NULL /* have_base */,
                    have_work,
                    NULL /* conflicted */,
                    NULL /* lock */,
                    wcroot, local_relpath, result_pool, scratch_pool));

  if (*status == svn_wc__db_status_excluded)
    {
      /* The parent cannot be excluded, so look at the parent and then
         adjust the relpath */
      const char *parent_relpath, *base_name;
      svn_wc__db_status_t parent_status;
      svn_wc__db_kind_t parent_kind;
      svn_boolean_t parent_have_work;

      svn_dirent_split(&parent_relpath, &base_name, local_relpath,
                       scratch_pool);
      SVN_ERR(get_info_for_copy(copyfrom_id, copyfrom_relpath, copyfrom_rev,
                                &parent_status, &parent_kind, &parent_have_work,
                                wcroot, parent_relpath,
                                scratch_pool, scratch_pool));
      if (*copyfrom_relpath)
        *copyfrom_relpath = svn_relpath_join(*copyfrom_relpath, base_name,
                                             result_pool);
    }
  else if (*status == svn_wc__db_status_added)
    {
      const char *op_root_relpath;

      SVN_ERR(scan_addition(NULL, &op_root_relpath,
                            NULL, NULL, /* repos_* */
                            copyfrom_relpath, copyfrom_id, copyfrom_rev,
                            wcroot, local_relpath,
                            scratch_pool, scratch_pool));
      if (*copyfrom_relpath)
        {
          *copyfrom_relpath
            = svn_relpath_join(*copyfrom_relpath,
                               svn_dirent_skip_ancestor(op_root_relpath,
                                                        local_relpath),
                               result_pool);
        }
    }
  else if (*status == svn_wc__db_status_deleted)
    {
      const char *base_del_relpath, *work_del_relpath;

      SVN_ERR(scan_deletion(&base_del_relpath, NULL, &work_del_relpath,
                            wcroot, local_relpath, scratch_pool,
                            scratch_pool));
      if (work_del_relpath)
        {
          const char *op_root_relpath;
          const char *parent_del_relpath = svn_dirent_dirname(work_del_relpath,
                                                              scratch_pool);

          /* Similar to, but not the same as, the _scan_addition and
             _join above.  Can we use get_copyfrom here? */
          SVN_ERR(scan_addition(NULL, &op_root_relpath,
                                NULL, NULL, /* repos_* */
                                copyfrom_relpath, copyfrom_id, copyfrom_rev,
                                wcroot, parent_del_relpath,
                                scratch_pool, scratch_pool));
          *copyfrom_relpath
            = svn_relpath_join(*copyfrom_relpath,
                               svn_dirent_skip_ancestor(op_root_relpath,
                                                        local_relpath),
                               result_pool);
        }
      else if (base_del_relpath)
        {
          SVN_ERR(base_get_info(NULL, NULL, copyfrom_rev, copyfrom_relpath,
                                copyfrom_id,
                                NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                NULL, NULL,
                                wcroot, base_del_relpath,
                                result_pool, scratch_pool));
        }
      else
        SVN_ERR_MALFUNCTION();
    }
  else
    {
      *copyfrom_relpath = repos_relpath;
      *copyfrom_rev = revision;
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
op_depth_of(apr_int64_t *op_depth,
            svn_wc__db_wcroot_t *wcroot,
            const char *local_relpath);

static svn_error_t *
op_depth_for_copy(apr_int64_t *op_depth,
                  apr_int64_t copyfrom_repos_id,
                  const char *copyfrom_relpath,
                  svn_revnum_t copyfrom_revision,
                  svn_wc__db_wcroot_t *wcroot,
                  const char *local_relpath,
                  apr_pool_t *scratch_pool);

/* Like svn_wc__db_op_copy(), but with PDH+LOCAL_RELPATH instead of
 * DB+LOCAL_ABSPATH. */
static svn_error_t *
db_op_copy(svn_wc__db_wcroot_t *src_wcroot,
           const char *src_relpath,
           svn_wc__db_wcroot_t *dst_wcroot,
           const char *dst_relpath,
           const svn_skel_t *work_items,
           apr_pool_t *scratch_pool)
{
  const char *copyfrom_relpath;
  svn_revnum_t copyfrom_rev;
  svn_wc__db_status_t status, dst_status;
  svn_boolean_t have_work;
  apr_int64_t copyfrom_id;
  apr_int64_t dst_op_depth;
  svn_wc__db_kind_t kind;
  const apr_array_header_t *children;

  SVN_ERR(get_info_for_copy(&copyfrom_id, &copyfrom_relpath, &copyfrom_rev,
                            &status, &kind, &have_work, src_wcroot,
                            src_relpath, scratch_pool, scratch_pool));

  SVN_ERR(op_depth_for_copy(&dst_op_depth, copyfrom_id,
                            copyfrom_relpath, copyfrom_rev,
                            dst_wcroot, dst_relpath, scratch_pool));

  SVN_ERR_ASSERT(kind == svn_wc__db_kind_file || kind == svn_wc__db_kind_dir);

  /* ### New status, not finished, see notes/wc-ng/copying */
  switch (status)
    {
    case svn_wc__db_status_normal:
    case svn_wc__db_status_added:
    case svn_wc__db_status_moved_here:
    case svn_wc__db_status_copied:
      dst_status = svn_wc__db_status_normal;
      break;
    case svn_wc__db_status_deleted:
    case svn_wc__db_status_not_present:
      dst_status = svn_wc__db_status_not_present;
      break;
    case svn_wc__db_status_excluded:
      dst_status = svn_wc__db_status_excluded;
      break;
    case svn_wc__db_status_absent:
      return svn_error_createf(SVN_ERR_AUTHZ_UNREADABLE, NULL,
                               _("Cannot copy '%s' excluded by server"),
                               path_for_error_message(src_wcroot,
                                                      src_relpath,
                                                      scratch_pool));
    default:
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                               _("Cannot handle status of '%s'"),
                               path_for_error_message(src_wcroot,
                                                      src_relpath,
                                                      scratch_pool));
    }

  if (kind == svn_wc__db_kind_dir)
    {
      apr_int64_t src_op_depth;

      SVN_ERR(op_depth_of(&src_op_depth, src_wcroot, src_relpath));
      SVN_ERR(gather_repo_children(&children, src_wcroot, src_relpath,
                                   src_op_depth, scratch_pool, scratch_pool));
    }
  else
    children = NULL;

  if (src_wcroot == dst_wcroot)
    {
      svn_sqlite__stmt_t *stmt;
      const char *dst_parent_relpath = svn_relpath_dirname(dst_relpath,
                                                           scratch_pool);

      if (have_work)
        SVN_ERR(svn_sqlite__get_statement(&stmt, src_wcroot->sdb,
                         STMT_INSERT_WORKING_NODE_COPY_FROM_WORKING));
      else
        SVN_ERR(svn_sqlite__get_statement(&stmt, src_wcroot->sdb,
                          STMT_INSERT_WORKING_NODE_COPY_FROM_BASE));

      SVN_ERR(svn_sqlite__bindf(stmt, "issisnnnt",
                    src_wcroot->wc_id, src_relpath,
                    dst_relpath,
                    dst_op_depth,
                    dst_parent_relpath,
                    presence_map, dst_status));

      if (copyfrom_relpath)
        {
          SVN_ERR(svn_sqlite__bind_int64(stmt, 6, copyfrom_id));
          SVN_ERR(svn_sqlite__bind_text(stmt, 7, copyfrom_relpath));
          SVN_ERR(svn_sqlite__bind_int64(stmt, 8, copyfrom_rev));
        }
      SVN_ERR(svn_sqlite__step_done(stmt));

      /* ### Copying changelist is OK for a move but what about a copy? */
      SVN_ERR(svn_sqlite__get_statement(&stmt, src_wcroot->sdb,
                                  STMT_INSERT_ACTUAL_NODE_FROM_ACTUAL_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isss", src_wcroot->wc_id, src_relpath,
                                dst_relpath, dst_parent_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));

      /* Insert incomplete children, if relevant.
         The children are part of the same op and so have the same op_depth.
         (The only time we'd want a different depth is during a recursive
         simple add, but we never insert children here during a simple add.) */
      if (kind == svn_wc__db_kind_dir)
        SVN_ERR(insert_incomplete_children(dst_wcroot->sdb,
                                           dst_wcroot->wc_id,
                                           dst_relpath,
                                           INVALID_REPOS_ID /* inherit repos_id */,
                                           NULL /* inherit repos_path */,
                                           copyfrom_rev,
                                           children,
                                           dst_op_depth,
                                           scratch_pool));
    }
  else
    {
      SVN_ERR(cross_db_copy(src_wcroot, src_relpath, dst_wcroot,
                            dst_relpath, dst_status, dst_op_depth, kind,
                            children, copyfrom_id, copyfrom_relpath,
                            copyfrom_rev, scratch_pool));
    }

  SVN_ERR(add_work_items(dst_wcroot->sdb, work_items, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_copy(svn_wc__db_t *db,
                   const char *src_abspath,
                   const char *dst_abspath,
                   const char *dst_op_root_abspath,
                   const svn_skel_t *work_items,
                   apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *src_wcroot;
  svn_wc__db_wcroot_t *dst_wcroot;
  const char *src_relpath, *dst_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&src_wcroot, &src_relpath, db,
                                             src_abspath,
                                             svn_sqlite__mode_readwrite,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(src_wcroot);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&dst_wcroot, &dst_relpath, db,
                                             dst_abspath,
                                             svn_sqlite__mode_readwrite,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(dst_wcroot);

  /* ### This should all happen in one transaction. */
  SVN_ERR(db_op_copy(src_wcroot, src_relpath, dst_wcroot,
                     dst_relpath, work_items, scratch_pool));

  return SVN_NO_ERROR;
}

/* Set *OP_DEPTH to the highest op depth of WCROOT:LOCAL_RELPATH. */
static svn_error_t *
op_depth_of(apr_int64_t *op_depth,
            svn_wc__db_wcroot_t *wcroot,
            const char *local_relpath)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR_ASSERT(have_row);
  *op_depth = svn_sqlite__column_int64(stmt, 0);
  SVN_ERR(svn_sqlite__reset(stmt));
  return SVN_NO_ERROR;
}

/* If there are any absent (excluded by authz) base nodes then the
   copy must fail as it's not possible to commit such a copy.  Return
   an error if there are any absent nodes. */
static svn_error_t *
catch_copy_of_absent(svn_wc__db_wcroot_t *wcroot,
                     const char *local_relpath,
                     apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *absent_relpath;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ABSENT_NODES));
  SVN_ERR(svn_sqlite__bindf(stmt, "iss",
                            wcroot->wc_id,
                            local_relpath,
                            construct_like_arg(local_relpath,
                                               scratch_pool)));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    absent_relpath = svn_sqlite__column_text(stmt, 0, scratch_pool);
  SVN_ERR(svn_sqlite__reset(stmt));
  if (have_row)
    return svn_error_createf(SVN_ERR_AUTHZ_UNREADABLE, NULL,
                             _("Cannot copy '%s' excluded by server"),
                             path_for_error_message(wcroot, absent_relpath,
                                                    scratch_pool));

  return SVN_NO_ERROR;
}


/* If LOCAL_RELPATH is presence=incomplete then set *OP_DEPTH to the
   op_depth of the incomplete node, otherwise if the copyfrom
   information COPYFROM_REPOS_ID+COPYFROM_RELPATH+COPYFROM_REVISION
   "matches" the copyfrom information for the parent of LOCAL_RELPATH
   then set *OP_DEPTH to the op_depth of the parent, otherwise set
   *OP_DEPTH to the op_depth of LOCAL_RELPATH. */
static svn_error_t *
op_depth_for_copy(apr_int64_t *op_depth,
                  apr_int64_t copyfrom_repos_id,
                  const char *copyfrom_relpath,
                  svn_revnum_t copyfrom_revision,
                  svn_wc__db_wcroot_t *wcroot,
                  const char *local_relpath,
                  apr_pool_t *scratch_pool)
{
  const char *parent_relpath, *name;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  *op_depth = relpath_depth(local_relpath);

  if (!copyfrom_relpath)
    return SVN_NO_ERROR;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      svn_wc__db_status_t status = svn_sqlite__column_token(stmt, 1,
                                                            presence_map);
      if (status == svn_wc__db_status_incomplete)
        {
          *op_depth = svn_sqlite__column_int64(stmt, 0);
          SVN_ERR(svn_sqlite__reset(stmt));
          return SVN_NO_ERROR;
        }
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  svn_relpath_split(&parent_relpath, &name, local_relpath, scratch_pool);
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, parent_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      svn_wc__db_status_t status = svn_sqlite__column_token(stmt, 1,
                                                            presence_map);
      svn_error_t *err = convert_to_working_status(&status, status);
      if (err)
        SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
      if (status == svn_wc__db_status_added)
        {
          apr_int64_t parent_copyfrom_repos_id
            = svn_sqlite__column_int64(stmt, 10);
          const char *parent_copyfrom_relpath
            = svn_sqlite__column_text(stmt, 11, NULL);
          svn_revnum_t parent_copyfrom_revision
            = svn_sqlite__column_revnum(stmt, 12);

          if (parent_copyfrom_repos_id == copyfrom_repos_id
              && copyfrom_revision == parent_copyfrom_revision
              && !strcmp(svn_relpath_join(parent_copyfrom_relpath, name,
                                          scratch_pool),
                         copyfrom_relpath))
            *op_depth = svn_sqlite__column_int64(stmt, 0);
        }
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_copy_dir(svn_wc__db_t *db,
                       const char *local_abspath,
                       const apr_hash_t *props,
                       svn_revnum_t changed_rev,
                       apr_time_t changed_date,
                       const char *changed_author,
                       const char *original_repos_relpath,
                       const char *original_root_url,
                       const char *original_uuid,
                       svn_revnum_t original_revision,
                       const apr_array_header_t *children,
                       svn_depth_t depth,
                       const svn_skel_t *conflict,
                       const svn_skel_t *work_items,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_working_baton_t iwb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);
  /* ### any assertions for CHANGED_* ?  */
  /* ### any assertions for ORIGINAL_* ?  */
#if 0
  SVN_ERR_ASSERT(children != NULL);
#endif
  SVN_ERR_ASSERT(conflict == NULL);  /* ### can't handle yet  */

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_wc__db_kind_dir;
  iwb.wc_id = wcroot->wc_id;
  iwb.local_relpath = local_relpath;

  iwb.props = props;
  iwb.changed_rev = changed_rev;
  iwb.changed_date = changed_date;
  iwb.changed_author = changed_author;
  iwb.moved_here = FALSE;

  if (original_root_url != NULL)
    {
      SVN_ERR(create_repos_id(&iwb.original_repos_id,
                              original_root_url, original_uuid,
                              wcroot->sdb, scratch_pool));
      iwb.original_repos_relpath = original_repos_relpath;
      iwb.original_revnum = original_revision;
    }

  /* ### Should we do this inside the transaction? */
  SVN_ERR(op_depth_for_copy(&iwb.op_depth, iwb.original_repos_id,
                            original_repos_relpath, original_revision,
                            wcroot, local_relpath, scratch_pool));

  iwb.children = children;
  iwb.depth = depth;

  iwb.work_items = work_items;

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb, insert_working_node, &iwb,
                                       scratch_pool));
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_copy_file(svn_wc__db_t *db,
                        const char *local_abspath,
                        const apr_hash_t *props,
                        svn_revnum_t changed_rev,
                        apr_time_t changed_date,
                        const char *changed_author,
                        const char *original_repos_relpath,
                        const char *original_root_url,
                        const char *original_uuid,
                        svn_revnum_t original_revision,
                        const svn_checksum_t *checksum,
                        const svn_skel_t *conflict,
                        const svn_skel_t *work_items,
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_working_baton_t iwb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);
  /* ### any assertions for CHANGED_* ?  */
  SVN_ERR_ASSERT((! original_repos_relpath && ! original_root_url
                  && ! original_uuid && ! checksum
                  && original_revision == SVN_INVALID_REVNUM)
                 || (original_repos_relpath && original_root_url
                     && original_uuid && checksum
                     && original_revision != SVN_INVALID_REVNUM));
  SVN_ERR_ASSERT(conflict == NULL);  /* ### can't handle yet  */

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_wc__db_kind_file;
  iwb.wc_id = wcroot->wc_id;
  iwb.local_relpath = local_relpath;

  iwb.props = props;
  iwb.changed_rev = changed_rev;
  iwb.changed_date = changed_date;
  iwb.changed_author = changed_author;
  iwb.moved_here = FALSE;

  if (original_root_url != NULL)
    {
      SVN_ERR(create_repos_id(&iwb.original_repos_id,
                              original_root_url, original_uuid,
                              wcroot->sdb, scratch_pool));
      iwb.original_repos_relpath = original_repos_relpath;
      iwb.original_revnum = original_revision;
    }

  /* ### Should we do this inside the transaction? */
  SVN_ERR(op_depth_for_copy(&iwb.op_depth, iwb.original_repos_id,
                            original_repos_relpath, original_revision,
                            wcroot, local_relpath, scratch_pool));

  iwb.checksum = checksum;

  iwb.work_items = work_items;

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb, insert_working_node, &iwb,
                                       scratch_pool));
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_copy_symlink(svn_wc__db_t *db,
                           const char *local_abspath,
                           const apr_hash_t *props,
                           svn_revnum_t changed_rev,
                           apr_time_t changed_date,
                           const char *changed_author,
                           const char *original_repos_relpath,
                           const char *original_root_url,
                           const char *original_uuid,
                           svn_revnum_t original_revision,
                           const char *target,
                           const svn_skel_t *conflict,
                           const svn_skel_t *work_items,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_working_baton_t iwb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);
  /* ### any assertions for CHANGED_* ?  */
  /* ### any assertions for ORIGINAL_* ?  */
  SVN_ERR_ASSERT(target != NULL);
  SVN_ERR_ASSERT(conflict == NULL);  /* ### can't handle yet  */

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_wc__db_kind_symlink;
  iwb.wc_id = wcroot->wc_id;
  iwb.local_relpath = local_relpath;

  iwb.props = props;
  iwb.changed_rev = changed_rev;
  iwb.changed_date = changed_date;
  iwb.changed_author = changed_author;
  iwb.moved_here = FALSE;

  if (original_root_url != NULL)
    {
      SVN_ERR(create_repos_id(&iwb.original_repos_id,
                              original_root_url, original_uuid,
                              wcroot->sdb, scratch_pool));
      iwb.original_repos_relpath = original_repos_relpath;
      iwb.original_revnum = original_revision;
    }

  /* ### Should we do this inside the transaction? */
  SVN_ERR(op_depth_for_copy(&iwb.op_depth, iwb.original_repos_id,
                            original_repos_relpath, original_revision,
                            wcroot, local_relpath, scratch_pool));

  iwb.target = target;

  iwb.work_items = work_items;

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb,
                                       insert_working_node, &iwb,
                                       scratch_pool));
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_add_directory(svn_wc__db_t *db,
                            const char *local_abspath,
                            const svn_skel_t *work_items,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_working_baton_t iwb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_wc__db_kind_dir;
  iwb.wc_id = wcroot->wc_id;
  iwb.local_relpath = local_relpath;
  iwb.op_depth = relpath_depth(local_relpath);

  iwb.work_items = work_items;

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb,
                                       insert_working_node, &iwb,
                                       scratch_pool));
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_add_file(svn_wc__db_t *db,
                       const char *local_abspath,
                       const svn_skel_t *work_items,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_working_baton_t iwb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_wc__db_kind_file;
  iwb.wc_id = wcroot->wc_id;
  iwb.local_relpath = local_relpath;
  iwb.op_depth = relpath_depth(local_relpath);

  iwb.work_items = work_items;

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb,
                                       insert_working_node, &iwb,
                                       scratch_pool));
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_add_symlink(svn_wc__db_t *db,
                          const char *local_abspath,
                          const char *target,
                          const svn_skel_t *work_items,
                          apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  insert_working_baton_t iwb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(target != NULL);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_wc__db_kind_symlink;
  iwb.wc_id = wcroot->wc_id;
  iwb.local_relpath = local_relpath;
  iwb.op_depth = relpath_depth(local_relpath);

  iwb.target = target;

  iwb.work_items = work_items;

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb,
                                       insert_working_node, &iwb,
                                       scratch_pool));
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


struct set_props_baton
{
  apr_hash_t *props;

  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  const svn_skel_t *conflict;
  const svn_skel_t *work_items;
};


/* Set the ACTUAL_NODE properties column for (WC_ID, LOCAL_RELPATH) to
 * PROPS. */
static svn_error_t *
set_actual_props(apr_int64_t wc_id,
                 const char *local_relpath,
                 apr_hash_t *props,
                 svn_sqlite__db_t *db,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR(svn_sqlite__get_statement(&stmt, db, STMT_UPDATE_ACTUAL_PROPS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  SVN_ERR(svn_sqlite__bind_properties(stmt, 3, props, scratch_pool));
  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows == 1 || !props)
    return SVN_NO_ERROR; /* We are done */

  /* We have to insert a row in ACTUAL */

  SVN_ERR(svn_sqlite__get_statement(&stmt, db, STMT_INSERT_ACTUAL_PROPS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  if (*local_relpath != '\0')
    SVN_ERR(svn_sqlite__bind_text(stmt, 3,
                                  svn_relpath_dirname(local_relpath,
                                                      scratch_pool)));
  SVN_ERR(svn_sqlite__bind_properties(stmt, 4, props, scratch_pool));
  return svn_error_return(svn_sqlite__step_done(stmt));
}

/* Set the 'properties' column in the 'ACTUAL_NODE' table to BATON->props.
   Create an entry in the ACTUAL table for the node if it does not yet
   have one.
   To specify no properties, BATON->props must be an empty hash, not NULL.
   BATON is of type 'struct set_props_baton'. */
static svn_error_t *
set_props_txn(void *baton, svn_sqlite__db_t *db, apr_pool_t *scratch_pool)
{
  struct set_props_baton *spb = baton;
  apr_hash_t *pristine_props;

  /* ### we dunno what to do with CONFLICT yet.  */
  SVN_ERR_ASSERT(spb->conflict == NULL);

  /* First order of business: insert all the work items.  */
  SVN_ERR(add_work_items(db, spb->work_items, scratch_pool));

  /* Check if the props are modified. If no changes, then wipe out the
     ACTUAL props.  PRISTINE_PROPS==NULL means that any
     ACTUAL props are okay as provided, so go ahead and set them.  */
  SVN_ERR(db_read_pristine_props(&pristine_props, spb->wcroot,
                                 spb->local_relpath,
                                 scratch_pool, scratch_pool));
  if (spb->props && pristine_props)
    {
      apr_array_header_t *prop_diffs;

      SVN_ERR(svn_prop_diffs(&prop_diffs, spb->props, pristine_props,
                             scratch_pool));
      if (prop_diffs->nelts == 0)
        spb->props = NULL;
    }

  SVN_ERR(set_actual_props(spb->wcroot->wc_id, spb->local_relpath,
                           spb->props, db, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_set_props(svn_wc__db_t *db,
                        const char *local_abspath,
                        apr_hash_t *props,
                        const svn_skel_t *conflict,
                        const svn_skel_t *work_items,
                        apr_pool_t *scratch_pool)
{
  struct set_props_baton spb;
  svn_wc__db_wcroot_t *wcroot;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &spb.local_relpath,
                              db, local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  spb.props = props;
  spb.wcroot = wcroot;
  spb.conflict = conflict;
  spb.work_items = work_items;

  return svn_error_return(
            svn_sqlite__with_transaction(wcroot->sdb,
                                         set_props_txn,
                                         &spb,
                                         scratch_pool));
}

#ifdef SVN__SUPPORT_BASE_MERGE

/* Set properties in a given table. The row must exist.  */
static svn_error_t *
set_properties(svn_wc__db_t *db,
               const char *local_abspath,
               const apr_hash_t *props,
               int stmt_idx,
               const char *table_name,
               apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR_ASSERT(props != NULL);

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath, stmt_idx,
                                 scratch_pool));

  SVN_ERR(svn_sqlite__bind_properties(stmt, 3, props, scratch_pool));
  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows != 1)
    return svn_error_createf(SVN_ERR_WC_DB_ERROR, NULL,
                             _("Can't store properties for '%s' in '%s'."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool),
                             table_name);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_base_set_props(svn_wc__db_t *db,
                               const char *local_abspath,
                               const apr_hash_t *props,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR(set_properties(db, local_abspath, props,
                         STMT_UPDATE_NODE_BASE_PROPS,
                         "base node", scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_working_set_props(svn_wc__db_t *db,
                                  const char *local_abspath,
                                  const apr_hash_t *props,
                                  apr_pool_t *scratch_pool)
{
  SVN_ERR(set_properties(db, local_abspath, props,
                         STMT_UPDATE_NODE_WORKING_PROPS,
                         "working node", scratch_pool));
  return SVN_NO_ERROR;
}

#endif /* SVN__SUPPORT_BASE_MERGE  */

svn_error_t *
svn_wc__db_op_delete(svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_move(svn_wc__db_t *db,
                   const char *src_abspath,
                   const char *dst_abspath,
                   apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_modified(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


struct set_changelist_baton
{
  const char *local_relpath;
  apr_int64_t wc_id;
  const char *changelist;
};

/* */
static svn_error_t *
set_changelist_txn(void *baton,
                   svn_sqlite__db_t *sdb,
                   apr_pool_t *scratch_pool)
{
  struct set_changelist_baton *scb = baton;
  const char *existing_changelist;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", scb->wc_id, scb->local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    existing_changelist = svn_sqlite__column_text(stmt, 1, scratch_pool);
  SVN_ERR(svn_sqlite__reset(stmt));

  if (!have_row)
    {
      /* We need to insert an ACTUAL node, but only if we're not attempting
         to remove a (non-existent) changelist. */
      if (scb->changelist == NULL)
        return SVN_NO_ERROR;

      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_INSERT_ACTUAL_CHANGELIST));

      /* The parent of relpath=="" is null, so we simply skip binding the
         column. Otherwise, bind the proper value to 'parent_relpath'.  */
      if (*scb->local_relpath != '\0')
        SVN_ERR(svn_sqlite__bind_text(stmt, 4,
                                      svn_relpath_dirname(scb->local_relpath,
                                                          scratch_pool)));
    }
  else
    {
      /* We have an existing row, and it simply needs to be updated, if
         it's different. */
      if (existing_changelist
            && scb->changelist
            && strcmp(existing_changelist, scb->changelist) == 0)
        return SVN_NO_ERROR;

      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_UPDATE_ACTUAL_CHANGELIST));
    }

  SVN_ERR(svn_sqlite__bindf(stmt, "iss", scb->wc_id, scb->local_relpath,
                            scb->changelist));

  return svn_error_return(svn_sqlite__step_done(stmt));
}

svn_error_t *
svn_wc__db_op_set_changelist(svn_wc__db_t *db,
                             const char *local_abspath,
                             const char *changelist,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  struct set_changelist_baton scb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &scb.local_relpath,
                              db, local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  scb.wc_id = wcroot->wc_id;
  scb.changelist = changelist;

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb, set_changelist_txn,
                                       &scb, scratch_pool));

  /* No need to flush the parent entries; changelists were not stored in the
     stub */
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_mark_conflict(svn_wc__db_t *db,
                            const char *local_abspath,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_mark_resolved(svn_wc__db_t *db,
                            const char *local_abspath,
                            svn_boolean_t resolved_text,
                            svn_boolean_t resolved_props,
                            svn_boolean_t resolved_tree,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* ### we're not ready to handy RESOLVED_TREE just yet.  */
  SVN_ERR_ASSERT(!resolved_tree);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* ### these two statements are not transacted together. is this a
     ### problem? I suspect a failure simply leaves the other in a
     ### continued, unresolved state. However, that still retains
     ### "integrity", so another re-run by the user will fix it.  */

  if (resolved_text)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_CLEAR_TEXT_CONFLICT));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  if (resolved_props)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_CLEAR_PROPS_CONFLICT));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* Some entries have cached the above values. Kapow!!  */
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


struct set_tc_baton
{
  const char *local_abspath;
  const char *local_relpath;
  apr_int64_t wc_id;
  const char *parent_relpath;
  const char *parent_abspath;
  const svn_wc_conflict_description2_t *tree_conflict;
};


/* */
static svn_error_t *
set_tc_txn2(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  struct set_tc_baton *stb = baton;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *tree_conflict_data;

  /* Get existing conflict information for LOCAL_ABSPATH. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", stb->wc_id, stb->local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (stb->tree_conflict)
    {
      svn_skel_t *skel;

      SVN_ERR(svn_wc__serialize_conflict(&skel, stb->tree_conflict,
                                         scratch_pool, scratch_pool));
      tree_conflict_data = svn_skel__unparse(skel, scratch_pool)->data;
    }
  else
    tree_conflict_data = NULL;

  if (have_row)
    {
      /* There is an existing ACTUAL row, so just update it. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_UPDATE_ACTUAL_TREE_CONFLICTS));
    }
  else
    {
      /* We need to insert an ACTUAL row with the tree conflict data. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_INSERT_ACTUAL_TREE_CONFLICTS));
    }

  SVN_ERR(svn_sqlite__bindf(stmt, "iss", stb->wc_id, stb->local_relpath,
                            tree_conflict_data));
  if (!have_row)
    SVN_ERR(svn_sqlite__bind_text(stmt, 4, stb->parent_relpath));

  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Now, remove the actual node if it doesn't have any more useful
     information.  We only need to do this if we've remove data ourselves. */
  if (!tree_conflict_data)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_DELETE_ACTUAL_EMPTY));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", stb->wc_id, stb->local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_set_tree_conflict(svn_wc__db_t *db,
                                const char *local_abspath,
                                const svn_wc_conflict_description2_t *tree_conflict,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  struct set_tc_baton stb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  stb.local_abspath = local_abspath;
  stb.parent_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
  stb.tree_conflict = tree_conflict;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &stb.local_relpath,
                              db, local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  stb.wc_id = wcroot->wc_id;

  /* ### does this work correctly? */
  stb.parent_relpath = svn_relpath_dirname(stb.local_relpath, scratch_pool);

  /* Should probably be in the same txn as above, but since we can't
     guarantee that wcroot->sdb is the same for both, and since
     the above implementation is going away, we'll fudge a bit here.

     ### Or can we guarantee wcroot->sdb is the same, given single db? */
  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb, set_tc_txn2, &stb,
                                       scratch_pool));

  /* There may be some entries, and the lock info is now out of date.  */
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_revert_actual(svn_wc__db_t *db,
                            const char *local_abspath,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows == 0)
    {
      /* Failed to delete the row.
         Presumably because there was a changelist set on it */

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                 STMT_CLEAR_ACTUAL_NODE_LEAVING_CHANGELIST));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
      /* We're not interested here if there was an affected row or not:
         If there isn't by now, then there simply was no row to begin with */
    }

  /* Some entries have cached the above values. Kapow!!  */
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
remove_children(svn_wc__db_wcroot_t *wcroot,
                const char *local_relpath,
                svn_wc__db_status_t status,
                apr_int64_t op_depth,
                apr_pool_t *scratch_pool);

struct op_revert_baton {
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
};

static svn_error_t *
op_revert_txn(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  struct op_revert_baton *b = baton;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_int64_t op_depth;
  svn_wc__db_status_t status;
  int affected_rows;

  SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                    STMT_DELETE_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", b->wcroot->wc_id,
                            b->local_relpath));
  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", b->wcroot->wc_id,
                            b->local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    {
      SVN_ERR(svn_sqlite__reset(stmt));
      if (affected_rows)
        return SVN_NO_ERROR; /* actual-only revert */

      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("The node '%s' was not found."),
                               path_for_error_message(b->wcroot,
                                                      b->local_relpath,
                                                      scratch_pool));
    }

  op_depth = svn_sqlite__column_int64(stmt, 0);
  status = svn_sqlite__column_token(stmt, 3, presence_map);
  SVN_ERR(svn_sqlite__reset(stmt));

  if (op_depth > 0)
    {
      if (op_depth != relpath_depth(b->local_relpath))
        return svn_error_createf(SVN_ERR_WC_INVALID_OPERATION_DEPTH, NULL,
                                 _("Can't revert '%s' without"
                                   " reverting parent"),
                                 path_for_error_message(b->wcroot,
                                                        b->local_relpath,
                                                        scratch_pool));

      SVN_ERR(convert_to_working_status(&status, status));

      if (status == svn_wc__db_status_added)
        {
          /* Check for children */
          SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                            STMT_SELECT_GE_OP_DEPTH_CHILDREN));
          SVN_ERR(svn_sqlite__bindf(stmt, "isi", b->wcroot->wc_id,
                                    b->local_relpath, op_depth));
          SVN_ERR(svn_sqlite__step(&have_row, stmt));
          SVN_ERR(svn_sqlite__reset(stmt));
          if (have_row)
            return svn_error_createf(SVN_ERR_WC_INVALID_OPERATION_DEPTH, NULL,
                                     _("Can't revert '%s' without"
                                       " reverting children"),
                                     path_for_error_message(b->wcroot,
                                                            b->local_relpath,
                                                            scratch_pool));
        }

      /* Rewrite the op-depth of all deleted children making the
         direct children into roots of deletes. */
          
      SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                     STMT_UPDATE_OP_DEPTH_INCREASE_RECURSIVE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isi", b->wcroot->wc_id,
                                construct_like_arg(b->local_relpath,
                                                   scratch_pool),
                                op_depth));
      SVN_ERR(svn_sqlite__step_done(stmt));

      SVN_ERR(svn_sqlite__get_statement(&stmt, b->wcroot->sdb,
                                        STMT_DELETE_WORKING_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", b->wcroot->wc_id,
                                b->local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_revert(svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *scratch_pool)
{
  struct op_revert_baton b;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&b.wcroot, &b.local_relpath,
                              db, local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(b.wcroot);

  SVN_ERR(svn_sqlite__with_transaction(b.wcroot->sdb, op_revert_txn,
                                       &b, scratch_pool));

  return SVN_NO_ERROR;
}


/* Set *TREE_CONFLICT_DATA to a string describing any tree conflicts on
 * immediate children of PDH:LOCAL_RELPATH. The format of the string is as
 * produced by svn_wc__write_tree_conflicts(). */
static svn_error_t *
read_all_tree_conflicts(apr_hash_t **tree_conflicts,
                        svn_wc__db_wcroot_t *wcroot,
                        const char *local_relpath,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  *tree_conflicts = apr_hash_make(result_pool);

  /* Get the conflict information for children of LOCAL_ABSPATH. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                               STMT_SELECT_ACTUAL_CHILDREN_TREE_CONFLICT));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *child_basename;
      const char *child_relpath;
      const char *conflict_data;
      const svn_skel_t *skel;
      const svn_wc_conflict_description2_t *conflict;

      svn_pool_clear(iterpool);

      child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      child_basename = svn_relpath_basename(child_relpath, result_pool);

      conflict_data = svn_sqlite__column_text(stmt, 1, NULL);
      skel = svn_skel__parse(conflict_data, strlen(conflict_data), iterpool);
      SVN_ERR(svn_wc__deserialize_conflict(&conflict, skel, wcroot->abspath,
                                           result_pool, iterpool));

      apr_hash_set(*tree_conflicts, child_basename, APR_HASH_KEY_STRING,
                   conflict);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_read_all_tree_conflicts(apr_hash_t **tree_conflicts,
                                      svn_wc__db_t *db,
                                      const char *local_abspath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(read_all_tree_conflicts(tree_conflicts, wcroot, local_relpath,
                                  result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Like svn_wc__db_op_read_tree_conflict(), but with WCROOT+LOCAL_RELPATH
 * instead of DB+LOCAL_ABSPATH. */
static svn_error_t *
read_tree_conflict(const svn_wc_conflict_description2_t **tree_conflict,
                   svn_wc__db_wcroot_t *wcroot,
                   const char *local_relpath,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *conflict_data;
  const svn_skel_t *skel;
  svn_error_t *err;

  *tree_conflict = NULL;

  if (!local_relpath[0])
    return SVN_NO_ERROR;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_TREE_CONFLICT));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    return SVN_NO_ERROR;

  conflict_data = svn_sqlite__column_text(stmt, 0, NULL);
  skel = svn_skel__parse(conflict_data, strlen(conflict_data), scratch_pool);
  err = svn_wc__deserialize_conflict(tree_conflict, skel,
                                     wcroot->abspath, result_pool,
                                     scratch_pool);

  return svn_error_compose_create(err,
                                  svn_sqlite__reset(stmt));
}

svn_error_t *
svn_wc__db_op_read_tree_conflict(
                     const svn_wc_conflict_description2_t **tree_conflict,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

  SVN_ERR(read_tree_conflict(tree_conflict, wcroot, local_relpath,
                             result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_op_remove_entry(svn_wc__db_t *db,
                                const char *local_abspath,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  svn_sqlite__stmt_t *stmt;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_DELETE_NODES));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_ACTUAL_NODE_WITHOUT_CONFLICT));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_op_remove_working(svn_wc__db_t *db,
                                  const char *local_abspath,
                                  apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  svn_sqlite__stmt_t *stmt;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}


static svn_error_t *
update_depth_values(svn_wc__db_t *db,
                    const char *local_abspath,
                    svn_wc__db_wcroot_t *wcroot,
                    const char *local_relpath,
                    svn_depth_t depth,
                    apr_pool_t *scratch_pool)
{
  svn_boolean_t excluded = (depth == svn_depth_exclude);
  svn_sqlite__stmt_t *stmt;

  /* Flush any entries before we start monkeying the database.  */
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    excluded
                                      ? STMT_UPDATE_NODE_BASE_EXCLUDED
                                      : STMT_UPDATE_NODE_BASE_DEPTH));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  if (!excluded)
    SVN_ERR(svn_sqlite__bind_text(stmt, 3, svn_depth_to_word(depth)));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    excluded
                                      ? STMT_UPDATE_NODE_WORKING_EXCLUDED
                                      : STMT_UPDATE_NODE_WORKING_DEPTH));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  if (!excluded)
    SVN_ERR(svn_sqlite__bind_text(stmt, 3, svn_depth_to_word(depth)));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_op_set_dir_depth(svn_wc__db_t *db,
                                 const char *local_abspath,
                                 svn_depth_t depth,
                                  apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(depth >= svn_depth_empty && depth <= svn_depth_infinity);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* ### We set depth on working and base to match entry behavior.
         Maybe these should be separated later? */

  SVN_ERR(update_depth_values(db, local_abspath, wcroot, local_relpath, depth,
                              scratch_pool));

  return SVN_NO_ERROR;
}


/* Delete child sub-trees of LOCAL_RELPATH that are presence=not-present
   and at the same op_depth.

   ### Do we need to handle incomplete here? */
static svn_error_t *
remove_children(svn_wc__db_wcroot_t *wcroot,
                const char *local_relpath,
                svn_wc__db_status_t status,
                apr_int64_t op_depth,
                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_CHILD_NODES_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isit", wcroot->wc_id,
                            construct_like_arg(local_relpath,
                                               scratch_pool),
                            op_depth, presence_map, status));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}

static svn_error_t *
db_working_actual_remove(svn_wc__db_wcroot_t *wcroot,
                         const char *local_relpath,
                         apr_pool_t *scratch_pool);

static svn_error_t *
info_below_working(svn_boolean_t *have_base,
                   svn_boolean_t *have_work,
                   svn_wc__db_status_t *status,
                   svn_wc__db_wcroot_t *wcroot,
                   const char *local_relpath,
                   apr_pool_t *scratch_pool);

/* Update the working node for LOCAL_ABSPATH setting presence=STATUS */
static svn_error_t *
db_working_update_presence(apr_int64_t op_depth,
                           svn_wc__db_status_t status,
                           svn_wc__db_wcroot_t *wcroot,
                           const char *local_relpath,
                           apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_UPDATE_NODE_WORKING_PRESENCE));
  SVN_ERR(svn_sqlite__bindf(stmt, "ist", wcroot->wc_id, local_relpath,
                            presence_map, status));
  SVN_ERR(svn_sqlite__step_done(stmt));

  if (status == svn_wc__db_status_base_deleted)
    {
      /* Switching to base-deleted is undoing an add/copy.  By this
         stage an add will have no children. */
      const apr_array_header_t *children;
      apr_pool_t *iterpool;
      int i;

      /* Children of the copy will be marked deleted in the layer
         above. */
      SVN_ERR(remove_children(wcroot, local_relpath,
                              svn_wc__db_status_base_deleted, op_depth + 1,
                              scratch_pool));

      /* Children of the copy that overlay a lower level become
         base_deleted, otherwise they get removed. */
      SVN_ERR(gather_repo_children(&children, wcroot, local_relpath,
                                   op_depth, scratch_pool, scratch_pool));
      iterpool = svn_pool_create(scratch_pool);
      for (i = 0; i < children->nelts; ++i)
        {
          const char *name = APR_ARRAY_IDX(children, i, const char *);
          const char *child_relpath;
          svn_boolean_t below_base, below_work;
          svn_wc__db_status_t below_status;

          svn_pool_clear(iterpool);

          child_relpath = svn_relpath_join(local_relpath, name, iterpool);
          SVN_ERR(info_below_working(&below_base, &below_work, &below_status,
                                     wcroot, child_relpath, iterpool));
          if ((below_base || below_work)
              && (below_status == svn_wc__db_status_normal
                  || below_status == svn_wc__db_status_added
                  || below_status == svn_wc__db_status_incomplete))
            SVN_ERR(db_working_update_presence(op_depth,
                                               svn_wc__db_status_base_deleted,
                                               wcroot, child_relpath,
                                               iterpool));
          else
            SVN_ERR(db_working_actual_remove(wcroot, child_relpath, iterpool));
        }
      svn_pool_destroy(iterpool);

      /* Reset the copyfrom in case this was a copy.
         ### What else should be reset? Properties? Or copy the node again? */
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_UPDATE_COPYFROM_TO_INHERIT));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* ### Should the switch to not-present remove an ACTUAL row? */

  return SVN_NO_ERROR;
}


/* Delete working and actual nodes for LOCAL_ABSPATH.  When called any
   remaining working child sub-trees should be presence=not-present
   and will be deleted. */
static svn_error_t *
db_working_actual_remove(svn_wc__db_wcroot_t *wcroot,
                         const char *local_relpath,
                         apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  apr_int64_t op_depth;

  /* Precondition: There is a working row in NODES.
   * Record its op_depth, which is needed for postcondition checking. */
  {
    svn_boolean_t have_row;

    SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                      STMT_SELECT_WORKING_NODE));
    SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
    SVN_ERR(svn_sqlite__step(&have_row, stmt));
    SVN_ERR_ASSERT(have_row);
    op_depth = svn_sqlite__column_int64(stmt, 0);
    SVN_ERR(svn_sqlite__reset(stmt));
  }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_CLEAR_ACTUAL_NODE_LEAVING_CONFLICT));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_ACTUAL_NODE_WITHOUT_CONFLICT));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(remove_children(wcroot, local_relpath,
                          svn_wc__db_status_base_deleted,
                          op_depth + 1, scratch_pool));
  SVN_ERR(remove_children(wcroot, local_relpath, svn_wc__db_status_normal,
                          op_depth, scratch_pool));
  SVN_ERR(remove_children(wcroot, local_relpath, svn_wc__db_status_not_present,
                          op_depth, scratch_pool));
  SVN_ERR(remove_children(wcroot, local_relpath, svn_wc__db_status_incomplete,
                          op_depth, scratch_pool));

  /* Postcondition: There are no NODES rows in this subtree, at same or
   * greater op_depth. */
  {
    svn_boolean_t have_row;

    SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                      STMT_SELECT_NODES_GE_OP_DEPTH_RECURSIVE));
    SVN_ERR(svn_sqlite__bindf(stmt, "issi", wcroot->wc_id, local_relpath,
                              construct_like_arg(local_relpath, scratch_pool),
                              op_depth));
    SVN_ERR(svn_sqlite__step(&have_row, stmt));
    SVN_ERR_ASSERT(! have_row);
    SVN_ERR(svn_sqlite__reset(stmt));
  }
  /* Postcondition: There are no ACTUAL_NODE rows in this subtree, save
     those with conflict information. */
  {
    svn_boolean_t have_row;

    SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                      STMT_SELECT_ACTUAL_NODE_RECURSIVE));
    SVN_ERR(svn_sqlite__bindf(stmt, "iss", wcroot->wc_id, local_relpath,
                              construct_like_arg(local_relpath, scratch_pool)));
    SVN_ERR(svn_sqlite__step(&have_row, stmt));
    SVN_ERR_ASSERT(! have_row);
    SVN_ERR(svn_sqlite__reset(stmt));
  }

  return SVN_NO_ERROR;
}




/* Insert a working node for LOCAL_ABSPATH with presence=STATUS. */
static svn_error_t *
db_working_insert(svn_wc__db_status_t status,
                  svn_wc__db_wcroot_t *wcroot,
                  const char *local_relpath,
                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  const char *like_arg = construct_like_arg(local_relpath, scratch_pool);
  apr_int64_t op_depth = relpath_depth(local_relpath);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_INSERT_WORKING_NODE_FROM_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isit", wcroot->wc_id,
                            local_relpath, op_depth, presence_map, status));
  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  /* Need to update the op_depth of all deleted child trees -- this
     relies on the recursion having already deleted the trees so
     that they are all at op_depth+1.

     ### Rewriting the op_depth means that the number of queries is
     ### O(depth^2).  Fix it by implementing svn_wc__db_op_delete so
     ### that the recursion gets moved from adm_ops.c to wc_db.c and
     ### one transaction does the whole tree and thus each op_depth
     ### only gets written once. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_UPDATE_OP_DEPTH_REDUCE_RECURSIVE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isi",
                            wcroot->wc_id, like_arg, op_depth + 1));
  SVN_ERR(svn_sqlite__update(NULL, stmt));

  return SVN_NO_ERROR;
}


/* Set *ROOT_OF_COPY to TRUE if LOCAL_ABSPATH is an add or the root of
   a copy, to FALSE otherwise. */
static svn_error_t*
is_add_or_root_of_copy(svn_boolean_t *add_or_root_of_copy,
                       svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  const char *op_root_relpath;
  const char *original_repos_relpath;
  apr_int64_t original_repos_id;
  svn_revnum_t original_revision;

  SVN_ERR(scan_addition(&status, &op_root_relpath, NULL, NULL,
                        &original_repos_relpath,
                        &original_repos_id, &original_revision,
                        wcroot, local_relpath, scratch_pool, scratch_pool));

  SVN_ERR_ASSERT(status == svn_wc__db_status_added
                 || status == svn_wc__db_status_copied);
  SVN_ERR_ASSERT(op_root_relpath != NULL);

  *add_or_root_of_copy = (status == svn_wc__db_status_added
                          || !strcmp(local_relpath, op_root_relpath));

  if (*add_or_root_of_copy && status == svn_wc__db_status_copied)
    {
      /* ### merge sets the wrong copyfrom when adding a tree and so
             the root detection above is unreliable.  I'm "fixing" it
             here because I just need to detect whether this is an
             instance of the merge bug, and that's easier than fixing
             scan_addition or merge. */
      const char *parent_relpath;
      const char *name;
      svn_wc__db_status_t parent_status;
      const char *parent_original_repos_relpath;
      apr_int64_t parent_original_repos_id;
      svn_revnum_t parent_original_revision;
      svn_error_t *err;

      svn_relpath_split(&parent_relpath, &name, local_relpath, scratch_pool);

      err = scan_addition(&parent_status, NULL, NULL, NULL,
                          &parent_original_repos_relpath,
                          &parent_original_repos_id, &parent_original_revision,
                          wcroot, parent_relpath, scratch_pool, scratch_pool);
      if (err)
        {
          if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
            return svn_error_return(err);
          /* It really is a root */
          svn_error_clear(err);
        }
      else if (parent_status == svn_wc__db_status_copied
               && original_revision == parent_original_revision
               && original_repos_id == parent_original_repos_id
               && !strcmp(original_repos_relpath,
                          svn_dirent_join(parent_original_repos_relpath,
                                          name,
                                          scratch_pool)))
        /* An instance of the merge bug */
        *add_or_root_of_copy = FALSE;
    }

  return SVN_NO_ERROR;
}

/* Convert STATUS, the raw status obtained from the presence map, to
   the status appropriate for a working (op_depth > 0) node and return
   it in *WORKING_STATUS. */
static svn_error_t *
convert_to_working_status(svn_wc__db_status_t *working_status,
                          svn_wc__db_status_t status)
{
  svn_wc__db_status_t work_status = status;

  SVN_ERR_ASSERT(work_status == svn_wc__db_status_normal
                 || work_status == svn_wc__db_status_not_present
                 || work_status == svn_wc__db_status_base_deleted
                 || work_status == svn_wc__db_status_incomplete
                 || work_status == svn_wc__db_status_excluded);

  if (work_status == svn_wc__db_status_incomplete)
    {
      *working_status = svn_wc__db_status_incomplete;
    }
  else if (work_status == svn_wc__db_status_excluded)
    {
      *working_status = svn_wc__db_status_excluded;
    }
  else if (work_status == svn_wc__db_status_not_present
           || work_status == svn_wc__db_status_base_deleted)
    {
      /* The caller should scan upwards to detect whether this
         deletion has occurred because this node has been moved
         away, or it is a regular deletion. Also note that the
         deletion could be of the BASE tree, or a child of
         something that has been copied/moved here. */

      *working_status = svn_wc__db_status_deleted;
    }
  else /* normal */
    {
      /* The caller should scan upwards to detect whether this
         addition has occurred because of a simple addition,
         a copy, or is the destination of a move. */
      *working_status = svn_wc__db_status_added;
    }

  return SVN_NO_ERROR;
}

/* Return the status of the node, if any, below the "working" node.
   Set *HAVE_BASE or *HAVE_WORK to indicate if a base node or lower
   working node is present, and *STATUS to the status of the node.

   This is an experimental interface.  It appears that delete only
   needs to know whether the below node is base or not (if it is a
   base the status is available via base_get_info).  It's possible
   this function should be removed and read_info modified to return
   the "lower is base".  I'll leave it for now because delete may turn
   out to need more info. */
static svn_error_t *
info_below_working(svn_boolean_t *have_base,
                   svn_boolean_t *have_work,
                   svn_wc__db_status_t *status,
                   svn_wc__db_wcroot_t *wcroot,
                   const char *local_relpath,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  *have_base = *have_work =  FALSE;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (have_row)
        {
          apr_int64_t op_depth = svn_sqlite__column_int64(stmt, 0);

          if (op_depth > 0)
            *have_work = TRUE;
          else
            *have_base = TRUE;

          *status = svn_sqlite__column_token(stmt, 3, presence_map);
        }
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  if (*have_work)
    SVN_ERR(convert_to_working_status(status, *status));

  return SVN_NO_ERROR;
}

struct temp_op_delete_baton {
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  /* The following two are only needed for svn_wc__db_temp_forget_directory */
  svn_wc__db_t *db;
  const char *local_abspath;
};

/* Deletes BATON->LOCAL_RELPATH using BATON->PDH. */
static svn_error_t *
temp_op_delete_txn(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  struct temp_op_delete_baton *b = baton;
  svn_wc__db_status_t status, new_working_status;
  svn_boolean_t have_work, add_work = FALSE, del_work = FALSE, mod_work = FALSE;

  SVN_ERR(read_info(&status,
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    &have_work,
                    NULL, NULL,
                    b->wcroot, b->local_relpath,
                    scratch_pool, scratch_pool));

  if (!have_work)
    {
      /* No structural changes  */
      if (status == svn_wc__db_status_normal
          || status == svn_wc__db_status_incomplete)
        {
          add_work = TRUE;
        }
    }
  else if (status == svn_wc__db_status_added)
    {
      /* ADD/COPY-HERE/MOVE-HERE that could be a replace */
      svn_boolean_t add_or_root_of_copy;

      SVN_ERR(is_add_or_root_of_copy(&add_or_root_of_copy, b->wcroot,
                                     b->local_relpath, scratch_pool));
      if (add_or_root_of_copy)
        {
          svn_boolean_t below_base, below_work;
          svn_wc__db_status_t below_status;

          SVN_ERR(info_below_working(&below_base, &below_work, &below_status,
                                     b->wcroot, b->local_relpath,
                                     scratch_pool));

          if ((below_base || below_work)
              && below_status != svn_wc__db_status_not_present)
            mod_work = TRUE;
          else
            del_work = TRUE;
        }
      else
        {
          add_work = TRUE;
        }
    }
  else if (status == svn_wc__db_status_incomplete)
    {
      svn_boolean_t add_or_root_of_copy;
      SVN_ERR(is_add_or_root_of_copy(&add_or_root_of_copy, b->wcroot,
                                     b->local_relpath, scratch_pool));
      if (add_or_root_of_copy)
        del_work = TRUE;
      else
        add_work = TRUE;
    }

  if (del_work)
    {
      SVN_ERR(db_working_actual_remove(b->wcroot, b->local_relpath,
                                       scratch_pool));

      /* This is needed for access batons? */
      SVN_ERR(svn_wc__db_temp_forget_directory(b->db, b->local_abspath,
                                               scratch_pool));
    }
  else if (add_work)
    {
      new_working_status = svn_wc__db_status_base_deleted;
      SVN_ERR(db_working_insert(new_working_status, b->wcroot,
                                b->local_relpath, scratch_pool));
    }
  else if (mod_work)
    {
      new_working_status = svn_wc__db_status_base_deleted;
      SVN_ERR(db_working_update_presence(relpath_depth(b->local_relpath),
                                         new_working_status, b->wcroot,
                                         b->local_relpath, scratch_pool));
    }
  else
    {
      /* Already deleted, or absent or excluded. */
      /* ### Nothing to do, return an error?  Which one? */
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_op_delete(svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *scratch_pool)
{
  struct temp_op_delete_baton b;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&b.wcroot, &b.local_relpath,
                              db, local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(b.wcroot);

  /* These two for svn_wc__db_temp_forget_directory */
  b.db = db;
  b.local_abspath = local_abspath;

  SVN_ERR(svn_sqlite__with_transaction(b.wcroot->sdb, temp_op_delete_txn,
                                       &b, scratch_pool));

  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

/* Like svn_wc__db_read_info(), but taking PDH+LOCAL_RELPATH instead of
 * DB+LOCAL_ABSPATH, and outputting repos ids instead of URL+UUID. */
static svn_error_t *
read_info(svn_wc__db_status_t *status,
          svn_wc__db_kind_t *kind,
          svn_revnum_t *revision,
          const char **repos_relpath,
          apr_int64_t *repos_id,
          svn_revnum_t *changed_rev,
          apr_time_t *changed_date,
          const char **changed_author,
          apr_time_t *last_mod_time,
          svn_depth_t *depth,
          const svn_checksum_t **checksum,
          svn_filesize_t *translated_size,
          const char **target,
          const char **changelist,
          const char **original_repos_relpath,
          apr_int64_t *original_repos_id,
          svn_revnum_t *original_revision,
          svn_boolean_t *props_mod,
          svn_boolean_t *have_base,
          svn_boolean_t *have_work,
          svn_boolean_t *conflicted,
          svn_wc__db_lock_t **lock,
          svn_wc__db_wcroot_t *wcroot,
          const char *local_relpath,
          apr_pool_t *result_pool,
          apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt_info;
  svn_sqlite__stmt_t *stmt_act;
  svn_boolean_t have_info;
  svn_boolean_t have_act;
  svn_error_t *err = NULL;

  /* Obtain the most likely to exist record first, to make sure we don't
     have to obtain the SQLite read-lock multiple times */
  SVN_ERR(svn_sqlite__get_statement(&stmt_info, wcroot->sdb,
                                    lock ? STMT_SELECT_NODE_INFO_WITH_LOCK
                                         : STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt_info, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_info, stmt_info));

  if (changelist || conflicted || props_mod)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt_act, wcroot->sdb,
                                        STMT_SELECT_ACTUAL_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt_act, "is", wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step(&have_act, stmt_act));
    }
  else
    {
      have_act = FALSE;
      stmt_act = NULL;
    }

  if (have_info)
    {
      apr_int64_t op_depth;
      svn_wc__db_kind_t node_kind;

      op_depth = svn_sqlite__column_int64(stmt_info, 0);
      node_kind = svn_sqlite__column_token(stmt_info, 4, kind_map);

      if (status)
        {
          *status = svn_sqlite__column_token(stmt_info, 3, presence_map);

          if (op_depth != 0) /* WORKING */
            err = svn_error_compose_create(err,
                                           convert_to_working_status(status,
                                                                     *status));
        }
      if (kind)
        {
          *kind = node_kind;
        }
      if (op_depth != 0)
        {
          if (repos_id)
            *repos_id = INVALID_REPOS_ID;
          if (revision)
            *revision = SVN_INVALID_REVNUM;
          if (repos_relpath)
            /* Our path is implied by our parent somewhere up the tree.
               With the NULL value and status, the caller will know to
               search up the tree for the base of our path.  */
            *repos_relpath = NULL;
        }
      else
        {
          /* Fetch repository information. If we have a
             WORKING_NODE (and have been added), then the repository
             we're being added to will be dependent upon a parent. The
             caller can scan upwards to locate the repository.  */
          err = svn_error_compose_create(
            err, repos_location_from_columns(repos_id, revision, repos_relpath,
                                             stmt_info, 1, 5, 2, result_pool));
        }
      if (changed_rev)
        {
          *changed_rev = svn_sqlite__column_revnum(stmt_info, 8);
        }
      if (changed_date)
        {
          *changed_date = svn_sqlite__column_int64(stmt_info, 9);
        }
      if (changed_author)
        {
          *changed_author = svn_sqlite__column_text(stmt_info, 10,
                                                    result_pool);
        }
      if (last_mod_time)
        {
          *last_mod_time = svn_sqlite__column_int64(stmt_info, 13);
        }
      if (depth)
        {
          if (node_kind != svn_wc__db_kind_dir)
            {
              *depth = svn_depth_unknown;
            }
          else
            {
              const char *depth_str;

              depth_str = svn_sqlite__column_text(stmt_info, 11, NULL);

              if (depth_str == NULL)
                *depth = svn_depth_unknown;
              else
                *depth = svn_depth_from_word(depth_str);
            }
        }
      if (checksum)
        {
          if (node_kind != svn_wc__db_kind_file)
            {
              *checksum = NULL;
            }
          else
            {
              svn_error_t *err2;
              err2 = svn_sqlite__column_checksum(checksum, stmt_info, 6,
                                                 result_pool);

              if (err2 != NULL)
                err = svn_error_compose_create(
                         err,
                         svn_error_createf(
                               err->apr_err, err2,
                              _("The node '%s' has a corrupt checksum value."),
                              path_for_error_message(wcroot, local_relpath,
                                                     scratch_pool)));
            }
        }
      if (translated_size)
        {
          *translated_size = get_translated_size(stmt_info, 7);
        }
      if (target)
        {
          if (node_kind != svn_wc__db_kind_symlink)
            *target = NULL;
          else
            *target = svn_sqlite__column_text(stmt_info, 12, result_pool);
        }
      if (changelist)
        {
          if (have_act)
            *changelist = svn_sqlite__column_text(stmt_act, 1, result_pool);
          else
            *changelist = NULL;
        }
      if (op_depth == 0)
        {
          if (original_repos_id)
            *original_repos_id = INVALID_REPOS_ID;
          if (original_revision)
            *original_revision = SVN_INVALID_REVNUM;
          if (original_repos_relpath)
            *original_repos_relpath = NULL;
        }
      else
        {
          err = svn_error_compose_create(
            err, repos_location_from_columns(original_repos_id, original_revision,
                                             original_repos_relpath,
                                             stmt_info, 1, 5, 2, result_pool));
        }
      if (props_mod)
        {
          *props_mod = have_act && !svn_sqlite__column_is_null(stmt_act, 6);
        }
      if (conflicted)
        {
          if (have_act)
            {
              *conflicted =
                 !svn_sqlite__column_is_null(stmt_act, 2) || /* old */
                 !svn_sqlite__column_is_null(stmt_act, 3) || /* new */
                 !svn_sqlite__column_is_null(stmt_act, 4) || /* working */
                 !svn_sqlite__column_is_null(stmt_act, 0) || /* prop_reject */
                 !svn_sqlite__column_is_null(stmt_act, 5); /* tree_conflict_data */
            }
          else
            *conflicted = FALSE;
        }

      if (lock)
        {
          if (op_depth != 0)
            *lock = NULL;
          else
            *lock = lock_from_columns(stmt_info, 15, 16, 17, 18, result_pool);
        }

      if (have_work)
        *have_work = (op_depth != 0);

      if (have_base)
        {
          while (!err && op_depth != 0)
            {
              err = svn_sqlite__step(&have_info, stmt_info);

              if (err || !have_info)
                break;

              op_depth = svn_sqlite__column_int64(stmt_info, 0);
            }

          *have_base = (op_depth == 0);
        }
    }
  else if (have_act)
    {
      /* A row in ACTUAL_NODE should never exist without a corresponding
         node in BASE_NODE and/or WORKING_NODE unless it flags a conflict. */
      if (svn_sqlite__column_is_null(stmt_act, 5)) /* conflict_data */
          err = svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                  _("Corrupt data for '%s'"),
                                  path_for_error_message(wcroot, local_relpath,
                                                         scratch_pool));
      /* ### What should we return?  Should we have a separate
             function for reading actual-only nodes? */

      /* As a safety measure, until we decide if we want to use
         read_info for actual-only nodes, make sure the caller asked
         for the conflict status. */
      SVN_ERR_ASSERT(conflicted);

      if (status)
        *status = svn_wc__db_status_normal;  /* What! No it's not! */
      if (kind)
        *kind = svn_wc__db_kind_unknown;
      if (revision)
        *revision = SVN_INVALID_REVNUM;
      if (repos_relpath)
        *repos_relpath = NULL;
      if (repos_id)
        *repos_id = INVALID_REPOS_ID;
      if (changed_rev)
        *changed_rev = SVN_INVALID_REVNUM;
      if (changed_date)
        *changed_date = 0;
      if (last_mod_time)
        *last_mod_time = 0;
      if (depth)
        *depth = svn_depth_unknown;
      if (checksum)
        *checksum = NULL;
      if (translated_size)
        *translated_size = 0;
      if (target)
        *target = NULL;
      if (changelist)
        *changelist = svn_sqlite__column_text(stmt_act, 1, result_pool);
      if (original_repos_relpath)
        *original_repos_relpath = NULL;
      if (original_repos_id)
        *original_repos_id = INVALID_REPOS_ID;
      if (original_revision)
        *original_revision = SVN_INVALID_REVNUM;
      if (props_mod)
        *props_mod = !svn_sqlite__column_is_null(stmt_act, 6);
      if (have_base)
        *have_base = FALSE;
      if (have_work)
        *have_work = FALSE;
      if (conflicted)
        *conflicted = TRUE;
      if (lock)
        *lock = NULL;
    }
  else
    {
      err = svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("The node '%s' was not found."),
                              path_for_error_message(wcroot, local_relpath,
                                                     scratch_pool));
    }

  if (stmt_act != NULL)
    err = svn_error_compose_create(err, svn_sqlite__reset(stmt_act));

  SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt_info)));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_info(svn_wc__db_status_t *status,
                     svn_wc__db_kind_t *kind,
                     svn_revnum_t *revision,
                     const char **repos_relpath,
                     const char **repos_root_url,
                     const char **repos_uuid,
                     svn_revnum_t *changed_rev,
                     apr_time_t *changed_date,
                     const char **changed_author,
                     apr_time_t *last_mod_time,
                     svn_depth_t *depth,
                     const svn_checksum_t **checksum,
                     svn_filesize_t *translated_size,
                     const char **target,
                     const char **changelist,
                     const char **original_repos_relpath,
                     const char **original_root_url,
                     const char **original_uuid,
                     svn_revnum_t *original_revision,
                     svn_boolean_t *props_mod,
                     svn_boolean_t *have_base,
                     svn_boolean_t *have_work,
                     svn_boolean_t *conflicted,
                     svn_wc__db_lock_t **lock,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  apr_int64_t repos_id, original_repos_id;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(read_info(status, kind, revision, repos_relpath, &repos_id,
                    changed_rev, changed_date, changed_author,
                    last_mod_time, depth, checksum, translated_size, target,
                    changelist, original_repos_relpath, &original_repos_id,
                    original_revision, props_mod, have_base,
                    have_work, conflicted, lock,
                    wcroot, local_relpath, result_pool, scratch_pool));
  SVN_ERR(fetch_repos_info(repos_root_url, repos_uuid,
                           wcroot->sdb, repos_id, result_pool));
  SVN_ERR(fetch_repos_info(original_root_url, original_uuid,
                           wcroot->sdb, original_repos_id, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_children_info(apr_hash_t **nodes,
                              apr_hash_t **conflicts,
                              svn_wc__db_t *db,
                              const char *dir_abspath,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *dir_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *repos_root_url = NULL;
  apr_int64_t last_repos_id;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(dir_abspath));
  *conflicts = apr_hash_make(result_pool);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &dir_relpath, db,
                                             dir_abspath,
                                             svn_sqlite__mode_readonly,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_CHILDREN_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, dir_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  *nodes = apr_hash_make(result_pool);
  while (have_row)
    {
      struct svn_wc__db_info_t *child;
      const char *child_relpath = svn_sqlite__column_text(stmt, 19, NULL);
      const char *name = svn_relpath_basename(child_relpath, NULL);
      svn_error_t *err;
      apr_int64_t *op_depth, row_op_depth;
      svn_boolean_t new_child;

      child = apr_hash_get(*nodes, name, APR_HASH_KEY_STRING);
      if (child)
        new_child = FALSE;
      else
        {
          child = apr_palloc(result_pool, sizeof(*child) + sizeof(*op_depth));
          new_child = TRUE;
        }

      op_depth = (apr_int64_t *)(child + 1);
      row_op_depth = svn_sqlite__column_int(stmt, 0);

      if (new_child || *op_depth < row_op_depth)
        {
          apr_hash_t *properties;

          *op_depth = row_op_depth;

          child->kind = svn_sqlite__column_token(stmt, 4, kind_map);

          child->status = svn_sqlite__column_token(stmt, 3, presence_map);
          if (*op_depth != 0)
            {
              err = convert_to_working_status(&child->status, child->status);
              if (err)
                SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
            }

          if (*op_depth != 0)
            child->revnum = SVN_INVALID_REVNUM;
          else
            child->revnum = svn_sqlite__column_revnum(stmt, 5);


          if (*op_depth != 0)
            child->repos_relpath = NULL;
          else
            child->repos_relpath = svn_sqlite__column_text(stmt, 2,
                                                           result_pool);

          if (*op_depth != 0 || svn_sqlite__column_is_null(stmt, 1))
            {
              child->repos_root_url = NULL;
            }
          else
            {
              apr_int64_t repos_id = svn_sqlite__column_int64(stmt, 1);
              if (!repos_root_url)
                {
                  err = fetch_repos_info(&repos_root_url, NULL,
                                         wcroot->sdb, repos_id, result_pool);
                  if (err)
                    SVN_ERR(svn_error_compose_create(err,
                                                     svn_sqlite__reset(stmt)));
                  last_repos_id = repos_id;
                }

              /* Assume working copy is all one repos_id so that a
                 single cached value is sufficient. */
              SVN_ERR_ASSERT(repos_id == last_repos_id);
              child->repos_root_url = repos_root_url;
            }

          child->changed_rev = svn_sqlite__column_revnum(stmt, 8);

          child->changed_date = svn_sqlite__column_int64(stmt, 9);

          child->changed_author = svn_sqlite__column_text(stmt, 10,
                                                          result_pool);

          child->last_mod_time = svn_sqlite__column_int64(stmt, 13);

          if (child->kind != svn_wc__db_kind_dir)
            child->depth = svn_depth_unknown;
          else
            {
              const char *depth = svn_sqlite__column_text(stmt, 11,
                                                          scratch_pool);
              if (depth)
                child->depth = svn_depth_from_word(depth);
              else
                child->depth = svn_depth_unknown;
            }

          child->translated_size = get_translated_size(stmt, 7);

          child->lock = lock_from_columns(stmt, 15, 16, 17, 18, result_pool);

          err = svn_sqlite__column_properties(&properties, stmt, 14,
                                              scratch_pool, scratch_pool);
          if (err)
            SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
          child->has_props = properties && !!apr_hash_count(properties);
#ifdef HAVE_SYMLINK
          child->special = (child->has_props
                            && apr_hash_get(properties, SVN_PROP_SPECIAL,
                                            APR_HASH_KEY_STRING));
#endif

          child->changelist = NULL;
          child->have_base = (*op_depth == 0);
          child->props_mod = FALSE;
          child->conflicted = FALSE;

          apr_hash_set(*nodes, apr_pstrdup(result_pool, name),
                       APR_HASH_KEY_STRING, child);
        }
      else if (row_op_depth == 0)
        {
          child->have_base = TRUE;
        }

      err = svn_sqlite__step(&have_row, stmt);
      if (err)
        SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_CHILDREN_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, dir_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  while (have_row)
    {
      struct svn_wc__db_info_t *child;
      const char *child_relpath = svn_sqlite__column_text(stmt, 7, NULL);
      const char *name = svn_relpath_basename(child_relpath, NULL);
      svn_error_t *err;

      child = apr_hash_get(*nodes, name, APR_HASH_KEY_STRING);
      if (!child)
        {
          child = apr_palloc(result_pool, sizeof(*child) + sizeof(int));
          child->status = svn_wc__db_status_not_present;
        }

      child->changelist = svn_sqlite__column_text(stmt, 1, result_pool);

      child->props_mod = !svn_sqlite__column_is_null(stmt, 6);
      if (child->props_mod)
        {
          apr_hash_t *properties;

          err = svn_sqlite__column_properties(&properties, stmt, 6,
                                              scratch_pool, scratch_pool);
          if (err)
            SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
          child->has_props = properties && !!apr_hash_count(properties);
#ifdef HAVE_SYMLINK
          child->special = (child->has_props
                            && apr_hash_get(properties, SVN_PROP_SPECIAL,
                                            APR_HASH_KEY_STRING));
#endif
        }


      child->conflicted = !svn_sqlite__column_is_null(stmt, 2) ||  /* old */
                          !svn_sqlite__column_is_null(stmt, 3) ||  /* new */
                          !svn_sqlite__column_is_null(stmt, 4) ||  /* work */
                          !svn_sqlite__column_is_null(stmt, 0) ||  /* prop */
                          !svn_sqlite__column_is_null(stmt, 5);  /* tree */

      if (child->conflicted)
        apr_hash_set(*conflicts, apr_pstrdup(result_pool, name),
                     APR_HASH_KEY_STRING, "");

      err = svn_sqlite__step(&have_row, stmt);
      if (err)
        SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_children_walker_info(apr_hash_t **nodes,
                                     svn_wc__db_t *db,
                                     const char *dir_abspath,
                                     apr_pool_t *result_pool,
                                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *dir_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_int64_t op_depth;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(dir_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &dir_relpath, db,
                                             dir_abspath,
                                             svn_sqlite__mode_readonly,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_CHILDREN_WALKER_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, dir_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  *nodes = apr_hash_make(result_pool);
  while (have_row)
    {
      struct svn_wc__db_walker_info_t *child;
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      const char *name = svn_relpath_basename(child_relpath, NULL);
      svn_error_t *err;

      child = apr_hash_get(*nodes, name, APR_HASH_KEY_STRING);
      if (child == NULL)
        child = apr_palloc(result_pool, sizeof(*child));

      op_depth = svn_sqlite__column_int(stmt, 1);
      child->status = svn_sqlite__column_token(stmt, 2, presence_map);
      if (op_depth > 0)
        {
          err = convert_to_working_status(&child->status, child->status);
          if (err)
            SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
        }
      child->kind = svn_sqlite__column_token(stmt, 3, kind_map);
      apr_hash_set(*nodes, apr_pstrdup(result_pool, name),
                   APR_HASH_KEY_STRING, child);

      err = svn_sqlite__step(&have_row, stmt);
      if (err)
        SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_prop(const svn_string_t **propval,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     const char *propname,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  apr_hash_t *props;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(propname != NULL);

  /* Note: maybe one day, we'll have internal caches of this stuff, but
     for now, we just grab all the props and pick out the requested prop. */

  /* ### should: fetch into scratch_pool, then dup into result_pool.  */
  SVN_ERR(svn_wc__db_read_props(&props, db, local_abspath,
                                result_pool, scratch_pool));

  *propval = apr_hash_get(props, propname, APR_HASH_KEY_STRING);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_read_props(apr_hash_t **props,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err = NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_PROPS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row && !svn_sqlite__column_is_null(stmt, 0))
    {
      err = svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                          scratch_pool);
    }
  else
    have_row = FALSE;

  SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));

  if (have_row)
    return SVN_NO_ERROR;

  /* No local changes. Return the pristine props for this node.  */
  SVN_ERR(db_read_pristine_props(props, wcroot, local_relpath,
                                 result_pool, scratch_pool));
  if (*props == NULL)
    {
      /* Pristine properties are not defined for this node.
         ### we need to determine whether this node is in a state that
         ### allows for ACTUAL properties (ie. not deleted). for now,
         ### just say all nodes, no matter the state, have at least an
         ### empty set of props.  */
      *props = apr_hash_make(result_pool);
    }

  return SVN_NO_ERROR;
}

/* Parse a node's PROP_DATA (which is PROP_DATA_LEN bytes long)
 * into a hash table keyed by property names and containing property values.
 *
 * If parsing succeeds, and the set of properties is not empty,
 * add the hash table to PROPS_PER_CHILD, keyed by the absolute path
 * of the node CHILD_RELPATH within the working copy at WCROOT_ABSPATH.
 *
 * If the set of properties is empty, and PROPS_PER_CHILD already contains
 * an entry for the node, clear the entry. This facilitates overriding
 * properties retrieved from the NODES table with empty sets of properties
 * stored in the ACTUAL_NODE table. */
static svn_error_t *
maybe_add_child_props(apr_hash_t *props_per_child,
                      const char *prop_data,
                      apr_size_t prop_data_len,
                      const char *child_relpath,
                      const char *wcroot_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  const char *child_abspath;
  apr_hash_t *props;
  svn_skel_t *prop_skel;

  prop_skel = svn_skel__parse(prop_data, prop_data_len, scratch_pool);
  if (svn_skel__list_length(prop_skel) == 0)
    return SVN_NO_ERROR;

  child_abspath = svn_dirent_join(wcroot_abspath, child_relpath, result_pool);
  SVN_ERR(svn_skel__parse_proplist(&props, prop_skel, result_pool));
  if (apr_hash_count(props))
    apr_hash_set(props_per_child, child_abspath, APR_HASH_KEY_STRING, props);
  else
    apr_hash_set(props_per_child, child_abspath, APR_HASH_KEY_STRING, NULL);

  return SVN_NO_ERROR;
}

/* Call RECEIVER_FUNC, passing RECEIVER_BATON, an absolute path, and
 * a hash table mapping <tt>char *</tt> names onto svn_string_t *
 * values for any properties of immediate child nodes of LOCAL_ABSPATH.
 * If FILES_ONLY is true, only report properties for file child nodes.
 */
static svn_error_t *
read_props_of_children(svn_wc__db_t *db,
                       const char *local_abspath,
                       svn_boolean_t files_only,
                       svn_wc__proplist_receiver_t receiver_func,
                       void *receiver_baton,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *prev_child_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_hash_t *props_per_child;
  apr_hash_t *files;
  apr_hash_t *not_present;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(receiver_func);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                             local_abspath,
                                             svn_sqlite__mode_readwrite,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  props_per_child = apr_hash_make(scratch_pool);
  not_present = apr_hash_make(scratch_pool);
  if (files_only)
    files = apr_hash_make(scratch_pool);
  else
    files = NULL;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_NODE_PROPS_OF_CHILDREN));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  prev_child_relpath = NULL;
  while (have_row)
    {
      svn_wc__db_status_t child_presence;
      const char *child_relpath;
      const char *prop_data;
      apr_size_t len;

      child_relpath = svn_sqlite__column_text(stmt, 2, scratch_pool);

      if (prev_child_relpath && strcmp(child_relpath, prev_child_relpath) == 0)
        {
          /* Same child, but lower op_depth -- skip this row. */
          SVN_ERR(svn_sqlite__step(&have_row, stmt));
          continue;
        }
      prev_child_relpath = child_relpath;

      child_presence = svn_sqlite__column_token(stmt, 1, presence_map);
      if (child_presence != svn_wc__db_status_normal)
        {
          apr_hash_set(not_present, child_relpath, APR_HASH_KEY_STRING, "");
          SVN_ERR(svn_sqlite__step(&have_row, stmt));
          continue;
        }

      prop_data = svn_sqlite__column_blob(stmt, 0, &len, NULL);
      if (prop_data)
        {
          if (files_only)
            {
              svn_wc__db_kind_t child_kind;

              child_kind = svn_sqlite__column_token(stmt, 3, kind_map);
              if (child_kind != svn_wc__db_kind_file &&
                  child_kind != svn_wc__db_kind_symlink)
                {
                  SVN_ERR(svn_sqlite__step(&have_row, stmt));
                  continue;
                }
              apr_hash_set(files, child_relpath, APR_HASH_KEY_STRING, NULL);
            }

          SVN_ERR(maybe_add_child_props(props_per_child, prop_data, len,
                                        child_relpath, wcroot->abspath,
                                        scratch_pool, scratch_pool));
        }

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_PROPS_OF_CHILDREN));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *child_relpath;
      const char *prop_data;
      apr_size_t len;

      prop_data = svn_sqlite__column_blob(stmt, 0, &len, NULL);
      if (prop_data)
        {
          child_relpath = svn_sqlite__column_text(stmt, 1, scratch_pool);

          if (apr_hash_get(not_present, child_relpath, APR_HASH_KEY_STRING) ||
              (files_only &&
               apr_hash_get(files, child_relpath, APR_HASH_KEY_STRING) == NULL))
            {
                SVN_ERR(svn_sqlite__step(&have_row, stmt));
                continue;
            }
          SVN_ERR(maybe_add_child_props(props_per_child, prop_data, len,
                                        child_relpath, wcroot->abspath,
                                        scratch_pool, scratch_pool));
        }

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  iterpool = svn_pool_create(scratch_pool);
  for (hi = apr_hash_first(scratch_pool, props_per_child);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *child_abspath = svn__apr_hash_index_key(hi);
      apr_hash_t *child_props = svn__apr_hash_index_val(hi);

      svn_pool_clear(iterpool);

      if (child_props)
        SVN_ERR((*receiver_func)(receiver_baton, child_abspath, child_props,
                                 iterpool));
    }
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_props_of_files(svn_wc__db_t *db,
                               const char *local_abspath,
                               svn_wc__proplist_receiver_t receiver_func,
                               void *receiver_baton,
                               apr_pool_t *scratch_pool)
{
  return svn_error_return(read_props_of_children(db, local_abspath, TRUE,
                                                 receiver_func, receiver_baton,
                                                 scratch_pool));
}

svn_error_t *
svn_wc__db_read_props_of_immediates(svn_wc__db_t *db,
                                    const char *local_abspath,
                                    svn_wc__proplist_receiver_t receiver_func,
                                    void *receiver_baton,
                                    apr_pool_t *scratch_pool)
{
  return svn_error_return(read_props_of_children(db, local_abspath, FALSE,
                                                 receiver_func, receiver_baton,
                                                 scratch_pool));
}


static svn_error_t *
db_read_pristine_props(apr_hash_t **props,
                       svn_wc__db_wcroot_t *wcroot,
                       const char *local_relpath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_wc__db_status_t presence;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_SELECT_NODE_PROPS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    {
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                               svn_sqlite__reset(stmt),
                               _("The node '%s' was not found."),
                               path_for_error_message(wcroot,
                                                      local_relpath,
                                                      scratch_pool));
    }


  /* Examine the presence: */
  presence = svn_sqlite__column_token(stmt, 1, presence_map);

  /* For "base-deleted", it is obvious the pristine props are located
     in the BASE table. Fall through to fetch them.
     ### BH: Is this really the behavior we want here? */
  if (presence == svn_wc__db_status_base_deleted)
    {
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      SVN_ERR_ASSERT(have_row);

      presence = svn_sqlite__column_token(stmt, 1, presence_map);
    }

  /* normal or copied: Fetch properties (during update we want
     properties for incomplete as well) */
  if (presence == svn_wc__db_status_normal
      || presence == svn_wc__db_status_incomplete)
    {
      svn_error_t *err;

      err = svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                          scratch_pool);
      SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));

      if (!*props)
        *props = apr_hash_make(result_pool);

      return SVN_NO_ERROR;
    }

  *props = NULL;
  return svn_error_return(svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_read_pristine_props(apr_hash_t **props,
                               svn_wc__db_t *db,
                               const char *local_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(db_read_pristine_props(props, wcroot, local_relpath,
                                 result_pool, scratch_pool));
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_read_children(const apr_array_header_t **children,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                             local_abspath,
                                             svn_sqlite__mode_readonly,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  return gather_children(children, wcroot, local_relpath,
                         result_pool, scratch_pool);
}

struct relocate_baton
{
  apr_int64_t wc_id;
  const char *local_relpath;
  const char *repos_relpath;
  const char *repos_root_url;
  const char *repos_uuid;
  svn_boolean_t have_base_node;
  apr_int64_t old_repos_id;
};


/* */
static svn_error_t *
relocate_txn(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  struct relocate_baton *rb = baton;
  const char *like_arg;
  svn_sqlite__stmt_t *stmt;
  apr_int64_t new_repos_id;

  /* This function affects all the children of the given local_relpath,
     but the way that it does this is through the repos inheritance mechanism.
     So, we only need to rewrite the repos_id of the given local_relpath,
     as well as any children with a non-null repos_id, as well as various
     repos_id fields in the locks and working_node tables.
   */

  /* Get the repos_id for the new repository. */
  SVN_ERR(create_repos_id(&new_repos_id, rb->repos_root_url, rb->repos_uuid,
                          sdb, scratch_pool));

  like_arg = construct_like_arg(rb->local_relpath, scratch_pool);

  /* Set the (base and working) repos_ids and clear the dav_caches */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_RECURSIVE_UPDATE_NODE_REPO));
  SVN_ERR(svn_sqlite__bindf(stmt, "issii",
                            rb->wc_id, rb->local_relpath,
                            like_arg, rb->old_repos_id,
                            new_repos_id));
  SVN_ERR(svn_sqlite__step_done(stmt));

  if (rb->have_base_node)
    {
      /* Update any locks for the root or its children. */
      like_arg = construct_like_arg(rb->repos_relpath, scratch_pool);

      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_UPDATE_LOCK_REPOS_ID));
      SVN_ERR(svn_sqlite__bindf(stmt, "issi", rb->old_repos_id,
                                rb->repos_relpath, like_arg, new_repos_id));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_global_relocate(svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           const char *repos_root_url,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_dir_relpath;
  svn_wc__db_status_t status;
  struct relocate_baton rb;
  const char *stored_local_dir_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_dir_relpath,
                              db, local_dir_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);
  rb.local_relpath = local_dir_relpath;

  SVN_ERR(read_info(&status,
                    NULL, NULL,
                    &rb.repos_relpath, &rb.old_repos_id,
                    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                    NULL, NULL, NULL, NULL, NULL,
                    &rb.have_base_node,
                    NULL, NULL, NULL,
                    wcroot, rb.local_relpath,
                    scratch_pool, scratch_pool));

  if (status == svn_wc__db_status_excluded)
    {
      /* The parent cannot be excluded, so look at the parent and then
         adjust the relpath */
      const char *parent_relpath = svn_relpath_dirname(local_dir_relpath,
                                                       scratch_pool);
      SVN_ERR(read_info(&status,
                        NULL, NULL,
                        &rb.repos_relpath, &rb.old_repos_id,
                        NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        NULL, NULL, NULL,
                        wcroot, parent_relpath,
                        scratch_pool, scratch_pool));
      stored_local_dir_relpath = rb.local_relpath;
      local_dir_relpath = parent_relpath;
    }
  else
    stored_local_dir_relpath = NULL;

  if (!rb.repos_relpath || rb.old_repos_id == INVALID_REPOS_ID)
    {
      /* Do we need to support relocating something that is
         added/deleted/excluded without relocating the parent?  If not
         then perhaps relpath, root_url and uuid should be passed down
         to the children so that they don't have to scan? */

      if (status == svn_wc__db_status_deleted)
        {
          const char *work_del_relpath;
          SVN_ERR(scan_deletion(NULL, NULL, &work_del_relpath,
                                wcroot, local_dir_relpath,
                                scratch_pool, scratch_pool));
          if (work_del_relpath)
            {
              /* Deleted within a copy/move */
              SVN_ERR_ASSERT(!stored_local_dir_relpath);
              stored_local_dir_relpath = rb.local_relpath;

              /* The parent of the delete is added. */
              status = svn_wc__db_status_added;
              local_dir_relpath = svn_relpath_dirname(work_del_relpath,
                                                      scratch_pool);
            }
        }

      if (status == svn_wc__db_status_added)
        {
          SVN_ERR(scan_addition(NULL, NULL,
                                &rb.repos_relpath, &rb.old_repos_id,
                                NULL, NULL, NULL,
                                wcroot, local_dir_relpath,
                                scratch_pool, scratch_pool));
        }
      else
        SVN_ERR(scan_upwards_for_repos(&rb.old_repos_id, &rb.repos_relpath,
                                       wcroot, local_dir_relpath,
                                       scratch_pool, scratch_pool));
    }

  SVN_ERR(fetch_repos_info(NULL, &rb.repos_uuid,
                           wcroot->sdb, rb.old_repos_id, scratch_pool));
  SVN_ERR_ASSERT(rb.repos_relpath && rb.repos_uuid);

  if (stored_local_dir_relpath)
    {
      const char *part = svn_relpath_is_child(local_dir_relpath,
                                              stored_local_dir_relpath,
                                              scratch_pool);
      rb.repos_relpath = svn_relpath_join(rb.repos_relpath, part,
                                          scratch_pool);
    }

  rb.wc_id = wcroot->wc_id;
  rb.repos_root_url = repos_root_url;

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb, relocate_txn, &rb,
                                       scratch_pool));

  return SVN_NO_ERROR;
}


struct commit_baton {
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  svn_revnum_t new_revision;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  const svn_checksum_t *new_checksum;
  const apr_array_header_t *new_children;
  apr_hash_t *new_dav_cache;
  svn_boolean_t keep_changelist;
  svn_boolean_t no_unlock;

  apr_int64_t repos_id;
  const char *repos_relpath;

  const svn_skel_t *work_items;
};


/* */
static svn_error_t *
commit_node(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  struct commit_baton *cb = baton;
  svn_sqlite__stmt_t *stmt_base;
  svn_sqlite__stmt_t *stmt_work;
  svn_sqlite__stmt_t *stmt_act;
  svn_boolean_t have_base;
  svn_boolean_t have_work;
  svn_boolean_t have_act;
  svn_string_t prop_blob = { 0 };
  const char *changelist = NULL;
  const char *parent_relpath;
  svn_wc__db_status_t new_presence;
  svn_wc__db_kind_t new_kind;
  const char *new_depth_str = NULL;
  svn_sqlite__stmt_t *stmt;

  /* ### is it better to select only the data needed?  */
  SVN_ERR(svn_sqlite__get_statement(&stmt_base, cb->wcroot->sdb,
                                    STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__get_statement(&stmt_work, cb->wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt_base, "is",
                            cb->wcroot->wc_id, cb->local_relpath));
  SVN_ERR(svn_sqlite__bindf(stmt_work, "is",
                            cb->wcroot->wc_id, cb->local_relpath));
  SVN_ERR(svn_sqlite__step(&have_base, stmt_base));
  SVN_ERR(svn_sqlite__step(&have_work, stmt_work));

  SVN_ERR(svn_sqlite__get_statement(&stmt_act, cb->wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt_act, "is",
                            cb->wcroot->wc_id, cb->local_relpath));
  SVN_ERR(svn_sqlite__step(&have_act, stmt_act));

  /* There should be something to commit!  */
  /* ### not true. we could simply have text changes. how to assert?
     SVN_ERR_ASSERT(have_work || have_act);  */

  /* Figure out the new node's kind. It will be whatever is in WORKING_NODE,
     or there will be a BASE_NODE that has it.  */
  if (have_work)
    new_kind = svn_sqlite__column_token(stmt_work, 2, kind_map);
  else
    new_kind = svn_sqlite__column_token(stmt_base, 3, kind_map);

  /* What will the new depth be?  */
  if (new_kind == svn_wc__db_kind_dir)
    {
      if (have_work)
        new_depth_str = svn_sqlite__column_text(stmt_work, 8, scratch_pool);
      else
        new_depth_str = svn_sqlite__column_text(stmt_base, 10, scratch_pool);
    }

  /* Check that the repository information is not being changed.  */
  if (have_base)
    {
      SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt_base, 0));
      SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt_base, 1));

      /* A commit cannot change these values.  */
      SVN_ERR_ASSERT(cb->repos_id == svn_sqlite__column_int64(stmt_base, 0));
      SVN_ERR_ASSERT(strcmp(cb->repos_relpath,
                            svn_sqlite__column_text(stmt_base, 1, NULL)) == 0);
    }

  /* Find the appropriate new properties -- ACTUAL overrides any properties
     in WORKING that arrived as part of a copy/move.

     Note: we'll keep them as a big blob of data, rather than
     deserialize/serialize them.  */
  if (have_act)
    prop_blob.data = svn_sqlite__column_blob(stmt_act, 6, &prop_blob.len,
                                             scratch_pool);
  if (have_work && prop_blob.data == NULL)
    prop_blob.data = svn_sqlite__column_blob(stmt_work, 16, &prop_blob.len,
                                             scratch_pool);
  if (have_base && prop_blob.data == NULL)
    prop_blob.data = svn_sqlite__column_blob(stmt_base, 13, &prop_blob.len,
                                             scratch_pool);

  if (cb->keep_changelist && have_act)
    changelist = svn_sqlite__column_text(stmt_act, 1, scratch_pool);

  /* ### other stuff?  */

  SVN_ERR(svn_sqlite__reset(stmt_base));
  SVN_ERR(svn_sqlite__reset(stmt_work));
  SVN_ERR(svn_sqlite__reset(stmt_act));

  /* Update the BASE_NODE row with all the new information.  */

  if (*cb->local_relpath == '\0')
    parent_relpath = NULL;
  else
    parent_relpath = svn_relpath_dirname(cb->local_relpath, scratch_pool);

  /* ### other presences? or reserve that for separate functions?  */
  new_presence = svn_wc__db_status_normal;

  SVN_ERR(svn_sqlite__get_statement(&stmt, cb->wcroot->sdb,
                                    STMT_APPLY_CHANGES_TO_BASE_NODE));
  /* symlink_target not yet used */
  SVN_ERR(svn_sqlite__bindf(stmt, "issisrtstrisnbn",
                            cb->wcroot->wc_id, cb->local_relpath,
                            parent_relpath,
                            cb->repos_id,
                            cb->repos_relpath,
                            cb->new_revision,
                            presence_map, new_presence,
                            new_depth_str,
                            kind_map, new_kind,
                            cb->changed_rev,
                            cb->changed_date,
                            cb->changed_author,
                            prop_blob.data, prop_blob.len));

  SVN_ERR(svn_sqlite__bind_checksum(stmt, 13, cb->new_checksum,
                                    scratch_pool));
  SVN_ERR(svn_sqlite__bind_properties(stmt, 15, cb->new_dav_cache,
                                      scratch_pool));

  SVN_ERR(svn_sqlite__step_done(stmt));

  if (have_work)
    {
      /* This removes all op_depth > 0 and so does both layers of a
         two-layer replace. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, cb->wcroot->sdb,
                                        STMT_DELETE_ALL_WORKING_NODES));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                cb->wcroot->wc_id, cb->local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  if (have_act)
    {
      /* ### FIXME: We lose the tree conflict data recorded on the node for its
                    children here if we use this on a directory */
      if (cb->keep_changelist && changelist != NULL)
        {
          /* The user told us to keep the changelist. Replace the row in
             ACTUAL_NODE with the basic keys and the changelist.  */
          SVN_ERR(svn_sqlite__get_statement(
                    &stmt, cb->wcroot->sdb,
                    STMT_RESET_ACTUAL_WITH_CHANGELIST));
          SVN_ERR(svn_sqlite__bindf(stmt, "isss",
                                    cb->wcroot->wc_id, cb->local_relpath,
                                    svn_relpath_dirname(cb->local_relpath,
                                                        scratch_pool),
                                    changelist));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }
      else
        {
          /* Toss the ACTUAL_NODE row.  */
          SVN_ERR(svn_sqlite__get_statement(&stmt, cb->wcroot->sdb,
                                            STMT_DELETE_ACTUAL_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                    cb->wcroot->wc_id, cb->local_relpath));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }
    }

  if (new_kind == svn_wc__db_kind_dir)
    {
      /* When committing a directory, we should have its new children.  */
      /* ### one day. just not today.  */
#if 0
      SVN_ERR_ASSERT(cb->new_children != NULL);
#endif

      /* ### process the children  */
    }

  if (!cb->no_unlock)
    {
      svn_sqlite__stmt_t *lock_stmt;

      SVN_ERR(svn_sqlite__get_statement(&lock_stmt, sdb, STMT_DELETE_LOCK));
      SVN_ERR(svn_sqlite__bindf(lock_stmt, "is", cb->repos_id,
                                cb->repos_relpath));
      SVN_ERR(svn_sqlite__step_done(lock_stmt));
    }

  /* Install any work items into the queue, as part of this transaction.  */
  SVN_ERR(add_work_items(sdb, cb->work_items, scratch_pool));

  return SVN_NO_ERROR;
}


/* Set *REPOS_ID and *REPOS_RELPATH to the BASE repository location of
 * (PDH, LOCAL_RELPATH), directly if its BASE row exists or implied from
 * its parent's BASE row if not. In the latter case, error if the parent
 * BASE row does not exist. */
static svn_error_t *
determine_repos_info(apr_int64_t *repos_id,
                     const char **repos_relpath,
                     svn_wc__db_wcroot_t *wcroot,
                     const char *local_relpath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *repos_parent_relpath;
  const char *local_parent_relpath, *name;

  /* ### is it faster to fetch fewer columns? */

  /* Prefer the current node's repository information.  */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 0));
      SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 1));

      *repos_id = svn_sqlite__column_int64(stmt, 0);
      *repos_relpath = svn_sqlite__column_text(stmt, 1, result_pool);

      return svn_error_return(svn_sqlite__reset(stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  /* This was a child node within this wcroot. We want to look at the
     BASE node of the directory.  */
  svn_relpath_split(&local_parent_relpath, &name, local_relpath, scratch_pool);

  /* The REPOS_ID will be the same (### until we support mixed-repos)  */
  SVN_ERR(scan_upwards_for_repos(repos_id, &repos_parent_relpath,
                                 wcroot, local_parent_relpath,
                                 scratch_pool, scratch_pool));

  *repos_relpath = svn_relpath_join(repos_parent_relpath, name, result_pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_global_commit(svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_revnum_t new_revision,
                         svn_revnum_t changed_revision,
                         apr_time_t changed_date,
                         const char *changed_author,
                         const svn_checksum_t *new_checksum,
                         const apr_array_header_t *new_children,
                         apr_hash_t *new_dav_cache,
                         svn_boolean_t keep_changelist,
                         svn_boolean_t no_unlock,
                         const svn_skel_t *work_items,
                         apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  struct commit_baton cb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_revision));
  SVN_ERR_ASSERT(new_checksum == NULL || new_children == NULL);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&cb.wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(cb.wcroot);

  cb.local_relpath = local_relpath;

  cb.new_revision = new_revision;

  cb.changed_rev = changed_revision;
  cb.changed_date = changed_date;
  cb.changed_author = changed_author;
  cb.new_checksum = new_checksum;
  cb.new_children = new_children;
  cb.new_dav_cache = new_dav_cache;
  cb.keep_changelist = keep_changelist;
  cb.no_unlock = no_unlock;
  cb.work_items = work_items;

  /* If we are adding a file or directory, then we need to get
     repository information from the parent node since "this node" does
     not have a BASE).

     For existing nodes, we should retain the (potentially-switched)
     repository information.  */
  SVN_ERR(determine_repos_info(&cb.repos_id, &cb.repos_relpath,
                               cb.wcroot, local_relpath,
                               scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__with_transaction(cb.wcroot->sdb, commit_node, &cb,
                                       scratch_pool));

  /* We *totally* monkeyed the entries. Toss 'em.  */
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


struct update_baton {
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  const char *new_repos_relpath;
  svn_revnum_t new_revision;
  const apr_hash_t *new_props;
  svn_revnum_t new_changed_rev;
  apr_time_t new_changed_date;
  const char *new_changed_author;
  const apr_array_header_t *new_children;
  const svn_checksum_t *new_checksum;
  const char *new_target;
  const svn_skel_t *conflict;
  const svn_skel_t *work_items;
};


svn_error_t *
svn_wc__db_global_update(svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_wc__db_kind_t new_kind,
                         const char *new_repos_relpath,
                         svn_revnum_t new_revision,
                         const apr_hash_t *new_props,
                         svn_revnum_t new_changed_rev,
                         apr_time_t new_changed_date,
                         const char *new_changed_author,
                         const apr_array_header_t *new_children,
                         const svn_checksum_t *new_checksum,
                         const char *new_target,
                         const apr_hash_t *new_dav_cache,
                         const svn_skel_t *conflict,
                         const svn_skel_t *work_items,
                         apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  struct update_baton ub;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  /* ### allow NULL for NEW_REPOS_RELPATH to indicate "no change"?  */
  SVN_ERR_ASSERT(svn_relpath_is_canonical(new_repos_relpath, scratch_pool));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_revision));
  SVN_ERR_ASSERT(new_props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_changed_rev));
  SVN_ERR_ASSERT((new_children != NULL
                  && new_checksum == NULL
                  && new_target == NULL)
                 || (new_children == NULL
                     && new_checksum != NULL
                     && new_target == NULL)
                 || (new_children == NULL
                     && new_checksum == NULL
                     && new_target != NULL));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&ub.wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(ub.wcroot);

  ub.local_relpath = local_relpath;

  ub.new_repos_relpath = new_repos_relpath;
  ub.new_revision = new_revision;
  ub.new_props = new_props;
  ub.new_changed_rev = new_changed_rev;
  ub.new_changed_date = new_changed_date;
  ub.new_changed_author = new_changed_author;
  ub.new_children = new_children;
  ub.new_checksum = new_checksum;
  ub.new_target = new_target;

  ub.conflict = conflict;
  ub.work_items = work_items;

  NOT_IMPLEMENTED();

#if 0
  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb, update_node, &ub,
                                       scratch_pool));
#endif

  /* We *totally* monkeyed the entries. Toss 'em.  */
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


struct record_baton {
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  svn_filesize_t translated_size;
  apr_time_t last_mod_time;
};


/* Record TRANSLATED_SIZE and LAST_MOD_TIME into top layer in NODES */
static svn_error_t *
record_fileinfo(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  struct record_baton *rb = baton;
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_UPDATE_NODE_FILEINFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "isii",
                            rb->wcroot->wc_id, rb->local_relpath,
                            rb->translated_size, rb->last_mod_time));
  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  SVN_ERR_ASSERT(affected_rows == 1);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_global_record_fileinfo(svn_wc__db_t *db,
                                  const char *local_abspath,
                                  svn_filesize_t translated_size,
                                  apr_time_t last_mod_time,
                                  apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  struct record_baton rb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&rb.wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(rb.wcroot);

  rb.local_relpath = local_relpath;

  rb.translated_size = translated_size;
  rb.last_mod_time = last_mod_time;

  SVN_ERR(svn_sqlite__with_transaction(rb.wcroot->sdb, record_fileinfo, &rb,
                                       scratch_pool));

  /* We *totally* monkeyed the entries. Toss 'em.  */
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_lock_add(svn_wc__db_t *db,
                    const char *local_abspath,
                    const svn_wc__db_lock_t *lock,
                    apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *repos_relpath;
  apr_int64_t repos_id;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(lock != NULL);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(scan_upwards_for_repos(&repos_id, &repos_relpath,
                                 wcroot, local_relpath,
                                 scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_INSERT_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "iss",
                            repos_id, repos_relpath, lock->token));

  if (lock->owner != NULL)
    SVN_ERR(svn_sqlite__bind_text(stmt, 4, lock->owner));

  if (lock->comment != NULL)
    SVN_ERR(svn_sqlite__bind_text(stmt, 5, lock->comment));

  if (lock->date != 0)
    SVN_ERR(svn_sqlite__bind_int64(stmt, 6, lock->date));

  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  /* There may be some entries, and the lock info is now out of date.  */
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_lock_remove(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *repos_relpath;
  apr_int64_t repos_id;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(scan_upwards_for_repos(&repos_id, &repos_relpath,
                                 wcroot, local_relpath,
                                 scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", repos_id, repos_relpath));

  SVN_ERR(svn_sqlite__step_done(stmt));

  /* There may be some entries, and the lock info is now out of date.  */
  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_scan_base_repos(const char **repos_relpath,
                           const char **repos_root_url,
                           const char **repos_uuid,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  apr_int64_t repos_id;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(scan_upwards_for_repos(&repos_id, repos_relpath, wcroot,
                                 local_relpath, result_pool, scratch_pool));
  SVN_ERR(fetch_repos_info(repos_root_url, repos_uuid, wcroot->sdb,
                           repos_id, result_pool));

  return SVN_NO_ERROR;
}


/* Like svn_wc__db_scan_addition(), but with WCROOT+LOCAL_RELPATH instead of
 * DB+LOCAL_ABSPATH.
 *
 * The output value of *ORIGINAL_REPOS_ID will be INVALID_REPOS_ID if there
 * is no 'copy-from' repository. */
static svn_error_t *
scan_addition(svn_wc__db_status_t *status,
              const char **op_root_relpath,
              const char **repos_relpath,
              apr_int64_t *repos_id,
              const char **original_repos_relpath,
              apr_int64_t *original_repos_id,
              svn_revnum_t *original_revision,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  const char *current_relpath = local_relpath;
  const char *build_relpath = "";

  /* Initialize most of the OUT parameters. Generally, we'll only be filling
     in a subset of these, so it is easier to init all up front. Note that
     the STATUS parameter will be initialized once we read the status of
     the specified node.  */
  if (op_root_relpath)
    *op_root_relpath = NULL;
  if (original_repos_relpath)
    *original_repos_relpath = NULL;
  if (original_repos_id)
    *original_repos_id = INVALID_REPOS_ID;
  if (original_revision)
    *original_revision = SVN_INVALID_REVNUM;

  {
    svn_sqlite__stmt_t *stmt;
    svn_boolean_t have_row;
    svn_wc__db_status_t presence;
    apr_int64_t op_depth;
    const char *repos_prefix_path = "";
    int i;

    /* ### is it faster to fetch fewer columns? */
    SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                      STMT_SELECT_WORKING_NODE));
    SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
    SVN_ERR(svn_sqlite__step(&have_row, stmt));

    if (!have_row)
      {
        /* Reset statement before returning */
        SVN_ERR(svn_sqlite__reset(stmt));

        /* ### maybe we should return a usage error instead?  */
        return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("The node '%s' was not found."),
                                 path_for_error_message(wcroot,
                                                        local_relpath,
                                                        scratch_pool));
      }

    presence = svn_sqlite__column_token(stmt, 1, presence_map);

    /* The starting node should exist normally.  */
    if (presence != svn_wc__db_status_normal)
      /* reset the statement as part of the error generation process */
      return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                               svn_sqlite__reset(stmt),
                               _("Expected node '%s' to be added."),
                               path_for_error_message(wcroot,
                                                      local_relpath,
                                                      scratch_pool));

    if (original_revision)
      *original_revision = svn_sqlite__column_revnum(stmt, 12);

    /* Provide the default status; we'll override as appropriate. */
    if (status)
      *status = svn_wc__db_status_added;


    /* Calculate the op root local path components */
    op_depth = svn_sqlite__column_int64(stmt, 0);
    current_relpath = local_relpath;

    for (i = relpath_depth(local_relpath); i > op_depth; --i)
      {
        /* Calculate the path of the operation root */
        repos_prefix_path =
          svn_relpath_join(svn_dirent_basename(current_relpath, NULL),
                           repos_prefix_path,
                           scratch_pool);
        current_relpath = svn_relpath_dirname(current_relpath, scratch_pool);
      }

    if (op_root_relpath)
      *op_root_relpath = apr_pstrdup(result_pool, current_relpath);

    if (original_repos_relpath
        || original_repos_id
        || (original_revision && *original_revision == SVN_INVALID_REVNUM)
        || status)
      {
        if (local_relpath != current_relpath)
          /* requery to get the add/copy root */
          {
            SVN_ERR(svn_sqlite__reset(stmt));

            SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                      wcroot->wc_id, current_relpath));
            SVN_ERR(svn_sqlite__step(&have_row, stmt));

            if (!have_row)
              {
                /* Reset statement before returning */
                SVN_ERR(svn_sqlite__reset(stmt));

                /* ### maybe we should return a usage error instead?  */
                return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                         _("The node '%s' was not found."),
                                         path_for_error_message(wcroot,
                                                                current_relpath,
                                                                scratch_pool));
              }

            if (original_revision && *original_revision == SVN_INVALID_REVNUM)
              *original_revision = svn_sqlite__column_revnum(stmt, 12);
          }

        /* current_relpath / current_abspath
           as well as the record in stmt contain the data of the op_root */
        if (original_repos_relpath)
          *original_repos_relpath = svn_sqlite__column_text(stmt, 11,
                                                            result_pool);

        if (!svn_sqlite__column_is_null(stmt, 10)
            && (status
                || original_repos_id))
          /* If column 10 (original_repos_id) is NULL,
             this is a plain add, not a copy or a move */
          {
            if (original_repos_id)
              *original_repos_id = svn_sqlite__column_int64(stmt, 10);

            if (status)
              {
                if (svn_sqlite__column_boolean(stmt, 13 /* moved_here */))
                  *status = svn_wc__db_status_moved_here;
                else
                  *status = svn_wc__db_status_copied;
              }
          }
      }


    /* ### This loop here is to skip up to the first node which is a BASE node,
       because scan_upwards_for_repos() doesn't accomodate the scenario that
       we're looking at here; we found the true op_root, which may be inside
       further changed trees. */
    while (TRUE)
      {

        SVN_ERR(svn_sqlite__reset(stmt));

        /* Pointing at op_depth, look at the parent */
        repos_prefix_path =
          svn_relpath_join(svn_dirent_basename(current_relpath, NULL),
                           repos_prefix_path,
                           scratch_pool);
        current_relpath = svn_relpath_dirname(current_relpath, scratch_pool);


        SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, current_relpath));
        SVN_ERR(svn_sqlite__step(&have_row, stmt));

        if (! have_row)
          break;

        op_depth = svn_sqlite__column_int64(stmt, 0);

        /* Skip to op_depth */
        for (i = relpath_depth(current_relpath); i > op_depth; i--)
          {
            /* Calculate the path of the operation root */
            repos_prefix_path =
              svn_relpath_join(svn_dirent_basename(current_relpath, NULL),
                               repos_prefix_path,
                               scratch_pool);
            current_relpath =
              svn_relpath_dirname(current_relpath, scratch_pool);
          }
      }

    SVN_ERR(svn_sqlite__reset(stmt));

    build_relpath = repos_prefix_path;
  }

  /* If we're here, then we have an added/copied/moved (start) node, and
     CURRENT_ABSPATH now points to a BASE node. Figure out the repository
     information for the current node, and use that to compute the start
     node's repository information.  */
  if (repos_relpath || repos_id)
    {
      const char *base_relpath;

      SVN_ERR(scan_upwards_for_repos(repos_id, &base_relpath,
                                     wcroot, current_relpath,
                                     scratch_pool, scratch_pool));

      if (repos_relpath)
        *repos_relpath = svn_relpath_join(base_relpath, build_relpath,
                                          result_pool);
    }

  /* Postconditions */
#ifdef SVN_DEBUG
  if (status)
    {
      SVN_ERR_ASSERT(*status == svn_wc__db_status_added
                     || *status == svn_wc__db_status_copied
                     || *status == svn_wc__db_status_moved_here);
      if (*status == svn_wc__db_status_added)
        {
          SVN_ERR_ASSERT(!original_repos_relpath
                         || *original_repos_relpath == NULL);
          SVN_ERR_ASSERT(!original_revision
                         || *original_revision == SVN_INVALID_REVNUM);
          SVN_ERR_ASSERT(!original_repos_id
                         || *original_repos_id == INVALID_REPOS_ID);
        }
      else
        {
          SVN_ERR_ASSERT(!original_repos_relpath
                         || *original_repos_relpath != NULL);
          SVN_ERR_ASSERT(!original_revision
                         || *original_revision != SVN_INVALID_REVNUM);
          SVN_ERR_ASSERT(!original_repos_id
                         || *original_repos_id != INVALID_REPOS_ID);
        }
    }
  SVN_ERR_ASSERT(!op_root_relpath || *op_root_relpath != NULL);
#endif

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_scan_addition(svn_wc__db_status_t *status,
                         const char **op_root_abspath,
                         const char **repos_relpath,
                         const char **repos_root_url,
                         const char **repos_uuid,
                         const char **original_repos_relpath,
                         const char **original_root_url,
                         const char **original_uuid,
                         svn_revnum_t *original_revision,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *op_root_relpath;
  apr_int64_t repos_id = INVALID_REPOS_ID;
  apr_int64_t original_repos_id = INVALID_REPOS_ID;
  apr_int64_t *repos_id_p
    = (repos_root_url || repos_uuid) ? &repos_id : NULL;
  apr_int64_t *original_repos_id_p
    = (original_root_url || original_uuid) ? &original_repos_id : NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(scan_addition(status, &op_root_relpath, repos_relpath, repos_id_p,
                        original_repos_relpath, original_repos_id_p,
                        original_revision, wcroot, local_relpath,
                        result_pool, scratch_pool));

  if (op_root_abspath)
    *op_root_abspath = svn_dirent_join(wcroot->abspath, op_root_relpath,
                                       result_pool);
  /* REPOS_ID must be valid if requested; ORIGINAL_REPOS_ID need not be. */
  SVN_ERR_ASSERT(repos_id_p == NULL || repos_id != INVALID_REPOS_ID);

  SVN_ERR(fetch_repos_info(repos_root_url, repos_uuid, wcroot->sdb,
                           repos_id, result_pool));
  SVN_ERR(fetch_repos_info(original_root_url, original_uuid,
                           wcroot->sdb, original_repos_id,
                           result_pool));

  return SVN_NO_ERROR;
}


/* Like svn_wc__db_scan_deletion(), but with WCROOT+LOCAL_RELPATH instead of
 * DB+LOCAL_ABSPATH, and outputting relpaths instead of abspaths. */
static svn_error_t *
scan_deletion(const char **base_del_relpath,
              const char **moved_to_relpath,
              const char **work_del_relpath,
              svn_wc__db_wcroot_t *wcroot,
              const char *local_relpath,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  const char *current_relpath = local_relpath;
  const char *child_relpath = NULL;
  svn_wc__db_status_t child_presence;
  svn_boolean_t child_has_base = FALSE;
  svn_boolean_t found_moved_to = FALSE;
  apr_int64_t local_op_depth, op_depth;

  /* Initialize all the OUT parameters.  */
  if (base_del_relpath != NULL)
    *base_del_relpath = NULL;
  if (moved_to_relpath != NULL)
    *moved_to_relpath = NULL;
  if (work_del_relpath != NULL)
    *work_del_relpath = NULL;

  /* Initialize to something that won't denote an important parent/child
     transition.  */
  child_presence = svn_wc__db_status_base_deleted;

  while (TRUE)
    {
      svn_sqlite__stmt_t *stmt;
      svn_boolean_t have_row;
      svn_boolean_t have_base;
      svn_wc__db_status_t work_presence;

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_DELETION_INFO));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, current_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (!have_row)
        {
          /* There better be a row for the starting node!  */
          if (current_relpath == local_relpath)
            return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                                     svn_sqlite__reset(stmt),
                                     _("The node '%s' was not found."),
                                     path_for_error_message(wcroot,
                                                            local_relpath,
                                                            scratch_pool));

          /* There are no values, so go ahead and reset the stmt now.  */
          SVN_ERR(svn_sqlite__reset(stmt));

          /* No row means no WORKING node at this path, which means we just
             fell off the top of the WORKING tree.

             If the child was not-present this implies the root of the
             (added) WORKING subtree was deleted.  This can occur
             during post-commit processing when the copied parent that
             was in the WORKING tree has been moved to the BASE tree. */
          if (work_del_relpath != NULL
              && child_presence == svn_wc__db_status_not_present
              && *work_del_relpath == NULL)
            *work_del_relpath = apr_pstrdup(result_pool, child_relpath);

          /* If the child did not have a BASE node associated with it, then
             we're looking at a deletion that occurred within an added tree.
             There is no root of a deleted/replaced BASE tree.

             If the child was base-deleted, then the whole tree is a
             simple (explicit) deletion of the BASE tree.

             If the child was normal, then it is the root of a replacement,
             which means an (implicit) deletion of the BASE tree.

             In both cases, set the root of the operation (if we have not
             already set it as part of a moved-away).  */
          if (base_del_relpath != NULL
              && child_has_base
              && *base_del_relpath == NULL)
            *base_del_relpath = apr_pstrdup(result_pool, child_relpath);

          /* We found whatever roots we needed. This BASE node and its
             ancestors are unchanged, so we're done.  */
          break;
        }

      /* We need the presence of the WORKING node. Note that legal values
         are: normal, not-present, base-deleted, incomplete.  */
      work_presence = svn_sqlite__column_token(stmt, 1, presence_map);

      /* The starting node should be deleted.  */
      if (current_relpath == local_relpath
          && work_presence != svn_wc__db_status_not_present
          && work_presence != svn_wc__db_status_base_deleted)
        return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                                 svn_sqlite__reset(stmt),
                                 _("Expected node '%s' to be deleted."),
                                 path_for_error_message(wcroot,
                                                        local_relpath,
                                                        scratch_pool));

      /* ### incomplete not handled */
      SVN_ERR_ASSERT(work_presence == svn_wc__db_status_normal
                     || work_presence == svn_wc__db_status_not_present
                     || work_presence == svn_wc__db_status_base_deleted);

      have_base = !svn_sqlite__column_is_null(stmt,
                                              0 /* BASE_NODE.presence */);
      if (have_base)
        {
          svn_wc__db_status_t base_presence
            = svn_sqlite__column_token(stmt, 0, presence_map);

          /* Only "normal" and "not-present" are allowed.  */
          SVN_ERR_ASSERT(base_presence == svn_wc__db_status_normal
                         || base_presence == svn_wc__db_status_not_present

                         /* ### there are cases where the BASE node is
                            ### marked as incomplete. we should treat this
                            ### as a "normal" node for the purposes of
                            ### this function. we really should not allow
                            ### it, but this situation occurs within the
                            ### following tests:
                            ###   switch_tests 31
                            ###   update_tests 46
                            ###   update_tests 53
                         */
                         || base_presence == svn_wc__db_status_incomplete
                         );

#if 1
          /* ### see above comment  */
          if (base_presence == svn_wc__db_status_incomplete)
            base_presence = svn_wc__db_status_normal;
#endif

          /* If a BASE node is marked as not-present, then we'll ignore
             it within this function. That status is simply a bookkeeping
             gimmick, not a real node that may have been deleted.  */
        }

      /* Only grab the nearest ancestor.  */
      if (!found_moved_to &&
          (moved_to_relpath != NULL || base_del_relpath != NULL)
          && !svn_sqlite__column_is_null(stmt, 2 /* moved_to */))
        {
          /* There better be a BASE_NODE (that was moved-away).  */
          SVN_ERR_ASSERT(have_base);

          found_moved_to = TRUE;

          /* This makes things easy. It's the BASE_DEL_ABSPATH!  */
          if (base_del_relpath != NULL)
            *base_del_relpath = apr_pstrdup(result_pool, current_relpath);

          if (moved_to_relpath != NULL)
            *moved_to_relpath = apr_pstrdup(result_pool,
                                    svn_sqlite__column_text(stmt, 2, NULL));
        }

      op_depth = svn_sqlite__column_int64(stmt, 3);
      if (current_relpath == local_relpath)
        local_op_depth = op_depth;

      if (work_del_relpath && !work_del_relpath[0]
          && ((op_depth < local_op_depth && op_depth > 0)
              || child_presence == svn_wc__db_status_not_present))
        {
          *work_del_relpath = apr_pstrdup(result_pool, child_relpath);
        }

      /* We're all done examining the return values.  */
      SVN_ERR(svn_sqlite__reset(stmt));

      /* Move to the parent node. Remember the information about this node
         for our parent to use.  */
      child_relpath = current_relpath;
      child_presence = work_presence;
      child_has_base = have_base;

      /* The wcroot can't be deleted, but make sure we don't loop on invalid
         data */
      SVN_ERR_ASSERT(current_relpath[0] != '\0');

      current_relpath = svn_relpath_dirname(current_relpath, scratch_pool);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_scan_deletion(const char **base_del_abspath,
                         const char **moved_to_abspath,
                         const char **work_del_abspath,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *base_del_relpath, *moved_to_relpath, *work_del_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(scan_deletion(&base_del_relpath, &moved_to_relpath,
                        &work_del_relpath, wcroot,
                        local_relpath, result_pool, scratch_pool));

  if (base_del_abspath)
    {
      *base_del_abspath = (base_del_relpath
                           ? svn_dirent_join(wcroot->abspath,
                                             base_del_relpath, result_pool)
                           : NULL);
    }
  if (moved_to_abspath)
    {
      *moved_to_abspath = (moved_to_relpath
                           ? svn_dirent_join(wcroot->abspath,
                                             moved_to_relpath, result_pool)
                           : NULL);
    }
  if (work_del_abspath)
    {
      *work_del_abspath = (work_del_relpath
                           ? svn_dirent_join(wcroot->abspath,
                                             work_del_relpath, result_pool)
                           : NULL);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_upgrade_begin(svn_sqlite__db_t **sdb,
                         apr_int64_t *repos_id,
                         apr_int64_t *wc_id,
                         const char *dir_abspath,
                         const char *repos_root_url,
                         const char *repos_uuid,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  /* ### for now, using SDB_FILE rather than SDB_FILE_UPGRADE. there are
     ### too many interacting components that want to *read* the normal
     ### SDB_FILE as we perform the upgrade.  */
  return svn_error_return(create_db(sdb, repos_id, wc_id, dir_abspath,
                                    repos_root_url, repos_uuid,
                                    SDB_FILE,
                                    result_pool, scratch_pool));
}


svn_error_t *
svn_wc__db_upgrade_apply_dav_cache(svn_sqlite__db_t *sdb,
                                   const char *dir_relpath,
                                   apr_hash_t *cache_values,
                                   apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_int64_t wc_id;
  apr_hash_index_t *hi;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_wc__db_util_fetch_wc_id(&wc_id, sdb, iterpool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_UPDATE_BASE_NODE_DAV_CACHE));

  /* Iterate over all the wcprops, writing each one to the wc_db. */
  for (hi = apr_hash_first(scratch_pool, cache_values);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *name = svn__apr_hash_index_key(hi);
      apr_hash_t *props = svn__apr_hash_index_val(hi);
      const char *local_relpath;

      svn_pool_clear(iterpool);

      local_relpath = svn_relpath_join(dir_relpath, name, iterpool);

      SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
      SVN_ERR(svn_sqlite__bind_properties(stmt, 3, props, iterpool));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_upgrade_apply_props(svn_sqlite__db_t *sdb,
                               const char *dir_abspath,
                               const char *local_relpath,
                               apr_hash_t *base_props,
                               apr_hash_t *revert_props,
                               apr_hash_t *working_props,
                               int original_format,
                               apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_int64_t wc_id, top_op_depth = -1, below_op_depth = -1;
  svn_wc__db_status_t top_presence, below_presence;
  int affected_rows;

  /* ### working_props: use set_props_txn.
     ### if working_props == NULL, then skip. what if they equal the
     ### pristine props? we should probably do the compare here.
     ###
     ### base props go into WORKING_NODE if avail, otherwise BASE.
     ###
     ### revert only goes into BASE. (and WORKING better be there!)

     Prior to 1.4.0 (ORIGINAL_FORMAT < 8), REVERT_PROPS did not exist. If a
     file was deleted, then a copy (potentially with props) was disallowed
     and could not replace the deletion. An addition *could* be performed,
     but that would never bring its own props.

     1.4.0 through 1.4.5 created the concept of REVERT_PROPS, but had a
     bug in svn_wc_add_repos_file2() whereby a copy-with-props did NOT
     construct a REVERT_PROPS if the target had no props. Thus, reverting
     the delete/copy would see no REVERT_PROPS to restore, leaving the
     props from the copy source intact, and appearing as if they are (now)
     the base props for the previously-deleted file. (wc corruption)

     1.4.6 ensured that an empty REVERT_PROPS would be established at all
     times. See issue 2530, and r861670 as starting points.

     We will use ORIGINAL_FORMAT and SVN_WC__NO_REVERT_FILES to determine
     the handling of our inputs, relative to the state of this node.
  */

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_NODE_UPGRADE));
  SVN_ERR(svn_sqlite__bindf(stmt, "s", local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      top_op_depth = svn_sqlite__column_int64(stmt, 0);
      top_presence = svn_sqlite__column_token(stmt, 1, presence_map);
      wc_id = svn_sqlite__column_int64(stmt, 2);
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (have_row)
        {
          below_op_depth = svn_sqlite__column_int64(stmt, 0);
          below_presence = svn_sqlite__column_token(stmt, 1, presence_map);
        }
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  /* Detect the buggy scenario described above. We cannot upgrade this
     working copy if we have no idea where BASE_PROPS should go.  */
  if (original_format > SVN_WC__NO_REVERT_FILES
      && revert_props == NULL
      && top_op_depth != -1
      && top_presence == svn_wc__db_status_normal
      && below_op_depth != -1
      && below_presence != svn_wc__db_status_not_present)
    {
      /* There should be REVERT_PROPS, so it appears that we just ran into
         the described bug. Sigh.  */
      return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                               _("The properties of '%s' are in an "
                                 "indeterminate state and cannot be "
                                 "upgraded. See issue #2530."),
                               svn_dirent_local_style(
                                 svn_dirent_join(dir_abspath, local_relpath,
                                                 scratch_pool), scratch_pool));
    }

  /* Need at least one row, or two rows if there are revert props */
  if (top_op_depth == -1
      || (below_op_depth == -1 && revert_props))
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                             _("Insufficient NODES rows for '%s'"),
                             svn_dirent_local_style(
                               svn_dirent_join(dir_abspath, local_relpath,
                                               scratch_pool), scratch_pool));

  /* one row, base props only: upper row gets base props
     two rows, base props only: lower row gets base props
     two rows, revert props only: lower row gets revert props
     two rows, base and revert props: upper row gets base, lower gets revert */


  if (revert_props || below_op_depth == -1)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_UPDATE_NODE_PROPS));
      SVN_ERR(svn_sqlite__bindf(stmt, "isi",
                                wc_id, local_relpath, top_op_depth));
      SVN_ERR(svn_sqlite__bind_properties(stmt, 4, base_props, scratch_pool));
      SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

      SVN_ERR_ASSERT(affected_rows == 1);
    }

  if (below_op_depth != -1)
    {
      apr_hash_t *props = revert_props ? revert_props : base_props;

      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_UPDATE_NODE_PROPS));
      SVN_ERR(svn_sqlite__bindf(stmt, "isi",
                                wc_id, local_relpath, below_op_depth));
      SVN_ERR(svn_sqlite__bind_properties(stmt, 4, props, scratch_pool));
      SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

      SVN_ERR_ASSERT(affected_rows == 1);
    }

  /* If there are WORKING_PROPS, then they always go into ACTUAL_NODE.  */
  if (working_props != NULL
      && base_props != NULL)
    {
      apr_array_header_t *diffs;

      SVN_ERR(svn_prop_diffs(&diffs, working_props, base_props, scratch_pool));

      if (diffs->nelts == 0)
        working_props = NULL; /* No differences */
    }

  if (working_props != NULL)
    {
      SVN_ERR(set_actual_props(wc_id, local_relpath, working_props,
                               sdb, scratch_pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_upgrade_get_repos_id(apr_int64_t *repos_id,
                                svn_sqlite__db_t *sdb,
                                const char *repos_root_url,
                                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_REPOSITORY));
  SVN_ERR(svn_sqlite__bindf(stmt, "s", repos_root_url));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_DB_ERROR, svn_sqlite__reset(stmt),
                             _("Repository '%s' not found in the database"),
                             repos_root_url);

  *repos_id = svn_sqlite__column_int64(stmt, 0);
  return svn_error_return(svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_upgrade_finish(const char *dir_abspath,
                          svn_sqlite__db_t *sdb,
                          apr_pool_t *scratch_pool)
{
  /* ### eventually rename SDB_FILE_UPGRADE to SDB_FILE.  */
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_wq_add(svn_wc__db_t *db,
                  const char *wri_abspath,
                  const svn_skel_t *work_item,
                  apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  /* Quick exit, if there are no work items to queue up.  */
  if (work_item == NULL)
    return SVN_NO_ERROR;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* Add the work item(s) to the WORK_QUEUE.  */
  return svn_error_return(add_work_items(wcroot->sdb, work_item,
                                         scratch_pool));
}


svn_error_t *
svn_wc__db_wq_fetch(apr_uint64_t *id,
                    svn_skel_t **work_item,
                    svn_wc__db_t *db,
                    const char *wri_abspath,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(id != NULL);
  SVN_ERR_ASSERT(work_item != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_WORK_ITEM));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (!have_row)
    {
      *id = 0;
      *work_item = NULL;
    }
  else
    {
      apr_size_t len;
      const void *val;

      *id = svn_sqlite__column_int64(stmt, 0);

      val = svn_sqlite__column_blob(stmt, 1, &len, result_pool);

      *work_item = svn_skel__parse(val, len, result_pool);
    }

  return svn_error_return(svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_wq_completed(svn_wc__db_t *db,
                        const char *wri_abspath,
                        apr_uint64_t id,
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));
  SVN_ERR_ASSERT(id != 0);

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_WORK_ITEM));
  SVN_ERR(svn_sqlite__bind_int64(stmt, 1, id));
  return svn_error_return(svn_sqlite__step_done(stmt));
}


/* ### temporary API. remove before release.  */
svn_error_t *
svn_wc__db_temp_get_format(int *format,
                           svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  pdh = svn_wc__db_pdh_get_or_create(db, local_dir_abspath, FALSE,
                                     scratch_pool);

  /* If the PDH isn't present, or have wcroot information, then do a full
     upward traversal to find the wcroot.  */
  if (pdh == NULL || pdh->wcroot == NULL)
    {
      const char *local_relpath;
      svn_error_t *err;

      err = svn_wc__db_pdh_parse_local_abspath(&pdh, &local_relpath, db,
                                local_dir_abspath, svn_sqlite__mode_readonly,
                                scratch_pool, scratch_pool);
      /* NOTE: pdh does *not* have to have a usable format.  */

      /* If we hit an error examining this directory, then declare this
         directory to not be a working copy.  */
      /* ### for per-dir layouts, the wcroot should be this directory,
         ### so bail if the PDH is a parent (and, thus, local_relpath is
         ### something besides "").  */
      if (err)
        {
          if (err && err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
            return svn_error_return(err);
          svn_error_clear(err);

          /* We might turn this directory into a wcroot later, so let's
             just forget what we (didn't) find. The wcroot is still
             hanging off a parent though.
             Don't clear the wcroot of a parent if we just found a
             relative path here or we get multiple wcroot issues. */
          pdh->wcroot = NULL;

          /* Remap the returned error.  */
          *format = 0;
          return svn_error_createf(SVN_ERR_WC_MISSING, NULL,
                                   _("'%s' is not a working copy"),
                                   svn_dirent_local_style(local_dir_abspath,
                                                          scratch_pool));
        }

      SVN_ERR_ASSERT(pdh->wcroot != NULL);
    }

  SVN_ERR_ASSERT(pdh->wcroot->format >= 1);

  *format = pdh->wcroot->format;

  return SVN_NO_ERROR;
}

/* ### temporary API. remove before release.  */
svn_error_t *
svn_wc__db_temp_forget_directory(svn_wc__db_t *db,
                                 const char *local_dir_abspath,
                                 apr_pool_t *scratch_pool)
{
  apr_hash_t *roots = apr_hash_make(scratch_pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, db->dir_data);
       hi;
       hi = apr_hash_next(hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_wc__db_pdh_t *pdh;
      svn_error_t *err;

      apr_hash_this(hi, &key, &klen, &val);
      pdh = val;

      if (!svn_dirent_is_ancestor(local_dir_abspath, pdh->local_abspath))
        continue;

      err = svn_wc__db_wclock_release(db, pdh->local_abspath,
                                      scratch_pool);
      if (err
          && (err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY
              || err->apr_err == SVN_ERR_WC_NOT_LOCKED))
        {
          svn_error_clear(err);
        }
      else
        SVN_ERR(err);

      apr_hash_set(db->dir_data, key, klen, NULL);

      if (pdh->wcroot && pdh->wcroot->sdb &&
          svn_dirent_is_ancestor(local_dir_abspath, pdh->wcroot->abspath))
        {
          apr_hash_set(roots, pdh->wcroot->abspath, APR_HASH_KEY_STRING,
                       pdh->wcroot);
        }
    }

  return svn_error_return(svn_wc__db_close_many_wcroots(roots, db->state_pool,
                                                        scratch_pool));
}

/* ### temporary API. remove before release.  */
svn_wc_adm_access_t *
svn_wc__db_temp_get_access(svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  svn_wc__db_wcroot_t *wcroot;
  svn_error_t *err;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_dir_abspath));

  /* ### we really need to assert that we were passed a directory. sometimes
     ### adm_retrieve_internal is asked about a file, and then it asks us
     ### for an access baton for it. we should definitely return NULL, but
     ### ideally: the caller would never ask us about a non-directory.  */

  err = svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                            db, local_dir_abspath, svn_sqlite__mode_readwrite,
                            scratch_pool, scratch_pool);
  if (err)
    {
      svn_error_clear(err);
      return NULL;
    }

  if (!wcroot)
    return NULL;

  return apr_hash_get(wcroot->access_cache, local_dir_abspath,
                      APR_HASH_KEY_STRING);
}


/* ### temporary API. remove before release.  */
void
svn_wc__db_temp_set_access(svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           svn_wc_adm_access_t *adm_access,
                           apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  svn_wc__db_wcroot_t *wcroot;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  svn_error_clear(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                            db, local_dir_abspath, svn_sqlite__mode_readwrite,
                            scratch_pool, scratch_pool));

  /* Better not override something already there.  */
  SVN_ERR_ASSERT_NO_RETURN(apr_hash_get(wcroot->access_cache,
                                        local_dir_abspath,
                                        APR_HASH_KEY_STRING) == NULL);
  apr_hash_set(wcroot->access_cache, local_dir_abspath,
               APR_HASH_KEY_STRING, adm_access);
}


/* ### temporary API. remove before release.  */
svn_error_t *
svn_wc__db_temp_close_access(svn_wc__db_t *db,
                             const char *local_dir_abspath,
                             svn_wc_adm_access_t *adm_access,
                             apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  svn_wc__db_wcroot_t *wcroot;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_dir_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  apr_hash_set(wcroot->access_cache, local_dir_abspath,
               APR_HASH_KEY_STRING, NULL);

  return SVN_NO_ERROR;
}


/* ### temporary API. remove before release.  */
void
svn_wc__db_temp_clear_access(svn_wc__db_t *db,
                             const char *local_dir_abspath,
                             apr_pool_t *scratch_pool)
{
  const char *local_relpath;
  svn_wc__db_wcroot_t *wcroot;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  svn_error_clear(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                            db, local_dir_abspath, svn_sqlite__mode_readwrite,
                            scratch_pool, scratch_pool));

  apr_hash_set(wcroot->access_cache, local_dir_abspath,
               APR_HASH_KEY_STRING, NULL);
}


apr_hash_t *
svn_wc__db_temp_get_all_access(svn_wc__db_t *db,
                               apr_pool_t *result_pool)
{
  apr_hash_t *result = apr_hash_make(result_pool);
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(result_pool, db->dir_data);
       hi;
       hi = apr_hash_next(hi))
    {
      const svn_wc__db_pdh_t *pdh = svn__apr_hash_index_val(hi);

      /* This is highly redundant, 'cause many PDHs will have the same
         WCROOT. */
      result = apr_hash_overlay(result_pool, result,
                                pdh->wcroot->access_cache);
    }

  return result;
}


svn_error_t *
svn_wc__db_temp_borrow_sdb(svn_sqlite__db_t **sdb,
                           svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           svn_wc__db_openmode_t mode,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__mode_t smode;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));

  if (mode == svn_wc__db_openmode_readonly)
    smode = svn_sqlite__mode_readonly;
  else
    smode = svn_sqlite__mode_readwrite;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath,
                              db, local_dir_abspath, smode,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  *sdb = wcroot->sdb;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_conflict_victims(const apr_array_header_t **victims,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_array_header_t *new_victims;

  /* The parent should be a working copy directory. */
  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* ### This will be much easier once we have all conflicts in one
         field of actual*/

  /* Look for text, tree and property conflicts in ACTUAL */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_CONFLICT_VICTIMS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  new_victims = apr_array_make(result_pool, 0, sizeof(const char *));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);

      APR_ARRAY_PUSH(new_victims, const char *) =
                            svn_dirent_basename(child_relpath, result_pool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  *victims = new_victims;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_conflicts(const apr_array_header_t **conflicts,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_array_header_t *cflcts;

  /* The parent should be a working copy directory. */
  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* ### This will be much easier once we have all conflicts in one
         field of actual.*/

  /* First look for text and property conflicts in ACTUAL */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_CONFLICT_DETAILS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  cflcts = apr_array_make(result_pool, 4,
                           sizeof(svn_wc_conflict_description2_t*));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      const char *prop_reject;
      const char *conflict_old;
      const char *conflict_new;
      const char *conflict_working;
      const char *conflict_data;

      /* ### Store in description! */
      prop_reject = svn_sqlite__column_text(stmt, 0, result_pool);
      if (prop_reject)
        {
          svn_wc_conflict_description2_t *desc;

          desc  = svn_wc_conflict_description_create_prop2(local_abspath,
                                                           svn_node_unknown,
                                                           "",
                                                           result_pool);

          desc->their_file = prop_reject;

          APR_ARRAY_PUSH(cflcts, svn_wc_conflict_description2_t*) = desc;
        }

      conflict_old = svn_sqlite__column_text(stmt, 1, result_pool);
      conflict_new = svn_sqlite__column_text(stmt, 2, result_pool);
      conflict_working = svn_sqlite__column_text(stmt, 3, result_pool);

      if (conflict_old || conflict_new || conflict_working)
        {
          svn_wc_conflict_description2_t *desc
              = svn_wc_conflict_description_create_text2(local_abspath,
                                                         result_pool);

          desc->base_file = conflict_old;
          desc->their_file = conflict_new;
          desc->my_file = conflict_working;
          desc->merged_file = svn_dirent_basename(local_abspath, result_pool);

          APR_ARRAY_PUSH(cflcts, svn_wc_conflict_description2_t*) = desc;
        }

      conflict_data = svn_sqlite__column_text(stmt, 4, scratch_pool);
      if (conflict_data)
        {
          const svn_wc_conflict_description2_t *desc;
          const svn_skel_t *skel;

          skel = svn_skel__parse(conflict_data, strlen(conflict_data),
                                 scratch_pool);
          SVN_ERR(svn_wc__deserialize_conflict(&desc, skel,
                          svn_dirent_dirname(local_abspath, scratch_pool),
                          result_pool, scratch_pool));

          APR_ARRAY_PUSH(cflcts, const svn_wc_conflict_description2_t *) = desc;
        }
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  *conflicts = cflcts;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_read_kind(svn_wc__db_kind_t *kind,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     svn_boolean_t allow_missing,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt_info;
  svn_boolean_t have_info;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt_info, wcroot->sdb,
                                    STMT_SELECT_NODE_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt_info, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_info, stmt_info));

  if (!have_info)
    {
      if (allow_missing)
        {
          *kind = svn_wc__db_kind_unknown;
          SVN_ERR(svn_sqlite__reset(stmt_info));
          return SVN_NO_ERROR;
        }
      else
        {
          SVN_ERR(svn_sqlite__reset(stmt_info));
          return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                   _("The node '%s' was not found."),
                                   path_for_error_message(wcroot,
                                                          local_relpath,
                                                          scratch_pool));
        }
    }

  *kind = svn_sqlite__column_token(stmt_info, 4, kind_map);

  return svn_error_return(svn_sqlite__reset(stmt_info));
}


svn_error_t *
svn_wc__db_node_hidden(svn_boolean_t *hidden,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_wc__db_status_t base_status;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  /* This uses an optimisation that first reads the working node and
     then may read the base node.  It could call svn_wc__db_read_info
     but that would always read both nodes. */

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* First check the working node. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      /* Note: this can ONLY be an add/copy-here/move-here. It is not
         possible to delete a "hidden" node.  */
      svn_wc__db_status_t work_status =
                            svn_sqlite__column_token(stmt, 1, presence_map);
      *hidden = (work_status == svn_wc__db_status_excluded);
      SVN_ERR(svn_sqlite__reset(stmt));
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  /* Now check the BASE node's status.  */
  SVN_ERR(base_get_info(&base_status,
                        NULL, NULL, NULL, NULL, NULL, NULL,
                        NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                        wcroot, local_relpath,
                        scratch_pool, scratch_pool));

  *hidden = (base_status == svn_wc__db_status_absent
             || base_status == svn_wc__db_status_not_present
             || base_status == svn_wc__db_status_excluded);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_is_wcroot(svn_boolean_t *is_root,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  if (*local_relpath != '\0')
    {
      *is_root = FALSE; /* Node is a file, or has a parent directory within
                           the same wcroot */
      return SVN_NO_ERROR;
    }

   *is_root = TRUE;

   return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_wcroot_tempdir(const char **temp_dir_abspath,
                               svn_wc__db_t *db,
                               const char *wri_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(temp_dir_abspath != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              wri_abspath, svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  *temp_dir_abspath = svn_dirent_join_many(result_pool,
                                           wcroot->abspath,
                                           svn_wc_get_adm_dir(scratch_pool),
                                           WCROOT_TEMPDIR_RELPATH,
                                           NULL);
  return SVN_NO_ERROR;
}

/* Baton for wclock_obtain_cb() */
struct wclock_obtain_baton
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  int levels_to_lock;
  svn_boolean_t steal_lock;
};

/* Helper for wclock_obtain_cb() to steal an existing lock */
static svn_error_t *
wclock_steal(svn_wc__db_wcroot_t *wcroot,
             const char *local_relpath,
             apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb, STMT_DELETE_WC_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}

/* svn_sqlite__transaction_callback_t for svn_wc__db_wclock_obtain() */
static svn_error_t *
wclock_obtain_cb(void *baton,
                 svn_sqlite__db_t* sdb,
                 apr_pool_t *scratch_pool)
{
  struct wclock_obtain_baton *bt = baton;
  svn_sqlite__stmt_t *stmt;
  svn_error_t *err;
  const char *lock_relpath;
  int max_depth;
  int lock_depth;
  svn_boolean_t got_row;
  const char *filter;

  svn_wc__db_wclock_t lock;

  /* Upgrade locks the root before the node exists.  Apart from that
     the root node always exists so we will just skip the check.

     ### Perhaps the lock for upgrade should be created when the db is
         created?  1.6 used to lock .svn on creation. */
  if (bt->local_relpath[0])
    {
      svn_boolean_t have_base, have_working;
      SVN_ERR(which_trees_exist(&have_base, &have_working, sdb,
                                bt->wcroot->wc_id, bt->local_relpath));

      if (!have_base && !have_working)
        return svn_error_createf(
                                 SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                 _("The node '%s' was not found."),
                                 path_for_error_message(bt->wcroot,
                                                        bt->local_relpath,
                                                        scratch_pool));
    }

  filter = construct_like_arg(bt->local_relpath, scratch_pool);

  /* Check if there are nodes locked below the new lock root */
  SVN_ERR(svn_sqlite__get_statement(&stmt, bt->wcroot->sdb,
                                    STMT_FIND_WC_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", bt->wcroot->wc_id, filter));

  lock_depth = relpath_depth(bt->local_relpath);
  max_depth = lock_depth + bt->levels_to_lock;

  SVN_ERR(svn_sqlite__step(&got_row, stmt));

  while (got_row)
    {
      svn_boolean_t own_lock;

      lock_relpath = svn_sqlite__column_text(stmt, 0, scratch_pool);

      /* If we are not locking with depth infinity, check if this lock
         voids our lock request */
      if (bt->levels_to_lock >= 0
          && relpath_depth(lock_relpath) > max_depth)
        {
          SVN_ERR(svn_sqlite__step(&got_row, stmt));
          continue;
        }

      /* Check if we are the lock owner, because we should be able to
         extend our lock. */
      err = wclock_owns_lock(&own_lock, bt->wcroot, lock_relpath,
                             TRUE, scratch_pool);

      if (err)
        SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));

      if (!own_lock && !bt->steal_lock)
        {
          SVN_ERR(svn_sqlite__reset(stmt));
          err = svn_error_createf(SVN_ERR_WC_LOCKED, NULL,
                                   _("'%s' is already locked."),
                                   path_for_error_message(bt->wcroot,
                                                          lock_relpath,
                                                          scratch_pool));
          return svn_error_createf(SVN_ERR_WC_LOCKED, err,
                                   _("Working copy '%s' locked."),
                                   path_for_error_message(bt->wcroot,
                                                          bt->local_relpath,
                                                          scratch_pool));
        }
      else if (!own_lock)
        {
          err = wclock_steal(bt->wcroot, lock_relpath, scratch_pool);

          if (err)
            SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
        }

      SVN_ERR(svn_sqlite__step(&got_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  if (bt->steal_lock)
    SVN_ERR(wclock_steal(bt->wcroot, bt->local_relpath, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, bt->wcroot->sdb,
                                    STMT_SELECT_WC_LOCK));
  lock_relpath = bt->local_relpath;

  while (TRUE)
    {
      SVN_ERR(svn_sqlite__bindf(stmt, "is", bt->wcroot->wc_id, lock_relpath));

      SVN_ERR(svn_sqlite__step(&got_row, stmt));

      if (got_row)
        {
          int levels = svn_sqlite__column_int(stmt, 0);
          if (levels >= 0)
            levels += relpath_depth(lock_relpath);

          SVN_ERR(svn_sqlite__reset(stmt));

          if (levels == -1 || levels >= lock_depth)
            {

              err = svn_error_createf(
                              SVN_ERR_WC_LOCKED, NULL,
                              _("'%s' is already locked."),
                              svn_dirent_local_style(
                                       svn_dirent_join(bt->wcroot->abspath,
                                                       lock_relpath,
                                                       scratch_pool),
                              scratch_pool));
              return svn_error_createf(
                              SVN_ERR_WC_LOCKED, err,
                              _("Working copy '%s' locked."),
                              path_for_error_message(bt->wcroot,
                                                     bt->local_relpath,
                                                     scratch_pool));
            }

          break; /* There can't be interesting locks on higher nodes */
        }
      else
        SVN_ERR(svn_sqlite__reset(stmt));

      if (!*lock_relpath)
        break;

      lock_relpath = svn_relpath_dirname(lock_relpath, scratch_pool);
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, bt->wcroot->sdb,
                                    STMT_INSERT_WC_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "isi", bt->wcroot->wc_id,
                            bt->local_relpath,
                            (apr_int64_t) bt->levels_to_lock));
  err = svn_sqlite__insert(NULL, stmt);
  if (err)
    return svn_error_createf(SVN_ERR_WC_LOCKED, err,
                             _("Working copy '%s' locked"),
                             path_for_error_message(bt->wcroot,
                                                    bt->local_relpath,
                                                    scratch_pool));

  /* And finally store that we obtained the lock */
  lock.local_relpath = apr_pstrdup(bt->wcroot->owned_locks->pool,
                                   bt->local_relpath);
  lock.levels = bt->levels_to_lock;
  APR_ARRAY_PUSH(bt->wcroot->owned_locks, svn_wc__db_wclock_t) = lock;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_wclock_obtain(svn_wc__db_t *db,
                         const char *local_abspath,
                         int levels_to_lock,
                         svn_boolean_t steal_lock,
                         apr_pool_t *scratch_pool)
{
  struct wclock_obtain_baton baton;

  SVN_ERR_ASSERT(levels_to_lock >= -1);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&baton.wcroot,
                                             &baton.local_relpath,
                                             db, local_abspath,
                                             svn_sqlite__mode_readwrite,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(baton.wcroot);

  if (!steal_lock)
    {
      int i;
      int depth = relpath_depth(baton.local_relpath);

      for (i = 0; i < baton.wcroot->owned_locks->nelts; i++)
        {
          svn_wc__db_wclock_t* lock = &APR_ARRAY_IDX(baton.wcroot->owned_locks,
                                                     i, svn_wc__db_wclock_t);

          if (svn_relpath_is_ancestor(lock->local_relpath, baton.local_relpath)
              && (lock->levels == -1
                  || (lock->levels + relpath_depth(lock->local_relpath))
                            >= depth))
            {
              return svn_error_createf(
                SVN_ERR_WC_LOCKED, NULL,
                _("'%s' is already locked via '%s'."),
                svn_dirent_local_style(local_abspath, scratch_pool),
                path_for_error_message(baton.wcroot, lock->local_relpath,
                                       scratch_pool));
            }
        }
    }

  baton.steal_lock = steal_lock;
  baton.levels_to_lock = levels_to_lock;

  return svn_error_return(
            svn_sqlite__with_transaction(baton.wcroot->sdb,
                                         wclock_obtain_cb,
                                         &baton,
                                         scratch_pool));
}


/* */
static svn_error_t *
is_wclocked(svn_boolean_t *locked,
            svn_wc__db_t *db,
            const char *local_abspath,
            apr_int64_t recurse_depth,
            apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err;

  err = get_statement_for_path(&stmt, db, local_abspath,
                               STMT_SELECT_WC_LOCK, scratch_pool);
  if (err && SVN_WC__ERR_IS_NOT_CURRENT_WC(err))
    {
      svn_error_clear(err);
      *locked = FALSE;
      return SVN_NO_ERROR;
    }
  else
    SVN_ERR(err);

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      apr_int64_t locked_levels = svn_sqlite__column_int64(stmt, 0);

      /* The directory in question is considered locked if we find a lock
         with depth -1 or the depth of the lock is greater than or equal to
         the depth we've recursed. */
      *locked = (locked_levels == -1 || locked_levels >= recurse_depth);
      return svn_error_return(svn_sqlite__reset(stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  if (svn_dirent_is_root(local_abspath, strlen(local_abspath)))
    {
      *locked = FALSE;
      return SVN_NO_ERROR;
    }

  return svn_error_return(is_wclocked(locked, db,
                                      svn_dirent_dirname(local_abspath,
                                                         scratch_pool),
                                      recurse_depth + 1, scratch_pool));
}


svn_error_t *
svn_wc__db_wclocked(svn_boolean_t *locked,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    apr_pool_t *scratch_pool)
{
  return svn_error_return(is_wclocked(locked, db, local_abspath, 0,
                                      scratch_pool));
}


svn_error_t *
svn_wc__db_wclock_release(svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  int i;
  apr_array_header_t *owned_locks;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  VERIFY_USABLE_WCROOT(wcroot);

  /* First check and remove the owns-lock information as failure in
     removing the db record implies that we have to steal the lock later. */
  owned_locks = wcroot->owned_locks;
  for (i = 0; i < owned_locks->nelts; i++)
    {
      svn_wc__db_wclock_t *lock = &APR_ARRAY_IDX(owned_locks, i,
                                                 svn_wc__db_wclock_t);

      if (strcmp(lock->local_relpath, local_relpath) == 0)
        break;
    }

  if (i >= owned_locks->nelts)
    return svn_error_createf(SVN_ERR_WC_NOT_LOCKED, NULL,
                             _("Working copy not locked at '%s'."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  if (i < owned_locks->nelts)
    {
      owned_locks->nelts--;

      /* Move the last item in the array to the deleted place */
      if (owned_locks->nelts > 0)
        APR_ARRAY_IDX(owned_locks, i, svn_wc__db_wclock_t) =
           APR_ARRAY_IDX(owned_locks, owned_locks->nelts, svn_wc__db_wclock_t);
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_DELETE_WC_LOCK));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}

/* Like svn_wc__db_wclock_owns_lock() but taking WCROOT+LOCAL_RELPATH instead
 * of DB+LOCAL_ABSPATH. */
static svn_error_t *
wclock_owns_lock(svn_boolean_t *own_lock,
                 svn_wc__db_wcroot_t *wcroot,
                 const char *local_relpath,
                 svn_boolean_t exact,
                 apr_pool_t *scratch_pool)
{
  apr_array_header_t *owned_locks;
  int lock_level, i;

  *own_lock = FALSE;
  owned_locks = wcroot->owned_locks;
  lock_level = relpath_depth(local_relpath);

  if (exact)
    for (i = 0; i < owned_locks->nelts; i++)
      {
        svn_wc__db_wclock_t *lock = &APR_ARRAY_IDX(owned_locks, i,
                                                   svn_wc__db_wclock_t);

        if (strcmp(lock->local_relpath, local_relpath) == 0)
          {
            *own_lock = TRUE;
            return SVN_NO_ERROR;
          }
      }
  else
    for (i = 0; i < owned_locks->nelts; i++)
      {
        svn_wc__db_wclock_t* lock = &APR_ARRAY_IDX(owned_locks, i,
                                                   svn_wc__db_wclock_t);

        if (svn_relpath_is_ancestor(lock->local_relpath, local_relpath)
            && (lock->levels == -1
                || ((relpath_depth(lock->local_relpath) + lock->levels)
                            >= lock_level)))
          {
            *own_lock = TRUE;
            return SVN_NO_ERROR;
          }
      }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_wclock_owns_lock(svn_boolean_t *own_lock,
                            svn_wc__db_t *db,
                            const char *local_abspath,
                            svn_boolean_t exact,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  if (!wcroot)
    return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  VERIFY_USABLE_WCROOT(wcroot);

  SVN_ERR(wclock_owns_lock(own_lock, wcroot, local_relpath, exact,
                           scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_op_set_base_incomplete(svn_wc__db_t *db,
                                       const char *local_dir_abspath,
                                       svn_boolean_t incomplete,
                                       apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  int affected_rows;
  int affected_node_rows;
  svn_wc__db_status_t base_status;

  SVN_ERR(svn_wc__db_base_get_info(&base_status, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL,
                                   db, local_dir_abspath,
                                   scratch_pool, scratch_pool));

  SVN_ERR_ASSERT(base_status == svn_wc__db_status_normal ||
                 base_status == svn_wc__db_status_incomplete);

  SVN_ERR(get_statement_for_path(&stmt, db, local_dir_abspath,
                                 STMT_UPDATE_NODE_BASE_PRESENCE, scratch_pool));
  SVN_ERR(svn_sqlite__bind_text(stmt, 3, incomplete ? "incomplete" : "normal"));
  SVN_ERR(svn_sqlite__update(&affected_node_rows, stmt));

  affected_rows = affected_node_rows;

  if (affected_rows > 0)
   {
     svn_wc__db_pdh_t *pdh = svn_wc__db_pdh_get_or_create(db,
                                    local_dir_abspath, FALSE, scratch_pool);

     if (pdh != NULL)
       SVN_ERR(flush_entries(db, local_dir_abspath, scratch_pool));
   }

  return SVN_NO_ERROR;
}

struct start_directory_update_baton
{
  svn_wc__db_t *db;
  const char *local_abspath;
  apr_int64_t wc_id;
  const char *local_relpath;
  svn_revnum_t new_rev;
  const char *new_repos_relpath;
};

static svn_error_t *
start_directory_update_txn(void *baton,
                           svn_sqlite__db_t *db,
                           apr_pool_t *scratch_pool)
{
  struct start_directory_update_baton *du = baton;
  svn_sqlite__stmt_t *stmt;

  /* Note: In the majority of calls, the repos_relpath is unchanged. */
  /* ### TODO: Maybe check if we can make repos_relpath NULL. */
  SVN_ERR(svn_sqlite__get_statement(
               &stmt, db,
               STMT_UPDATE_BASE_NODE_PRESENCE_REVNUM_AND_REPOS_PATH));

  SVN_ERR(svn_sqlite__bindf(stmt, "istrs",
                            du->wc_id,
                            du->local_relpath,
                            presence_map, svn_wc__db_status_incomplete,
                            du->new_rev,
                            du->new_repos_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_op_start_directory_update(svn_wc__db_t *db,
                                          const char *local_abspath,
                                          const char *new_repos_relpath,
                                          svn_revnum_t new_rev,
                                          apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  struct start_directory_update_baton du;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_rev));
  SVN_ERR_ASSERT(svn_relpath_is_canonical(new_repos_relpath, scratch_pool));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &du.local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  du.db = db;
  du.wc_id = wcroot->wc_id;
  du.local_abspath = local_abspath;
  du.new_rev = new_rev;
  du.new_repos_relpath = new_repos_relpath;

  SVN_ERR(svn_sqlite__with_transaction(wcroot->sdb,
                                       start_directory_update_txn, &du,
                                       scratch_pool));

  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

/* Baton for make_copy_txn */
struct make_copy_baton
{
  svn_wc__db_t *db;
  const char *local_abspath;

  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  apr_int64_t op_depth;
};

/* Transaction callback for svn_wc__db_temp_op_make_copy.  This is
   used by the update editor when deleting a base node tree would be a
   tree-conflict because there are changes to subtrees.  This function
   inserts a copy of the base node tree below any existing working
   subtrees.  Given a tree:

             0            1           2            3
    /     normal          -
    A     normal          -
    A/B   normal          -         normal
    A/B/C normal          -         normal
    A/F   normal          -         normal
    A/F/G normal          -         normal
    A/F/H normal          -         base-deleted   normal
    A/F/E normal          -         not-present
    A/X   normal          -
    A/X/Y incomplete      -

    This function copies the tree for A from op_depth=0, into the
    working op_depth of A, i.e. 1, then marks as base-deleted any
    subtrees in that op_depth that are below higher op_depth, and
    finally removes base-deleted nodes from higher op_depth.

             0            1              2            3
    /     normal          -
    A     normal       normal
    A/B   normal       base-deleted   normal
    A/B/C normal       base-deleted   normal
    A/F   normal       base-deleted   normal
    A/F/G normal       base-deleted   normal
    A/F/H normal       base-deleted                   normal
    A/F/E normal       base-deleted   not-present
    A/X   normal       normal
    A/X/Y incomplete   incomplete

 */
static svn_error_t *
make_copy_txn(void *baton,
              svn_sqlite__db_t *sdb,
              apr_pool_t *scratch_pool)
{
  struct make_copy_baton *mcb = baton;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_boolean_t add_working_base_deleted = FALSE;
  svn_boolean_t remove_working = FALSE;
  const apr_array_header_t *children;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_SELECT_LOWEST_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", mcb->wcroot->wc_id,
                            mcb->local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      svn_wc__db_status_t working_status;

      working_status = svn_sqlite__column_token(stmt, 1, presence_map);
      SVN_ERR(svn_sqlite__reset(stmt));

      SVN_ERR_ASSERT(working_status == svn_wc__db_status_normal
                     || working_status == svn_wc__db_status_base_deleted
                     || working_status == svn_wc__db_status_not_present
                     || working_status == svn_wc__db_status_incomplete);

      if (working_status == svn_wc__db_status_base_deleted)
        /* Make existing deletions of BASE_NODEs remove WORKING_NODEs */
        remove_working = TRUE;

      add_working_base_deleted = TRUE;
    }
  else
    SVN_ERR(svn_sqlite__reset(stmt));

  /* Get the BASE children, as WORKING children don't need modifications */
  SVN_ERR(gather_repo_children(&children, mcb->wcroot, mcb->local_relpath,
                               0, scratch_pool, iterpool));

  for (i = 0; i < children->nelts; i++)
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);
      struct make_copy_baton cbt;

      svn_pool_clear(iterpool);
      cbt.local_abspath = svn_dirent_join(mcb->local_abspath, name, iterpool);

      SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&cbt.wcroot,
                                  &cbt.local_relpath, mcb->db,
                                  cbt.local_abspath,
                                  svn_sqlite__mode_readwrite,
                                  iterpool, iterpool));

      VERIFY_USABLE_WCROOT(cbt.wcroot);

      cbt.db = mcb->db;
      cbt.op_depth = mcb->op_depth;

      SVN_ERR(make_copy_txn(&cbt, cbt.wcroot->sdb, iterpool));
    }

  if (remove_working)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_DELETE_LOWEST_WORKING_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                mcb->wcroot->wc_id,
                                mcb->local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  if (add_working_base_deleted)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                             STMT_INSERT_WORKING_NODE_FROM_BASE_COPY_PRESENCE));
      SVN_ERR(svn_sqlite__bindf(stmt, "isit",
                                mcb->wcroot->wc_id,
                                mcb->local_relpath,
                                mcb->op_depth,
                                presence_map, svn_wc__db_status_base_deleted));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  else
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                      STMT_INSERT_WORKING_NODE_FROM_BASE_COPY));
      SVN_ERR(svn_sqlite__bindf(stmt, "isi",
                                mcb->wcroot->wc_id,
                                mcb->local_relpath,
                                mcb->op_depth));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  SVN_ERR(flush_entries(mcb->db, mcb->local_abspath, iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_op_make_copy(svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *scratch_pool)
{
  struct make_copy_baton mcb;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&mcb.wcroot,
                              &mcb.local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(mcb.wcroot);

  /* The update editor is supposed to call this function when there is
     no working node for LOCAL_ABSPATH. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, mcb.wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", mcb.wcroot->wc_id,
                            mcb.local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));
  if (have_row)
    return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                             _("Modification of '%s' already exists"),
                             path_for_error_message(mcb.wcroot,
                                                    mcb.local_relpath,
                                                    scratch_pool));

  /* We don't allow copies to contain absent (denied by authz) nodes;
     the update editor is going to have to bail out. */
  SVN_ERR(catch_copy_of_absent(mcb.wcroot, mcb.local_relpath,
                               scratch_pool));

  mcb.db = db;
  mcb.local_abspath = local_abspath;
  mcb.op_depth = relpath_depth(mcb.local_relpath);

  SVN_ERR(svn_sqlite__with_transaction(mcb.wcroot->sdb,
                                       make_copy_txn, &mcb,
                                       scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_get_file_external(const char **serialized_file_external,
                                  svn_wc__db_t *db,
                                  const char *local_abspath,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_SELECT_FILE_EXTERNAL, scratch_pool));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  /* ### file externals are pretty bogus right now. they have just a
     ### WORKING_NODE for a while, eventually settling into just a BASE_NODE.
     ### until we get all that fixed, let's just not worry about raising
     ### an error, and just say it isn't a file external.  */
#if 1
  if (!have_row)
    *serialized_file_external = NULL;
  else
    /* see below: *serialized_file_external = ...  */
#else
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                             svn_sqlite__reset(stmt),
                             _("'%s' has no BASE_NODE"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));
#endif

  *serialized_file_external = svn_sqlite__column_text(stmt, 0, result_pool);

  return svn_error_return(svn_sqlite__reset(stmt));
}

svn_error_t *
svn_wc__db_temp_op_set_file_external(svn_wc__db_t *db,
                                     const char *local_abspath,
                                     const char *repos_relpath,
                                     const svn_opt_revision_t *peg_rev,
                                     const svn_opt_revision_t *rev,
                                     apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t got_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(!repos_relpath
                 || svn_relpath_is_canonical(repos_relpath, scratch_pool));

  SVN_ERR(svn_wc__db_pdh_parse_local_abspath(&pdh, &local_relpath, db,
                                             local_abspath,
                                             svn_sqlite__mode_readwrite,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(pdh->wcroot);

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&got_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (!got_row)
    {
      const char *repos_root_url, *repos_uuid;
      apr_int64_t repos_id;

      if (!repos_relpath)
        return SVN_NO_ERROR; /* Don't add a BASE node */

      SVN_ERR(svn_wc__db_scan_base_repos(NULL, &repos_root_url,
                                         &repos_uuid, db, pdh->local_abspath,
                                         scratch_pool, scratch_pool));

      SVN_ERR(fetch_repos_id(&repos_id, repos_root_url, repos_uuid,
                             pdh->wcroot->sdb, scratch_pool));

      SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                        STMT_INSERT_NODE));

      SVN_ERR(svn_sqlite__bindf(stmt, "isisisntnt",
                                pdh->wcroot->wc_id,
                                local_relpath,
                                (apr_int64_t)0, /* op_depth == BASE */
                                svn_relpath_dirname(local_relpath,
                                                    scratch_pool),
                                repos_id,
                                repos_relpath,
                                presence_map, svn_wc__db_status_not_present,
                                kind_map, svn_wc__db_kind_file));

      SVN_ERR(svn_sqlite__insert(NULL, stmt));
    }

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_UPDATE_FILE_EXTERNAL));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id,
                            local_relpath));
  if (repos_relpath)
    {
      const char *str;

      SVN_ERR(svn_wc__serialize_file_external(&str,
                                              repos_relpath,
                                              peg_rev,
                                              rev,
                                              scratch_pool));

      SVN_ERR(svn_sqlite__bind_text(stmt, 3, str));
    }
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_op_set_text_conflict_marker_files(svn_wc__db_t *db,
                                                  const char *local_abspath,
                                                  const char *old_basename,
                                                  const char *new_basename,
                                                  const char *wrk_basename,
                                                  apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t got_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                             local_abspath,
                                             svn_sqlite__mode_readwrite,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* This should be handled in a transaction, but we can assume a db lock
     and this code won't survive until 1.7 */

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&got_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (got_row)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_UPDATE_ACTUAL_TEXT_CONFLICTS));
    }
  else if (old_basename == NULL
           && new_basename == NULL
           && wrk_basename == NULL)
    {
      return SVN_NO_ERROR; /* We don't have to add anything */
    }
  else
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_INSERT_ACTUAL_TEXT_CONFLICTS));

      SVN_ERR(svn_sqlite__bind_text(stmt, 6,
                                    svn_relpath_dirname(local_relpath,
                                                        scratch_pool)));
    }

  SVN_ERR(svn_sqlite__bindf(stmt, "issss", wcroot->wc_id,
                                           local_relpath,
                                           old_basename,
                                           new_basename,
                                           wrk_basename));

  return svn_error_return(svn_sqlite__step_done(stmt));
}

/* Set the conflict marker information on LOCAL_ABSPATH to the specified
   values */
svn_error_t *
svn_wc__db_temp_op_set_property_conflict_marker_file(svn_wc__db_t *db,
                                                     const char *local_abspath,
                                                     const char *prej_basename,
                                                     apr_pool_t *scratch_pool)
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t got_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                                             local_abspath,
                                             svn_sqlite__mode_readwrite,
                                             scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);

  /* This should be handled in a transaction, but we can assume a db locl\
     and this code won't survive until 1.7 */

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&got_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (got_row)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_UPDATE_ACTUAL_PROPERTY_CONFLICTS));
    }
  else if (!prej_basename)
    return SVN_NO_ERROR;
  else
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_INSERT_ACTUAL_PROPERTY_CONFLICTS));

      if (*local_relpath != '\0')
        SVN_ERR(svn_sqlite__bind_text(stmt, 4,
                                      svn_relpath_dirname(local_relpath,
                                                          scratch_pool)));
    }

  SVN_ERR(svn_sqlite__bindf(stmt, "iss", wcroot->wc_id,
                                         local_relpath,
                                         prej_basename));

  return svn_error_return(svn_sqlite__step_done(stmt));
}

/* Baton for set_rev_relpath_txn */
struct set_rev_relpath_baton
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  svn_revnum_t rev;
  svn_boolean_t set_repos_relpath;
  const char *repos_relpath;
  const char *repos_root_url;
  const char *repos_uuid;
};

/* Implements svn_sqlite__transaction_callback_t for handling
   svn_wc__db_temp_op_set_rev_and_repos_relpath() db operations. */
static svn_error_t *
set_rev_relpath_txn(void *baton,
                    svn_sqlite__db_t *sdb,
                    apr_pool_t *scratch_pool)
{
  struct set_rev_relpath_baton *rrb = baton;
  svn_sqlite__stmt_t *stmt;

  if (SVN_IS_VALID_REVNUM(rrb->rev))
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_UPDATE_BASE_REVISION));

      SVN_ERR(svn_sqlite__bindf(stmt, "isr", rrb->wcroot->wc_id,
                                             rrb->local_relpath,
                                             rrb->rev));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  if (rrb->set_repos_relpath)
    {
      apr_int64_t repos_id;
      SVN_ERR(create_repos_id(&repos_id, rrb->repos_root_url, rrb->repos_uuid,
                              rrb->wcroot->sdb, scratch_pool));

      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_UPDATE_BASE_REPOS));

      SVN_ERR(svn_sqlite__bindf(stmt, "isis", rrb->wcroot->wc_id,
                                              rrb->local_relpath,
                                              repos_id,
                                              rrb->repos_relpath));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_op_set_rev_and_repos_relpath(svn_wc__db_t *db,
                                             const char *local_abspath,
                                             svn_revnum_t rev,
                                             svn_boolean_t set_repos_relpath,
                                             const char *repos_relpath,
                                             const char *repos_root_url,
                                             const char *repos_uuid,
                                             apr_pool_t *scratch_pool)
{
  struct set_rev_relpath_baton baton;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(rev) || set_repos_relpath);

  baton.rev = rev;
  baton.set_repos_relpath = set_repos_relpath;
  baton.repos_relpath = repos_relpath;
  baton.repos_root_url = repos_root_url;
  baton.repos_uuid = repos_uuid;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&baton.wcroot,
                                             &baton.local_relpath,
                                             db, local_abspath,
                                             svn_sqlite__mode_readwrite,
                                             scratch_pool, scratch_pool));

  VERIFY_USABLE_WCROOT(baton.wcroot);

  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  SVN_ERR(svn_sqlite__with_transaction(baton.wcroot->sdb,
                                       set_rev_relpath_txn,
                                       &baton, scratch_pool));

  return SVN_NO_ERROR;
}

struct set_new_dir_to_incomplete_baton
{
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;
  const char *repos_relpath;
  const char *repos_root_url;
  const char *repos_uuid;
  svn_revnum_t revision;
  svn_depth_t depth;
};

static svn_error_t *
set_new_dir_to_incomplete_txn(void *baton,
                              svn_sqlite__db_t *sdb,
                              apr_pool_t *scratch_pool)
{
  struct set_new_dir_to_incomplete_baton *dtb = baton;
  svn_sqlite__stmt_t *stmt;
  apr_int64_t repos_id;
  const char *parent_relpath = (*dtb->local_relpath == '\0')
                                  ? NULL
                                  : svn_relpath_dirname(dtb->local_relpath,
                                                        scratch_pool);

  SVN_ERR(create_repos_id(&repos_id, dtb->repos_root_url, dtb->repos_uuid,
                          sdb, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, dtb->wcroot->sdb,
                                    STMT_INSERT_NODE));

  SVN_ERR(svn_sqlite__bindf(stmt, "isis" /* 1 - 4 */
                            "isr" "sns", /* 5 - 7, 8, 9(n), 10 */
                            dtb->wcroot->wc_id,      /* 1 */
                            dtb->local_relpath,      /* 2 */
                            (apr_int64_t)0, /* op_depth == 0; BASE */
                            parent_relpath,          /* 4 */
                            repos_id,
                            dtb->repos_relpath,
                            dtb->revision,
                            "incomplete",            /* 8, presence */
                            "dir"));                 /* 10, kind */

  /* If depth is not unknown: record depth */
  if (dtb->depth >= svn_depth_empty && dtb->depth <= svn_depth_infinity)
    SVN_ERR(svn_sqlite__bind_text(stmt, 9, svn_depth_to_word(dtb->depth)));

  SVN_ERR(svn_sqlite__step_done(stmt));

  if (parent_relpath)
    SVN_ERR(extend_parent_delete(dtb->wcroot->sdb, dtb->wcroot->wc_id,
                                 dtb->local_relpath, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_op_set_new_dir_to_incomplete(svn_wc__db_t *db,
                                             const char *local_abspath,
                                             const char *repos_relpath,
                                             const char *repos_root_url,
                                             const char *repos_uuid,
                                             svn_revnum_t revision,
                                             svn_depth_t depth,
                                             apr_pool_t *scratch_pool)
{
  struct set_new_dir_to_incomplete_baton baton;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(repos_relpath && repos_root_url && repos_uuid);

  baton.repos_relpath = repos_relpath;
  baton.repos_root_url = repos_root_url;
  baton.repos_uuid = repos_uuid;
  baton.revision = revision;
  baton.depth = depth;

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&baton.wcroot,
                                             &baton.local_relpath,
                                             db, local_abspath,
                                             svn_sqlite__mode_readwrite,
                                             scratch_pool, scratch_pool));

  VERIFY_USABLE_WCROOT(baton.wcroot);

  SVN_ERR(flush_entries(db, local_abspath, scratch_pool));

  SVN_ERR(svn_sqlite__with_transaction(baton.wcroot->sdb,
                                       set_new_dir_to_incomplete_txn,
                                       &baton, scratch_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_below_work(svn_boolean_t *have_work,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_pool_t *scratch_pool)
{
  svn_boolean_t have_base;
  svn_wc__db_status_t status;
  svn_wc__db_wcroot_t *wcroot;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(svn_wc__db_wcroot_parse_local_abspath(&wcroot, &local_relpath, db,
                              local_abspath, svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_WCROOT(wcroot);
  SVN_ERR(info_below_working(&have_base, have_work, &status,
                             wcroot, local_relpath, scratch_pool));

  return SVN_NO_ERROR;
}
