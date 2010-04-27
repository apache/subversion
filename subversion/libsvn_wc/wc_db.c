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
#include "wc_db_pdh.h"

#include "svn_private_config.h"
#include "private/svn_sqlite.h"
#include "private/svn_skel.h"
#include "private/svn_wc_private.h"
#include "private/svn_token.h"


#define NOT_IMPLEMENTED() \
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.")


/*
 * Some filename constants.
 */
#define SDB_FILE  "wc.db"
#define SDB_FILE_UPGRADE "wc.db.upgrade"

#define PRISTINE_STORAGE_RELPATH "pristine"
#define PRISTINE_TEMPDIR_RELPATH ""
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

#define UNKNOWN_WC_ID ((apr_int64_t) -1)
#define FORMAT_FROM_SDB (-1)


/* Assert that the given PDH is usable.
   NOTE: the expression is multiply-evaluated!!  */
#define VERIFY_USABLE_PDH(pdh) SVN_ERR_ASSERT(  \
    (pdh)->wcroot != NULL                       \
    && (pdh)->wcroot->format == SVN_WC__VERSION)


/* Verify the checksum kind for pristine storage.  */
#define VERIFY_CHECKSUM_KIND(checksum)                                     \
  do {                                                                     \
    if ((checksum)->kind != svn_checksum_sha1)                             \
      return svn_error_create(SVN_ERR_BAD_CHECKSUM_KIND, NULL,             \
                              _("Only SHA1 checksums can be used as keys " \
                                "in the pristine file storage.\n"));       \
  } while (0)
/* ### not ready to enforce SHA1 yet. disable the check.  */
#undef VERIFY_CHECKSUM_KIND
#define VERIFY_CHECKSUM_KIND(checksum) ((void)0)


/* ### since we're putting the pristine files per-dir, then we don't need
   ### to create subdirectories in order to keep the directory size down.
   ### when we can aggregate pristine files across dirs/wcs, then we will
   ### need to undo the SKIP. */
#define SVN__SKIP_SUBDIR

WC_QUERIES_SQL_DECLARE_STATEMENTS(statements);


/* This is a character used to escape itself and the globbing character in
   globbing sql expressions below.  See escape_sqlite_like().

   NOTE: this should match the character used within wc-metadata.sql  */
#define LIKE_ESCAPE_CHAR     "#"


typedef struct {
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


typedef struct {
  /* common to all insertions into WORKING */
  svn_wc__db_status_t presence;
  svn_wc__db_kind_t kind;
  apr_int64_t wc_id;
  const char *local_relpath;

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
  { "subdir", svn_wc__db_kind_subdir },
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


/* */
static svn_filesize_t
get_translated_size(svn_sqlite__stmt_t *stmt, int slot)
{
  if (svn_sqlite__column_is_null(stmt, slot))
    return SVN_INVALID_FILESIZE;
  return svn_sqlite__column_int64(stmt, slot);
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


/* */
static svn_error_t *
verify_no_work(svn_sqlite__db_t *sdb)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_LOOK_FOR_WORK));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  if (have_row)
    return svn_error_create(SVN_ERR_WC_CLEANUP_REQUIRED, NULL,
                            NULL /* nothing to add.  */);

  return SVN_NO_ERROR;
}


/* */
static apr_status_t
close_wcroot(void *data)
{
  svn_wc__db_wcroot_t *wcroot = data;
  svn_error_t *err;

  SVN_ERR_ASSERT_NO_RETURN(wcroot->sdb != NULL);

  err = svn_sqlite__close(wcroot->sdb);
  wcroot->sdb = NULL;
  if (err)
    {
      apr_status_t result = err->apr_err;
      svn_error_clear(err);
      return result;
    }

  return APR_SUCCESS;
}


svn_error_t *
svn_wc__db_close_many_wcroots(apr_hash_t *roots,
                              apr_pool_t *state_pool,
                              apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;

  for (hi = apr_hash_first(scratch_pool, roots); hi; hi = apr_hash_next(hi))
    {
      svn_wc__db_wcroot_t *wcroot = svn__apr_hash_index_val(hi);
      apr_status_t result;

      result = apr_pool_cleanup_run(state_pool, wcroot, close_wcroot);
      if (result != APR_SUCCESS)
        return svn_error_wrap_apr(result, NULL);
    }

  return SVN_NO_ERROR;
}


/* Construct a new svn_wc__db_wcroot_t. The WCROOT_ABSPATH and SDB parameters
   must have lifetime of at least RESULT_POOL.  */
static svn_error_t *
create_wcroot(svn_wc__db_wcroot_t **wcroot,
              const char *wcroot_abspath,
              svn_sqlite__db_t *sdb,
              apr_int64_t wc_id,
              int format,
              svn_boolean_t auto_upgrade,
              svn_boolean_t enforce_empty_wq,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  if (sdb != NULL)
    SVN_ERR(svn_sqlite__read_schema_version(&format, sdb, scratch_pool));

  /* If we construct a wcroot, then we better have a format.  */
  SVN_ERR_ASSERT(format >= 1);

  /* If this working copy is PRE-1.0, then simply bail out.  */
  if (format < 4)
    {
      return svn_error_createf(
        SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
        _("Working copy format of '%s' is too old (%d); "
          "please check out your working copy again"),
        svn_dirent_local_style(wcroot_abspath, scratch_pool), format);
    }

  /* If this working copy is from a future version, then bail out.  */
  if (format > SVN_WC__VERSION)
    {
      return svn_error_createf(
        SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
        _("This client is too old to work with the working copy at\n"
          "'%s' (format %d).\n"
          "You need to get a newer Subversion client. For more details, see\n"
          "  http://subversion.apache.org/faq.html#working-copy-format-change\n"
          ),
        svn_dirent_local_style(wcroot_abspath, scratch_pool),
        format);
    }

  /* Auto-upgrade the SDB if possible.  */
  if (format < SVN_WC__VERSION && auto_upgrade)
    SVN_ERR(svn_wc__upgrade_sdb(&format, wcroot_abspath, sdb, format,
                                scratch_pool));

  /* Verify that no work items exists. If they do, then our integrity is
     suspect and, thus, we cannot use this database.  */
  if (format >= SVN_WC__HAS_WORK_QUEUE && enforce_empty_wq)
    SVN_ERR(verify_no_work(sdb));

  *wcroot = apr_palloc(result_pool, sizeof(**wcroot));

  (*wcroot)->abspath = wcroot_abspath;
  (*wcroot)->sdb = sdb;
  (*wcroot)->wc_id = wc_id;
  (*wcroot)->format = format;

  /* SDB will be NULL for pre-NG working copies. We only need to run a
     cleanup when the SDB is present.  */
  if (sdb != NULL)
    apr_pool_cleanup_register(result_pool, *wcroot, close_wcroot,
                              apr_pool_cleanup_null);
  return SVN_NO_ERROR;
}


/* Returns in PRISTINE_ABSPATH a new string allocated from RESULT_POOL,
   holding the local absolute path to the file location that is dedicated
   to hold CHECKSUM's pristine file, relating to the pristine store
   configured for the working copy indicated by PDH. The returned path
   does not necessarily currently exist.
#ifndef SVN__SKIP_SUBDIR
   Iff CREATE_SUBDIR is TRUE, then this function will make sure that the
   parent directory of PRISTINE_ABSPATH exists. This is only useful when
   about to create a new pristine.
#endif
   Any other allocations are made in SCRATCH_POOL. */
static svn_error_t *
get_pristine_fname(const char **pristine_abspath,
                   svn_wc__db_pdh_t *pdh,
                   const svn_checksum_t *checksum,
#ifndef SVN__SKIP_SUBDIR
                   svn_boolean_t create_subdir,
#endif
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *base_dir_abspath;
  const char *hexdigest = svn_checksum_to_cstring(checksum, scratch_pool);
#ifndef SVN__SKIP_SUBDIR
  char subdir[3];
#endif

  /* ### code is in transition. make sure we have the proper data.  */
  SVN_ERR_ASSERT(pdh->wcroot != NULL);
  SVN_ERR_ASSERT(checksum->kind == svn_checksum_sha1);

  /* ### need to fix this to use a symbol for ".svn". we don't need
     ### to use join_many since we know "/" is the separator for
     ### internal canonical paths */
  base_dir_abspath = svn_dirent_join_many(scratch_pool,
                                          pdh->wcroot->abspath,
                                          svn_wc_get_adm_dir(scratch_pool),
                                          PRISTINE_STORAGE_RELPATH,
                                          NULL);

  /* We should have a valid checksum and (thus) a valid digest. */
  SVN_ERR_ASSERT(hexdigest != NULL);

#ifndef SVN__SKIP_SUBDIR
  /* Get the first two characters of the digest, for the subdir. */
  subdir[0] = hexdigest[0];
  subdir[1] = hexdigest[1];
  subdir[2] = '\0';

  if (create_subdir)
    {
      const char *subdir_abspath = svn_dirent_join(base_dir_abspath, subdir,
                                                   scratch_pool);
      svn_error_t *err;

      err = svn_io_dir_make(subdir_abspath, APR_OS_DEFAULT, scratch_pool);

      /* Whatever error may have occurred... ignore it. Typically, this
         will be "directory already exists", but if it is something
         *different*, then presumably another error will follow when we
         try to access the file within this (missing?) pristine subdir. */
      svn_error_clear(err);
    }
#endif

  /* The file is located at DIR/.svn/pristine/XX/XXYYZZ... */
  *pristine_abspath = svn_dirent_join_many(result_pool,
                                           base_dir_abspath,
#ifndef SVN__SKIP_SUBDIR
                                           subdir,
#endif
                                           hexdigest,
                                           NULL);
  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
fetch_repos_info(const char **repos_root_url,
                 const char **repos_uuid,
                 svn_sqlite__db_t *sdb,
                 apr_int64_t repos_id,
                 apr_pool_t *result_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

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


/* Scan from LOCAL_RELPATH upwards through parent nodes until we find a parent
   that has values in the 'repos_id' and 'repos_relpath' columns.  Return
   that information in REPOS_ID and REPOS_RELPATH (either may be NULL).
   Use LOCAL_ABSPATH for diagnostics */
static svn_error_t *
scan_upwards_for_repos(apr_int64_t *repos_id,
                       const char **repos_relpath,
                       const svn_wc__db_wcroot_t *wcroot,
                       const char *local_abspath,
                       const char *local_relpath,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  const char *relpath_suffix = "";
  const char *current_basename = svn_dirent_basename(local_relpath,
                                                     scratch_pool);
  const char *current_relpath = local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(wcroot->sdb != NULL && wcroot->wc_id != UNKNOWN_WC_ID);
  SVN_ERR_ASSERT(repos_id != NULL || repos_relpath != NULL);

  /* ### is it faster to fetch fewer columns? */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_BASE_NODE));

  while (TRUE)
    {
      svn_boolean_t have_row;

      /* Get the current node's repository information.  */
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wcroot->wc_id, current_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (!have_row)
        {
          svn_error_t *err;

          /* If we moved upwards at least once, or we're looking at the
             root directory of this WCROOT, then something is wrong.  */
          if (*relpath_suffix != '\0' || *local_relpath == '\0')
            {
              err = svn_error_createf(
                SVN_ERR_WC_CORRUPT, NULL,
                _("Parent(s) of '%s' should have been present."),
                svn_dirent_local_style(local_abspath, scratch_pool));
            }
          else
            {
              err = svn_error_createf(
                SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                _("The node '%s' was not found."),
                svn_dirent_local_style(local_abspath, scratch_pool));
            }

          return svn_error_compose_create(err, svn_sqlite__reset(stmt));
        }

      /* Did we find some non-NULL repository columns? */
      if (!svn_sqlite__column_is_null(stmt, 0))
        {
          /* If one is non-NULL, then so should the other. */
          SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 1));

          if (repos_id)
            *repos_id = svn_sqlite__column_int64(stmt, 0);

          /* Given the node's relpath, append all the segments that
             we stripped as we scanned upwards. */
          if (repos_relpath)
            *repos_relpath = svn_relpath_join(svn_sqlite__column_text(stmt, 1,
                                                                      NULL),
                                              relpath_suffix,
                                              result_pool);
          return svn_sqlite__reset(stmt);
        }
      SVN_ERR(svn_sqlite__reset(stmt));

      if (*current_relpath == '\0')
        {
          /* We scanned all the way up, and did not find the information.
             Something is corrupt in the database. */
          return svn_error_createf(
            SVN_ERR_WC_CORRUPT, NULL,
            _("Parent(s) of '%s' should have repository information."),
            svn_relpath_local_style(local_abspath, scratch_pool));
        }

      /* Strip a path segment off the end, and append it to the suffix
         that we'll use when we finally find a base relpath.  */
      svn_relpath_split(current_relpath, &current_relpath, &current_basename,
                        scratch_pool);
      relpath_suffix = svn_relpath_join(relpath_suffix, current_basename,
                                        scratch_pool);

      /* Loop to try the parent.  */

      /* ### strictly speaking, moving to the parent could send us to a
         ### different SDB, and (thus) we would need to fetch STMT again.
         ### but we happen to know the parent is *always* in the same db,
         ### and will have the repos info.  */
    }
}


/* Get the format version from a wc-1 directory. If it is not a working copy
   directory, then it sets VERSION to zero and returns no error.  */
static svn_error_t *
get_old_version(int *version,
                const char *abspath,
                apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const char *format_file_path;

  /* Try reading the format number from the entries file.  */
  format_file_path = svn_wc__adm_child(abspath, SVN_WC__ADM_ENTRIES,
                                       scratch_pool);
  err = svn_io_read_version_file(version, format_file_path, scratch_pool);
  if (err == NULL)
    return SVN_NO_ERROR;
  if (err->apr_err != SVN_ERR_BAD_VERSION_FILE_FORMAT
      && !APR_STATUS_IS_ENOENT(err->apr_err)
      && !APR_STATUS_IS_ENOTDIR(err->apr_err))
    return svn_error_createf(SVN_ERR_WC_MISSING, err, _("'%s' does not exist"),
                             svn_dirent_local_style(abspath, scratch_pool));
  svn_error_clear(err);

  /* This must be a really old working copy!  Fall back to reading the
     format file.

     Note that the format file might not exist in newer working copies
     (format 7 and higher), but in that case, the entries file should
     have contained the format number. */
  format_file_path = svn_wc__adm_child(abspath, SVN_WC__ADM_FORMAT,
                                       scratch_pool);
  err = svn_io_read_version_file(version, format_file_path, scratch_pool);
  if (err == NULL)
    return SVN_NO_ERROR;

  /* Whatever error may have occurred... we can just ignore. This is not
     a working copy directory. Signal the caller.  */
  svn_error_clear(err);

  *version = 0;
  return SVN_NO_ERROR;
}


/* POOL may be NULL if the lifetime of LOCAL_ABSPATH is sufficient.  */
static const char *
compute_pdh_relpath(const svn_wc__db_pdh_t *pdh,
                    apr_pool_t *result_pool)
{
  const char *relpath = svn_dirent_is_child(pdh->wcroot->abspath,
                                            pdh->local_abspath,
                                            result_pool);
  if (relpath == NULL)
    return "";
  return relpath;
}


/* The filesystem has a directory at LOCAL_RELPATH. Examine the metadata
   to determine if a *file* was supposed to be there.

   ### this function is only required for per-dir .svn support. once all
   ### metadata is collected in a single wcroot, then we won't need to
   ### look in subdirs for other metadata.  */
