/*
 * wc_db.c :  manipulating the administrative database
 *
 * ====================================================================
 * Copyright (c) 2008 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */


#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_dirent_uri.h"
#include "svn_wc.h"
#include "svn_checksum.h"

#include "wc.h"
#include "wc_db.h"
#include "adm_files.h"
#include "wc-metadata.h"

#include "svn_private_config.h"
#include "private/svn_sqlite.h"


#define NOT_IMPLEMENTED() \
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.")


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


struct svn_wc__db_t {
  /* What's the appropriate mode for this datastore? */
  svn_wc__db_openmode_t mode;

  /* We need the config whenever we run into a new WC directory, in order
     to figure out where we should look for the corresponding datastore. */
  svn_config_t *config;

  /* Map a given working copy directory to its relevant data. */
  apr_hash_t *dir_data;

  /* As we grow the state of this DB, allocate that state here. */
  apr_pool_t *state_pool;
};

/**
 * This structure records all the information that we need to deal with
 * a given working copy directory.
 */
struct svn_wc__db_pdh_t {
  /* This per-dir state is associated with this global state. */
  svn_wc__db_t *db;

  /* The absolute path to this working copy directory. */
  const char *local_abspath;

  /* The relative path from the wcroot to this directory. */
  const char *local_relpath;

  /* The SQLite database containing the metadata for everything in
     this directory.  */
  svn_sqlite__db_t *sdb;

  /* The WCROOT id this directory is part of. */
  apr_int64_t wc_id;

  /* The root directory of this WCROOT. */
  const char *wcroot_abspath;

  /* Root of the TEXT-BASE directory structure for the WORKING/ACTUAL files
     in this directory. */
  const char *base_dir;

  /* The parent directory's per-dir information. */
  svn_wc__db_pdh_t *parent;
};

/* ### since we're putting the pristine files per-dir, then we don't need
   ### to create subdirectories in order to keep the directory size down.
   ### when we can aggregate pristine files across dirs/wcs, then we will
   ### need to undo the SKIP. */
#define SVN__SKIP_SUBDIR

/* ### duplicates entries.c */
static const char * const upgrade_sql[] = { NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL, NULL, WC_METADATA_SQL };

/* These values map to the members of STATEMENTS below, and should be added
   and removed at the same time. */
enum statement_keys {
  STMT_SELECT_BASE_NODE,
  STMT_SELECT_WORKING_NODE,
  STMT_SELECT_ACTUAL_NODE,
  STMT_SELECT_REPOSITORY_BY_ID,
  STMT_SELECT_WCROOT_NULL,
  STMT_SELECT_REPOSITORY,
  STMT_INSERT_REPOSITORY,
  STMT_INSERT_BASE_NODE,
  STMT_INSERT_BASE_NODE_INCOMPLETE,
  STMT_SELECT_BASE_NODE_CHILDREN,
  STMT_SELECT_WORKING_CHILDREN
};

static const char * const statements[] = {
  "select wc_id, local_relpath, repos_id, repos_relpath, "
  "  presence, kind, revnum, checksum, translated_size, "
  "  changed_rev, changed_date, changed_author, depth, symlink_target "
  "from base_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  "select presence, kind, checksum, translated_size, "
  "  changed_rev, changed_date, changed_author, depth, symlink_target, "
  "  copyfrom_repos_id, copyfrom_repos_path, copyfrom_revnum, "
  "  moved_here, moved_to "
  "from working_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  "select changelist "
  "from actual_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  "select root, uuid from repository where id = ?1;",

  "select id from wcroot where local_abspath is null;",

  "select id from repository where uuid = ?1;",

  "insert into repository (root, uuid) values (?1, ?2);",

  "insert or replace into base_node ("
  "  wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, presence, "
  "  kind, revnum, properties, changed_rev, changed_date, changed_author, "
  "  depth, checksum, translated_size, symlink_target) "
  "values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, "
  "        ?15, ?16);",

  "insert or ignore into base_node ("
  "  wc_id, local_relpath, parent_relpath, presence, kind, revnum) "
  "values (?1, ?2, ?3, 'incomplete', 'unknown', ?5);",

  "select local_relpath from base_node "
  "where wc_id = ?1 and parent_relpath = ?2;",

  "select local_relpath from base_node "
  "where wc_id = ?1 and parent_relpath = ?2 "
  "union "
  "select local_relpath from working_node "
  "where wc_id = ?1 and parent_relpath = ?2;",

  NULL
};

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

  /* for inserting absent nodes */

  /* for temporary allocations */
  apr_pool_t *scratch_pool;

} insert_base_baton_t;


static svn_wc__db_kind_t
word_to_kind(const char *kind)
{
  /* Let's be lazy and fast */
  if (*kind == 'f')
    return svn_wc__db_kind_file;
  if (*kind == 'd')
    return svn_wc__db_kind_dir;
  if (*kind == 's')
    return svn_wc__db_kind_symlink;
  /* ### MALFUNCTION if not "normal" ? */
  return svn_wc__db_kind_unknown;
}


static const char *
kind_to_word(svn_wc__db_kind_t kind)
{
  switch (kind)
    {
    case svn_wc__db_kind_dir:
      return "dir";
    case svn_wc__db_kind_file:
      return "file";
    case svn_wc__db_kind_symlink:
      return "symlink";
    case svn_wc__db_kind_unknown:
      return "unknown";
    default:
      SVN_ERR_MALFUNCTION_NO_RETURN();
    }
}


/* Note: we only decode presence values from the databse. These are a subset
   of all the status values. */
static svn_wc__db_status_t
word_to_presence(const char *presence)
{
  /* Be lazy and fast. */
  if (*presence == 'a')
    return svn_wc__db_status_absent;
  if (*presence == 'e')
    return svn_wc__db_status_excluded;
  if (*presence == 'i')
    return svn_wc__db_status_incomplete;
  if (strcmp(presence, "not-present") == 0)
    return svn_wc__db_status_not_present;
  /* ### MALFUNCTION if not "normal" ? */
  return svn_wc__db_status_normal;
}


