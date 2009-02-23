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
 */


struct svn_wc__db_t {
  /* What's the appropriate mode for this datastore? */
  svn_wc__db_openmode_t mode;

  /* We need the config whenever we run into a new WC directory, in order
     to figure out where we should look for the corresponding datastore. */
  svn_config_t *config;

  /* Map a given working copy directory to its relevant data. */
  apr_hash_t *dir_data;
};

/**
 * This structure records all the information that we need to deal with
 * a given working copy directory.
 */
struct svn_wc__db_pdh_t {
  /* This per-dir state is associated with this global state. */
  svn_wc__db_t *db;

  /* Root of the TEXT-BASE directory structure for the WORKING/ACTUAL files
     in this directory. */
  const char *base_dir;
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
  STMT_SELECT_REPOSITORY_BY_ID,
  STMT_SELECT_WCROOT_NULL,
  STMT_SELECT_REPOSITORY,
  STMT_INSERT_REPOSITORY,
  STMT_INSERT_BASE_NODE,
  STMT_INSERT_BASE_NODE_INCOMPLETE,
  STMT_SELECT_BASE_NODE_CHILDREN
};

static const char * const statements[] = {
  "select wc_id, local_relpath, repos_id, repos_relpath, "
  "  presence, kind, revnum, checksum, translated_size, "
  "  changed_rev, changed_date, changed_author, depth "
  "from base_node "
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

  "select local_relpath from base_node where parent_relpath = ?1;",

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
                   apr_pool_t *result_pool,
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

  pdh = apr_palloc(result_pool, sizeof(*pdh));
  pdh->db = db;

  /* ### for now, every directory still has a .svn subdir, and a
     ### "pristine" subdir in there. later on, we'll alter the
     ### storage location/strategy */

  /* ### need to fix this to use a symbol for ".svn". we shouldn't need
     ### to use join_many since we know "/" is the separator for
     ### internal canonical paths */
  pdh->base_dir = svn_dirent_join(path, ".svn/pristine", result_pool);

  /* Make sure the key lasts as long as the hash. Note that if we did
     not call dirname(), then this path is the provided path, but we
     do not know its lifetime (nor does our API contract specify a
     requirement for the lifetime). */
  path = apr_pstrdup(result_pool, path);
  apr_hash_set(db->dir_data, path, APR_HASH_KEY_STRING, pdh);

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
parse_local_abspath(svn_sqlite__db_t **sdb,
                    apr_int64_t *wc_id,
                    const char **relpath,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    svn_sqlite__mode_t smode,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  const char *original_abspath = local_abspath;
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  const char *local_relpath;

  /* ### we need more logic for finding the database (if it is located
     ### outside of the wcroot) and then managing all of that within DB.
     ### for now: play quick & dirty. */

  /* Assume that LOCAL_ABSPATH is a directory, and look for the SQLite
     database in the right place. If we find it... great! If not, then
     peel off some components, and try again. */

  local_relpath = "";
  while (TRUE)
    {
      svn_error_t *err;
      const char *base;

      err = svn_sqlite__open(sdb,
                             svn_wc__adm_child(local_abspath, "wc.db",
                                               scratch_pool),
                             smode, statements,
                             SVN_WC__VERSION_EXPERIMENTAL, upgrade_sql,
                             result_pool, scratch_pool);
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

      local_relpath = svn_dirent_join(base, local_relpath, scratch_pool);
      local_abspath = svn_dirent_dirname(local_abspath, scratch_pool);
    }

  *relpath = apr_pstrdup(result_pool, local_relpath);

  /* ### cheat. we know there is just one WORKING_COPY row, and it has a
     ### NULL value for local_abspath. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, *sdb, STMT_SELECT_WCROOT_NULL));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                             _("Missing a row in WCROOT for '%s'."),
                             svn_dirent_local_style(original_abspath,
                                                    scratch_pool));

  SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 0));
  *wc_id = svn_sqlite__column_int64(stmt, 0);

  return svn_sqlite__reset(stmt);
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


svn_error_t *
svn_wc__db_open(svn_wc__db_t **db,
                svn_wc__db_openmode_t mode,
                const char *local_abspath,
                svn_config_t *config,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  *db = new_db_state(mode, config, result_pool);

  return open_one_directory(*db, local_abspath, result_pool, scratch_pool);
}


svn_error_t *
svn_wc__db_open_many(svn_wc__db_t **db,
                     svn_wc__db_openmode_t mode,
                     const apr_array_header_t *paths,
                     svn_config_t *config,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  int i;

  *db = new_db_state(mode, config, result_pool);

  for (i = 0; i < paths->nelts; ++i)
    {
      const char *path = APR_ARRAY_IDX(paths, i, const char *);

      SVN_ERR(open_one_directory(*db, path, result_pool, scratch_pool));
    }

  return SVN_NO_ERROR;
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
  if (err
      && err->apr_err != SVN_ERR_SQLITE_ERROR
      && !APR_STATUS_IS_ENOENT(err->apr_err))
    return err;
  else if (!err)
    return SVN_NO_ERROR;

  /* Hmm, that didn't work.  Now try reading the format number from the
     entries file. */
  svn_error_clear(err);
  format_file_path = svn_wc__adm_child(path, SVN_WC__ADM_ENTRIES, scratch_pool);
  err = svn_io_read_version_file(version, format_file_path, scratch_pool);
  if (err && err->apr_err != SVN_ERR_BAD_VERSION_FILE_FORMAT)
    return svn_error_createf(SVN_ERR_WC_MISSING, err, _("'%s' does not exist"),
                             svn_dirent_local_style(path, scratch_pool));
  else if (!err)
    return SVN_NO_ERROR;

  /* Wow, another error; this must be a really old working copy!  Fall back
     to reading the format file. */
  svn_error_clear(err);
  /* Note that the format file might not exist in newer working copies
     (format 7 and higher), but in that case, the entries file should
     have contained the format number. */
  format_file_path = svn_wc__adm_child(path, SVN_WC__ADM_FORMAT, scratch_pool);
  err = svn_io_read_version_file(version, format_file_path, scratch_pool);

  if (err && (APR_STATUS_IS_ENOENT(err->apr_err)
              || APR_STATUS_IS_ENOTDIR(err->apr_err)))
    return svn_error_createf(SVN_ERR_WC_MISSING, err, _("'%s' does not exist"),
                             svn_dirent_local_style(path, scratch_pool));
  else if (!err)
    return SVN_NO_ERROR;

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
  svn_sqlite__db_t *sdb;
  apr_int64_t wc_id;
  const char *relpath;
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

  SVN_ERR(parse_local_abspath(&sdb, &wc_id, &relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid, sdb,
                          scratch_pool));

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_dir;
  ibb.wc_id = wc_id;
  ibb.local_relpath = relpath;
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
  return svn_sqlite__with_transaction(sdb, insert_base_node, &ibb);
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
  svn_sqlite__db_t *sdb;
  apr_int64_t wc_id;
  const char *relpath;
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

  SVN_ERR(parse_local_abspath(&sdb, &wc_id, &relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid, sdb,
                          scratch_pool));

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_file;
  ibb.wc_id = wc_id;
  ibb.local_relpath = relpath;
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

  return insert_base_node(&ibb, sdb);
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
  svn_sqlite__db_t *sdb;
  apr_int64_t wc_id;
  const char *relpath;
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

  SVN_ERR(parse_local_abspath(&sdb, &wc_id, &relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid, sdb,
                          scratch_pool));

  ibb.status = svn_wc__db_status_normal;
  ibb.kind = svn_wc__db_kind_symlink;
  ibb.wc_id = wc_id;
  ibb.local_relpath = relpath;
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

  return insert_base_node(&ibb, sdb);
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
  svn_sqlite__db_t *sdb;
  apr_int64_t wc_id;
  const char *relpath;
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

  SVN_ERR(parse_local_abspath(&sdb, &wc_id, &relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid, sdb,
                          scratch_pool));

  ibb.status = status;
  ibb.kind = kind;
  ibb.wc_id = wc_id;
  ibb.local_relpath = relpath;
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

  return insert_base_node(&ibb, sdb);
}


svn_error_t *
svn_wc__db_base_remove(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  svn_sqlite__db_t *sdb;
  apr_int64_t wc_id;
  const char *relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&sdb, &wc_id, &relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_base_get_info(svn_wc__db_kind_t *kind,
                         svn_wc__db_status_t *status,
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
                         svn_wc__db_t *db,
                         const char *local_abspath,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_sqlite__db_t *sdb;
  apr_int64_t wc_id;
  const char *relpath;
  svn_boolean_t have_row;
  svn_error_t *err = SVN_NO_ERROR;
  svn_boolean_t inherit_repos = FALSE;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&sdb, &wc_id, &relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, relpath));
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
          if (svn_sqlite__column_is_null(stmt, 3))
            inherit_repos = TRUE;
          else
            *repos_relpath = svn_sqlite__column_text(stmt, 3, result_pool);
        }
      if (repos_root_url || repos_uuid)
        {
          /* Fetch repository information via REPOS_ID. */
          if (svn_sqlite__column_is_null(stmt, 2))
            inherit_repos = TRUE;
          else
            err = fetch_repos_info(repos_root_url, repos_uuid, sdb,
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
          if (svn_sqlite__column_is_null(stmt, 8))
            *translated_size = SVN_INVALID_FILESIZE;
          else
            *translated_size = svn_sqlite__column_int64(stmt, 8);
        }
    }
  else
    {
      err = svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                              _("The node '%s' was not found."),
                              svn_dirent_local_style(local_abspath,
                                                     scratch_pool));
    }

  err = svn_error_compose_create(err, svn_sqlite__reset(stmt));

  if (err == NULL && inherit_repos)
    {
      apr_int64_t repos_id;

      /* Fetch repository information from the parent in order to compute
         this node's information.

         Note: we delay this lookup until AFTER the statement used above
         has been reset. We happen to use the same one for the scan.  */
      SVN_ERR(scan_upwards_for_repos(&repos_id, repos_relpath, wc_id, relpath,
                                     sdb, result_pool, scratch_pool));

      if (repos_root_url || repos_uuid)
        err = fetch_repos_info(repos_root_url, repos_uuid, sdb, repos_id,
                               result_pool);
    }

  return err;
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
  svn_sqlite__db_t *sdb;
  apr_int64_t wc_id;
  const char *relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&sdb, &wc_id, &relpath, db, local_abspath,
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
  svn_sqlite__db_t *sdb;
  apr_int64_t wc_id;
  const char *relpath;
  svn_sqlite__stmt_t *stmt;
  apr_array_header_t *child_names;
  svn_boolean_t have_row;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&sdb, &wc_id, &relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

  /* ### should test the node to ensure it is a directory */

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_SELECT_BASE_NODE_CHILDREN));
  SVN_ERR(svn_sqlite__bindf(stmt, "s", relpath));

  child_names = apr_array_make(result_pool, 20, sizeof(const char *));

  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      const char *local_relpath = svn_sqlite__column_text(stmt, 0, NULL);

      APR_ARRAY_PUSH(child_names, const char *) =
        svn_dirent_basename(local_relpath, result_pool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  *children = child_names;

  return svn_sqlite__reset(stmt);
}


svn_error_t *
svn_wc__db_base_get_symlink_target(const char **target,
                                   svn_wc__db_t *db,
                                   const char *local_abspath,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  svn_sqlite__db_t *sdb;
  apr_int64_t wc_id;
  const char *relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&sdb, &wc_id, &relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

  NOT_IMPLEMENTED();
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
      SVN_ERR(open_one_directory(db, local_dir_abspath,
                                 result_pool, scratch_pool));

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
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
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
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_read_symlink_target(const char **target,
                               svn_wc__db_t *db,
                               const char *local_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
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