static svn_error_t *
determine_obstructed_file(svn_boolean_t *obstructed_file,
                          const svn_wc__db_wcroot_t *wcroot,
                          const char *local_relpath,
                          apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(wcroot->sdb != NULL && wcroot->wc_id != UNKNOWN_WC_ID);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                    STMT_SELECT_WORKING_IS_FILE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is",
                            wcroot->wc_id,
                            local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      *obstructed_file = svn_sqlite__column_boolean(stmt, 0);
    }
  else
    {
      SVN_ERR(svn_sqlite__reset(stmt));

      SVN_ERR(svn_sqlite__get_statement(&stmt, wcroot->sdb,
                                        STMT_SELECT_BASE_IS_FILE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                wcroot->wc_id,
                                local_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (have_row)
        *obstructed_file = svn_sqlite__column_boolean(stmt, 0);
    }

  return svn_sqlite__reset(stmt);
}


/* */
static svn_error_t *
fetch_wc_id(apr_int64_t *wc_id,
            svn_sqlite__db_t *sdb,
            apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  /* ### cheat. we know there is just one WORKING_COPY row, and it has a
     ### NULL value for local_abspath. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_WCROOT_NULL));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_CORRUPT, svn_sqlite__reset(stmt),
                             _("Missing a row in WCROOT."));

  SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 0));
  *wc_id = svn_sqlite__column_int64(stmt, 0);

  return svn_error_return(svn_sqlite__reset(stmt));
}


/* */
static svn_error_t *
open_db(svn_sqlite__db_t **sdb,
        const char *dir_abspath,
        const char *sdb_fname,
        svn_sqlite__mode_t smode,
        apr_pool_t *result_pool,
        apr_pool_t *scratch_pool)
{
  const char *sdb_abspath = svn_wc__adm_child(dir_abspath, sdb_fname,
                                              scratch_pool);

  return svn_error_return(svn_sqlite__open(sdb, sdb_abspath,
                                           smode, statements,
                                           0, NULL,
                                           result_pool, scratch_pool));
}


/* For a given LOCAL_ABSPATH, figure out what sqlite database (PDH) to
   use and the RELPATH within that wcroot.  If a sqlite database needs
   to be opened, then use SMODE for it.

   *LOCAL_RELPATH will be allocated within RESULT_POOL. Temporary allocations
   will be made in SCRATCH_POOL.

   Certain internal structures will be allocated in DB->STATE_POOL.
*/
static svn_error_t *
parse_local_abspath(svn_wc__db_pdh_t **pdh,
                    const char **local_relpath,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    svn_sqlite__mode_t smode,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  const char *original_abspath = local_abspath;
  svn_node_kind_t kind;
  svn_boolean_t special;
  const char *build_relpath;
  svn_wc__db_pdh_t *found_pdh = NULL;
  svn_wc__db_pdh_t *child_pdh;
  svn_boolean_t obstruction_possible = FALSE;
  svn_sqlite__db_t *sdb;
  svn_boolean_t moved_upwards = FALSE;
  svn_boolean_t always_check = FALSE;
  int wc_format = 0;

  /* ### we need more logic for finding the database (if it is located
     ### outside of the wcroot) and then managing all of that within DB.
     ### for now: play quick & dirty. */

  /* ### for now, overwrite the provided mode.  We currently cache the
     ### sdb handles, which is great but for the occasion where we
     ### initially open the sdb in readonly mode and then later want
     ### to write to it.  The solution is to reopen the db in readwrite
     ### mode, but that assumes we can track the fact that it was
     ### originally opened readonly.  So for now, just punt and open
     ### everything in readwrite mode.  */
  smode = svn_sqlite__mode_readwrite;

  *pdh = apr_hash_get(db->dir_data, local_abspath, APR_HASH_KEY_STRING);
  if (*pdh != NULL && (*pdh)->wcroot != NULL)
    {
      /* We got lucky. Just return the thing BEFORE performing any I/O.  */
      /* ### validate SMODE against how we opened wcroot->sdb? and against
         ### DB->mode? (will we record per-dir mode?)  */

      /* ### for most callers, we could pass NULL for result_pool.  */
      *local_relpath = compute_pdh_relpath(*pdh, result_pool);

      return SVN_NO_ERROR;
    }

  /* ### at some point in the future, we may need to find a way to get
     ### rid of this stat() call. it is going to happen for EVERY call
     ### into wc_db which references a file. calls for directories could
     ### get an early-exit in the hash lookup just above.  */
  SVN_ERR(svn_io_check_special_path(local_abspath, &kind,
                                    &special /* unused */, scratch_pool));
  if (kind != svn_node_dir)
    {
      /* If the node specified by the path is NOT present, then it cannot
         possibly be a directory containing ".svn/wc.db".

         If it is a file, then it cannot contain ".svn/wc.db".

         For both of these cases, strip the basename off of the path and
         move up one level. Keep record of what we strip, though, since
         we'll need it later to construct local_relpath.  */
      svn_dirent_split(local_abspath, &local_abspath, &build_relpath,
                       scratch_pool);

      /* ### if *pdh != NULL (from further above), then there is (quite
         ### probably) a bogus value in the DIR_DATA hash table. maybe
         ### clear it out? but what if there is an access baton?  */

      /* Is this directory in our hash?  */
      *pdh = apr_hash_get(db->dir_data, local_abspath, APR_HASH_KEY_STRING);
      if (*pdh != NULL && (*pdh)->wcroot != NULL)
        {
          const char *dir_relpath;

          /* Stashed directory's local_relpath + basename. */
          dir_relpath = compute_pdh_relpath(*pdh, NULL);
          *local_relpath = svn_relpath_join(dir_relpath,
                                            build_relpath,
                                            result_pool);
          return SVN_NO_ERROR;
        }

      /* If the requested path is not on the disk, then we don't know how
         many ancestors need to be scanned until we start hitting content
         on the disk. Set ALWAYS_CHECK to keep looking for .svn/entries
         rather than bailing out after the first check.  */
      if (kind == svn_node_none)
        always_check = TRUE;
    }
  else
    {
      /* Start the local_relpath empty. If *this* directory contains the
         wc.db, then relpath will be the empty string.  */
      build_relpath = "";

      /* It is possible that LOCAL_ABSPATH was *intended* to be a file,
         but we just found a directory in its place. After we build
         the PDH, then we'll examine the parent to see how it describes
         this particular path.

         ### this is only possible with per-dir wc.db databases.  */
      obstruction_possible = TRUE;
    }

  /* LOCAL_ABSPATH refers to a directory at this point. The PDH corresponding
     to that directory is what we need to return. At this point, we've
     determined that a PDH with a discovered WCROOT is NOT in the DB's hash
     table of wcdirs. Let's fill in an existing one, or create one. Then
     go figure out where the WCROOT is.  */

  if (*pdh == NULL)
    {
      *pdh = apr_pcalloc(db->state_pool, sizeof(**pdh));
      (*pdh)->local_abspath = apr_pstrdup(db->state_pool, local_abspath);
    }
  else
    {
      /* The PDH should have been built correctly (so far).  */
      SVN_ERR_ASSERT(strcmp((*pdh)->local_abspath, local_abspath) == 0);
    }

  /* Assume that LOCAL_ABSPATH is a directory, and look for the SQLite
     database in the right place. If we find it... great! If not, then
     peel off some components, and try again. */

  while (TRUE)
    {
      svn_error_t *err;

      err = open_db(&sdb, local_abspath, SDB_FILE, smode,
                    db->state_pool, scratch_pool);
      if (err == NULL)
        break;
      if (err->apr_err != SVN_ERR_SQLITE_ERROR
          && !APR_STATUS_IS_ENOENT(err->apr_err))
        return svn_error_return(err);
      svn_error_clear(err);

      /* If we have not moved upwards, then check for a wc-1 working copy.
         Since wc-1 has a .svn in every directory, and we didn't find one
         in the original directory, then we aren't looking at a wc-1.

         If the original path is not present, then we have to check on every
         iteration. The content may be the immediate parent, or possibly
         five ancetors higher. We don't test for directory presence (just
         for the presence of subdirs/files), so we don't know when we can
         stop checking ... so just check always.  */
      if (!moved_upwards || always_check)
        {
          SVN_ERR(get_old_version(&wc_format, local_abspath, scratch_pool));
          if (wc_format != 0)
            break;
        }

      /* We couldn't open the SDB within the specified directory, so
         move up one more directory. */
      if (svn_dirent_is_root(local_abspath, strlen(local_abspath)))
        {
          /* Hit the root without finding a wcroot. */
          return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                                   _("'%s' is not a working copy"),
                                   svn_dirent_local_style(original_abspath,
                                                          scratch_pool));
        }

      local_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

      moved_upwards = TRUE;

      /* An obstruction is no longer possible.

         Example: we were given "/some/file" and "file" turned out to be
         a directory. We did not find an SDB at "/some/file/.svn/wc.db",
         so we are now going to look at "/some/.svn/wc.db". That SDB will
         contain the correct information for "file".

         ### obstruction is only possible with per-dir wc.db databases.  */
      obstruction_possible = FALSE;

      /* Is the parent directory recorded in our hash?  */
      found_pdh = apr_hash_get(db->dir_data,
                               local_abspath, APR_HASH_KEY_STRING);
      if (found_pdh != NULL)
        {
          if (found_pdh->wcroot != NULL)
            break;
          found_pdh = NULL;
        }
    }

  if (found_pdh != NULL)
    {
      /* We found a PDH with data in it. We can now construct the child
         from this, rather than continuing to scan upwards.  */

      /* The subdirectory uses the same WCROOT as the parent dir.  */
      (*pdh)->wcroot = found_pdh->wcroot;
    }
  else if (wc_format == 0)
    {
      /* We finally found the database. Construct the PDH record.  */

      apr_int64_t wc_id;
      svn_error_t *err;

      err = fetch_wc_id(&wc_id, sdb, scratch_pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_WC_CORRUPT)
            return svn_error_quick_wrap(
              err, apr_psprintf(scratch_pool,
                                _("Missing a row in WCROOT for '%s'."),
                                svn_dirent_local_style(original_abspath,
                                                       scratch_pool)));
          return svn_error_return(err);
        }

      /* WCROOT.local_abspath may be NULL when the database is stored
         inside the wcroot, but we know the abspath is this directory
         (ie. where we found it).  */

      SVN_ERR(create_wcroot(&(*pdh)->wcroot,
                            apr_pstrdup(db->state_pool, local_abspath),
                            sdb, wc_id, FORMAT_FROM_SDB,
                            db->auto_upgrade, db->enforce_empty_wq,
                            db->state_pool, scratch_pool));
    }
  else
    {
      /* We found a wc-1 working copy directory.  */
      SVN_ERR(create_wcroot(&(*pdh)->wcroot,
                            apr_pstrdup(db->state_pool, local_abspath),
                            NULL, UNKNOWN_WC_ID, wc_format,
                            db->auto_upgrade, db->enforce_empty_wq,
                            db->state_pool, scratch_pool));

      /* Don't test for a directory obstructing a versioned file. The wc-1
         code can manage that itself.  */
      obstruction_possible = FALSE;
    }

  {
    const char *dir_relpath;

    /* The subdirectory's relpath is easily computed relative to the
       wcroot that we just found.  */
    dir_relpath = compute_pdh_relpath(*pdh, NULL);

    /* And the result local_relpath may include a filename.  */
    *local_relpath = svn_relpath_join(dir_relpath, build_relpath, result_pool);
  }

  /* Check to see if this (versioned) directory is obstructing what should
     be a file in the parent directory.

     ### obstruction is only possible with per-dir wc.db databases.  */
  if (obstruction_possible)
    {
      const char *parent_dir;
      svn_wc__db_pdh_t *parent_pdh;

      /* We should NOT have moved up a directory.  */
      assert(!moved_upwards);

      /* Get/make a PDH for the parent.  */
      parent_dir = svn_dirent_dirname(local_abspath, scratch_pool);
      parent_pdh = apr_hash_get(db->dir_data, parent_dir, APR_HASH_KEY_STRING);
      if (parent_pdh == NULL || parent_pdh->wcroot == NULL)
        {
          svn_error_t *err = open_db(&sdb, parent_dir, SDB_FILE, smode,
                                     db->state_pool, scratch_pool);
          if (err)
            {
              if (err->apr_err != SVN_ERR_SQLITE_ERROR
                  && !APR_STATUS_IS_ENOENT(err->apr_err))
                return svn_error_return(err);
              svn_error_clear(err);

              /* No parent, so we're at a wcroot apparently. An obstruction
                 is (therefore) not possible.  */
              parent_pdh = NULL;
            }
          else
            {
              /* ### construct this according to per-dir semantics.  */
              if (parent_pdh == NULL)
                {
                  parent_pdh = apr_pcalloc(db->state_pool,
                                           sizeof(*parent_pdh));
                  parent_pdh->local_abspath = apr_pstrdup(db->state_pool,
                                                          parent_dir);
                }
              else
                {
                  /* The PDH should have been built correctly (so far).  */
                  SVN_ERR_ASSERT(strcmp(parent_pdh->local_abspath,
                                        parent_dir) == 0);
                }

              SVN_ERR(create_wcroot(&parent_pdh->wcroot,
                                    parent_pdh->local_abspath,
                                    sdb,
                                    1 /* ### hack.  */,
                                    FORMAT_FROM_SDB,
                                    db->auto_upgrade, db->enforce_empty_wq,
                                    db->state_pool, scratch_pool));

              apr_hash_set(db->dir_data,
                           parent_pdh->local_abspath, APR_HASH_KEY_STRING,
                           parent_pdh);

              (*pdh)->parent = parent_pdh;
            }
        }

      if (parent_pdh)
        {
          const char *lookfor_relpath = svn_dirent_basename(local_abspath,
                                                            scratch_pool);

          /* Was there supposed to be a file sitting here?  */
          SVN_ERR(determine_obstructed_file(&(*pdh)->obstructed_file,
                                            parent_pdh->wcroot,
                                            lookfor_relpath,
                                            scratch_pool));

          /* If we determined that a file was supposed to be at the
             LOCAL_ABSPATH requested, then return the PDH and LOCAL_RELPATH
             which describes that file.  */
          if ((*pdh)->obstructed_file)
            {
              *pdh = parent_pdh;
              *local_relpath = apr_pstrdup(result_pool, lookfor_relpath);
              return SVN_NO_ERROR;
            }
        }
    }

  /* The PDH is complete. Stash it into DB.  */
  apr_hash_set(db->dir_data,
               (*pdh)->local_abspath, APR_HASH_KEY_STRING,
               *pdh);

  /* Did we traverse up to parent directories?  */
  if (!moved_upwards)
    {
      /* We did NOT move to a parent of the original requested directory.
         We've constructed and filled in a PDH for the request, so we
         are done.  */
      return SVN_NO_ERROR;
    }

  /* The PDH that we just built was for the LOCAL_ABSPATH originally passed
     into this function. We stepped *at least* one directory above that.
     We should now create PDH records for each parent directory that does
     not (yet) have one.  */

  child_pdh = *pdh;

  do
    {
      const char *parent_dir = svn_dirent_dirname(child_pdh->local_abspath,
                                                  scratch_pool);
      svn_wc__db_pdh_t *parent_pdh;

      parent_pdh = apr_hash_get(db->dir_data, parent_dir, APR_HASH_KEY_STRING);
      if (parent_pdh == NULL)
        {
          parent_pdh = apr_pcalloc(db->state_pool, sizeof(*parent_pdh));
          parent_pdh->local_abspath = apr_pstrdup(db->state_pool, parent_dir);

          /* All the PDHs have the same wcroot.  */
          parent_pdh->wcroot = (*pdh)->wcroot;

          apr_hash_set(db->dir_data,
                       parent_pdh->local_abspath, APR_HASH_KEY_STRING,
                       parent_pdh);
        }
      else if (parent_pdh->wcroot == NULL)
        {
          parent_pdh->wcroot = (*pdh)->wcroot;
        }

      /* Point the child PDH at this (new) parent PDH. This will allow for
         easy traversals without path munging.  */
      child_pdh->parent = parent_pdh;
      child_pdh = parent_pdh;

      /* Loop if we haven't reached the PDH we found, or the abspath
         where we terminated the search (when we found wc.db). Note that
         if we never located a PDH in our ancestry, then FOUND_PDH will
         be NULL and that portion of the test will always be TRUE.  */
    }
  while (child_pdh != found_pdh
         && strcmp(child_pdh->local_abspath, local_abspath) != 0);

  return SVN_NO_ERROR;
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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(svn_sqlite__get_statement(stmt, pdh->wcroot->sdb, stmt_idx));
  SVN_ERR(svn_sqlite__bindf(*stmt, "is", pdh->wcroot->wc_id, local_relpath));

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
navigate_to_parent(svn_wc__db_pdh_t **parent_pdh,
                   svn_wc__db_t *db,
                   svn_wc__db_pdh_t *child_pdh,
                   svn_sqlite__mode_t smode,
                   apr_pool_t *scratch_pool)
{
  const char *parent_abspath;
  const char *local_relpath;

  if ((*parent_pdh = child_pdh->parent) != NULL
      && (*parent_pdh)->wcroot != NULL)
    return SVN_NO_ERROR;

  parent_abspath = svn_dirent_dirname(child_pdh->local_abspath, scratch_pool);
  SVN_ERR(parse_local_abspath(parent_pdh, &local_relpath, db,
                              parent_abspath, smode,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(*parent_pdh);

  child_pdh->parent = *parent_pdh;

  return SVN_NO_ERROR;
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


/* */
static svn_error_t *
insert_base_node(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  const insert_base_baton_t *pibb = baton;
  svn_sqlite__stmt_t *stmt;

  /* ### we can't handle this right now  */
  SVN_ERR_ASSERT(pibb->conflict == NULL);

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pibb->wc_id, pibb->local_relpath));

  if (TRUE /* maybe_bind_repos() */)
    {
      SVN_ERR(svn_sqlite__bind_int64(stmt, 3, pibb->repos_id));
      SVN_ERR(svn_sqlite__bind_text(stmt, 4, pibb->repos_relpath));
    }

  /* The directory at the WCROOT has a NULL parent_relpath. Otherwise,
     bind the appropriate parent_relpath. */
  if (*pibb->local_relpath != '\0')
    SVN_ERR(svn_sqlite__bind_text(stmt, 5,
                                  svn_relpath_dirname(pibb->local_relpath,
                                                      scratch_pool)));

  SVN_ERR(svn_sqlite__bind_token(stmt, 6, presence_map, pibb->status));
  SVN_ERR(svn_sqlite__bind_token(stmt, 7, kind_map, pibb->kind));
  SVN_ERR(svn_sqlite__bind_int64(stmt, 8, pibb->revision));

  SVN_ERR(svn_sqlite__bind_properties(stmt, 9, pibb->props, scratch_pool));

  if (SVN_IS_VALID_REVNUM(pibb->changed_rev))
    SVN_ERR(svn_sqlite__bind_int64(stmt, 10, pibb->changed_rev));
  if (pibb->changed_date)
    SVN_ERR(svn_sqlite__bind_int64(stmt, 11, pibb->changed_date));
  if (pibb->changed_author)
    SVN_ERR(svn_sqlite__bind_text(stmt, 12, pibb->changed_author));

  if (pibb->kind == svn_wc__db_kind_dir)
    {
      SVN_ERR(svn_sqlite__bind_text(stmt, 13, svn_depth_to_word(pibb->depth)));
    }
  else if (pibb->kind == svn_wc__db_kind_file)
    {
      SVN_ERR(svn_sqlite__bind_checksum(stmt, 14, pibb->checksum,
                                        scratch_pool));
      if (pibb->translated_size != SVN_INVALID_FILESIZE)
        SVN_ERR(svn_sqlite__bind_int64(stmt, 15, pibb->translated_size));
    }
  else if (pibb->kind == svn_wc__db_kind_symlink)
    {
      /* Note: incomplete nodes may have a NULL target.  */
      if (pibb->target)
        SVN_ERR(svn_sqlite__bind_text(stmt, 16, pibb->target));
    }

  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  if (pibb->kind == svn_wc__db_kind_dir && pibb->children)
    {
      int i;

      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_INSERT_BASE_NODE_INCOMPLETE));

      for (i = pibb->children->nelts; i--; )
        {
          const char *name = APR_ARRAY_IDX(pibb->children, i, const char *);

          SVN_ERR(svn_sqlite__bindf(stmt, "issi",
                                    pibb->wc_id,
                                    svn_dirent_join(pibb->local_relpath,
                                                    name,
                                                    scratch_pool),
                                    pibb->local_relpath,
                                    (apr_int64_t)pibb->revision));
          SVN_ERR(svn_sqlite__insert(NULL, stmt));
        }
    }

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


/* */
static svn_error_t *
insert_working_node(void *baton,
                    svn_sqlite__db_t *sdb,
                    apr_pool_t *scratch_pool)
{
  const insert_working_baton_t *piwb = baton;
  const char *parent_relpath;
  svn_sqlite__stmt_t *stmt;

  /* We cannot insert a WORKING_NODE row at the wcroot.  */
  /* ### actually, with per-dir DB, we can... */
#if 0
  SVN_ERR_ASSERT(*piwb->local_relpath != '\0');
#endif
  if (*piwb->local_relpath == '\0')
    parent_relpath = NULL;
  else
    parent_relpath = svn_relpath_dirname(piwb->local_relpath, scratch_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "isstt",
                            piwb->wc_id, piwb->local_relpath,
                            parent_relpath,
                            presence_map, piwb->presence,
                            kind_map, piwb->kind));

  if (piwb->original_repos_relpath != NULL)
    {
      SVN_ERR_ASSERT(piwb->original_repos_id > 0);
      SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(piwb->original_revnum));

      SVN_ERR(svn_sqlite__bind_int64(stmt, 6, piwb->original_repos_id));
      SVN_ERR(svn_sqlite__bind_text(stmt, 7, piwb->original_repos_relpath));
      SVN_ERR(svn_sqlite__bind_int64(stmt, 8, piwb->original_revnum));
    }

  /* Do not bind 'moved_here' (9), nor 'moved_to' (10).  */

  /* 'checksum' (11) is bound below.  */

  /* Do not bind 'translated_size' (12).  */

  if (SVN_IS_VALID_REVNUM(piwb->changed_rev))
    SVN_ERR(svn_sqlite__bind_int64(stmt, 13, piwb->changed_rev));
  if (piwb->changed_date)
    SVN_ERR(svn_sqlite__bind_int64(stmt, 14, piwb->changed_date));
  if (piwb->changed_author)
    SVN_ERR(svn_sqlite__bind_text(stmt, 15, piwb->changed_author));

  if (piwb->kind == svn_wc__db_kind_dir)
    {
      SVN_ERR(svn_sqlite__bind_text(stmt, 16, svn_depth_to_word(piwb->depth)));
    }
  else if (piwb->kind == svn_wc__db_kind_file)
    {
      SVN_ERR(svn_sqlite__bind_checksum(stmt, 11, piwb->checksum,
                                        scratch_pool));
    }
  else if (piwb->kind == svn_wc__db_kind_symlink)
    {
      SVN_ERR_ASSERT(piwb->target != NULL);

      SVN_ERR(svn_sqlite__bind_text(stmt, 20, piwb->target));
    }

  /* Do not bind 'last_mod_time' (17).  */

  SVN_ERR(svn_sqlite__bind_properties(stmt, 18, piwb->props, scratch_pool));

  /* Do not bind 'keep_local' (19).  */

  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  if (piwb->kind == svn_wc__db_kind_dir && piwb->children)
    {
      int i;

      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_INSERT_WORKING_NODE_INCOMPLETE));

      for (i = piwb->children->nelts; i--; )
        {
          const char *name = APR_ARRAY_IDX(piwb->children, i, const char *);

          SVN_ERR(svn_sqlite__bindf(stmt, "iss",
                                    piwb->wc_id,
                                    svn_relpath_join(piwb->local_relpath,
                                                     name,
                                                     scratch_pool),
                                    piwb->local_relpath));
          SVN_ERR(svn_sqlite__insert(NULL, stmt));
        }
    }

  SVN_ERR(add_work_items(sdb, piwb->work_items, scratch_pool));

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
gather_children(const apr_array_header_t **children,
                svn_boolean_t base_only,
                svn_wc__db_t *db,
                const char *local_abspath,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  apr_array_header_t *child_names;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    base_only
                                      ? STMT_SELECT_BASE_NODE_CHILDREN
                                      : STMT_SELECT_WORKING_CHILDREN));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));

  /* ### should test the node to ensure it is a directory */

  /* ### 10 is based on Subversion's average of 8.5 files per versioned
     ### directory in its repository. maybe use a different value? or
     ### count rows first?  */
  child_names = apr_array_make(result_pool, 10, sizeof(const char *));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);

      APR_ARRAY_PUSH(child_names, const char *) =
        svn_relpath_basename(child_relpath, result_pool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  *children = child_names;

  return svn_sqlite__reset(stmt);
}