static const char *
presence_to_word(svn_wc__db_status_t presence)
{
  switch (presence)
    {
    case svn_wc__db_status_normal:
      return "normal";
    case svn_wc__db_status_absent:
      return "absent";
    case svn_wc__db_status_excluded:
      return "excluded";
    case svn_wc__db_status_not_present:
      return "not-present";
    case svn_wc__db_status_incomplete:
      return "incomplete";
    default:
      SVN_ERR_MALFUNCTION_NO_RETURN();
    }
}


static svn_filesize_t
get_translated_size(svn_sqlite__stmt_t *stmt, int slot)
{
  if (svn_sqlite__column_is_null(stmt, slot))
    return SVN_INVALID_FILESIZE;
  return svn_sqlite__column_int64(stmt, slot);
}


static svn_error_t *
get_pristine_fname(const char **path,
                   svn_wc__db_pdh_t *pdh,
                   const svn_checksum_t *checksum,
                   svn_boolean_t create_subdir,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *hexdigest = svn_checksum_to_cstring(checksum, scratch_pool);
#ifndef SVN__SKIP_SUBDIR
  char subdir[3] = { 0 };
#endif

  /* We should have a valid checksum and (thus) a valid digest. */
  SVN_ERR_ASSERT(hexdigest != NULL);

#ifndef SVN__SKIP_SUBDIR
  /* Get the first two characters of the digest, for the subdir. */
  subdir[0] = hexdigest[0];
  subdir[1] = hexdigest[1];

  if (create_subdir)
    {
      const char *subdir_path = svn_dirent_join(pdh->base_dir, subdir,
                                                scratch_pool);
      svn_error_t *err;

      err = svn_io_dir_make(subdir_path, APR_OS_DEFAULT, scratch_pool);

      /* Whatever error may have occurred... ignore it. Typically, this
         will be "directory already exists", but if it is something
         *different*, then presumably another error will follow when we
         try to access the file within this (missing?) pristine subdir. */
      svn_error_clear(err);
    }
#endif

  /* The file is located at DIR/.svn/pristine/XX/XXYYZZ... */
  *path = svn_dirent_join_many(result_pool,
                               pdh->base_dir,
#ifndef SVN__SKIP_SUBDIR
                               subdir,
#endif
                               hexdigest,
                               NULL);
  return SVN_NO_ERROR;
}


static svn_error_t *
open_one_directory(svn_wc__db_t *db,
                   const char *path,
                   apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  svn_boolean_t special;
  svn_wc__db_pdh_t *pdh;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(path));

  /* If the file is special, then we need to refer to the encapsulating
     directory instead, rather than resolving through a symlink to a
     file or directory. */
  SVN_ERR(svn_io_check_special_path(path, &kind, &special, scratch_pool));

  /* ### skip unknown and/or not-found paths? need to examine typical
     ### caller usage. */

  if (kind != svn_node_dir)
    {
      /* ### doesn't seem that we need to keep the original path */
      path = svn_dirent_dirname(path, scratch_pool);
    }

  pdh = apr_hash_get(db->dir_data, path, APR_HASH_KEY_STRING);
  if (pdh != NULL)
    return SVN_NO_ERROR;  /* seen this directory already! */

  pdh = apr_palloc(db->state_pool, sizeof(*pdh));
  pdh->db = db;

  /* Make sure the key lasts as long as the hash. Note that if we did
     not call dirname(), then this path is the provided path, but we
     do not know its lifetime (nor does our API contract specify a
     requirement for the lifetime). */
  pdh->local_abspath = apr_pstrdup(db->state_pool, path);

  /* ### local_relpath */
  /* ### sdb */
  /* ### wc_id */

  /* ### for now, every directory still has a .svn subdir, and a
     ### "pristine" subdir in there. later on, we'll alter the
     ### storage location/strategy */

  /* ### need to fix this to use a symbol for ".svn". we shouldn't need
     ### to use join_many since we know "/" is the separator for
     ### internal canonical paths */
  pdh->base_dir = svn_dirent_join(path, ".svn/pristine", db->state_pool);

  /* ### parent */

  apr_hash_set(db->dir_data, pdh->local_abspath, APR_HASH_KEY_STRING, pdh);

  return SVN_NO_ERROR;
}


static svn_wc__db_t *
new_db_state(svn_wc__db_openmode_t mode,
             svn_config_t *config,
             apr_pool_t *result_pool)
{
  svn_wc__db_t *db = apr_palloc(result_pool, sizeof(*db));

  db->mode = mode;
  db->config = config;
  db->dir_data = apr_hash_make(result_pool);
  db->state_pool = result_pool;

  return db;
}


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
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                             _("No REPOSITORY table entry for id '%ld'"),
                             (long int)repos_id);

  if (repos_root_url)
    *repos_root_url = svn_sqlite__column_text(stmt, 0, result_pool);
  if (repos_uuid)
    *repos_uuid = svn_sqlite__column_text(stmt, 1, result_pool);

  return svn_sqlite__reset(stmt);
}


/* Scan from RELPATH upwards through parent nodes until we find a parent
   that has values in the 'repos_id' and 'repos_relpath' columns.  Return
   that information in REPOS_ID and REPOS_RELPATH (either may be NULL). */
static svn_error_t *
scan_upwards_for_repos(apr_int64_t *repos_id,
                       const char **repos_relpath,
                       apr_int64_t wc_id,
                       const char *relpath,
                       svn_sqlite__db_t *sdb,
                       apr_pool_t *result_pool,
                       apr_pool_t *scratch_pool)
{
  const char *relpath_suffix = "";
  const char *current_relpath = relpath;
  svn_sqlite__stmt_t *stmt;

  SVN_ERR_ASSERT(repos_id != NULL || repos_relpath != NULL);

  /* ### is it faster to fetch fewer columns? */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_BASE_NODE));

  while (TRUE)
    {
      const char *basename;
      svn_boolean_t have_row;

      /* Strip a path segment off the end, and append it to the suffix
         that we'll use when we finally find a base relpath. */
      svn_dirent_split(current_relpath, &current_relpath, &basename,
                       scratch_pool);
      relpath_suffix = svn_dirent_join(relpath_suffix, basename, scratch_pool);

      /* ### strictly speaking, moving to the parent could send us to a
         ### different SDB, and (thus) we would need to fetch STMT again.
         ### but we happen to know the parent is *always* in the same db. */

      /* Rebind the statement to fetch parent information. */
      SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, current_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (!have_row)
          return svn_error_createf(
            SVN_ERR_WC_CORRUPT, NULL,
            _("Parent(s) of '%s' should have been present."),
            svn_dirent_local_style(relpath, scratch_pool));

      /* Did we find some non-NULL repository columns? */
      if (!svn_sqlite__column_is_null(stmt, 2))
        {
          /* If one is non-NULL, then so should the other. */
          SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 3));

          if (repos_id)
            *repos_id = svn_sqlite__column_int64(stmt, 2);

          /* Given the parent's relpath, append all the segments that
             we stripped as we scanned upwards. */
          if (repos_relpath)
            *repos_relpath = svn_dirent_join(svn_sqlite__column_text(stmt, 3,
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
            svn_dirent_local_style(relpath, scratch_pool));
        }

      /* Loop to move further upwards. */
    }
}


/* For a given LOCAL_ABSPATH, figure out what sqlite database (SDB) to use,
   what WC_ID is implied, and the RELPATH within that wcroot.  If a sqlite
   database needs to be opened, then use SMODE for it. */
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
  const char *pdh_relpath;
  svn_wc__db_pdh_t *found_pdh = NULL;
  svn_wc__db_pdh_t *child_pdh;

  /* ### we need more logic for finding the database (if it is located
     ### outside of the wcroot) and then managing all of that within DB.
     ### for now: play quick & dirty. */

  *pdh = apr_hash_get(db->dir_data, local_abspath, APR_HASH_KEY_STRING);
  if (*pdh != NULL)
    {
      /* We got lucky. Just return the thing BEFORE performing any I/O.  */
      /* ### validate SMODE against how we opened pdh->sdb? and against
         ### DB->mode? (will we record per-dir mode?)  */
      /* ### what if the whole structure is not (yet) filled in? */

      *local_relpath = apr_pstrdup(result_pool, (*pdh)->local_relpath);
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

      /* Is this directory in our hash?  */
      *pdh = apr_hash_get(db->dir_data, local_abspath, APR_HASH_KEY_STRING);
      if (*pdh != NULL)
        {
          /* Stashed directory's local_relpath + basename. */
          *local_relpath = svn_dirent_join((*pdh)->local_relpath,
                                           build_relpath,
                                           result_pool);
          return SVN_NO_ERROR;
        }
    }
  else
    {
      /* Start the local_relpath empty. If *this* directory contains the
         wc.db, then relpath will be the empty string.  */
      build_relpath = "";
    }

  /* The local_relpath that we put into the PDH starts empty. */
  pdh_relpath = "";

  /* The PDH corresponding to the directory LOCAL_ABSPATH is what we need
     to return. At this point, we've determined that it is NOT in the DB's
     hash table of wcdirs. Let's create it, and begin to populate it.  */
     
  *pdh = apr_pcalloc(db->state_pool, sizeof(**pdh));
  (*pdh)->db = db;
  (*pdh)->local_abspath = apr_pstrdup(db->state_pool, local_abspath);

  /* Assume that LOCAL_ABSPATH is a directory, and look for the SQLite
     database in the right place. If we find it... great! If not, then
     peel off some components, and try again. */

  while (TRUE)
    {
      svn_error_t *err;
      const char *base;

      err = svn_sqlite__open(&(*pdh)->sdb,
                             svn_wc__adm_child(local_abspath, "wc.db",
                                               scratch_pool),
                             smode, statements,
                             SVN_WC__VERSION_EXPERIMENTAL, upgrade_sql,
                             db->state_pool, scratch_pool);
      if (err == NULL)
        break;
      if (err->apr_err != SVN_ERR_SQLITE_ERROR
          && !APR_STATUS_IS_ENOENT(err->apr_err))
        return err;
      svn_error_clear(err);

      /* We couldn't open the SDB within the specified directory, so
         move up one more directory. */
      base = svn_dirent_basename(local_abspath, scratch_pool);
      if (*base == '\0')
        {
          /* Hit the root without finding a wcroot. */
          return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                                   _("'%s' is not a working copy"),
                                   svn_dirent_local_style(original_abspath,
                                                          scratch_pool));
        }

      build_relpath = svn_dirent_join(base, build_relpath, scratch_pool);
      pdh_relpath = svn_dirent_join(base, pdh_relpath, scratch_pool);
      local_abspath = svn_dirent_dirname(local_abspath, scratch_pool);

      /* Is the parent directory recorded in our hash?  */
      found_pdh = apr_hash_get(db->dir_data,
                               local_abspath, APR_HASH_KEY_STRING);
      if (found_pdh != NULL)
        break;
    }

  if (found_pdh != NULL)
    {
      /* We found a PDH with data in it. We can now construct the child
         from this, rather than continuing to scan upwards.  */

      /* The subdirectory's relpath is a join of the parent's plus what
         we've stripped off the input so far.  */
      (*pdh)->local_relpath = svn_dirent_join(found_pdh->local_relpath,
                                              pdh_relpath,
                                              db->state_pool);

      /* And the result local_relpath may include a filename.  */
      *local_relpath = svn_dirent_join(found_pdh->local_relpath,
                                       build_relpath,
                                       result_pool);

      /* The subdirectory uses the same SDB and WC_ID as the parent dir.  */
      (*pdh)->sdb = found_pdh->sdb;
      (*pdh)->wc_id = found_pdh->wc_id;
      (*pdh)->wcroot_abspath = found_pdh->wcroot_abspath;
    }
  else
    {
      /* We finally found the database. Construct the PDH record.  */

      svn_sqlite__stmt_t *stmt;
      svn_boolean_t have_row;

      (*pdh)->local_relpath = apr_pstrdup(db->state_pool, pdh_relpath);
      *local_relpath = apr_pstrdup(result_pool, build_relpath);

      /* ### cheat. we know there is just one WORKING_COPY row, and it has a
         ### NULL value for local_abspath. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, (*pdh)->sdb,
                                        STMT_SELECT_WCROOT_NULL));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (!have_row)
        return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                 _("Missing a row in WCROOT for '%s'."),
                                 svn_dirent_local_style(original_abspath,
                                                        scratch_pool));

      SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 0));
      (*pdh)->wc_id = svn_sqlite__column_int64(stmt, 0);

      /* ### WCROOT.local_abspath will be NULL. but we know the abspath.  */
      (*pdh)->wcroot_abspath = (*pdh)->local_abspath;

      SVN_ERR(svn_sqlite__reset(stmt));
    }

  /* The PDH is complete. Stash it into DB.  */
  apr_hash_set(db->dir_data,
               (*pdh)->local_abspath, APR_HASH_KEY_STRING,
               *pdh);

  /* Did we traverse up to parent directories?

     Note that if found_pdh is non-NULL, then the second part of this
     condition is also true -- found_pdh is just a quick way to avoid
     a string compare.  */
  if (found_pdh == NULL && strcmp(local_abspath, (*pdh)->local_abspath) == 0)
    {
      /* We did not move to a parent of the original requested directory.
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
          parent_pdh->db = db;
          parent_pdh->local_abspath = apr_pstrdup(db->state_pool, parent_dir);
          parent_pdh->local_relpath =
            svn_dirent_dirname(child_pdh->local_relpath, db->state_pool);
          parent_pdh->sdb = child_pdh->sdb;
          parent_pdh->wc_id = child_pdh->wc_id;
          parent_pdh->wcroot_abspath = child_pdh->wcroot_abspath;

          apr_hash_set(db->dir_data,
                       parent_pdh->local_abspath, APR_HASH_KEY_STRING,
                       parent_pdh);
        }

      /* Point the child PDH at this (new) parent PDH. This will allow for
         easy traversals without path munging.  */
      child_pdh->parent = parent_pdh;
      child_pdh = parent_pdh;

      /* Loop if we haven't reached the PDH we found, or the abspath
         where we terminated the search (when we found wc.db).  */
    }
  while (child_pdh != found_pdh
         && strcmp(child_pdh->local_abspath, local_abspath) != 0);

  return SVN_NO_ERROR;
}