/* */
static void
flush_entries(const svn_wc__db_pdh_t *pdh)
{
  if (pdh->adm_access)
    svn_wc__adm_access_set_entries(pdh->adm_access, NULL);
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

  /* If SKEL has an atom as its first child, then this is a work item
     (and that atom is one of the OP_* values).  */
  if (skel->children->is_atom)
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

  SVN_ERR(open_db(sdb, dir_abspath, sdb_fname,
                  svn_sqlite__mode_rwcreate,
                  result_pool, scratch_pool));

  /* Create the database's schema.  */
  SVN_ERR(svn_sqlite__exec_statements(*sdb, STMT_CREATE_SCHEMA));

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
  SVN_ERR(create_wcroot(&pdh->wcroot, pdh->local_abspath,
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
                              const svn_skel_t *conflict,
                              const svn_skel_t *work_items,
                              apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  apr_int64_t repos_id;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_absolute(repos_root_url));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
#if 0
  SVN_ERR_ASSERT(children != NULL);
#endif

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          pdh->wcroot->sdb, scratch_pool));

  blank_ibb(&ibb);

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_dir;
  ibb.wc_id = pdh->wcroot->wc_id;
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

  ibb.conflict = conflict;
  ibb.work_items = work_items;

  /* Insert the directory and all its children transactionally.

     Note: old children can stick around, even if they are no longer present
     in this directory's revision.  */
  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb,
                                       insert_base_node, &ibb,
                                       scratch_pool));

  /* ### worry about flushing child subdirs?  */
  flush_entries(pdh);
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
                         const svn_skel_t *conflict,
                         const svn_skel_t *work_items,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  apr_int64_t repos_id;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_absolute(repos_root_url));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
  SVN_ERR_ASSERT(checksum != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          pdh->wcroot->sdb, scratch_pool));

  blank_ibb(&ibb);

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_file;
  ibb.wc_id = pdh->wcroot->wc_id;
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

  ibb.conflict = conflict;
  ibb.work_items = work_items;

  /* ### hmm. if this used to be a directory, we should remove children.
     ### or maybe let caller deal with that, if there is a possibility
     ### of a node kind change (rather than eat an extra lookup here).  */

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb,
                                       insert_base_node, &ibb,
                                       scratch_pool));

  flush_entries(pdh);
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
                            const svn_skel_t *conflict,
                            const svn_skel_t *work_items,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  apr_int64_t repos_id;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_absolute(repos_root_url));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));
  SVN_ERR_ASSERT(target != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          pdh->wcroot->sdb, scratch_pool));

  blank_ibb(&ibb);

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_symlink;
  ibb.wc_id = pdh->wcroot->wc_id;
  ibb.local_relpath = local_relpath;
  ibb.repos_id = repos_id;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  ibb.props = props;
  ibb.changed_rev = changed_rev;
  ibb.changed_date = changed_date;
  ibb.changed_author = changed_author;

  ibb.target = target;

  ibb.conflict = conflict;
  ibb.work_items = work_items;

  /* ### hmm. if this used to be a directory, we should remove children.
     ### or maybe let caller deal with that, if there is a possibility
     ### of a node kind change (rather than eat an extra lookup here).  */

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb,
                                       insert_base_node, &ibb,
                                       scratch_pool));

  flush_entries(pdh);
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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  apr_int64_t repos_id;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_absolute(repos_root_url));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(status == svn_wc__db_status_absent
                 || status == svn_wc__db_status_excluded
                 || status == svn_wc__db_status_not_present);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          pdh->wcroot->sdb, scratch_pool));

  blank_ibb(&ibb);

  ibb.status = status;
  ibb.kind = kind;
  ibb.wc_id = pdh->wcroot->wc_id;
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

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb,
                                       insert_base_node, &ibb,
                                       scratch_pool));

  flush_entries(pdh);
  return SVN_NO_ERROR;
}


/* ### temp API.  Remove before release. */
svn_error_t *
svn_wc__db_temp_base_add_subdir(svn_wc__db_t *db,
                                const char *local_abspath,
                                const char *repos_relpath,
                                const char *repos_root_url,
                                const char *repos_uuid,
                                svn_revnum_t revision,
                                const apr_hash_t *props,
                                svn_revnum_t changed_rev,
                                apr_time_t changed_date,
                                const char *changed_author,
                                svn_depth_t depth,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  apr_int64_t repos_id;
  insert_base_baton_t ibb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_absolute(repos_root_url));
  SVN_ERR_ASSERT(repos_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(revision));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(changed_rev));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          pdh->wcroot->sdb, scratch_pool));

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_subdir;
  ibb.wc_id = pdh->wcroot->wc_id;
  ibb.local_relpath = local_relpath;
  ibb.repos_id = repos_id;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  ibb.props = NULL;
  ibb.changed_rev = changed_rev;
  ibb.changed_date = changed_date;
  ibb.changed_author = changed_author;

  ibb.children = NULL;
  ibb.depth = depth;

  /* ### no children, conflicts, or work items to install in a txn... */

  return svn_error_return(insert_base_node(&ibb, pdh->wcroot->sdb,
                                           scratch_pool));
}


svn_error_t *
svn_wc__db_base_remove(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_DELETE_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step_done(stmt));

  flush_entries(pdh);

  return SVN_NO_ERROR;
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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err = SVN_NO_ERROR;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    lock ? STMT_SELECT_BASE_NODE_WITH_LOCK
                                         : STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      svn_wc__db_kind_t node_kind = svn_sqlite__column_token(stmt, 3,
                                                             kind_map);

      if (kind)
        {
          if (node_kind == svn_wc__db_kind_subdir)
            *kind = svn_wc__db_kind_dir;
          else
            *kind = node_kind;
        }
      if (status)
        {
          *status = svn_sqlite__column_token(stmt, 2, presence_map);

          if (node_kind == svn_wc__db_kind_subdir
              && *status == svn_wc__db_status_normal)
            {
              /* We're looking at the subdir record in the *parent* directory,
                 which implies per-dir .svn subdirs. We should be looking
                 at the subdir itself; therefore, it is missing or obstructed
                 in some way. Inform the caller.  */
              *status = svn_wc__db_status_obstructed;
            }
        }
      if (revision)
        {
          *revision = svn_sqlite__column_revnum(stmt, 4);
        }
      if (repos_relpath)
        {
          *repos_relpath = svn_sqlite__column_text(stmt, 1, result_pool);
        }
      if (lock)
        {
          if (svn_sqlite__column_is_null(stmt, 14))
            {
              *lock = NULL;
            }
          else
            {
              *lock = apr_pcalloc(result_pool, sizeof(svn_wc__db_lock_t));
              (*lock)->token = svn_sqlite__column_text(stmt, 14, result_pool);
              if (!svn_sqlite__column_is_null(stmt, 15))
                (*lock)->owner = svn_sqlite__column_text(stmt, 15,
                                                         result_pool);
              if (!svn_sqlite__column_is_null(stmt, 16))
                (*lock)->comment = svn_sqlite__column_text(stmt, 16,
                                                           result_pool);
              if (!svn_sqlite__column_is_null(stmt, 17))
                (*lock)->date = svn_sqlite__column_int64(stmt, 17);
            }
        }
      if (repos_root_url || repos_uuid)
        {
          /* Fetch repository information via REPOS_ID. */
          if (svn_sqlite__column_is_null(stmt, 0))
            {
              if (repos_root_url)
                *repos_root_url = NULL;
              if (repos_uuid)
                *repos_uuid = NULL;
            }
          else
            {
              err = fetch_repos_info(repos_root_url, repos_uuid,
                                     pdh->wcroot->sdb,
                                     svn_sqlite__column_int64(stmt, 0),
                                     result_pool);
            }
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
                        svn_dirent_local_style(local_abspath, scratch_pool));
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
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool));
    }

  /* Note: given the composition, no need to wrap for tracing.  */
  return svn_error_compose_create(err, svn_sqlite__reset(stmt));
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
  return gather_children(children, TRUE,
                         db, local_abspath, result_pool, scratch_pool);
}


svn_error_t *
svn_wc__db_base_set_dav_cache(svn_wc__db_t *db,
                              const char *local_abspath,
                              const apr_hash_t *props,
                              apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_UPDATE_BASE_DAV_CACHE, scratch_pool));
  SVN_ERR(svn_sqlite__bind_properties(stmt, 3, props, scratch_pool));

  /* ### we should assert that 1 row was affected.  */

  return svn_error_return(svn_sqlite__step_done(stmt));
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
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, svn_sqlite__reset(stmt),
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                        scratch_pool));
  return svn_error_return(svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_pristine_read(svn_stream_t **contents,
                         svn_wc__db_t *db,
                         const char *wri_abspath,
                         const svn_checksum_t *checksum,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  const char *pristine_abspath;
  svn_error_t *err;

  SVN_ERR_ASSERT(contents != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));
  SVN_ERR_ASSERT(checksum != NULL);

  VERIFY_CHECKSUM_KIND(checksum);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, wri_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* ### should we look in the PRISTINE table for anything?  */

  err = get_pristine_fname(&pristine_abspath, pdh, checksum,
#ifndef SVN__SKIP_SUBDIR
                           FALSE /* create_subdir */,
#endif
                           scratch_pool, scratch_pool);
  SVN_ERR(err);

  return svn_error_return(svn_stream_open_readonly(
                            contents, pristine_abspath,
                            result_pool, scratch_pool));
}


svn_error_t *
svn_wc__db_pristine_get_tempdir(const char **temp_dir_abspath,
                                svn_wc__db_t *db,
                                const char *wri_abspath,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR_ASSERT(temp_dir_abspath != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, wri_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  *temp_dir_abspath = svn_dirent_join_many(result_pool,
                                           pdh->wcroot->abspath,
                                           svn_wc_get_adm_dir(scratch_pool),
                                           PRISTINE_TEMPDIR_RELPATH,
                                           NULL);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_pristine_install(svn_wc__db_t *db,
                            const char *tempfile_abspath,
                            const svn_checksum_t *sha1_checksum,
                            const svn_checksum_t *md5_checksum,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  const char *wri_abspath;
  const char *pristine_abspath;
  apr_finfo_t finfo;
  svn_sqlite__stmt_t *stmt;
  svn_error_t *err;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(tempfile_abspath));
  SVN_ERR_ASSERT(sha1_checksum != NULL);
  SVN_ERR_ASSERT(md5_checksum != NULL);

  VERIFY_CHECKSUM_KIND(checksum);

  /* ### this logic assumes that TEMPFILE_ABSPATH follows this pattern:
     ###   WCROOT_ABSPATH/COMPONENT/TEMPFNAME
     ### if we change this (see PRISTINE_TEMPDIR_RELPATH), then this
     ### logic should change.  */
  wri_abspath = svn_dirent_dirname(svn_dirent_dirname(tempfile_abspath,
                                                      scratch_pool),
                                   scratch_pool);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, wri_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  err = get_pristine_fname(&pristine_abspath, pdh, sha1_checksum,
#ifndef SVN__SKIP_SUBDIR
                           TRUE /* create_subdir */,
#endif
                           scratch_pool, scratch_pool);
  SVN_ERR(err);

  /* Put the file into its target location.  */
  SVN_ERR(svn_io_file_rename(tempfile_abspath, pristine_abspath,
                             scratch_pool));

  SVN_ERR(svn_io_stat(&finfo, pristine_abspath, APR_FINFO_SIZE,
                      scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_INSERT_PRISTINE));
  SVN_ERR(svn_sqlite__bind_checksum(stmt, 1, sha1_checksum, scratch_pool));
  SVN_ERR(svn_sqlite__bind_checksum(stmt, 2, md5_checksum, scratch_pool));
  SVN_ERR(svn_sqlite__bind_int64(stmt, 3, finfo.size));
  SVN_ERR(svn_sqlite__insert(NULL, stmt));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_pristine_check(svn_boolean_t *present,
                          svn_wc__db_t *db,
                          const char *wri_abspath,
                          const svn_checksum_t *checksum,
                          svn_wc__db_checkmode_t mode,
                          apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(present != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));
  SVN_ERR_ASSERT(checksum != NULL);

  VERIFY_CHECKSUM_KIND(checksum);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_repair(svn_wc__db_t *db,
                           const char *wri_abspath,
                           const svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));
  SVN_ERR_ASSERT(checksum != NULL);

  VERIFY_CHECKSUM_KIND(checksum);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_repos_ensure(apr_int64_t *repos_id,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        const char *repos_root_url,
                        const char *repos_uuid,
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  return svn_error_return(create_repos_id(repos_id, repos_root_url,
                                          repos_uuid, pdh->wcroot->sdb,
                                          scratch_pool));
}


svn_error_t *
svn_wc__db_op_copy(svn_wc__db_t *db,
                   const char *src_abspath,
                   const char *dst_abspath,
                   apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_copy_url(svn_wc__db_t *db,
                       const char *local_abspath,
                       const char *copyfrom_repos_relpath,
                       const char *copyfrom_root_url,
                       const char *copyfrom_uuid,
                       svn_revnum_t copyfrom_revision,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(copyfrom_repos_relpath != NULL);
  SVN_ERR_ASSERT(svn_uri_is_absolute(copyfrom_root_url));
  SVN_ERR_ASSERT(copyfrom_uuid != NULL);
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(copyfrom_revision));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_add_directory(svn_wc__db_t *db,
                            const char *local_abspath,
                            const svn_skel_t *work_items,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  insert_working_baton_t iwb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_wc__db_kind_dir;
  iwb.wc_id = pdh->wcroot->wc_id;
  iwb.local_relpath = local_relpath;

  iwb.work_items = work_items;

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb,
                                       insert_working_node, &iwb,
                                       scratch_pool));
  flush_entries(pdh);

  /* Add a parent stub.  */
  {
    svn_error_t *err;

    err = navigate_to_parent(&pdh, db, pdh, svn_sqlite__mode_readwrite,
                             scratch_pool);
    if (err)
      {
        /* Prolly fell off teh top of the wcroot. Just call it a day.  */
        svn_error_clear(err);
        return SVN_NO_ERROR;
      }

    blank_iwb(&iwb);

    iwb.presence = svn_wc__db_status_normal;
    iwb.kind = svn_wc__db_kind_subdir;
    iwb.wc_id = pdh->wcroot->wc_id;
    iwb.local_relpath = svn_dirent_basename(local_abspath, scratch_pool);

    /* No children or work items, so a txn is not needed.  */
    SVN_ERR(insert_working_node(&iwb, pdh->wcroot->sdb, scratch_pool));
    flush_entries(pdh);
  }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_add_file(svn_wc__db_t *db,
                       const char *local_abspath,
                       const svn_skel_t *work_items,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  insert_working_baton_t iwb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_wc__db_kind_file;
  iwb.wc_id = pdh->wcroot->wc_id;
  iwb.local_relpath = local_relpath;

  iwb.work_items = work_items;

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb,
                                       insert_working_node, &iwb,
                                       scratch_pool));
  flush_entries(pdh);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_add_symlink(svn_wc__db_t *db,
                          const char *local_abspath,
                          const char *target,
                          const svn_skel_t *work_items,
                          apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  insert_working_baton_t iwb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(target != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  blank_iwb(&iwb);

  iwb.presence = svn_wc__db_status_normal;
  iwb.kind = svn_wc__db_kind_symlink;
  iwb.wc_id = pdh->wcroot->wc_id;
  iwb.local_relpath = local_relpath;

  iwb.target = target;

  iwb.work_items = work_items;

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb,
                                       insert_working_node, &iwb,
                                       scratch_pool));
  flush_entries(pdh);

  return SVN_NO_ERROR;
}


struct set_props_baton
{
  apr_hash_t *props;
  const char *local_relpath;
  svn_wc__db_pdh_t *pdh;
};

/* Set the 'properties' column in the 'ACTUAL_NODE' table to BATON->props.
   Create an entry in the ACTUAL table for the node if it does not yet
   have one.
   To specify no properties, BATON->props must be an empty hash, not NULL.
   BATON is of type 'struct set_props_baton'. */
static svn_error_t *
set_props_txn(void *baton, svn_sqlite__db_t *db, apr_pool_t *scratch_pool)
{
  struct set_props_baton *spb = baton;
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR(svn_sqlite__get_statement(&stmt, db, STMT_UPDATE_ACTUAL_PROPS));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", spb->pdh->wcroot->wc_id,
                            spb->local_relpath));

  SVN_ERR(svn_sqlite__bind_properties(stmt, 3, spb->props, scratch_pool));
  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows == 1)
    return SVN_NO_ERROR; /* We are done */


  /* We have to insert a row in actual */
  /* ### Check if we have base or working here ? */

  SVN_ERR(svn_sqlite__get_statement(&stmt, db, STMT_INSERT_ACTUAL_PROPS));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", spb->pdh->wcroot->wc_id,
                            spb->local_relpath));

  if (*spb->local_relpath != '\0')
    SVN_ERR(svn_sqlite__bind_text(stmt, 3,
                                  svn_relpath_dirname(spb->local_relpath,
                                                      scratch_pool)));

  SVN_ERR(svn_sqlite__bind_properties(stmt, 4, spb->props, scratch_pool));
  return svn_error_return(svn_sqlite__step_done(stmt));
}

svn_error_t *
svn_wc__db_op_set_props(svn_wc__db_t *db,
                        const char *local_abspath,
                        apr_hash_t *props,
                        apr_pool_t *scratch_pool)
{
  struct set_props_baton spb;
  spb.props = props;

  SVN_ERR(parse_local_abspath(&spb.pdh, &spb.local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  return svn_error_return(
            svn_sqlite__with_transaction(spb.pdh->wcroot->sdb,
                                         set_props_txn,
                                         &spb,
                                         scratch_pool));
}


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
  return svn_error_return(set_properties(db, local_abspath, props,
                                         STMT_UPDATE_BASE_PROPS,
                                         "base_node",
                                         scratch_pool));
}


svn_error_t *
svn_wc__db_temp_working_set_props(svn_wc__db_t *db,
                                  const char *local_abspath,
                                  const apr_hash_t *props,
                                  apr_pool_t *scratch_pool)
{
  return svn_error_return(set_properties(db, local_abspath, props,
                                         STMT_UPDATE_WORKING_PROPS,
                                         "working_node",
                                         scratch_pool));
}


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
    existing_changelist = svn_sqlite__column_text(stmt, 0, scratch_pool);
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
  svn_wc__db_pdh_t *pdh;
  struct set_changelist_baton scb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &scb.local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  scb.wc_id = pdh->wcroot->wc_id;
  scb.changelist = changelist;

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb, set_changelist_txn,
                                       &scb, scratch_pool));

  flush_entries(pdh);

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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* ### we're not ready to handy RESOLVED_TREE just yet.  */
  SVN_ERR_ASSERT(!resolved_tree);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* ### these two statements are not transacted together. is this a
     ### problem? I suspect a failure simply leaves the other in a
     ### continued, unresolved state. However, that still retains
     ### "integrity", so another re-run by the user will fix it.  */

  if (resolved_text)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                        STMT_CLEAR_TEXT_CONFLICT));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                pdh->wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  if (resolved_props)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                        STMT_CLEAR_PROPS_CONFLICT));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                pdh->wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* Some entries have cached the above values. Kapow!!  */
  flush_entries(pdh);

  return SVN_NO_ERROR;
}


struct set_tc_baton
{
  const char *local_abspath;
  apr_int64_t wc_id;
  const char *local_relpath;
  const char *parent_abspath;
  const svn_wc_conflict_description2_t *tree_conflict;
};


/* */
static svn_error_t *
set_tc_txn(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  struct set_tc_baton *stb = baton;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *tree_conflict_data;
  apr_hash_t *conflicts;

  /* Get the conflict information for the parent of LOCAL_ABSPATH. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", stb->wc_id, stb->local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  /* No ACTUAL node, no conflict info, no problem. */
  if (!have_row)
    tree_conflict_data = NULL;
  else
    tree_conflict_data = svn_sqlite__column_text(stmt, 5, scratch_pool);

  SVN_ERR(svn_sqlite__reset(stmt));

  /* Parse the conflict data, set the desired conflict, and then rewrite
     the conflict data. */
  SVN_ERR(svn_wc__read_tree_conflicts(&conflicts, tree_conflict_data,
                                      stb->parent_abspath, scratch_pool));

  apr_hash_set(conflicts, svn_dirent_basename(stb->local_abspath,
                                              scratch_pool),
               APR_HASH_KEY_STRING, stb->tree_conflict);

  if (apr_hash_count(conflicts) == 0 && !have_row)
    {
      /* We're removing conflict information that doesn't even exist, so
         don't bother rewriting it, just exit. */
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__write_tree_conflicts(&tree_conflict_data, conflicts,
                                       scratch_pool));

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

  return svn_error_return(svn_sqlite__step_done(stmt));
}


svn_error_t *
svn_wc__db_op_set_tree_conflict(svn_wc__db_t *db,
                                const char *local_abspath,
                                const svn_wc_conflict_description2_t *tree_conflict,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  struct set_tc_baton stb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  stb.parent_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  SVN_ERR(parse_local_abspath(&pdh, &stb.local_relpath, db, stb.parent_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  stb.local_abspath = local_abspath;
  stb.wc_id = pdh->wcroot->wc_id;
  stb.tree_conflict = tree_conflict;

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb, set_tc_txn, &stb,
                                       scratch_pool));

  /* There may be some entries, and the lock info is now out of date.  */
  flush_entries(pdh);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_op_revert(svn_wc__db_t *db,
                     const char *local_abspath,
                     svn_depth_t depth,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_read_all_tree_conflicts(apr_hash_t **tree_conflicts,
                                      svn_wc__db_t *db,
                                      const char *local_abspath,
                                      apr_pool_t *result_pool,
                                      apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *tree_conflict_data;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* Get the conflict information for the parent of LOCAL_ABSPATH. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  /* No ACTUAL node, no conflict info, no problem. */
  if (!have_row)
    {
      *tree_conflicts = NULL;
      SVN_ERR(svn_sqlite__reset(stmt));
      return SVN_NO_ERROR;
    }

  tree_conflict_data = svn_sqlite__column_text(stmt, 5, scratch_pool);
  SVN_ERR(svn_sqlite__reset(stmt));

  /* No tree conflict data?  no problem. */
  if (tree_conflict_data == NULL)
    {
      *tree_conflicts = NULL;
      return SVN_NO_ERROR;
    }

  SVN_ERR(svn_wc__read_tree_conflicts(tree_conflicts, tree_conflict_data,
                                      local_abspath, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_op_read_tree_conflict(
                     const svn_wc_conflict_description2_t **tree_conflict,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  const char *parent_abspath;
  apr_hash_t *tree_conflicts;
  svn_error_t *err;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  parent_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

  err = svn_wc__db_op_read_all_tree_conflicts(&tree_conflicts, db,
                                              parent_abspath,
                                              result_pool, scratch_pool);
  if (err && err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY)
    {
       /* We walked off the top of a working copy.  */
       svn_error_clear(err);
       *tree_conflict = NULL;
       return SVN_NO_ERROR;
    }
  else if (err)
    return svn_error_return(err);

  if (tree_conflicts)
    *tree_conflict = apr_hash_get(tree_conflicts,
                                  svn_dirent_basename(local_abspath,
                                                      scratch_pool),
                                  APR_HASH_KEY_STRING);
  else
    *tree_conflict = NULL;

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_op_remove_entry(svn_wc__db_t *db,
                                const char *local_abspath,
                                svn_boolean_t flush_entry_cache,
                                apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  svn_sqlite__stmt_t *stmt;
  svn_sqlite__db_t *sdb;
  apr_int64_t wc_id;
  const char *current_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &current_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  if (flush_entry_cache)
    flush_entries(pdh);

  /* Check if we should remove it from the parent db instead */
  if (strcmp(current_relpath, "") == 0)
    {
      SVN_ERR(navigate_to_parent(&pdh, db, pdh, svn_sqlite__mode_readwrite,
                                 scratch_pool));

      VERIFY_USABLE_PDH(pdh);
      current_relpath = svn_dirent_basename(local_abspath, NULL);

      if (flush_entry_cache)
        flush_entries(pdh);
    }

  sdb = pdh->wcroot->sdb;
  wc_id = pdh->wcroot->wc_id;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_DELETE_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, current_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_DELETE_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, current_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_DELETE_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, current_relpath));

  return svn_error_return(svn_sqlite__step_done(stmt));
}


static svn_error_t *
update_depth_values(svn_wc__db_pdh_t *pdh,
                    const char *local_relpath,
                    svn_depth_t depth)
{
  svn_boolean_t excluded = (depth == svn_depth_exclude);
  svn_sqlite__stmt_t *stmt;

  /* Flush any entries before we start monkeying the database.  */
  flush_entries(pdh);

  /* Parent stubs have only two depth options: excluded, or infinity.  */
  if (*local_relpath != '\0' && !excluded)
    depth = svn_depth_infinity;

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    excluded
                                      ? STMT_UPDATE_BASE_EXCLUDED
                                      : STMT_UPDATE_BASE_DEPTH));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
  if (!excluded)
    SVN_ERR(svn_sqlite__bind_text(stmt, 3, svn_depth_to_word(depth)));
  SVN_ERR(svn_sqlite__step_done(stmt));

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    excluded
                                      ? STMT_UPDATE_WORKING_EXCLUDED
                                      : STMT_UPDATE_WORKING_DEPTH));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(depth >= svn_depth_empty && depth <= svn_depth_infinity);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* ### We set depth on working and base to match entry behavior.
         Maybe these should be separated later? */

  SVN_ERR(update_depth_values(pdh, local_relpath, depth));

  /* If we're in the subdir, then navigate to the parent to set its
     depth value.  */
  if (*local_relpath == '\0')
    {
      svn_error_t *err;

      err = navigate_to_parent(&pdh, db, pdh, svn_sqlite__mode_readwrite,
                               scratch_pool);
      if (err)
        {
          if (err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
            return svn_error_return(err);

          /* No parent to update */
          svn_error_clear(err);
          return SVN_NO_ERROR;
        }

      /* Get the stub name, and update the depth.  */
      local_relpath = svn_dirent_basename(local_abspath, scratch_pool);
      SVN_ERR(update_depth_values(pdh, local_relpath, depth));
    }

  return SVN_NO_ERROR;
}


/* Update the working node for LOCAL_ABSPATH setting presence=STATUS */
static svn_error_t *
db_working_update_presence(svn_wc__db_status_t status,
                           svn_wc__db_t *db,
                           const char *local_abspath,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_UPDATE_WORKING_PRESENCE));
  SVN_ERR(svn_sqlite__bindf(stmt, "ist", pdh->wcroot->wc_id, local_relpath,
                            presence_map, status));
  SVN_ERR(svn_sqlite__step_done(stmt));

  flush_entries(pdh);

  /* ### Parent stub?  I don't know; I'll punt for now as it passes
         the regression tests as is and the problem will evaporate
         when the db is centralised. */

  return SVN_NO_ERROR;
}


/* Delete working and actual nodes for LOCAL_ABSPATH */
static svn_error_t *
db_working_actual_remove(svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_DELETE_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));
  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_DELETE_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  flush_entries(pdh);

  if (*local_relpath == '\0')
    {
      /* ### Delete parent stub. Remove when db is centralised. */
      SVN_ERR(navigate_to_parent(&pdh, db, pdh, svn_sqlite__mode_readwrite,
                                 scratch_pool));
      local_relpath = svn_dirent_basename(local_abspath, NULL);
      VERIFY_USABLE_PDH(pdh);

      SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                        STMT_DELETE_WORKING_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                pdh->wcroot->wc_id, local_relpath));
      SVN_ERR(svn_sqlite__step_done(stmt));

      flush_entries(pdh);
    }

  return SVN_NO_ERROR;
}


/* Insert a working node for LOCAL_ABSPATH with presence=STATUS. */
static svn_error_t *
db_working_insert(svn_wc__db_status_t status,
                  svn_wc__db_t *db,
                  const char *local_abspath,
                  apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_INSERT_WORKING_NODE_FROM_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "ist", pdh->wcroot->wc_id, local_relpath,
                            presence_map, status));
  SVN_ERR(svn_sqlite__step_done(stmt));

  flush_entries(pdh);

  if (*local_relpath == '\0')
    {
      /* ### Insert parent stub. Remove when db is centralised. */
      SVN_ERR(navigate_to_parent(&pdh, db, pdh, svn_sqlite__mode_readwrite,
                                 scratch_pool));
      local_relpath = svn_dirent_basename(local_abspath, NULL);
      VERIFY_USABLE_PDH(pdh);

      /* ### Should the parent stub have a full row like this? */
      SVN_ERR(svn_sqlite__get_statement(
                &stmt, pdh->wcroot->sdb,
                STMT_INSERT_WORKING_NODE_FROM_BASE_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "ist",
                                pdh->wcroot->wc_id, local_relpath,
                                presence_map, status));
      SVN_ERR(svn_sqlite__step_done(stmt));

      flush_entries(pdh);
    }

  return SVN_NO_ERROR;
}


/* Set *ROOT_OF_COPY to TRUE if LOCAL_ABSPATH is an add or the root of
   a copy, to FALSE otherwise. */
static svn_error_t*
is_add_or_root_of_copy(svn_boolean_t *add_or_root_of_copy,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t status;
  const char *op_root_abspath;
  const char *original_repos_relpath, *original_repos_root;
  const char *original_repos_uuid;
  svn_revnum_t original_revision;

  SVN_ERR(svn_wc__db_scan_addition(&status, &op_root_abspath,
                                   NULL, NULL, NULL,
                                   &original_repos_relpath,
                                   &original_repos_root,
                                   &original_repos_uuid,
                                   &original_revision,
                                   db, local_abspath,
                                   scratch_pool, scratch_pool));

  SVN_ERR_ASSERT(status == svn_wc__db_status_added
                 || status == svn_wc__db_status_copied);
  SVN_ERR_ASSERT(op_root_abspath != NULL);

  *add_or_root_of_copy = (status == svn_wc__db_status_added
                          || !strcmp(local_abspath, op_root_abspath));

  if (*add_or_root_of_copy && status == svn_wc__db_status_copied)
    {
      /* ### merge sets the wrong copyfrom when adding a tree and so
             the root detection above is unreliable.  I'm "fixing" it
             here because I just need to detect whether this is an
             instance of the merge bug, and that's easier than fixing
             scan_addition or merge. */
      const char *parent_abspath;
      const char *name;
      svn_wc__db_status_t parent_status;
      const char *parent_original_repos_relpath, *parent_original_repos_root;
      const char *parent_original_repos_uuid;
      svn_revnum_t parent_original_revision;
      svn_error_t *err;

      svn_dirent_split(local_abspath, &parent_abspath, &name, scratch_pool);

      err = svn_wc__db_scan_addition(&parent_status,
                                     NULL, NULL, NULL, NULL,
                                     &parent_original_repos_relpath,
                                     &parent_original_repos_root,
                                     &parent_original_repos_uuid,
                                     &parent_original_revision,
                                     db, parent_abspath,
                                     scratch_pool, scratch_pool);
      if (err)
        {
          if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
            return svn_error_return(err);
          /* It really is a root */
          svn_error_clear(err);
        }
      else if (parent_status == svn_wc__db_status_copied
               && original_revision == parent_original_revision
               && !strcmp(original_repos_uuid, parent_original_repos_uuid)
               && !strcmp(original_repos_root, parent_original_repos_root)
               && !strcmp(original_repos_relpath,
                          svn_dirent_join(parent_original_repos_relpath,
                                          name,
                                          scratch_pool)))
        /* An instance of the merge bug */
        *add_or_root_of_copy = FALSE;
    }

  return SVN_NO_ERROR;
}


/* Delete LOCAL_ABSPATH.  Implements the delete transition from
   notes/wc-ng/transitions. */