static svn_error_t *
navigate_to_parent(svn_wc__db_pdh_t **parent_pdh,
                   svn_wc__db_pdh_t *child_pdh,
                   svn_sqlite__mode_t smode,
                   apr_pool_t *scratch_pool)
{
  const char *parent_abspath;
  const char *local_relpath;

  if ((*parent_pdh = child_pdh->parent) != NULL)
    return SVN_NO_ERROR;

  parent_abspath = svn_dirent_dirname(child_pdh->local_abspath, scratch_pool);
  SVN_ERR(parse_local_abspath(parent_pdh, &local_relpath, child_pdh->db,
                              parent_abspath, smode,
                              scratch_pool, scratch_pool));
  child_pdh->parent = *parent_pdh;
  return SVN_NO_ERROR;
}


/* For a given REPOS_ROOT_URL/REPOS_UUID pair, return the existing REPOS_ID
   value. If one does not exist, then create a new one. */
static svn_error_t *
create_repos_id(apr_int64_t *repos_id, const char *repos_root_url,
                const char *repos_uuid, svn_sqlite__db_t *sdb,
                apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_REPOSITORY));
  SVN_ERR(svn_sqlite__bindf(stmt, "s", repos_uuid));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (have_row)
    {
      *repos_id = svn_sqlite__column_int64(stmt, 0);
      return svn_sqlite__reset(stmt);
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  /* NOTE: strictly speaking, there is a race condition between the
     above query and the insertion below. We're simply going to ignore
     that, as it means two processes are *modifying* the working copy
     at the same time, *and* new repositores are becoming visible.
     This is rare enough, let alone the miniscule chance of hitting
     this race condition. Further, simply failing out will leave the
     database in a consistent state, and the user can just re-run the
     failed operation. */

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_REPOSITORY));
  SVN_ERR(svn_sqlite__bindf(stmt, "ss", repos_root_url, repos_uuid));
  return svn_sqlite__insert(repos_id, stmt);
}


static svn_error_t *
insert_base_node(void *baton, svn_sqlite__db_t *sdb)
{
  const insert_base_baton_t *pibb = baton;
  apr_pool_t *scratch_pool = pibb->scratch_pool;
  svn_sqlite__stmt_t *stmt;

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
                                  svn_dirent_dirname(pibb->local_relpath,
                                                     scratch_pool)));

  SVN_ERR(svn_sqlite__bind_text(stmt, 6, presence_to_word(pibb->status)));
  SVN_ERR(svn_sqlite__bind_text(stmt, 7, kind_to_word(pibb->kind)));
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
                                    pibb->revision));
          SVN_ERR(svn_sqlite__insert(NULL, stmt));
        }
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
gather_children(const apr_array_header_t **children,
                enum statement_keys key,
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

  /* ### should test the node to ensure it is a directory */

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->sdb, key));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wc_id, local_relpath));

  child_names = apr_array_make(result_pool, 20, sizeof(const char *));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *child_relpath = svn_sqlite__column_text(stmt, 0, NULL);

      APR_ARRAY_PUSH(child_names, const char *) =
        svn_dirent_basename(child_relpath, result_pool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  *children = child_names;

  return svn_sqlite__reset(stmt);
}


svn_error_t *
svn_wc__db_open(svn_wc__db_t **db,
                svn_wc__db_openmode_t mode,
                const char *local_abspath,
                svn_config_t *config,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  *db = new_db_state(mode, config, result_pool);

  /* ### open_one_directory() doesn't fill in SDB and other data. for now,
     ### we want that in all structures, so we don't have to do on-demand
     ### searching/opening when we already have a PDH.  */
#if 0
  return open_one_directory(*db, local_abspath, scratch_pool);
#else
  return SVN_NO_ERROR;
#endif
}