svn_error_t *
svn_wc__db_temp_op_delete(svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_boolean_t base_none, working_none, new_working_none;
  svn_wc__db_status_t base_status, working_status, new_working_status;
  svn_boolean_t base_shadowed;

  err = svn_wc__db_base_get_info(&base_status,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                 db, local_abspath,
                                 scratch_pool, scratch_pool);
  if (err && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      base_none = TRUE;
      svn_error_clear(err);
    }
  else if (! err)
    base_none = FALSE;
  else
    return svn_error_return(err);

  /* ### should error on excluded, too. excluded nodes could be removed
     ### from our metadata, but they cannot be scheduled for deletion.  */
  if (!base_none && base_status == svn_wc__db_status_absent)
    return SVN_NO_ERROR; /* ### should return an error.... WHICH ONE? */

  /* No need to check for SVN_ERR_WC_PATH_NOT_FOUND. Something has to
     be there for us to delete.  */
  SVN_ERR(svn_wc__db_read_info(&working_status, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, &base_shadowed, NULL, NULL,
                               db, local_abspath,
                               scratch_pool, scratch_pool));
  if (working_status == svn_wc__db_status_deleted
      || working_status == svn_wc__db_status_obstructed_delete)
    {
      /* The node is already deleted.  */
      /* ### return an error? callers should know better.  */
      return SVN_NO_ERROR;
    }

  /* We must have a WORKING node if there is no BASE node (gotta have
     something!). If there IS a BASE node, then we have a WORKING node
     if BASE_SHADOWED is TRUE.  */
  working_none = !(base_none || base_shadowed);

  new_working_none = working_none;
  new_working_status = working_status;

  if (working_status == svn_wc__db_status_normal
      || working_status == svn_wc__db_status_not_present
      || working_status == svn_wc__db_status_obstructed)
    {
      /* No structural changes (ie. no WORKING node). Mark the BASE node
         as deleted.  */

      SVN_ERR_ASSERT(working_none);

      new_working_none = FALSE;
      new_working_status = svn_wc__db_status_base_deleted;
    }
  else if (working_status == svn_wc__db_status_obstructed_add)
    {
      /* There is a parent stub for some kind of addition.

         ### we cannot tell if this is a local-add or a copied/moved-here.
         ### when the latter case, if this node is the root of that
         ### copy/move, then we just "revert" it. otherwise, we're deleting
         ### a child of that copy/move and marked it as delete. and the
         ### second proble: we also cannot tell whether this is the root
         ### or not.
         ###
         ### return WC_MISSING here. we need that working copy metadata
         ###
         ### when we get to single-db, there are no "obstructed" status
         ### codes, so this will magically work...  */
      SVN_ERR_ASSERT(!working_none);

      return svn_error_createf(SVN_ERR_WC_MISSING, NULL,
                               _("The directory '%s' is missing and cannot "
                                 "be marked for deletion."),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }
  /* ### remaining states: added, absent, excluded, incomplete
     ### the last three have debatable schedule-delete semantics,
     ### and this code may need to change further, but I'm not
     ### going to worry about it now
  */
  else if (working_none)
    {
      /* No structural changes  */
      if (base_status == svn_wc__db_status_normal
          || base_status == svn_wc__db_status_obstructed
          || base_status == svn_wc__db_status_incomplete
          || base_status == svn_wc__db_status_excluded)
        {
          new_working_none = FALSE;
          new_working_status = svn_wc__db_status_base_deleted;
        }
    }
  else if (working_status == svn_wc__db_status_added
           && (base_none || base_status == svn_wc__db_status_not_present))
    {
      /* ADD/COPY-HERE/MOVE-HERE. There is "no BASE node".  */

      svn_boolean_t add_or_root_of_copy;

      SVN_ERR(is_add_or_root_of_copy(&add_or_root_of_copy,
                                     db, local_abspath, scratch_pool));
      if (add_or_root_of_copy)
        new_working_none = TRUE;
      else
        new_working_status = svn_wc__db_status_not_present;
    }
  else if (working_status == svn_wc__db_status_added)
    {
      /* DELETE + ADD  */
      svn_boolean_t add_or_root_of_copy;
      SVN_ERR(is_add_or_root_of_copy(&add_or_root_of_copy,
                                     db, local_abspath, scratch_pool));
      if (add_or_root_of_copy)
        new_working_status = svn_wc__db_status_base_deleted;
      else
        new_working_status = svn_wc__db_status_not_present;
    }
  else if (working_status == svn_wc__db_status_incomplete)
    {
      svn_boolean_t add_or_root_of_copy;
      SVN_ERR(is_add_or_root_of_copy(&add_or_root_of_copy,
                                     db, local_abspath, scratch_pool));
      if (add_or_root_of_copy)
        new_working_none = TRUE;
    }

  if (new_working_none && !working_none)
    {
      SVN_ERR(db_working_actual_remove(db, local_abspath, scratch_pool));
      /* ### Search the cached directories in db for directories below
             local_abspath and close their handles to allow deleting
             them from the working copy */
      SVN_ERR(svn_wc__db_temp_forget_directory(db, local_abspath,
                                               scratch_pool));
    }
  else if (!new_working_none && working_none)
    SVN_ERR(db_working_insert(new_working_status,
                              db, local_abspath, scratch_pool));
  else if (!new_working_none && !working_none
           && new_working_status != working_status)
    SVN_ERR(db_working_update_presence(new_working_status,
                                       db, local_abspath, scratch_pool));
  /* ### else nothing to do, return an error? */

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
                     svn_boolean_t *text_mod,
                     svn_boolean_t *props_mod,
                     svn_boolean_t *base_shadowed,
                     svn_boolean_t *conflicted,
                     svn_wc__db_lock_t **lock,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt_base;
  svn_sqlite__stmt_t *stmt_work;
  svn_sqlite__stmt_t *stmt_act;
  svn_boolean_t have_base;
  svn_boolean_t have_work;
  svn_boolean_t have_act;
  svn_error_t *err = NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(svn_sqlite__get_statement(&stmt_base, pdh->wcroot->sdb,
                                    lock ? STMT_SELECT_BASE_NODE_WITH_LOCK
                                         : STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt_base, "is",
                            pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_base, stmt_base));

  SVN_ERR(svn_sqlite__get_statement(&stmt_work, pdh->wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt_work, "is",
                            pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_work, stmt_work));

  SVN_ERR(svn_sqlite__get_statement(&stmt_act, pdh->wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt_act, "is",
                            pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_act, stmt_act));

  if (have_base || have_work)
    {
      svn_wc__db_kind_t node_kind;

      if (have_work)
        node_kind = svn_sqlite__column_token(stmt_work, 1, kind_map);
      else
        node_kind = svn_sqlite__column_token(stmt_base, 3, kind_map);

      if (status)
        {
          if (have_base)
            {
              *status = svn_sqlite__column_token(stmt_base, 2, presence_map);

              /* We have a presence that allows a WORKING_NODE override
                 (normal or not-present), or we don't have an override.  */
              /* ### for now, allow an override of an incomplete BASE_NODE
                 ### row. it appears possible to get rows in BASE/WORKING
                 ### both set to 'incomplete'.  */
              SVN_ERR_ASSERT((*status != svn_wc__db_status_absent
                              && *status != svn_wc__db_status_excluded
                              /* && *status != svn_wc__db_status_incomplete */)
                             || !have_work);

              if (node_kind == svn_wc__db_kind_subdir
                  && *status == svn_wc__db_status_normal)
                {
                  /* We should have read a row from the subdir wc.db. It
                     must be obstructed in some way.

                     It is also possible that a WORKING node will override
                     this value with a proper status.  */
                  *status = svn_wc__db_status_obstructed;
                }
            }

          if (have_work)
            {
              svn_wc__db_status_t work_status;

              work_status = svn_sqlite__column_token(stmt_work, 0,
                                                     presence_map);
              SVN_ERR_ASSERT(work_status == svn_wc__db_status_normal
                             || work_status == svn_wc__db_status_not_present
                             || work_status == svn_wc__db_status_base_deleted
                             || work_status == svn_wc__db_status_incomplete);

              if (work_status == svn_wc__db_status_incomplete)
                {
                  *status = svn_wc__db_status_incomplete;
                }
              else if (work_status == svn_wc__db_status_not_present
                       || work_status == svn_wc__db_status_base_deleted)
                {
                  /* The caller should scan upwards to detect whether this
                     deletion has occurred because this node has been moved
                     away, or it is a regular deletion. Also note that the
                     deletion could be of the BASE tree, or a child of
                     something that has been copied/moved here.

                     If we're looking at the data in the parent, then
                     something has obstructed the child data. Inform
                     the caller.  */
                  if (node_kind == svn_wc__db_kind_subdir)
                    *status = svn_wc__db_status_obstructed_delete;
                  else
                    *status = svn_wc__db_status_deleted;
                }
              else /* normal */
                {
                  /* The caller should scan upwards to detect whether this
                     addition has occurred because of a simple addition,
                     a copy, or is the destination of a move.

                     If we're looking at the data in the parent, then
                     something has obstructed the child data. Inform
                     the caller.  */
                  if (node_kind == svn_wc__db_kind_subdir)
                    *status = svn_wc__db_status_obstructed_add;
                  else
                    *status = svn_wc__db_status_added;
                }
            }
        }
      if (kind)
        {
          if (node_kind == svn_wc__db_kind_subdir)
            *kind = svn_wc__db_kind_dir;
          else
            *kind = node_kind;
        }
      if (revision)
        {
          if (have_work)
            *revision = SVN_INVALID_REVNUM;
          else
            *revision = svn_sqlite__column_revnum(stmt_base, 4);
        }
      if (repos_relpath)
        {
          if (have_work)
            {
              /* Our path is implied by our parent somewhere up the tree.
                 With the NULL value and status, the caller will know to
                 search up the tree for the base of our path.  */
              *repos_relpath = NULL;
            }
          else
            *repos_relpath = svn_sqlite__column_text(stmt_base, 1,
                                                     result_pool);
        }
      if (repos_root_url || repos_uuid)
        {
          /* Fetch repository information via REPOS_ID. If we have a
             WORKING_NODE (and have been added), then the repository
             we're being added to will be dependent upon a parent. The
             caller can scan upwards to locate the repository.  */
          if (have_work || svn_sqlite__column_is_null(stmt_base, 0))
            {
              if (repos_root_url)
                *repos_root_url = NULL;
              if (repos_uuid)
                *repos_uuid = NULL;
            }
          else
            err = svn_error_compose_create(
                     err,
                     fetch_repos_info(repos_root_url,
                                      repos_uuid,
                                      pdh->wcroot->sdb,
                                      svn_sqlite__column_int64(stmt_base, 0),
                                      result_pool));
        }
      if (changed_rev)
        {
          if (have_work)
            *changed_rev = svn_sqlite__column_revnum(stmt_work, 4);
          else
            *changed_rev = svn_sqlite__column_revnum(stmt_base, 7);
        }
      if (changed_date)
        {
          if (have_work)
            *changed_date = svn_sqlite__column_int64(stmt_work, 5);
          else
            *changed_date = svn_sqlite__column_int64(stmt_base, 8);
        }
      if (changed_author)
        {
          if (have_work)
            *changed_author = svn_sqlite__column_text(stmt_work, 6,
                                                      result_pool);
          else
            *changed_author = svn_sqlite__column_text(stmt_base, 9,
                                                      result_pool);
        }
      if (last_mod_time)
        {
          if (have_work)
            *last_mod_time = svn_sqlite__column_int64(stmt_work, 14);
          else
            *last_mod_time = svn_sqlite__column_int64(stmt_base, 12);
        }
      if (depth)
        {
          if (node_kind != svn_wc__db_kind_dir
                && node_kind != svn_wc__db_kind_subdir)
            {
              *depth = svn_depth_unknown;
            }
          else
            {
              const char *depth_str;

              if (have_work)
                depth_str = svn_sqlite__column_text(stmt_work, 7, NULL);
              else
                depth_str = svn_sqlite__column_text(stmt_base, 10, NULL);

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
              if (have_work)
                err2 = svn_sqlite__column_checksum(checksum, stmt_work, 2,
                                                   result_pool);
              else
                err2 = svn_sqlite__column_checksum(checksum, stmt_base, 5,
                                                   result_pool);

              if (err2 != NULL)
                err = svn_error_compose_create(
                         err,
                         svn_error_createf(
                               err->apr_err, err2,
                              _("The node '%s' has a corrupt checksum value."),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool)));
            }
        }
      if (translated_size)
        {
          if (have_work)
            *translated_size = get_translated_size(stmt_work, 3);
          else
            *translated_size = get_translated_size(stmt_base, 6);
        }
      if (target)
        {
          if (node_kind != svn_wc__db_kind_symlink)
            *target = NULL;
          else if (have_work)
            *target = svn_sqlite__column_text(stmt_work, 8, result_pool);
          else
            *target = svn_sqlite__column_text(stmt_base, 11, result_pool);
        }
      if (changelist)
        {
          if (have_act)
            *changelist = svn_sqlite__column_text(stmt_act, 1, result_pool);
          else
            *changelist = NULL;
        }
      if (original_repos_relpath)
        {
          if (have_work)
            *original_repos_relpath = svn_sqlite__column_text(stmt_work, 10,
                                                              result_pool);
          else
            *original_repos_relpath = NULL;
        }
      if (!have_work || svn_sqlite__column_is_null(stmt_work, 9))
        {
          if (original_root_url)
            *original_root_url = NULL;
          if (original_uuid)
            *original_uuid = NULL;
        }
      else if (original_root_url || original_uuid)
        {
          /* Fetch repository information via COPYFROM_REPOS_ID. */
          err = svn_error_compose_create(
                     err,
                     fetch_repos_info(original_root_url, original_uuid,
                                      pdh->wcroot->sdb,
                                      svn_sqlite__column_int64(stmt_work, 9),
                                      result_pool));
        }
      if (original_revision)
        {
          if (have_work)
            *original_revision = svn_sqlite__column_revnum(stmt_work, 11);
          else
            *original_revision = SVN_INVALID_REVNUM;
        }
      if (text_mod)
        {
          /* ### fix this */
          *text_mod = FALSE;
        }
      if (props_mod)
        {
          *props_mod = have_act && !svn_sqlite__column_is_null(stmt_act, 7);
        }
      if (base_shadowed)
        {
          *base_shadowed = have_base && have_work;
        }
      if (conflicted)
        {
          if (have_act)
            {
              *conflicted =
                 svn_sqlite__column_text(stmt_act, 2, NULL) || /* old */
                 svn_sqlite__column_text(stmt_act, 3, NULL) || /* new */
                 svn_sqlite__column_text(stmt_act, 4, NULL) || /* working */
                 svn_sqlite__column_text(stmt_act, 0, NULL); /* prop_reject */

              /* At the end of this function we check for tree conflicts */
            }
          else
            *conflicted = FALSE;
        }
      if (lock)
        {
          if (svn_sqlite__column_is_null(stmt_base, 14))
            *lock = NULL;
          else
            {
              *lock = apr_pcalloc(result_pool, sizeof(svn_wc__db_lock_t));
              (*lock)->token = svn_sqlite__column_text(stmt_base, 14,
                                                       result_pool);
              if (!svn_sqlite__column_is_null(stmt_base, 15))
                (*lock)->owner = svn_sqlite__column_text(stmt_base, 15,
                                                         result_pool);
              if (!svn_sqlite__column_is_null(stmt_base, 16))
                (*lock)->comment = svn_sqlite__column_text(stmt_base, 16,
                                                           result_pool);
              if (!svn_sqlite__column_is_null(stmt_base, 17))
                (*lock)->date = svn_sqlite__column_int64(stmt_base, 17);
            }
        }
    }
  else if (have_act)
    {
      /* A row in ACTUAL_NODE should never exist without a corresponding
         node in BASE_NODE and/or WORKING_NODE.  */
      err = svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                              _("Corrupt data for '%s'"),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool));
    }
  else
    {
      err = svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("The node '%s' was not found."),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool));
    }

  err = svn_error_compose_create(err, svn_sqlite__reset(stmt_base));
  err = svn_error_compose_create(err, svn_sqlite__reset(stmt_work));
  SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt_act)));

  /* ### And finally, check for tree conflicts via parent.
         This reuses stmt_act and throws an error in Sqlite if
         we do it directly */
  if (conflicted && !*conflicted)
    {
      const svn_wc_conflict_description2_t *cd;

      SVN_ERR(svn_wc__db_op_read_tree_conflict(&cd, db, local_abspath,
                                               scratch_pool, scratch_pool));

      *conflicted = (cd != NULL);
    }

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
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err = NULL;

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_SELECT_ACTUAL_PROPS, scratch_pool));
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
  return svn_error_return(
      svn_wc__db_read_pristine_props(props, db, local_abspath,
                                     result_pool, scratch_pool));
}


svn_error_t *
svn_wc__db_read_pristine_props(apr_hash_t **props,
                               svn_wc__db_t *db,
                               const char *local_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_error_t *err;

  *props = NULL;

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_SELECT_WORKING_PROPS, scratch_pool));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  /* If there is a WORKING row, then we're done.

     For adds/copies/moves, then properties are in this row.
     For deletes, there are no properties.

     Regardless, we never look at BASE. The properties (or not) are here.  */
  if (have_row)
    {
      err = svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                          scratch_pool);
      SVN_ERR(svn_error_compose_create(err, svn_sqlite__reset(stmt)));
      if (*props == NULL)
        {
          /* ### is this a DB constraint violation? the column "probably"
             ### should never be null. maybe we should leave it null for
             ### delete operations, so this is okay.  */
          *props = apr_hash_make(result_pool);
        }
      return SVN_NO_ERROR;
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  /* No WORKING node, so the props must be in the BASE node.  */
  return svn_error_return(svn_wc__db_base_get_props(props, db, local_abspath,
                                                    result_pool,
                                                    scratch_pool));
}


svn_error_t *
svn_wc__db_read_children(const apr_array_header_t **children,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  return gather_children(children, FALSE,
                         db, local_abspath, result_pool, scratch_pool);
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

  if (rb->local_relpath[0] == 0)
    like_arg = "%";
  else
    like_arg = apr_pstrcat(scratch_pool,
                           escape_sqlite_like(rb->local_relpath, scratch_pool),
                           "/%", NULL);

  /* Update non-NULL WORKING_NODE.copyfrom_repos_id. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                               STMT_UPDATE_WORKING_RECURSIVE_COPYFROM_REPO));
  SVN_ERR(svn_sqlite__bindf(stmt, "issi", rb->wc_id, rb->local_relpath,
                            like_arg, new_repos_id));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Do a bunch of stuff which is conditional on us actually having a
     base_node in the first place. */
  if (rb->have_base_node)
    {
      /* Purge the DAV cache (wcprops) from any BASE that have 'em. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_CLEAR_BASE_RECURSIVE_DAV_CACHE));
      SVN_ERR(svn_sqlite__bindf(stmt, "iss", rb->wc_id, rb->local_relpath,
                                like_arg));
      SVN_ERR(svn_sqlite__step_done(stmt));

      /* Update any BASE which have non-NULL repos_id's */
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_UPDATE_BASE_RECURSIVE_REPO));
      SVN_ERR(svn_sqlite__bindf(stmt, "issi", rb->wc_id, rb->local_relpath,
                                like_arg, new_repos_id));
      SVN_ERR(svn_sqlite__step_done(stmt));

      /* Update any locks for the root or its children. */
      if (rb->repos_relpath[0] == 0)
        like_arg = "%";
      else
        like_arg = apr_pstrcat(scratch_pool,
                           escape_sqlite_like(rb->repos_relpath, scratch_pool),
                           "/%", NULL);

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
                           svn_boolean_t single_db,  /* ### */
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  struct relocate_baton rb;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  SVN_ERR(parse_local_abspath(&pdh, &rb.local_relpath, db, local_dir_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* Get the existing repos_id of the base node, since we'll need it to
     update a potential lock. */
  /* ### is it faster to fetch fewer columns? */
  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id,
                            rb.local_relpath));
  SVN_ERR(svn_sqlite__step(&rb.have_base_node, stmt));
  if (rb.have_base_node)
    {
      rb.old_repos_id = svn_sqlite__column_int64(stmt, 0);
      rb.repos_relpath = svn_sqlite__column_text(stmt, 1, scratch_pool);
      SVN_ERR(svn_sqlite__reset(stmt));

      SVN_ERR(fetch_repos_info(NULL, &rb.repos_uuid, pdh->wcroot->sdb,
                               rb.old_repos_id, scratch_pool));
    }
  else
    {
      SVN_ERR(svn_sqlite__reset(stmt));
      SVN_ERR(svn_wc__db_scan_addition(NULL, NULL, NULL, NULL, &rb.repos_uuid,
                                       NULL, NULL, NULL, NULL,
                                       db, local_dir_abspath, scratch_pool,
                                       scratch_pool));
    }

  rb.wc_id = pdh->wcroot->wc_id;
  rb.repos_root_url = repos_root_url;

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb, relocate_txn, &rb,
                                       scratch_pool));

  if (!single_db)
    {
      /* ### Now, a bit of a dance because we don't yet have a centralized
             metadata store.  We need to update the repos_id in the databases
             of subdirectories. */
      apr_pool_t *iterpool;
      const apr_array_header_t *children;
      int i;

      iterpool = svn_pool_create(scratch_pool);
      SVN_ERR(svn_wc__db_read_children(&children, db, local_dir_abspath,
                                       scratch_pool, iterpool));

      for (i = 0; i < children->nelts; i++)
        {
          const char *child = APR_ARRAY_IDX(children, i, const char *);
          const char *child_abspath;
          svn_wc__db_kind_t kind;

          svn_pool_clear(iterpool);

          child_abspath = svn_dirent_join(local_dir_abspath, child, iterpool);
          SVN_ERR(svn_wc__db_read_info(NULL, &kind, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       NULL, NULL, NULL, NULL, NULL, NULL,
                                       db, child_abspath,
                                       iterpool, iterpool));
          if (kind != svn_wc__db_kind_dir)
            continue;

          /* Recurse on the child directory */
          SVN_ERR(svn_wc__db_global_relocate(db, child_abspath, repos_root_url,
                                             single_db, iterpool));
        }

      svn_pool_destroy(iterpool);
    }

  return SVN_NO_ERROR;
}


struct commit_baton {
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  svn_revnum_t new_revision;
  apr_time_t new_date;
  const char *new_author;
  const svn_checksum_t *new_checksum;
  const apr_array_header_t *new_children;
  apr_hash_t *new_dav_cache;
  svn_boolean_t keep_changelist;

  apr_int64_t repos_id;
  const char *repos_relpath;
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
  SVN_ERR(svn_sqlite__get_statement(&stmt_base, cb->pdh->wcroot->sdb,
                                    STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__get_statement(&stmt_work, cb->pdh->wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__get_statement(&stmt_act, cb->pdh->wcroot->sdb,
                                    STMT_SELECT_ACTUAL_NODE));

  SVN_ERR(svn_sqlite__bindf(stmt_base, "is",
                            cb->pdh->wcroot->wc_id, cb->local_relpath));
  SVN_ERR(svn_sqlite__bindf(stmt_work, "is",
                            cb->pdh->wcroot->wc_id, cb->local_relpath));
  SVN_ERR(svn_sqlite__bindf(stmt_act, "is",
                            cb->pdh->wcroot->wc_id, cb->local_relpath));

  SVN_ERR(svn_sqlite__step(&have_base, stmt_base));
  SVN_ERR(svn_sqlite__step(&have_work, stmt_work));
  SVN_ERR(svn_sqlite__step(&have_act, stmt_act));

  /* There should be something to commit!  */
  /* ### not true. we could simply have text changes. how to assert?
     SVN_ERR_ASSERT(have_work || have_act);  */

  /* Figure out the new node's kind. It will be whatever is in WORKING_NODE,
     or there will be a BASE_NODE that has it.  */
  if (have_work)
    new_kind = svn_sqlite__column_token(stmt_work, 1, kind_map);
  else
    new_kind = svn_sqlite__column_token(stmt_base, 3, kind_map);

  /* What will the new depth be?  */
  if (new_kind == svn_wc__db_kind_dir)
    {
      if (have_work)
        new_depth_str = svn_sqlite__column_text(stmt_work, 7, scratch_pool);
      else
        new_depth_str = svn_sqlite__column_text(stmt_base, 10, scratch_pool);
    }

  /* Get the repository information. REPOS_RELPATH will indicate whether
     we bind REPOS_ID/REPOS_RELPATH as null values in the database (in order
     to inherit values from the parent node), or that we have actual data.
     Note: only inherit if we're not at the root.  */
  if (have_base && !svn_sqlite__column_is_null(stmt_base, 0))
    {
      /* If 'repos_id' is valid, then 'repos_relpath' should be, too.  */
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
    prop_blob.data = svn_sqlite__column_blob(stmt_work, 15, &prop_blob.len,
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

#ifndef SINGLE_DB
  /* We're committing a file/symlink, or we're committing a dir at "". We
     never commit child directories (parent stubs).  */
  SVN_ERR_ASSERT(new_kind != svn_wc__db_kind_dir
                 || *cb->local_relpath == '\0');
#endif

  /* Update the BASE_NODE row with all the new information.  */

  if (*cb->local_relpath == '\0')
    parent_relpath = NULL;
  else
    parent_relpath = svn_relpath_dirname(cb->local_relpath, scratch_pool);

  /* ### other presences? or reserve that for separate functions?  */
  new_presence = svn_wc__db_status_normal;

  SVN_ERR(svn_sqlite__get_statement(&stmt, cb->pdh->wcroot->sdb,
                                    STMT_APPLY_CHANGES_TO_BASE));
  SVN_ERR(svn_sqlite__bindf(stmt, "issttisb",
                            cb->pdh->wcroot->wc_id, cb->local_relpath,
                            parent_relpath,
                            presence_map, new_presence,
                            kind_map, new_kind,
                            (apr_int64_t)cb->new_revision,
                            cb->new_author,
                            prop_blob.data, prop_blob.len));

  /* ### for now, always set the repos_id/relpath. we should make these
     ### null whenever possible. but that also means we'd have to check
     ### on whether this node is switched, so the values would need to
     ### remain unchanged.  */
  SVN_ERR(svn_sqlite__bind_int64(stmt, 9, cb->repos_id));
  SVN_ERR(svn_sqlite__bind_text(stmt, 10, cb->repos_relpath));

  SVN_ERR(svn_sqlite__bind_checksum(stmt, 11, cb->new_checksum,
                                    scratch_pool));
  if (cb->new_date > 0)
    SVN_ERR(svn_sqlite__bind_int64(stmt, 12, cb->new_date));
  SVN_ERR(svn_sqlite__bind_text(stmt, 13, new_depth_str));
  /* ### 14. target.  */
  SVN_ERR(svn_sqlite__bind_properties(stmt, 15, cb->new_dav_cache,
                                      scratch_pool));

  SVN_ERR(svn_sqlite__step_done(stmt));

  if (have_work)
    {
      /* Get rid of the WORKING_NODE row.  */
      SVN_ERR(svn_sqlite__get_statement(&stmt, cb->pdh->wcroot->sdb,
                                        STMT_DELETE_WORKING_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                cb->pdh->wcroot->wc_id, cb->local_relpath));
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
                    &stmt, cb->pdh->wcroot->sdb,
                    STMT_RESET_ACTUAL_WITH_CHANGELIST));
          SVN_ERR(svn_sqlite__bindf(stmt, "isss",
                                    cb->pdh->wcroot->wc_id,
                                    cb->local_relpath,
                                    svn_relpath_dirname(cb->local_relpath,
                                                        scratch_pool),
                                    changelist));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }
      else
        {
          /* Toss the ACTUAL_NODE row.  */
          SVN_ERR(svn_sqlite__get_statement(&stmt, cb->pdh->wcroot->sdb,
                                            STMT_DELETE_ACTUAL_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                    cb->pdh->wcroot->wc_id,
                                    cb->local_relpath));
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

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
determine_repos_info(apr_int64_t *repos_id,
                     const char **repos_relpath,
                     svn_wc__db_t *db,
                     svn_wc__db_pdh_t *pdh,
                     const char *local_relpath,
                     const char *name,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *repos_parent_relpath;

  /* ### is it faster to fetch fewer columns? */

  /* Prefer the current node's repository information.  */
  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row && !svn_sqlite__column_is_null(stmt, 0))
    {
      /* If one is non-NULL, then so should the other. */
      SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 1));

      *repos_id = svn_sqlite__column_int64(stmt, 0);
      *repos_relpath = svn_sqlite__column_text(stmt, 1, result_pool);

      return svn_error_return(svn_sqlite__reset(stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  /* The parent MUST have a BASE node (otherwise, THIS node cannot be
     processed for a commit). Move up and re-query.   */

  if (*local_relpath == '\0')
    {
      /* There is no entry for "" in the BASE_NODE table, so this directory
         is just now being added. Therefore, the stub in the parent dir
         does not exist either. We want to jump to the logical parent node,
         which means one PDH up, and stick to local_relpath == "".  */
      SVN_ERR(navigate_to_parent(&pdh, db, pdh,
                                 svn_sqlite__mode_readonly,
                                 scratch_pool));
      local_relpath = "";
    }
  else
    {
      /* This was a child node within this wcroot. We want to look at the
         BASE node of the directory, which is local_relpath == "".  */
      local_relpath = "";
    }

  /* The REPOS_ID will be the same (### until we support mixed-repos)  */
  SVN_ERR(scan_upwards_for_repos(repos_id, &repos_parent_relpath,
                                 pdh->wcroot, pdh->local_abspath,
                                 "" /* local_relpath. see above.  */,
                                 scratch_pool, scratch_pool));

  *repos_relpath = svn_relpath_join(repos_parent_relpath, name, result_pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_global_commit(svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_revnum_t new_revision,
                         apr_time_t new_date,
                         const char *new_author,
                         const svn_checksum_t *new_checksum,
                         const apr_array_header_t *new_children,
                         apr_hash_t *new_dav_cache,
                         svn_boolean_t keep_changelist,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  struct commit_baton cb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_revision));
  SVN_ERR_ASSERT(new_checksum == NULL || new_children == NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  cb.pdh = pdh;
  cb.local_relpath = local_relpath;

  cb.new_revision = new_revision;
  cb.new_date = new_date;
  cb.new_author = new_author;
  cb.new_checksum = new_checksum;
  cb.new_children = new_children;
  cb.new_dav_cache = new_dav_cache;
  cb.keep_changelist = keep_changelist;

  /* If we are adding a directory (no BASE_NODE), then we need to get
     repository information from an ancestor node (start scanning from the
     parent node since "this node" does not have a BASE). We cannot simply
     inherit that information (across SDB boundaries).

     If we're adding a file, then leaving the fields as null (in order to
     inherit) would be possible.

     For existing nodes, we should retain the (potentially-switched)
     repository information.

     ### this always returns values. we should switch to null if/when
     ### possible.  */
  SVN_ERR(determine_repos_info(&cb.repos_id, &cb.repos_relpath,
                               db, pdh, local_relpath,
                               svn_dirent_basename(local_abspath,
                                                   scratch_pool),
                               scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb, commit_node, &cb,
                                       scratch_pool));

  /* We *totally* monkeyed the entries. Toss 'em.  */
  flush_entries(pdh);

  return SVN_NO_ERROR;
}


struct update_baton {
  svn_wc__db_pdh_t *pdh;
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
  svn_wc__db_pdh_t *pdh;
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

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  ub.pdh = pdh;
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
  flush_entries(pdh);

  return SVN_NO_ERROR;
}


struct record_baton {
  apr_int64_t wc_id;
  const char *local_relpath;

  svn_filesize_t translated_size;
  apr_time_t last_mod_time;

  /* For error reporting.  */
  const char *local_abspath;
};


/* Record TRANSLATED_SIZE and LAST_MOD_TIME into the WORKING tree if a
   node is present; otherwise, record it into the BASE tree. One of them
   must exist.  */
static svn_error_t *
record_fileinfo(void *baton, svn_sqlite__db_t *sdb, apr_pool_t *scratch_pool)
{
  struct record_baton *rb = baton;
  svn_boolean_t base_exists;
  svn_boolean_t working_exists;
  svn_sqlite__stmt_t *stmt;
  int affected_rows;

  SVN_ERR(which_trees_exist(&base_exists, &working_exists,
                            sdb, rb->wc_id, rb->local_relpath));
  if (!base_exists && !working_exists)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("Could not find node '%s' for recording file "
                               "information."),
                             svn_dirent_local_style(rb->local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    working_exists
                                      ? STMT_UPDATE_WORKING_FILEINFO
                                      : STMT_UPDATE_BASE_FILEINFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "isii",
                            rb->wc_id, rb->local_relpath,
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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  struct record_baton rb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  rb.wc_id = pdh->wcroot->wc_id;
  rb.local_relpath = local_relpath;

  rb.translated_size = translated_size;
  rb.last_mod_time = last_mod_time;

  rb.local_abspath = local_abspath;

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb, record_fileinfo, &rb,
                                       scratch_pool));

  /* We *totally* monkeyed the entries. Toss 'em.  */
  flush_entries(pdh);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_lock_add(svn_wc__db_t *db,
                    const char *local_abspath,
                    const svn_wc__db_lock_t *lock,
                    apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  const char *repos_relpath;
  apr_int64_t repos_id;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(lock != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(scan_upwards_for_repos(&repos_id, &repos_relpath,
                                 pdh->wcroot, local_abspath, local_relpath,
                                 scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_INSERT_LOCK));
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
  flush_entries(pdh);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_lock_remove(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  const char *repos_relpath;
  apr_int64_t repos_id;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(scan_upwards_for_repos(&repos_id, &repos_relpath,
                                 pdh->wcroot, local_abspath, local_relpath,
                                 scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_DELETE_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", repos_id, repos_relpath));

  SVN_ERR(svn_sqlite__step_done(stmt));

  /* There may be some entries, and the lock info is now out of date.  */
  flush_entries(pdh);

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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  apr_int64_t repos_id;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(scan_upwards_for_repos(&repos_id, repos_relpath,
                                 pdh->wcroot, local_abspath, local_relpath,
                                 result_pool, scratch_pool));

  if (repos_root_url || repos_uuid)
    return fetch_repos_info(repos_root_url, repos_uuid, pdh->wcroot->sdb,
                            repos_id, result_pool);

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
  const char *current_abspath = local_abspath;
  const char *current_relpath;
  const char *child_abspath = NULL;
  const char *build_relpath = "";
  svn_wc__db_pdh_t *pdh;
  svn_boolean_t found_info = FALSE;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Initialize all the OUT parameters. Generally, we'll only be filling
     in a subset of these, so it is easier to init all up front. Note that
     the STATUS parameter will be initialized once we read the status of
     the specified node.  */
  if (op_root_abspath)
    *op_root_abspath = NULL;
  if (repos_relpath)
    *repos_relpath = NULL;
  if (repos_root_url)
    *repos_root_url = NULL;
  if (repos_uuid)
    *repos_uuid = NULL;
  if (original_repos_relpath)
    *original_repos_relpath = NULL;
  if (original_root_url)
    *original_root_url = NULL;
  if (original_uuid)
    *original_uuid = NULL;
  if (original_revision)
    *original_revision = SVN_INVALID_REVNUM;

  SVN_ERR(parse_local_abspath(&pdh, &current_relpath, db, current_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  while (TRUE)
    {
      svn_sqlite__stmt_t *stmt;
      svn_boolean_t have_row;
      svn_wc__db_status_t presence;

      /* ### is it faster to fetch fewer columns? */
      SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                        STMT_SELECT_WORKING_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                pdh->wcroot->wc_id, current_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (!have_row)
        {
          if (current_abspath == local_abspath)
            /* ### maybe we should return a usage error instead?  */
            return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                                     svn_sqlite__reset(stmt),
                                     _("The node '%s' was not found."),
                                     svn_dirent_local_style(local_abspath,
                                                            scratch_pool));
          SVN_ERR(svn_sqlite__reset(stmt));

          /* We just fell off the top of the WORKING tree. If we haven't
             found the operation root, then the child node that we just
             left was that root.  */
          if (op_root_abspath && *op_root_abspath == NULL)
            {
              SVN_ERR_ASSERT(child_abspath != NULL);
              *op_root_abspath = apr_pstrdup(result_pool, child_abspath);
            }

          /* This node was added/copied/moved and has an implicit location
             in the repository. We now need to traverse BASE nodes looking
             for repository info.  */
          break;
        }

      presence = svn_sqlite__column_token(stmt, 0, presence_map);

      /* Record information from the starting node.  */
      if (current_abspath == local_abspath)
        {
          /* The starting node should exist normally.  */
          if (presence != svn_wc__db_status_normal)
            return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                                     svn_sqlite__reset(stmt),
                                     _("Expected node '%s' to be added."),
                                     svn_dirent_local_style(local_abspath,
                                                            scratch_pool));

          /* ### in per-dir operation, it is possible that we just fetched
             ### the parent stub. examine the KIND field.
             ###
             ### scan_addition is NOT allowed for an obstructed_add status
             ### from read_info. there may be key information in the
             ### subdir record (eg. copyfrom_*).  */
          {
            svn_wc__db_kind_t kind = svn_sqlite__column_token(stmt, 1,
                                                              kind_map);
            SVN_ERR_ASSERT(kind != svn_wc__db_kind_subdir);
          }

          /* Provide the default status; we'll override as appropriate. */
          if (status)
            *status = svn_wc__db_status_added;
        }

      /* We want the operation closest to the start node, and then we
         ignore any operations on its ancestors.  */
      if (!found_info
          && presence == svn_wc__db_status_normal
          && !svn_sqlite__column_is_null(stmt, 9 /* copyfrom_repos_id */))
        {
          if (status)
            {
              if (svn_sqlite__column_boolean(stmt, 12 /* moved_here */))
                *status = svn_wc__db_status_moved_here;
              else
                *status = svn_wc__db_status_copied;
            }
          if (op_root_abspath)
            *op_root_abspath = apr_pstrdup(result_pool, current_abspath);
          if (original_repos_relpath)
            *original_repos_relpath = svn_sqlite__column_text(stmt, 10,
                                                              result_pool);
          if (original_root_url || original_uuid)
            SVN_ERR(fetch_repos_info(original_root_url, original_uuid,
                                     pdh->wcroot->sdb,
                                     svn_sqlite__column_int64(stmt, 9),
                                     result_pool));
          if (original_revision)
            *original_revision = svn_sqlite__column_revnum(stmt, 11);

          /* We may have to keep tracking upwards for REPOS_* values.
             If they're not needed, then just return.  */
          if (repos_relpath == NULL
              && repos_root_url == NULL
              && repos_uuid == NULL)
            return svn_error_return(svn_sqlite__reset(stmt));

          /* We've found the info we needed. Scan for the top of the
             WORKING tree, and then the REPOS_* information.  */
          found_info = TRUE;
        }

      SVN_ERR(svn_sqlite__reset(stmt));

      /* If the caller wants to know the starting node's REPOS_RELPATH,
         then keep track of what we're stripping off the ABSPATH as we
         traverse up the tree.  */
      if (repos_relpath)
        {
          build_relpath = svn_relpath_join(svn_dirent_basename(current_abspath,
                                                              scratch_pool),
                                           build_relpath,
                                           scratch_pool);
        }

      /* Move to the parent node. Remember the abspath to this node, since
         it could be the root of an add/delete.  */
      child_abspath = current_abspath;
      if (strcmp(current_abspath, pdh->local_abspath) == 0)
        {
          /* The current node is a directory, so move to the parent dir.  */
          SVN_ERR(navigate_to_parent(&pdh, db, pdh, svn_sqlite__mode_readonly,
                                     scratch_pool));
        }
      current_abspath = pdh->local_abspath;
      current_relpath = compute_pdh_relpath(pdh, NULL);
    }

  /* If we're here, then we have an added/copied/moved (start) node, and
     CURRENT_ABSPATH now points to a BASE node. Figure out the repository
     information for the current node, and use that to compute the start
     node's repository information.  */
  if (repos_relpath || repos_root_url || repos_uuid)
    {
      const char *base_relpath;

      /* ### unwrap this. we can optimize away the parse_local_abspath.  */
      SVN_ERR(svn_wc__db_scan_base_repos(&base_relpath, repos_root_url,
                                         repos_uuid, db, current_abspath,
                                         result_pool, scratch_pool));

      if (repos_relpath)
        *repos_relpath = svn_relpath_join(base_relpath, build_relpath,
                                          result_pool);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_scan_deletion(const char **base_del_abspath,
                         svn_boolean_t *base_replaced,
                         const char **moved_to_abspath,
                         const char **work_del_abspath,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  const char *current_abspath = local_abspath;
  const char *current_relpath;
  const char *child_abspath = NULL;
  svn_wc__db_status_t child_presence;
  svn_boolean_t child_has_base = FALSE;
  svn_boolean_t found_moved_to = FALSE;
  svn_wc__db_pdh_t *pdh;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Initialize all the OUT parameters.  */
  if (base_del_abspath != NULL)
    *base_del_abspath = NULL;
  if (base_replaced != NULL)
    *base_replaced = FALSE;  /* becomes TRUE when we know for sure.  */
  if (moved_to_abspath != NULL)
    *moved_to_abspath = NULL;
  if (work_del_abspath != NULL)
    *work_del_abspath = NULL;

  /* Initialize to something that won't denote an important parent/child
     transition.  */
  child_presence = svn_wc__db_status_base_deleted;

  SVN_ERR(parse_local_abspath(&pdh, &current_relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  while (TRUE)
    {
      svn_sqlite__stmt_t *stmt;
      svn_boolean_t have_row;
      svn_boolean_t have_base;
      svn_wc__db_status_t work_presence;

      SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                        STMT_SELECT_DELETION_INFO));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                pdh->wcroot->wc_id, current_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (!have_row)
        {
          /* There better be a row for the starting node!  */
          if (current_abspath == local_abspath)
            return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND,
                                     svn_sqlite__reset(stmt),
                                     _("The node '%s' was not found."),
                                     svn_dirent_local_style(local_abspath,
                                                            scratch_pool));

          /* There are no values, so go ahead and reset the stmt now.  */
          SVN_ERR(svn_sqlite__reset(stmt));

          /* No row means no WORKING node at this path, which means we just
             fell off the top of the WORKING tree.

             If the child was not-present this implies the root of the
             (added) WORKING subtree was deleted.  This can occur
             during post-commit processing when the copied parent that
             was in the WORKING tree has been moved to the BASE tree. */
          if (work_del_abspath != NULL
              && child_presence == svn_wc__db_status_not_present
              && *work_del_abspath == NULL)
            *work_del_abspath = apr_pstrdup(result_pool, child_abspath);

          /* If the child did not have a BASE node associated with it, then
             we're looking at a deletion that occurred within an added tree.
             There is no root of a deleted/replaced BASE tree.

             If the child was base-deleted, then the whole tree is a
             simple (explicit) deletion of the BASE tree.

             If the child was normal, then it is the root of a replacement,
             which means an (implicit) deletion of the BASE tree.

             In both cases, set the root of the operation (if we have not
             already set it as part of a moved-away).  */
          if (base_del_abspath != NULL
              && child_has_base
              && *base_del_abspath == NULL)
            *base_del_abspath = apr_pstrdup(result_pool, child_abspath);

          /* We found whatever roots we needed. This BASE node and its
             ancestors are unchanged, so we're done.  */
          break;
        }

      /* We need the presence of the WORKING node. Note that legal values
         are: normal, not-present, base-deleted.  */
      work_presence = svn_sqlite__column_token(stmt, 1, presence_map);

      /* The starting node should be deleted.  */
      if (current_abspath == local_abspath
          && work_presence != svn_wc__db_status_not_present
          && work_presence != svn_wc__db_status_base_deleted)
        return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS,
                                 svn_sqlite__reset(stmt),
                                 _("Expected node '%s' to be deleted."),
                                 svn_dirent_local_style(local_abspath,
                                                        scratch_pool));
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

          /* If we're looking at a present BASE node, *and* there is a
             WORKING node (present or deleted), then a replacement has
             occurred here or in an ancestor.  */
          if (base_replaced != NULL
              && base_presence == svn_wc__db_status_normal
              && work_presence != svn_wc__db_status_base_deleted)
            {
              *base_replaced = TRUE;
            }
        }

      /* Only grab the nearest ancestor.  */
      if (!found_moved_to &&
          (moved_to_abspath != NULL || base_del_abspath != NULL)
          && !svn_sqlite__column_is_null(stmt, 2 /* moved_to */))
        {
          /* There better be a BASE_NODE (that was moved-away).  */
          SVN_ERR_ASSERT(have_base);

          found_moved_to = TRUE;

          /* This makes things easy. It's the BASE_DEL_ABSPATH!  */
          if (base_del_abspath != NULL)
            *base_del_abspath = apr_pstrdup(result_pool, current_abspath);

          if (moved_to_abspath != NULL)
            *moved_to_abspath = svn_dirent_join(
                                    pdh->wcroot->abspath,
                                    svn_sqlite__column_text(stmt, 2, NULL),
                                    result_pool);
        }

      if (work_del_abspath != NULL
          && work_presence == svn_wc__db_status_normal
          && child_presence == svn_wc__db_status_not_present)
        {
          /* Parent is normal, but child was deleted. Therefore, the child
             is the root of a WORKING subtree deletion.  */
          *work_del_abspath = apr_pstrdup(result_pool, child_abspath);
        }

      /* We're all done examining the return values.  */
      SVN_ERR(svn_sqlite__reset(stmt));

      /* Move to the parent node. Remember the information about this node
         for our parent to use.  */
      child_abspath = current_abspath;
      child_presence = work_presence;
      child_has_base = have_base;
      if (strcmp(current_abspath, pdh->local_abspath) == 0)
        {
          /* The current node is a directory, so move to the parent dir.  */
          SVN_ERR(navigate_to_parent(&pdh, db, pdh, svn_sqlite__mode_readonly,
                                     scratch_pool));
        }
      current_abspath = pdh->local_abspath;
      current_relpath = compute_pdh_relpath(pdh, NULL);
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
                                   apr_hash_t *cache_values,
                                   apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_int64_t wc_id;
  apr_hash_index_t *hi;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(fetch_wc_id(&wc_id, sdb, iterpool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_UPDATE_BASE_DAV_CACHE));

  /* Iterate over all the wcprops, writing each one to the wc_db. */
  for (hi = apr_hash_first(scratch_pool, cache_values);
       hi;
       hi = apr_hash_next(hi))
    {
      const char *local_relpath = svn__apr_hash_index_key(hi);
      apr_hash_t *props = svn__apr_hash_index_val(hi);

      svn_pool_clear(iterpool);

      SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
      SVN_ERR(svn_sqlite__bind_properties(stmt, 3, props, iterpool));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  svn_pool_destroy(iterpool);

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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));
  SVN_ERR_ASSERT(work_item != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, wri_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

#ifndef SINGLE_DB
  if (*local_relpath != '\0')
    {
      svn_wc__db_kind_t kind;

      SVN_ERR(svn_wc__db_read_kind(&kind, db, wri_abspath, TRUE,
                                   scratch_pool));
      if (kind == svn_wc__db_kind_dir)
        {
          /* This node is a directory which is not on disk (since
             LOCAL_RELPATH is specifying the stub). Therefore, the
             work queue does not exist.  */
          return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                   _("There is no work queue for '%s'."),
                                   svn_dirent_local_style(wri_abspath,
                                                          scratch_pool));
        }
    }
#endif

  /* Add the work item(s) to the WORK_QUEUE.  */
  return svn_error_return(add_work_items(pdh->wcroot->sdb,
                                         work_item,
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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(id != NULL);
  SVN_ERR_ASSERT(work_item != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, wri_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

#ifndef SINGLE_DB
  if (*local_relpath != '\0')
    {
      svn_wc__db_kind_t kind;

      SVN_ERR(svn_wc__db_read_kind(&kind, db, wri_abspath, TRUE,
                                   scratch_pool));
      if (kind == svn_wc__db_kind_dir)
        {
          /* This node is a directory which is not on disk (since
             LOCAL_RELPATH is specifying the stub). Therefore, it
             has no items in the work queue.  */
          *id = 0;
          *work_item = NULL;
          return SVN_NO_ERROR;
        }
    }
#endif

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));
  SVN_ERR_ASSERT(id != 0);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, wri_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

#ifndef SINGLE_DB
  if (*local_relpath != '\0')
    {
      svn_wc__db_kind_t kind;

      SVN_ERR(svn_wc__db_read_kind(&kind, db, wri_abspath, TRUE,
                                   scratch_pool));
      if (kind == svn_wc__db_kind_dir)
        {
          /* This node is a directory which is not on disk (since
             LOCAL_RELPATH is specifying the stub). Therefore, the
             work queue does not exist, and this work item has been
             (implicitly) removed/completed.  */
          return SVN_NO_ERROR;
        }
    }
#endif

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
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

  /* ### for per-dir layouts, the wcroot should be this directory. under
     ### wc-ng, the wcroot may have become set for this missing subdir.  */
  if (pdh != NULL && pdh->wcroot != NULL
      && strcmp(local_dir_abspath, pdh->wcroot->abspath) != 0)
    {
      /* Forget the WCROOT. The subdir may have been missing when this
         got set, but has since been constructed.  */
      pdh->wcroot = NULL;
    }

  /* If the PDH isn't present, or have wcroot information, then do a full
     upward traversal to find the wcroot.  */
  if (pdh == NULL || pdh->wcroot == NULL)
    {
      const char *local_relpath;
      svn_error_t *err;

      err = parse_local_abspath(&pdh, &local_relpath, db, local_dir_abspath,
                                svn_sqlite__mode_readonly,
                                scratch_pool, scratch_pool);
      /* NOTE: pdh does *not* have to have a usable format.  */

      /* If we hit an error examining this directory, then declare this
         directory to not be a working copy.  */
      /* ### for per-dir layouts, the wcroot should be this directory,
         ### so bail if the PDH is a parent (and, thus, local_relpath is
         ### something besides "").  */
      if (err || *local_relpath != '\0')
        {
          if (err && err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
            return svn_error_return(err);
          svn_error_clear(err);

          /* We might turn this directory into a wcroot later, so let's
             just forget what we (didn't) find. The wcroot is still
             hanging off a parent though.
             Don't clear the wcroot of a parent if we just found a
             relative path here or we get multiple wcroot issues. */
          if (err)
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
svn_wc__db_temp_reset_format(int format,
                             svn_wc__db_t *db,
                             const char *local_dir_abspath,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  SVN_ERR_ASSERT(format >= 1);
  /* ### assert that we were passed a directory?  */

  /* Do not create a PDH. If we don't have one, then we don't have any
     cached version information.  */
  pdh = svn_wc__db_pdh_get_or_create(db, local_dir_abspath, FALSE,
                                     scratch_pool);
  if (pdh != NULL)
    {
      /* ### ideally, we would reset this to UNKNOWN, and then read the working
         ### copy to see what format it is in. however, we typically *write*
         ### whatever we *read*. so to break the cycle and write a different
         ### version (during upgrade), then we have to force a new format.  */

      /* ### since this is a temporary API, I feel I can indulge in a hack
         ### here.  If we are upgrading *to* wc-ng, we need to blow away the
         ### pdh->wcroot member.  If we are upgrading to format 11 (pre-wc-ng),
         ### we just need to store the format number.  */
      pdh->wcroot = NULL;
    }

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

      apr_hash_this(hi, &key, &klen, &val);
      pdh = val;

      if (!svn_dirent_is_ancestor(local_dir_abspath, pdh->local_abspath))
        continue;

      SVN_ERR(svn_wc__db_wclock_remove(db, pdh->local_abspath, scratch_pool));
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
  svn_wc__db_pdh_t *pdh;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_dir_abspath));

  /* ### we really need to assert that we were passed a directory. sometimes
     ### adm_retrieve_internal is asked about a file, and then it asks us
     ### for an access baton for it. we should definitely return NULL, but
     ### ideally: the caller would never ask us about a non-directory.  */

  /* Do not create a PDH. If we don't have one, then we don't have an
     access baton.  */
  pdh = svn_wc__db_pdh_get_or_create(db, local_dir_abspath, FALSE,
                                     scratch_pool);

  return pdh ? pdh->adm_access : NULL;
}


/* ### temporary API. remove before release.  */
void
svn_wc__db_temp_set_access(svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           svn_wc_adm_access_t *adm_access,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  pdh = svn_wc__db_pdh_get_or_create(db, local_dir_abspath, TRUE,
                                     scratch_pool);

  /* Better not override something already there.  */
  SVN_ERR_ASSERT_NO_RETURN(pdh->adm_access == NULL);
  pdh->adm_access = adm_access;
}


/* ### temporary API. remove before release.  */
svn_error_t *
svn_wc__db_temp_close_access(svn_wc__db_t *db,
                             const char *local_dir_abspath,
                             svn_wc_adm_access_t *adm_access,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  /* Do not create a PDH. If we don't have one, then we don't have an
     access baton to close.  */
  pdh = svn_wc__db_pdh_get_or_create(db, local_dir_abspath, FALSE,
                                     scratch_pool);
  if (pdh != NULL)
    {
      /* We should be closing the correct one, *or* it's already closed.  */
      SVN_ERR_ASSERT_NO_RETURN(pdh->adm_access == adm_access
                               || pdh->adm_access == NULL);
      pdh->adm_access = NULL;
    }

  return SVN_NO_ERROR;
}


/* ### temporary API. remove before release.  */
void
svn_wc__db_temp_clear_access(svn_wc__db_t *db,
                             const char *local_dir_abspath,
                             apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;

  SVN_ERR_ASSERT_NO_RETURN(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  /* Do not create a PDH. If we don't have one, then we don't have an
     access baton to clear out.  */
  pdh = svn_wc__db_pdh_get_or_create(db, local_dir_abspath, FALSE,
                                     scratch_pool);
  if (pdh != NULL)
    pdh->adm_access = NULL;
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
      const void *key = svn__apr_hash_index_key(hi);
      const svn_wc__db_pdh_t *pdh = svn__apr_hash_index_val(hi);

      if (pdh->adm_access != NULL)
        apr_hash_set(result, key, APR_HASH_KEY_STRING, pdh->adm_access);
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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__mode_t smode;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));

  if (mode == svn_wc__db_openmode_readonly)
    smode = svn_sqlite__mode_readonly;
  else
    smode = svn_sqlite__mode_readwrite;

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath,
                              db, local_dir_abspath, smode,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* We better be looking at the proper wcroot for this directory.
     If we ended up with a stub, then the subdirectory (and its SDB!)
     are missing.  */
  SVN_ERR_ASSERT(*local_relpath == '\0');

  *sdb = pdh->wcroot->sdb;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_is_dir_deleted(svn_boolean_t *not_present,
                               svn_revnum_t *base_revision,
                               svn_wc__db_t *db,
                               const char *local_dir_abspath,
                               apr_pool_t *scratch_pool)
{
  const char *parent_abspath;
  const char *base_name;
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  SVN_ERR_ASSERT(not_present != NULL);
  SVN_ERR_ASSERT(base_revision != NULL);

  svn_dirent_split(local_dir_abspath, &parent_abspath, &base_name,
                   scratch_pool);

  /* The parent should be a working copy if this function is called.
     Basically, the child is in an "added" state, which is not possible
     for a working copy root.  */
  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, parent_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* Build the local_relpath for the requested directory.  */
  local_relpath = svn_dirent_join(local_relpath, base_name, scratch_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_SELECT_PARENT_STUB_INFO));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));

  /* There MAY be a BASE_NODE row in the parent directory. It is entirely
     possible the parent only has WORKING_NODE rows. If there is no BASE_NODE,
     then we certainly aren't looking at a 'not-present' row.  */
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  *not_present = have_row && svn_sqlite__column_int(stmt, 0);
  if (*not_present)
    {
      *base_revision = svn_sqlite__column_revnum(stmt, 1);
    }
  /* else don't touch *BASE_REVISION.  */

  return svn_error_return(svn_sqlite__reset(stmt));
}

svn_error_t *
svn_wc__db_temp_determine_keep_local(svn_boolean_t *keep_local,
                                     svn_wc__db_t *db,
                                     const char *local_abspath,
                                     apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  /* ### This will fail for nodes that don't have a WORKING_NODE record,
         but this is not an issue for this function, as this call is only
         valid for deleted nodes anyway. */
  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_SELECT_KEEP_LOCAL_FLAG, scratch_pool));
  SVN_ERR(svn_sqlite__step_row(stmt));

  *keep_local = svn_sqlite__column_boolean(stmt, 0);

  return svn_error_return(svn_sqlite__reset(stmt));
}

svn_error_t *
svn_wc__db_temp_set_keep_local(svn_wc__db_t *db,
                               const char *local_abspath,
                               svn_boolean_t keep_local,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* First flush the entries */
  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  flush_entries(pdh);

  /* Then update the database */
  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_UPDATE_KEEP_LOCAL_FLAG, scratch_pool));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 3, keep_local ? 1 : 0));

  return svn_error_return(svn_sqlite__step_done(stmt));
}

svn_error_t *
svn_wc__db_read_conflict_victims(const apr_array_header_t **victims,
                                 svn_wc__db_t *db,
                                 const char *local_abspath,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  const char *tree_conflict_data;
  svn_boolean_t have_row;
  apr_hash_t *found;

  *victims = NULL;

  /* The parent should be a working copy directory. */
  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* ### This will be much easier once we have all conflicts in one
         field of actual*/

  /* First look for text and property conflicts in ACTUAL */
  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_SELECT_ACTUAL_CONFLICT_VICTIMS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));