svn_error_t *
svn_wc__db_version(int *version,
                   const char *path,
                   apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const char *format_file_path;

  /* First, try reading the wc.db file.  Instead of stat'ing the file to
     see if it exists, and then opening it, we just try opening it.  If we
     get any kind of an error, wrap that eith an ENOENT error and return. */
  err = svn_sqlite__get_schema_version(version,
                                       svn_wc__adm_child(path, "wc.db",
                                                         scratch_pool),
                                       scratch_pool);
  if (err == NULL)
    return SVN_NO_ERROR;
  if (err->apr_err != SVN_ERR_SQLITE_ERROR
      && !APR_STATUS_IS_ENOENT(err->apr_err))
    return err;
  svn_error_clear(err);

  /* Hmm, that didn't work.  Now try reading the format number from the
     entries file. */
  format_file_path = svn_wc__adm_child(path, SVN_WC__ADM_ENTRIES, scratch_pool);
  err = svn_io_read_version_file(version, format_file_path, scratch_pool);
  if (err == NULL)
    return SVN_NO_ERROR;
  if (err->apr_err != SVN_ERR_BAD_VERSION_FILE_FORMAT)
    return svn_error_createf(SVN_ERR_WC_MISSING, err, _("'%s' does not exist"),
                             svn_dirent_local_style(path, scratch_pool));
  svn_error_clear(err);

  /* Wow, another error; this must be a really old working copy!  Fall back
     to reading the format file. */
  /* Note that the format file might not exist in newer working copies
     (format 7 and higher), but in that case, the entries file should
     have contained the format number. */
  format_file_path = svn_wc__adm_child(path, SVN_WC__ADM_FORMAT, scratch_pool);
  err = svn_io_read_version_file(version, format_file_path, scratch_pool);
  if (err == NULL)
    return SVN_NO_ERROR;
  if (APR_STATUS_IS_ENOENT(err->apr_err)
      || APR_STATUS_IS_ENOTDIR(err->apr_err))
    return svn_error_createf(SVN_ERR_WC_MISSING, err, _("'%s' does not exist"),
                             svn_dirent_local_style(path, scratch_pool));
  svn_error_clear(err);

  /* If we've gotten this far, all of the above checks have failed, so just
     bail. */
  return svn_error_createf(SVN_ERR_WC_MISSING, NULL,
                           _("'%s' is not a working copy"),
                           svn_dirent_local_style(path, scratch_pool));
}


svn_error_t *
svn_wc__db_txn_begin(svn_wc__db_t *db,
                     apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_txn_rollback(svn_wc__db_t *db,
                        apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_txn_commit(svn_wc__db_t *db,
                      apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_close(svn_wc__db_t *db,
                 apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__db_txn_rollback(db, scratch_pool));
  return SVN_NO_ERROR;
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
  SVN_ERR_ASSERT(changed_date > 0);
  SVN_ERR_ASSERT(changed_author != NULL);
  SVN_ERR_ASSERT(children != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid, pdh->sdb,
                          scratch_pool));

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_dir;
  ibb.wc_id = pdh->wc_id;
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

  ibb.scratch_pool = scratch_pool;

  /* Insert the directory and all its children transactionally.

     Note: old children can stick around, even if they are no longer present
     in this directory's revision.  */
  return svn_sqlite__with_transaction(pdh->sdb, insert_base_node, &ibb);
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
  SVN_ERR_ASSERT(changed_date > 0);
  SVN_ERR_ASSERT(changed_author != NULL);
  SVN_ERR_ASSERT(checksum != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid, pdh->sdb,
                          scratch_pool));

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_file;
  ibb.wc_id = pdh->wc_id;
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

  ibb.scratch_pool = scratch_pool;

  /* ### hmm. if this used to be a directory, we should remove children.
     ### or maybe let caller deal with that, if there is a possibility
     ### of a node kind change (rather than eat an extra lookup here).  */

  return insert_base_node(&ibb, pdh->sdb);
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
  SVN_ERR_ASSERT(changed_date > 0);
  SVN_ERR_ASSERT(changed_author != NULL);
  SVN_ERR_ASSERT(target != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid, pdh->sdb,
                          scratch_pool));

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_symlink;
  ibb.wc_id = pdh->wc_id;
  ibb.local_relpath = local_relpath;
  ibb.repos_id = repos_id;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  ibb.props = props;
  ibb.changed_rev = changed_rev;
  ibb.changed_date = changed_date;
  ibb.changed_author = changed_author;

  ibb.target = target;

  ibb.scratch_pool = scratch_pool;

  /* ### hmm. if this used to be a directory, we should remove children.
     ### or maybe let caller deal with that, if there is a possibility
     ### of a node kind change (rather than eat an extra lookup here).  */

  return insert_base_node(&ibb, pdh->sdb);
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

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid, pdh->sdb,
                          scratch_pool));

  ibb.status = status;
  ibb.kind = kind;
  ibb.wc_id = pdh->wc_id;
  ibb.local_relpath = local_relpath;
  ibb.repos_id = repos_id;
  ibb.repos_relpath = repos_relpath;
  ibb.revision = revision;

  ibb.props = NULL;
  ibb.changed_rev = SVN_INVALID_REVNUM;
  ibb.changed_date = 0;
  ibb.changed_author = NULL;

  /* Depending upon KIND, any of these might get used. */
  ibb.children = NULL;
  ibb.depth = svn_depth_unknown;
  ibb.checksum = NULL;
  ibb.translated_size = SVN_INVALID_FILESIZE;
  ibb.target = NULL;

  ibb.scratch_pool = scratch_pool;

  /* ### hmm. if this used to be a directory, we should remove children.
     ### or maybe let caller deal with that, if there is a possibility
     ### of a node kind change (rather than eat an extra lookup here).  */

  return insert_base_node(&ibb, pdh->sdb);
}