  found = apr_hash_make(result_pool);

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);
      const char *child_name = svn_dirent_basename(child_relpath, result_pool);

      apr_hash_set(found, child_name, APR_HASH_KEY_STRING, child_name);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  /* And add tree conflicts */
  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_SELECT_ACTUAL_TREE_CONFLICT));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    tree_conflict_data = svn_sqlite__column_text(stmt, 0, scratch_pool);
  else
    tree_conflict_data = NULL;

  SVN_ERR(svn_sqlite__reset(stmt));

  if (tree_conflict_data)
    {
      apr_hash_t *conflict_items;
      apr_hash_index_t *hi;
      SVN_ERR(svn_wc__read_tree_conflicts(&conflict_items, tree_conflict_data,
                                          local_abspath, scratch_pool));

      for(hi = apr_hash_first(scratch_pool, conflict_items);
          hi;
          hi = apr_hash_next(hi))
        {
          const char *child_name =
              svn_dirent_basename(svn__apr_hash_index_key(hi), result_pool);

          /* Using a hash avoids duplicates */
          apr_hash_set(found, child_name, APR_HASH_KEY_STRING, child_name);
        }
    }

  SVN_ERR(svn_hash_keys((apr_array_header_t **)victims, found, result_pool));

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_read_conflicts(const apr_array_header_t **conflicts,
                          svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_array_header_t *cflcts;

  /* The parent should be a working copy directory. */
  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* ### This will be much easier once we have all conflicts in one
         field of actual.*/

  /* First look for text and property conflicts in ACTUAL */
  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_SELECT_CONFLICT_DETAILS));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));

  cflcts = apr_array_make(result_pool, 4,
                           sizeof(svn_wc_conflict_description2_t*));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      const char *prop_reject;
      const char *conflict_old;
      const char *conflict_new;
      const char *conflict_working;

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
    }

  SVN_ERR(svn_sqlite__reset(stmt));

  /* ### Tree conflicts are still stored on the directory */
  {
    const svn_wc_conflict_description2_t *desc;

    SVN_ERR(svn_wc__db_op_read_tree_conflict(&desc,
                                             db, local_abspath,
                                             result_pool, scratch_pool));

    if (desc)
      APR_ARRAY_PUSH(cflcts, const svn_wc_conflict_description2_t*) = desc;
  }

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
  svn_error_t *err;

  err = svn_wc__db_read_info(NULL, kind, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL,
                             db, local_abspath, scratch_pool, scratch_pool);
  if (!err)
    return SVN_NO_ERROR;

  if (allow_missing && err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      *kind = svn_wc__db_kind_unknown;
      return SVN_NO_ERROR;
    }

  return svn_error_return(err);
}


svn_error_t *
svn_wc__db_node_hidden(svn_boolean_t *hidden,
                       svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_wc__db_status_t base_status;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  /* Check two things: does a WORKING node exist, and what is the BASE
     status? */

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* First check the working node. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  /* If a working node exists, the node will not be hidden. */
  if (have_row)
    {
      *hidden = FALSE;
      return SVN_NO_ERROR;
    }

  /* Now check the BASE node's presence and depth. */
  SVN_ERR(svn_wc__db_base_get_info(&base_status, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, db, local_abspath,
                                   scratch_pool, scratch_pool));

  *hidden = (base_status == svn_wc__db_status_absent
             || base_status == svn_wc__db_status_not_present
             || base_status == svn_wc__db_status_excluded);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_wcroot_tempdir(const char **temp_dir_abspath,
                               svn_wc__db_t *db,
                               const char *wri_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR_ASSERT(temp_dir_abspath != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, wri_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  *temp_dir_abspath = svn_dirent_join_many(result_pool,
                                           pdh->wcroot->abspath,
                                           svn_wc_get_adm_dir(scratch_pool),
                                           WCROOT_TEMPDIR_RELPATH,
                                           NULL);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_wclock_set(svn_wc__db_t *db,
                      const char *local_abspath,
                      int levels_to_lock,
                      apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_error_t *err;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* ### Can only lock this directory in the per-dir layout.  This is
     ### a temporary restriction until metadata gets centralised.
     ### Perhaps this should be a runtime error, rather than an
     ### assert?  Perhaps check the path is versioned? */
  SVN_ERR_ASSERT(*local_relpath == '\0');

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_INSERT_WC_LOCK));
  SVN_ERR(svn_sqlite__bindf(stmt, "isi", pdh->wcroot->wc_id, local_relpath,
                            (apr_int64_t) levels_to_lock));
  err = svn_sqlite__insert(NULL, stmt);
  if (err)
    return svn_error_createf(SVN_ERR_WC_LOCKED, err,
                             _("Working copy '%s' locked"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  return SVN_NO_ERROR;
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
  if (err && err->apr_err == SVN_ERR_WC_NOT_WORKING_COPY)
    {
      svn_error_clear(err);
      *locked = FALSE;
      return SVN_NO_ERROR;
    }

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
svn_wc__db_wclock_remove(svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_wc__db_pdh_t *pdh;

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_DELETE_WC_LOCK, scratch_pool));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* If we've just removed the "physical" lock, we also need to ensure we
     don't continue to think we own the lock. */
  pdh = svn_wc__db_pdh_get_or_create(db, local_abspath, FALSE, scratch_pool);
  if (pdh)
    pdh->locked = FALSE;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_mark_locked(svn_wc__db_t *db,
                            const char *local_dir_abspath,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));

  pdh = svn_wc__db_pdh_get_or_create(db, local_dir_abspath, TRUE,
                                     scratch_pool);
  pdh->locked = TRUE;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_own_lock(svn_boolean_t *own_lock,
                         svn_wc__db_t *db,
                         const char *local_dir_abspath,
                         apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));

  pdh = svn_wc__db_pdh_get_or_create(db, local_dir_abspath, FALSE,
                                     scratch_pool);
  *own_lock = (pdh != NULL && pdh->locked);

  return SVN_NO_ERROR;

}