svn_error_t *
svn_wc__db_base_remove(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  NOT_IMPLEMENTED();
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
                         svn_depth_t *depth,
                         svn_checksum_t **checksum,
                         svn_filesize_t *translated_size,
                         const char **target,
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

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->sdb, STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      const char *kind_str = svn_sqlite__column_text(stmt, 5, NULL);
      svn_wc__db_kind_t node_kind;

      SVN_ERR_ASSERT(kind_str != NULL);
      node_kind = word_to_kind(kind_str);

      if (kind)
        {
          *kind = node_kind;
        }
      if (status)
        {
          const char *presence = svn_sqlite__column_text(stmt, 4, NULL);

          SVN_ERR_ASSERT(presence != NULL);
          *status = word_to_presence(presence);
        }
      if (revision)
        {
          *revision = svn_sqlite__column_revnum(stmt, 6);
        }
      if (repos_relpath)
        {
          *repos_relpath = svn_sqlite__column_text(stmt, 3, result_pool);
        }
      if (repos_root_url || repos_uuid)
        {
          /* Fetch repository information via REPOS_ID. */
          if (svn_sqlite__column_is_null(stmt, 2))
            {
              if (repos_root_url)
                *repos_root_url = NULL;
              if (repos_uuid)
                *repos_uuid = NULL;
            }
          else
            err = fetch_repos_info(repos_root_url, repos_uuid, pdh->sdb,
                                   svn_sqlite__column_int64(stmt, 2),
                                   result_pool);
        }
      if (changed_rev)
        {
          *changed_rev = svn_sqlite__column_revnum(stmt, 9);
        }
      if (changed_date)
        {
          *changed_date = svn_sqlite__column_int64(stmt, 10);
        }
      if (changed_author)
        {
          /* Result may be NULL. */
          *changed_author = svn_sqlite__column_text(stmt, 11, result_pool);
        }
      if (depth)
        {
          if (node_kind != svn_wc__db_kind_dir)
            *depth = svn_depth_unknown;
          else
            {
              const char *depth_str = svn_sqlite__column_text(stmt, 12, NULL);

              if (depth_str == NULL)
                *depth = svn_depth_unknown;
              else
                *depth = svn_depth_from_word(depth_str);
            }
        }
      if (checksum)
        {
          if (node_kind != svn_wc__db_kind_file)
            *checksum = NULL;
          else
            {
              err = svn_sqlite__column_checksum(checksum, stmt, 7,
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
          *translated_size = get_translated_size(stmt, 8);
        }
      if (target)
        {
          if (node_kind != svn_wc__db_kind_symlink)
            *target = NULL;
          else
            *target = svn_sqlite__column_text(stmt, 13, result_pool);
        }
    }
  else
    {
      err = svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("The node '%s' was not found."),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool));
    }

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
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_base_get_children(const apr_array_header_t **children,
                             svn_wc__db_t *db,
                             const char *local_abspath,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool)
{
  return gather_children(children, STMT_SELECT_BASE_NODE_CHILDREN,
                         db, local_abspath, result_pool, scratch_pool);
}