svn_error_t *
svn_wc__db_temp_op_set_base_incomplete(svn_wc__db_t *db,
                                       const char *local_dir_abspath,
                                       svn_boolean_t incomplete,
                                       apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_wc__db_pdh_t *pdh;
  int affected_rows;
  svn_wc__db_status_t base_status;

  SVN_ERR(svn_wc__db_base_get_info(&base_status, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                   NULL, NULL,
                                   db, local_dir_abspath,
                                   scratch_pool, scratch_pool));

  SVN_ERR_ASSERT(base_status == svn_wc__db_status_normal ||
                 base_status == svn_wc__db_status_incomplete);

  SVN_ERR(get_statement_for_path(&stmt, db, local_dir_abspath,
                                 STMT_UPDATE_BASE_PRESENCE, scratch_pool));

  SVN_ERR(svn_sqlite__bind_text(stmt, 3, incomplete ? "incomplete" : "normal"));

  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows > 0)
   {
     pdh = svn_wc__db_pdh_get_or_create(db, local_dir_abspath, FALSE,
                                        scratch_pool);
     flush_entries(pdh);
   }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__db_temp_op_set_working_incomplete(svn_wc__db_t *db,
                                       const char *local_dir_abspath,
                                       svn_boolean_t incomplete,
                                       apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_wc__db_pdh_t *pdh;
  int affected_rows;
  svn_wc__db_status_t status;

  SVN_ERR(svn_wc__db_read_info(&status, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL,
                               db, local_dir_abspath,
                               scratch_pool, scratch_pool));

  /* Presence in WORKING_NODE must be normal or incomplete */
  SVN_ERR_ASSERT(status == svn_wc__db_status_added ||
                 status == svn_wc__db_status_incomplete);

  SVN_ERR(get_statement_for_path(&stmt, db, local_dir_abspath,
                                 STMT_UPDATE_WORKING_PRESENCE, scratch_pool));

  SVN_ERR(svn_sqlite__bind_text(stmt, 3, incomplete ? "incomplete" : "normal"));

  SVN_ERR(svn_sqlite__update(&affected_rows, stmt));

  if (affected_rows > 0)
   {
     pdh = svn_wc__db_pdh_get_or_create(db, local_dir_abspath, FALSE,
                                        scratch_pool);
     flush_entries(pdh);
   }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_op_set_working_last_change(svn_wc__db_t *db,
                                           const char *local_abspath,
                                           svn_revnum_t changed_rev,
                                           apr_time_t changed_date,
                                           const char *changed_author,
                                           apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  svn_sqlite__stmt_t *stmt;
  const char *local_relpath;
  int affected;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_UPDATE_WORKING_LAST_CHANGE));

  SVN_ERR(svn_sqlite__bindf(stmt, "isiis",
                            pdh->wcroot->wc_id, local_relpath,
                            (apr_int64_t)changed_rev,
                            (apr_int64_t)changed_date,
                            changed_author));

  SVN_ERR(svn_sqlite__update(&affected, stmt));

  /* The following check might fail if none of the values changes.
     But currently this function is never called in such cases */
  if (affected != 1)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("'%s' has no WORKING_NODE"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  flush_entries(pdh);

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
  const char *repos_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_wc__db_scan_base_repos(&repos_relpath, NULL, NULL,
                                     du->db, du->local_abspath,
                                     scratch_pool, scratch_pool));

  if (strcmp(du->new_repos_relpath, repos_relpath) == 0)
    {
      /* Just update revision and status */
      SVN_ERR(svn_sqlite__get_statement(
                        &stmt, db,
                        STMT_UPDATE_BASE_PRESENCE_AND_REVNUM));

      SVN_ERR(svn_sqlite__bindf(stmt, "isti",
                                du->wc_id,
                                du->local_relpath,
                                presence_map, svn_wc__db_status_incomplete,
                                (apr_int64_t)du->new_rev));
    }
  else
    {
      /* ### TODO: Maybe check if we can make repos_relpath NULL. */
      SVN_ERR(svn_sqlite__get_statement(
                        &stmt, db,
                        STMT_UPDATE_BASE_PRESENCE_REVNUM_AND_REPOS_RELPATH));

      SVN_ERR(svn_sqlite__bindf(stmt, "istis",
                                du->wc_id,
                                du->local_relpath,
                                presence_map, svn_wc__db_status_incomplete,
                                (apr_int64_t)du->new_rev,
                                du->new_repos_relpath));
    }

  return svn_error_return(svn_sqlite__step_done(stmt));
}

svn_error_t *
svn_wc__db_temp_op_start_directory_update(svn_wc__db_t *db,
                                          const char *local_abspath,
                                          const char *new_repos_relpath,
                                          svn_revnum_t new_rev,
                                          apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  struct start_directory_update_baton du;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_rev));
  SVN_ERR_ASSERT(svn_relpath_is_canonical(new_repos_relpath, scratch_pool));

  SVN_ERR(parse_local_abspath(&pdh, &du.local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  du.db = db;
  du.wc_id = pdh->wcroot->wc_id;
  du.local_abspath = local_abspath;
  du.new_rev = new_rev;
  du.new_repos_relpath = new_repos_relpath;

  SVN_ERR(svn_sqlite__with_transaction(pdh->wcroot->sdb,
                                       start_directory_update_txn, &du,
                                       scratch_pool));

  flush_entries(pdh);

  return SVN_NO_ERROR;
}

/* Baton for make_copy_txn */
struct make_copy_baton
{
  svn_wc__db_t *db;
  const char *local_abspath;

  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_boolean_t remove_base;
  svn_boolean_t is_root;
};

/* Transaction callback for svn_wc__db_temp_op_make_copy */
static svn_error_t *
make_copy_txn(void *baton,
              svn_sqlite__db_t *sdb,
              apr_pool_t *scratch_pool)
{
  struct make_copy_baton *mcb = baton;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_boolean_t remove_working = FALSE;
  svn_boolean_t check_base = TRUE;
  svn_boolean_t add_working_normal = FALSE;
  svn_boolean_t add_working_not_present = FALSE;
  const apr_array_header_t *children;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_wc__db_kind_t kind;
  int i;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", mcb->pdh->wcroot->wc_id,
                            mcb->local_relpath));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      svn_wc__db_status_t working_status;

      working_status = svn_sqlite__column_token(stmt, 0, presence_map);
      kind = svn_sqlite__column_token(stmt, 1, kind_map);
      SVN_ERR(svn_sqlite__reset(stmt));

      SVN_ERR_ASSERT(working_status == svn_wc__db_status_normal
                     || working_status == svn_wc__db_status_base_deleted
                     || working_status == svn_wc__db_status_not_present
                     || working_status == svn_wc__db_status_incomplete);

      /* Make existing deletions of BASE_NODEs remove WORKING_NODEs */
      if (working_status == svn_wc__db_status_base_deleted)
        {
          remove_working = TRUE;
          add_working_not_present = TRUE;
        }

      check_base = FALSE;
    }
  else
    SVN_ERR(svn_sqlite__reset(stmt));

  if (check_base)
    {
      svn_wc__db_status_t base_status;

      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_BASE_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", mcb->pdh->wcroot->wc_id, 
                                mcb->local_relpath));

      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      /* If there is no BASE_NODE, we don't have to copy anything */
      if (!have_row)
        return svn_error_return(svn_sqlite__reset(stmt));

      base_status = svn_sqlite__column_token(stmt, 2, presence_map);
      kind = svn_sqlite__column_token(stmt, 3, kind_map);

      SVN_ERR(svn_sqlite__reset(stmt));

      switch (base_status)
        {
          case svn_wc__db_status_normal:
          case svn_wc__db_status_incomplete:
            add_working_normal = TRUE;
            break;
          case svn_wc__db_status_not_present:
            add_working_not_present = TRUE;
            break;
          case svn_wc__db_status_excluded:
          case svn_wc__db_status_absent:
            /* ### Make the copy match the WC or the repository? */
            add_working_not_present = TRUE; /* ### Match WC */
            break;
          default:
            SVN_ERR_MALFUNCTION();
        }
    }

  /* Get the BASE children, as WORKING children don't need modifications */
  SVN_ERR(svn_wc__db_base_get_children(&children, mcb->db, mcb->local_abspath,
                                       scratch_pool, iterpool));

  for (i = 0; i < children->nelts; i++)
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);
      struct make_copy_baton cbt;

      svn_pool_clear(iterpool);
      cbt.local_abspath = svn_dirent_join(mcb->local_abspath, name, iterpool);

      SVN_ERR(parse_local_abspath(&cbt.pdh, &cbt.local_relpath, mcb->db,
                                  cbt.local_abspath,
                                  svn_sqlite__mode_readwrite,
                                  iterpool, iterpool));

      VERIFY_USABLE_PDH(cbt.pdh);

      cbt.db = mcb->db;
      cbt.remove_base = mcb->remove_base;
      cbt.is_root = FALSE;

      SVN_ERR(make_copy_txn(&cbt, cbt.pdh->wcroot->sdb, iterpool));
    }

  if (remove_working)
    {
      /* Remove current WORKING_NODE record */
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_DELETE_WORKING_NODE));

      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                mcb->pdh->wcroot->wc_id,
                                mcb->local_relpath));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  if (add_working_normal)
    {
      /* Add a copy of the BASE_NODE to WORKING_NODE */

      SVN_ERR(svn_sqlite__get_statement(
                        &stmt, sdb,
                        STMT_INSERT_WORKING_NODE_NORMAL_FROM_BASE_NODE));

      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                mcb->pdh->wcroot->wc_id,
                                mcb->local_relpath));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }
  else if (add_working_not_present)
    {
      /* Add a not present WORKING_NODE */

      SVN_ERR(svn_sqlite__get_statement(
                        &stmt, sdb,
                        STMT_INSERT_WORKING_NODE_NOT_PRESENT_FROM_BASE_NODE));

      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                mcb->pdh->wcroot->wc_id,
                                mcb->local_relpath));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  if (mcb->is_root && (add_working_normal || add_working_not_present))
    {
      const char *repos_relpath, *repos_root_url, *repos_uuid;
      apr_int64_t repos_id;
      /* Make sure the copy origin is set on the root even if the node
         didn't have a local relpath */

      SVN_ERR(svn_wc__db_scan_base_repos(&repos_relpath, &repos_root_url,
                                         &repos_uuid, mcb->db,
                                         mcb->local_abspath,
                                         iterpool, iterpool));

      SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid, sdb,
                              iterpool));

      /* ### this is not setting the COPYFROM_REVISION column!!  */

      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_UPDATE_COPYFROM));
      SVN_ERR(svn_sqlite__bindf(stmt, "isis",
                                mcb->pdh->wcroot->wc_id,
                                mcb->local_relpath,
                                repos_id,
                                repos_relpath));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* And now, do the same for the parent stub :( If we kind from the
     working node could the base node be different?  Not until we move
     to a single db and then the parent stubs should go away. */
  if (kind == svn_wc__db_kind_dir)
    {
      if (remove_working)
        {
          const char *local_relpath;
          svn_wc__db_pdh_t *pdh;

          /* Remove WORKING_NODE stub */
          SVN_ERR(navigate_to_parent(&pdh, mcb->db, mcb->pdh,
                                     svn_sqlite__mode_readwrite,
                                     iterpool));
          local_relpath = svn_dirent_basename(mcb->local_abspath, NULL);
          VERIFY_USABLE_PDH(pdh);

          SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                            STMT_DELETE_WORKING_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                    pdh->wcroot->wc_id, local_relpath));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }

      if (add_working_normal)
        {
          const char *local_relpath;
          svn_wc__db_pdh_t *pdh;

          /* Add a copy of the BASE_NODE to WORKING_NODE for the stub */
          SVN_ERR(navigate_to_parent(&pdh, mcb->db, mcb->pdh,
                                     svn_sqlite__mode_readwrite,
                                     iterpool));
          local_relpath = svn_dirent_basename(mcb->local_abspath, NULL);
          VERIFY_USABLE_PDH(pdh);

          /* Remove old data */
          SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                            STMT_DELETE_WORKING_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                    pdh->wcroot->wc_id, local_relpath));
          SVN_ERR(svn_sqlite__step_done(stmt));

          /* And insert the right data */
          SVN_ERR(svn_sqlite__get_statement(
                        &stmt, pdh->wcroot->sdb,
                        STMT_INSERT_WORKING_NODE_NORMAL_FROM_BASE_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                    pdh->wcroot->wc_id, local_relpath));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }
      else if (add_working_not_present)
        {
          const char *local_relpath;
          svn_wc__db_pdh_t *pdh;

          /* Add a not present WORKING_NODE stub */
          SVN_ERR(navigate_to_parent(&pdh, mcb->db, mcb->pdh,
                                     svn_sqlite__mode_readwrite,
                                     iterpool));
          local_relpath = svn_dirent_basename(mcb->local_abspath, NULL);
          VERIFY_USABLE_PDH(pdh);

          /* Remove old data */
          SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                            STMT_DELETE_WORKING_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                    pdh->wcroot->wc_id, local_relpath));
          SVN_ERR(svn_sqlite__step_done(stmt));

          /* And insert the right data */
          SVN_ERR(svn_sqlite__get_statement(
                        &stmt, pdh->wcroot->sdb,
                        STMT_INSERT_WORKING_NODE_NOT_PRESENT_FROM_BASE_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                    pdh->wcroot->wc_id, local_relpath));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }
    }

  /* Remove the BASE_NODE if the caller asked us to do that */
  if (mcb->remove_base)
    {
      const char *local_relpath;
      svn_wc__db_pdh_t *pdh;

      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_DELETE_BASE_NODE));

      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                mcb->pdh->wcroot->wc_id,
                                mcb->local_relpath));

      SVN_ERR(svn_sqlite__step_done(stmt));

      /* Remove BASE_NODE_STUB */
      if (kind == svn_wc__db_kind_dir)
        {
          SVN_ERR(navigate_to_parent(&pdh, mcb->db, mcb->pdh,
                                     svn_sqlite__mode_readwrite,
                                     iterpool));
          local_relpath = svn_dirent_basename(mcb->local_abspath, NULL);
          VERIFY_USABLE_PDH(pdh);

          SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                            STMT_DELETE_BASE_NODE));
          SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                    pdh->wcroot->wc_id, local_relpath));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }
    }

  svn_pool_destroy(iterpool);

  flush_entries(mcb->pdh);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_op_make_copy(svn_wc__db_t *db,
                             const char *local_abspath,
                             svn_boolean_t remove_base,
                             apr_pool_t *scratch_pool)
{
  struct make_copy_baton mcb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&mcb.pdh, &mcb.local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(mcb.pdh);

  mcb.db = db;
  mcb.local_abspath = local_abspath;
  mcb.remove_base = remove_base;
  mcb.is_root = TRUE;

  SVN_ERR(svn_sqlite__with_transaction(mcb.pdh->wcroot->sdb,
                                       make_copy_txn, &mcb,
                                       scratch_pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_temp_elide_copyfrom(svn_wc__db_t *db,
                               const char *local_abspath,
                               apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_int64_t original_repos_id;
  const char *original_repos_relpath;
  svn_revnum_t original_revision;
  const char *parent_abspath;
  const char *name;
  svn_error_t *err;
  const char *op_root_abspath;
  const char *parent_repos_relpath;
  const char *parent_uuid;
  svn_revnum_t parent_revision;
  const char *implied_relpath;
  const char *original_uuid;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  /* Examine the current WORKING_NODE row's copyfrom information. If there
     is no WORKING node, then simply exit.  */
  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_return(svn_sqlite__reset(stmt));

  /* Already inheriting copyfrom information?  */
  if (svn_sqlite__column_is_null(stmt, 9 /* copyfrom_repos_id */))
    return svn_error_return(svn_sqlite__reset(stmt));

  original_repos_id = svn_sqlite__column_int64(stmt, 9);
  original_repos_relpath = svn_sqlite__column_text(stmt, 10, scratch_pool);
  original_revision = svn_sqlite__column_revnum(stmt, 11);

  SVN_ERR(svn_sqlite__reset(stmt));

  /* If this node is copied/moved, then there MUST be a parent. The above
     copyfrom values cannot be set on a wcroot.  */
  svn_dirent_split(local_abspath, &parent_abspath, &name, scratch_pool);
  err = svn_wc__db_scan_addition(NULL, &op_root_abspath, NULL, NULL, NULL,
                                 &parent_repos_relpath,
                                 NULL,
                                 &parent_uuid,
                                 &parent_revision,
                                 db, parent_abspath,
                                 scratch_pool, scratch_pool);
  if (err)
    {
      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
        return svn_error_return(err);
      svn_error_clear(err);

      /* ### hunh? sometimes the parent is missing? stupid semi-stable
         ### state crap, probably. don't bother trying to reset the
         ### copyfrom data for this case.  */
      return SVN_NO_ERROR;
    }

  /* Now we need to determine if the child's values are derivable from
     the parent values.  */

  /* If the revision numbers are not the same, then easy exit.

     Note that we *can* have a mixed-rev copied subtree. We don't want
     to elide the copyfrom information for these cases.  */
  if (original_revision != parent_revision)
    return SVN_NO_ERROR;

  /* The child repos_relpath should be under the parent's.  */
  if (svn_relpath_is_child(parent_repos_relpath,
                           original_repos_relpath,
                           NULL) == NULL)
    return SVN_NO_ERROR;

  /* Given the relpath from OP_ROOT_ABSPATH down to LOCAL_ABSPATH, compute
     an implied REPOS_RELPATH. If that does not match the RELPATH we found,
     then we can exit (the child is a new copy root).  */
  implied_relpath = svn_relpath_join(parent_repos_relpath,
                                     svn_dirent_skip_ancestor(op_root_abspath,
                                                              local_abspath),
                                     scratch_pool);
  if (strcmp(implied_relpath, original_repos_relpath) != 0)
    return SVN_NO_ERROR;

  /* Everything matches up. Grab the details for ORIGINAL_REPOS_ID and
     compare to the parent.  */
  SVN_ERR(fetch_repos_info(NULL, &original_uuid,
                           pdh->wcroot->sdb, original_repos_id,
                           scratch_pool));
  if (strcmp(original_uuid, parent_uuid) != 0)
    return SVN_NO_ERROR;

  /* The child's copyfrom information is derivable from the parent.
     The data should be reset to null, indicating the derivation.  */
  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_UPDATE_COPYFROM_TO_INHERIT));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__update(NULL, stmt));

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
svn_wc__db_pristine_get_md5(const svn_checksum_t **md5_checksum,
                            svn_wc__db_t *db,
                            const char *wri_abspath,
                            const svn_checksum_t *sha1_checksum,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(wri_abspath));
  SVN_ERR_ASSERT(sha1_checksum->kind == svn_checksum_sha1);
  VERIFY_CHECKSUM_KIND(sha1_checksum);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, wri_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_SELECT_PRISTINE_MD5_CHECKSUM));
  SVN_ERR(svn_sqlite__bind_checksum(stmt, 1, sha1_checksum, scratch_pool));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_DB_ERROR, svn_sqlite__reset(stmt),
                             _("The pristine text with checksum '%s' was "
                               "not found"),
                             svn_checksum_to_cstring_display(sha1_checksum,
                                                             scratch_pool));

  SVN_ERR(svn_sqlite__column_checksum(md5_checksum, stmt, 0, result_pool));
  SVN_ERR_ASSERT((*md5_checksum)->kind == svn_checksum_md5);

  return svn_error_return(svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_temp_remove_subdir_record(svn_wc__db_t *db,
                                     const char *local_abspath,
                                     apr_pool_t *scratch_pool)
{
  const char *parent_abspath;
  const char *name;
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  svn_dirent_split(local_abspath, &parent_abspath, &name, scratch_pool);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));
  VERIFY_USABLE_PDH(pdh);
  
  SVN_ERR_ASSERT(*local_relpath == '\0');

  /* Delete the NAME row from BASE_NODE.  */
  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    STMT_DELETE_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, name));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}