svn_error_t *
svn_wc__db_pristine_get_handle(svn_wc__db_pdh_t **pdh,
                               svn_wc__db_t *db,
                               const char *local_dir_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));

  /* ### need to fix this up. we'll probably get called with a subdirectory
     ### of the path that we opened originally. that means we probably
     ### won't have the subdir in the hash table. need to be able to
     ### incrementally grow the hash of per-dir structures. */

  *pdh = apr_hash_get(db->dir_data, local_dir_abspath, APR_HASH_KEY_STRING);

  if (*pdh == NULL)
    {
      /* Oops. We haven't seen this WC directory before. Let's get it into
         our hash of per-directory information. */
      SVN_ERR(open_one_directory(db, local_dir_abspath, scratch_pool));

      *pdh = apr_hash_get(db->dir_data, local_dir_abspath, APR_HASH_KEY_STRING);

      SVN_ERR_ASSERT(*pdh != NULL);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_pristine_read(svn_stream_t **contents,
                         svn_wc__db_pdh_t *pdh,
                         const svn_checksum_t *checksum,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  const char *path;

  SVN_ERR(get_pristine_fname(&path, pdh, checksum, FALSE /* create_subdir */,
                             scratch_pool, scratch_pool));

  return svn_stream_open_readonly(contents, path, result_pool, scratch_pool);
}


svn_error_t *
svn_wc__db_pristine_write(svn_stream_t **contents,
                          svn_wc__db_pdh_t *pdh,
                          const svn_checksum_t *checksum,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  const char *path;

  SVN_ERR(get_pristine_fname(&path, pdh, checksum, TRUE /* create_subdir */,
                             scratch_pool, scratch_pool));

  SVN_ERR(svn_stream_open_writable(contents, path, result_pool, scratch_pool));

  /* ### we should wrap the stream. count the bytes. at close, then we
     ### should write the count into the sqlite database. */

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_pristine_get_tempdir(const char **temp_dir,
                                svn_wc__db_pdh_t *pdh,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_install(svn_wc__db_pdh_t *pdh,
                            const char *local_abspath,
                            const svn_checksum_t *checksum,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_check(svn_boolean_t *present,
                          int *refcount,
                          svn_wc__db_pdh_t *pdh,
                          const svn_checksum_t *checksum,
                          svn_wc__db_checkmode_t mode,
                          apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_repair(svn_wc__db_pdh_t *pdh,
                           const svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_incref(int *new_refcount,
                           svn_wc__db_pdh_t *pdh,
                           const svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_pristine_decref(int *new_refcount,
                           svn_wc__db_pdh_t *pdh,
                           const svn_checksum_t *checksum,
                           apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
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
                            apr_hash_t *props,
                            const apr_array_header_t *children,
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(children != NULL);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_add_file(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_hash_t *props,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_add_symlink(svn_wc__db_t *db,
                          const char *local_abspath,
                          apr_hash_t *props,
                          const char *target,
                          apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(props != NULL);
  SVN_ERR_ASSERT(target != NULL);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_set_prop(svn_wc__db_t *db,
                       const char *local_abspath,
                       const char *propname,
                       const svn_string_t *propval,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_set_props(svn_wc__db_t *db,
                        const char *local_abspath,
                        apr_hash_t *props,
                        apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
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


svn_error_t *
svn_wc__db_op_add_to_changelist(svn_wc__db_t *db,
                                const char *local_abspath,
                                const char *changelist,
                                apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
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
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
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
svn_wc__db_read_info(svn_wc__db_status_t *status,
                     svn_wc__db_kind_t *kind,
                     svn_revnum_t *revision,
                     const char **repos_relpath,
                     const char **repos_root_url,
                     const char **repos_uuid,
                     svn_revnum_t *changed_rev,
                     apr_time_t *changed_date,
                     const char **changed_author,
                     svn_depth_t *depth,
                     svn_checksum_t **checksum,
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
  svn_error_t *err;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt_base, pdh->sdb,
                                    STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt_base, "is", pdh->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_base, stmt_base));

  SVN_ERR(svn_sqlite__get_statement(&stmt_work, pdh->sdb,
                                    STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt_work, "is", pdh->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_work, stmt_work));

  SVN_ERR(svn_sqlite__get_statement(&stmt_act, pdh->sdb,
                                    STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt_act, "is", pdh->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_act, stmt_act));

  if (have_base || have_work)
    {
      const char *kind_str;
      svn_wc__db_kind_t node_kind;

      if (have_work)
        kind_str = svn_sqlite__column_text(stmt_work, 1, NULL);
      else
        kind_str = svn_sqlite__column_text(stmt_base, 5, NULL);

      SVN_ERR_ASSERT(kind_str != NULL);
      node_kind = word_to_kind(kind_str);

      if (status)
        {
          const char *presence_str;

          if (have_base)
            {
              presence_str = svn_sqlite__column_text(stmt_base, 4, NULL);
              *status = word_to_presence(presence_str);

              /* We have a presence that allows a WORKING_NODE override
                 (normal or not-present), or we don't have an override.  */
              SVN_ERR_ASSERT((*status != svn_wc__db_status_absent
                              && *status != svn_wc__db_status_excluded
                              && *status != svn_wc__db_status_incomplete)
                             || !have_work);
            }

          if (have_work)
            {
              svn_wc__db_status_t work_status;

              presence_str = svn_sqlite__column_text(stmt_work, 0, NULL);
              work_status = word_to_presence(presence_str);
              SVN_ERR_ASSERT(work_status == svn_wc__db_status_normal
                             || work_status == svn_wc__db_status_not_present
                             || work_status == svn_wc__db_status_incomplete);

              /* ### detect status_changed? or toss that status? */

              if (work_status == svn_wc__db_status_incomplete)
                {
                  *status = svn_wc__db_status_incomplete;
                }
              else if (work_status == svn_wc__db_status_not_present)
                {
                  /* The caller should scan upwards to detect whether this
                     deletion has occurred because this node has been moved
                     away, or it is a regular deletion. Also note that the
                     deletion could be of the BASE tree, or a child of
                     something that has been copied/moved here.  */
                  *status = svn_wc__db_status_deleted;
                }
              else
                {
                  /* The caller should scan upwards to detect whether this
                     addition has occurred because of a simple addition,
                     a copy, or is the destination of a move.  */
                  *status = svn_wc__db_status_added;
                }
            }
        }
      if (kind)
        {
          *kind = node_kind;
        }
      if (revision)
        {
          if (have_work)
            *revision = SVN_INVALID_REVNUM;
          else
            *revision = svn_sqlite__column_revnum(stmt_base, 6);
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
            *repos_relpath = svn_sqlite__column_text(stmt_base, 3,
                                                     result_pool);
        }
      if (repos_root_url || repos_uuid)
        {
          /* Fetch repository information via REPOS_ID. If we have a
             WORKING_NODE (and have been added), then the repository
             we're being added to will be dependent upon a parent. The
             caller can scan upwards to locate the repository.  */
          if (have_work || svn_sqlite__column_is_null(stmt_base, 2))
            {
              if (repos_root_url)
                *repos_root_url = NULL;
              if (repos_uuid)
                *repos_uuid = NULL;
            }
          else
            err = fetch_repos_info(repos_root_url, repos_uuid, pdh->sdb,
                                   svn_sqlite__column_int64(stmt_base, 2),
                                   result_pool);
        }
      if (changed_rev)
        {
          if (have_work)
            *changed_rev = svn_sqlite__column_revnum(stmt_work, 4);
          else
            *changed_rev = svn_sqlite__column_revnum(stmt_base, 9);
        }
      if (changed_date)
        {
          if (have_work)
            *changed_date = svn_sqlite__column_int64(stmt_work, 5);
          else
            *changed_date = svn_sqlite__column_int64(stmt_base, 10);
        }
      if (changed_author)
        {
          if (have_work)
            *changed_author = svn_sqlite__column_text(stmt_work, 6,
                                                      result_pool);
          else
            *changed_author = svn_sqlite__column_text(stmt_base, 11,
                                                      result_pool);
        }
      if (depth)
        {
          if (node_kind != svn_wc__db_kind_dir)
            *depth = svn_depth_unknown;
          else
            {
              const char *depth_str;

              if (have_work)
                depth_str = svn_sqlite__column_text(stmt_work, 7, NULL);
              else
                depth_str = svn_sqlite__column_text(stmt_base, 12, NULL);

              if (depth_str == NULL)
                *depth = svn_depth_unknown;
              else
                *depth = svn_depth_from_word(depth_str);
            }
        }
      if (checksum)
        {
          if (node_kind != svn_wc__db_kind_file)
            *checksum = NULL;
          else
            {
              if (have_work)
                err = svn_sqlite__column_checksum(checksum, stmt_work, 2,
                                                  result_pool);
              else
                err = svn_sqlite__column_checksum(checksum, stmt_base, 7,
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
          if (have_work)
            *translated_size = get_translated_size(stmt_work, 3);
          else
            *translated_size = get_translated_size(stmt_base, 8);
        }
      if (target)
        {
          if (node_kind != svn_wc__db_kind_symlink)
            *target = NULL;
          else if (have_work)
            *target = svn_sqlite__column_text(stmt_work, 8, result_pool);
          else
            *target = svn_sqlite__column_text(stmt_base, 13, result_pool);
        }
      if (changelist)
        {
          if (have_act)
            *changelist = svn_sqlite__column_text(stmt_act, 0, result_pool);
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
          err = fetch_repos_info(original_root_url, original_uuid, pdh->sdb,
                                 svn_sqlite__column_int64(stmt_work, 9),
                                 result_pool);
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
          /* ### fix this */
          *props_mod = FALSE;
        }
      if (base_shadowed)
        {
          *base_shadowed = have_base && have_work;
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
  return svn_error_compose_create(err, svn_sqlite__reset(stmt_act));
}


svn_error_t *
svn_wc__db_read_prop(const svn_string_t **propval,
                     svn_wc__db_t *db,
                     const char *local_abspath,
                     const char *propname,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_read_props(apr_hash_t **props,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_read_pristine_props(apr_hash_t **props,
                               svn_wc__db_t *db,
                               const char *local_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_read_children(const apr_array_header_t **children,
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  return gather_children(children, STMT_SELECT_WORKING_CHILDREN,
                         db, local_abspath, result_pool, scratch_pool);
}


svn_error_t *
svn_wc__db_global_relocate(svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           const char *from_url,
                           const char *to_url,
                           svn_depth_t depth,
                           apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_global_commit(svn_wc__db_t *db,
                         const char *local_abspath,
                         svn_revnum_t new_revision,
                         apr_time_t new_date,
                         const char *new_author,
                         apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(SVN_IS_VALID_REVNUM(new_revision));
  SVN_ERR_ASSERT(new_date > 0);
  SVN_ERR_ASSERT(new_author != NULL);

  NOT_IMPLEMENTED();
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

  SVN_ERR(scan_upwards_for_repos(&repos_id, repos_relpath,
                                 pdh->wc_id, local_relpath, pdh->sdb,
                                 result_pool, scratch_pool));

  if (repos_root_url || repos_uuid)
    return fetch_repos_info(repos_root_url, repos_uuid, pdh->sdb, repos_id,
                            result_pool);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_scan_working(svn_wc__db_status_t *status,
                        const char **op_root_abspath,
                        const char **repos_relpath,
                        const char **repos_root_url,
                        const char **repos_uuid,
                        const char **original_repos_relpath,
                        const char **original_root_url,
                        const char **original_uuid,
                        svn_revnum_t *original_revision,
                        const char **moved_to_abspath,
                        svn_wc__db_t *db,
                        const char *local_abspath,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_status_t start_status;
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
  if (moved_to_abspath)
    *moved_to_abspath = NULL;

  SVN_ERR(parse_local_abspath(&pdh, &current_relpath, db, current_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

  while (TRUE)
    {
      svn_sqlite__stmt_t *stmt;
      svn_boolean_t have_row;

      /* ### is it faster to fetch fewer columns? */
      SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->sdb,
                                        STMT_SELECT_WORKING_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wc_id, current_relpath));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (!have_row)
        {
          if (current_abspath == local_abspath)
            {
              svn_error_clear(svn_sqlite__reset(stmt));

              return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                       _("The node '%s' was not found."),
                                       svn_dirent_local_style(local_abspath,
                                                              scratch_pool));
            }
          SVN_ERR(svn_sqlite__reset(stmt));

          /* We just fell off the top of the WORKING tree. If we haven't
             found the operation root, then the child node that we just
             left was that root.  */
          if (op_root_abspath && *op_root_abspath == NULL)
            {
              SVN_ERR_ASSERT(child_abspath != NULL);
              *op_root_abspath = apr_pstrdup(result_pool, child_abspath);
            }

          /* If the subtree was deleted, then we can exit since there is no
             need to continue scanning BASE nodes upwards to determine a
             repository location.  */
          if (start_status == svn_wc__db_status_deleted)
            return SVN_NO_ERROR;

          /* Otherwise, this node was added/copied/moved and has an implicit
             location in the repository. We now need to traverse BASE nodes
             looking for repository info.  */
          break;
        }

      /* Record information from the starting node.  */
      if (current_abspath == local_abspath)
        {
          start_status = word_to_presence(svn_sqlite__column_text(stmt, 0,
                                                                  NULL));
          if (start_status == svn_wc__db_status_normal)
            start_status = svn_wc__db_status_added;
          else
            start_status = svn_wc__db_status_deleted;

          /* Provide the default status; we'll override as appropriate. */
          if (status)
            *status = start_status;
        }
      else if (start_status == svn_wc__db_status_deleted
               && strcmp("normal",
                         svn_sqlite__column_text(stmt, 0, NULL)) == 0)
        {
          /* We have moved upwards at least one node, the start node
             was deleted, but we have now run into a not-deleted node.
             Thus, the node we just left was the root of a delete.
             Record that and exit, as we have no further information
             to discover.  */
          if (op_root_abspath)
            *op_root_abspath = apr_pstrdup(result_pool, child_abspath);

          return svn_sqlite__reset(stmt);
        }

      if (!svn_sqlite__column_is_null(stmt, 13 /* moved_to */))
        {
          SVN_ERR_ASSERT(start_status == svn_wc__db_status_deleted);

          if (status)
            *status = svn_wc__db_status_moved_src;
          if (op_root_abspath)
            *op_root_abspath = apr_pstrdup(result_pool, current_abspath);
          if (moved_to_abspath)
            *moved_to_abspath = svn_dirent_join(
                                  pdh->wcroot_abspath,
                                  svn_sqlite__column_text(stmt, 13, NULL),
                                  result_pool);

          /* There is no other information to retrieve. We're done. */
          return svn_sqlite__reset(stmt);
        }

      /* We want the operation closest to the start node, and then we
         ignore any operations on its ancestors.  */
      if (!found_info
          && !svn_sqlite__column_is_null(stmt, 9 /* copyfrom_repos_id */))
        {
          SVN_ERR_ASSERT(start_status == svn_wc__db_status_added);

          if (status)
            {
              if (svn_sqlite__column_boolean(stmt, 12 /* moved_here */))
                *status = svn_wc__db_status_moved_dst;
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
                                     pdh->sdb,
                                     svn_sqlite__column_int64(stmt, 9),
                                     result_pool));
          if (original_revision)
            *original_revision = svn_sqlite__column_revnum(stmt, 11);

          /* We may have to keep tracking upwards for REPOS_* values.
             If they're not needed, then just return.  */
          if (repos_relpath == NULL
              && repos_root_url == NULL
              && repos_uuid == NULL)
            return svn_sqlite__reset(stmt);

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
          build_relpath = svn_dirent_join(svn_dirent_basename(current_abspath,
                                                              scratch_pool),
                                          build_relpath,
                                          scratch_pool);
        }

      /* Move to the parent node. Remember the abspath to this node, since
         it could be the root of an add/delete.  */
      child_abspath = current_abspath;
      if (strcmp(current_relpath, pdh->local_relpath) == 0)
        {
          /* The current node is a directory, so move to the parent dir.  */
          SVN_ERR(navigate_to_parent(&pdh, pdh, svn_sqlite__mode_readonly,
                                     scratch_pool));
        }
      current_abspath = pdh->local_abspath;
      current_relpath = pdh->local_relpath;
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
        *repos_relpath = svn_dirent_join(base_relpath, build_relpath,
                                         result_pool);
    }

  return SVN_NO_ERROR;
}
