/*
 * wc_db.c :  manipulating the administrative database
 *
 * ====================================================================
 * Copyright (c) 2008-2009 CollabNet.  All rights reserved.
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

#include <assert.h>
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
#include "entries.h"
#include "lock.h"

#include "svn_private_config.h"
#include "private/svn_sqlite.h"
#include "private/svn_skel.h"


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

#define UNKNOWN_WC_ID ((apr_int64_t) -1)
#define FORMAT_FROM_SDB (-1)


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

/** Hold information about a WCROOT.
 *
 * This structure is referenced by all per-directory handles underneath it.
 */
typedef struct {
  /* Location of this wcroot in the filesystem.  */
  const char *abspath;

  /* The SQLite database containing the metadata for everything in
     this wcroot.  */
  svn_sqlite__db_t *sdb;

  /* The WCROOT.id for this directory (and all its children).  */
  apr_int64_t wc_id;

  /* The format of this wcroot's metadata storage (see wc.h). If the
     format has not (yet) been determined, this will be UNKNOWN_FORMAT.  */
  int format;

} wcroot_t;

/**
 * This structure records all the information that we need to deal with
 * a given working copy directory.
 */
struct svn_wc__db_pdh_t {
  /* This per-dir state is associated with this global state. */
  svn_wc__db_t *db;

  /* This (versioned) working copy directory is obstructing what *should*
     be a file in the parent directory (according to its metadata).

     Note: this PDH should probably be ignored (or not created).

     ### obstruction is only possible with per-dir wc.db databases.  */
  svn_boolean_t obstructed_file;

  /* The absolute path to this working copy directory. */
  const char *local_abspath;

  /* What wcroot does this directory belong to?  */
  wcroot_t *wcroot;

  /* The parent directory's per-dir information. */
  svn_wc__db_pdh_t *parent;

  /* Hold onto the old-style access baton that corresponds to this PDH.  */
  svn_wc_adm_access_t *adm_access;
};

/* ### since we're putting the pristine files per-dir, then we don't need
   ### to create subdirectories in order to keep the directory size down.
   ### when we can aggregate pristine files across dirs/wcs, then we will
   ### need to undo the SKIP. */
#define SVN__SKIP_SUBDIR

/* ### duplicates entries.c */
static const char * const upgrade_sql[] = {
  NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL,
  NULL, NULL,
  WC_METADATA_SQL_12,
  WC_METADATA_SQL_13
};

/* These values map to the members of STATEMENTS below, and should be added
   and removed at the same time. */
enum statement_keys {
  STMT_SELECT_BASE_NODE,
  STMT_SELECT_BASE_NODE_WITH_LOCK,
  STMT_SELECT_WORKING_NODE,
  STMT_SELECT_ACTUAL_NODE,
  STMT_SELECT_REPOSITORY_BY_ID,
  STMT_SELECT_WCROOT_NULL,
  STMT_SELECT_REPOSITORY,
  STMT_INSERT_REPOSITORY,
  STMT_INSERT_BASE_NODE,
  STMT_INSERT_BASE_NODE_INCOMPLETE,
  STMT_SELECT_BASE_NODE_CHILDREN,
  STMT_SELECT_WORKING_CHILDREN,
  STMT_SELECT_WORKING_IS_FILE,
  STMT_SELECT_BASE_IS_FILE,
  STMT_SELECT_BASE_PROPS,
  STMT_UPDATE_ACTUAL_PROPS,
  STMT_SELECT_ALL_PROPS,
  STMT_SELECT_PRISTINE_PROPS,
  STMT_INSERT_LOCK,
  STMT_INSERT_WCROOT,
  STMT_UPDATE_BASE_DAV_CACHE,
  STMT_SELECT_BASE_DAV_CACHE,
  STMT_SELECT_DELETION_INFO,
  STMT_SELECT_PARENT_STUB_INFO,
  STMT_DELETE_LOCK,
  STMT_UPDATE_BASE_REPO,
  STMT_UPDATE_BASE_CHILDREN_REPO,
  STMT_UPDATE_WORKING_COPYFROM_REPO,
  STMT_UPDATE_WORKING_CHILDREN_COPYFROM_REPO,
  STMT_UPDATE_LOCK_REPOS_ID
};

static const char * const statements[] = {
  /* STMT_SELECT_BASE_NODE */
  "select wc_id, local_relpath, repos_id, repos_relpath, "
  "  presence, kind, revnum, checksum, translated_size, "
  "  changed_rev, changed_date, changed_author, depth, symlink_target, "
  "  last_mod_time "
  "from base_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_SELECT_BASE_NODE_WITH_LOCK */
  "select wc_id, local_relpath, base_node.repos_id, base_node.repos_relpath, "
  "  presence, kind, revnum, checksum, translated_size, "
  "  changed_rev, changed_date, changed_author, depth, symlink_target, "
  "  last_mod_time, "
  "  lock_token, lock_owner, lock_comment, lock_date "
  "from base_node "
  "left outer join lock on base_node.repos_id = lock.repos_id "
  "  and base_node.repos_relpath = lock.repos_relpath "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_SELECT_WORKING_NODE */
  "select presence, kind, checksum, translated_size, "
  "  changed_rev, changed_date, changed_author, depth, symlink_target, "
  "  copyfrom_repos_id, copyfrom_repos_path, copyfrom_revnum, "
  "  moved_here, moved_to, last_mod_time "
  "from working_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_SELECT_ACTUAL_NODE */
  "select changelist "
  "from actual_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_SELECT_REPOSITORY_BY_ID */
  "select root, uuid from repository where id = ?1;",

  /* STMT_SELECT_WCROOT_NULL */
  "select id from wcroot where local_abspath is null;",

  /* STMT_SELECT_REPOSITORY */
  "select id from repository where uuid = ?1 and root = ?2;",

  /* STMT_INSERT_REPOSITORY */
  "insert into repository (root, uuid) values (?1, ?2);",

  /* STMT_INSERT_BASE_NODE */
  "insert or replace into base_node ("
  "  wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, presence, "
  "  kind, revnum, properties, changed_rev, changed_date, changed_author, "
  "  depth, checksum, translated_size, symlink_target) "
  "values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, "
  "        ?15, ?16);",

  /* STMT_INSERT_BASE_NODE_INCOMPLETE */
  "insert or ignore into base_node ("
  "  wc_id, local_relpath, parent_relpath, presence, kind, revnum) "
  "values (?1, ?2, ?3, 'incomplete', 'unknown', ?5);",

  /* STMT_SELECT_BASE_NODE_CHILDREN */
  "select local_relpath from base_node "
  "where wc_id = ?1 and parent_relpath = ?2;",

  /* STMT_SELECT_WORKING_CHILDREN */
  "select local_relpath from base_node "
  "where wc_id = ?1 and parent_relpath = ?2 "
  "union "
  "select local_relpath from working_node "
  "where wc_id = ?1 and parent_relpath = ?2;",

  /* STMT_SELECT_WORKING_IS_FILE */
  "select kind == 'file' from working_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_SELECT_BASE_IS_FILE */
  "select kind == 'file' from base_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_SELECT_BASE_PROPS */
  "select properties from base_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_UPDATE_ACTUAL_PROPS */
  "update actual_node set properties = ?3 "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_SELECT_ALL_PROPS */
  "select actual_node.properties, working_node.properties, "
  "  base_node.properties "
  "from base_node "
  "left outer join working_node on base_node.wc_id = working_node.wc_id "
  "  and base_node.local_relpath = working_node.local_relpath "
  "left outer join actual_node on base_node.wc_id = actual_node.wc_id "
  "  and base_node.local_relpath = actual_node.local_relpath "
  "where base_node.wc_id = ?1 and base_node.local_relpath = ?2;",

  /* STMT_SELECT_PRISTINE_PROPS */
  "select working_node.properties, base_node.properties "
  "from base_node "
  "left outer join working_node on base_node.wc_id = working_node.wc_id "
  "  and base_node.local_relpath = working_node.local_relpath "
  "where base_node.wc_id = ?1 and base_node.local_relpath = ?2;",

  /* STMT_INSERT_LOCK */
  "insert or replace into lock "
    "(repos_id, repos_relpath, lock_token, lock_owner, lock_comment, "
    " lock_date)"
  "values (?1, ?2, ?3, ?4, ?5, ?6);",

  /* STMT_INSERT_WCROOT */
  "insert into wcroot (local_abspath) "
  "values (?1);",

  /* STMT_UPDATE_BASE_DAV_CACHE */
  "update base_node set dav_cache = ?3 "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_SELECT_BASE_DAV_CACHE */
  "select dav_cache from base_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_SELECT_DELETION_INFO */
  "select base_node.presence, working_node.presence, moved_to "
  "from working_node "
  "left outer join base_node on base_node.wc_id = working_node.wc_id "
  "  and base_node.local_relpath = working_node.local_relpath "
  "where working_node.wc_id = ?1 and working_node.local_relpath = ?2;",

  /* STMT_SELECT_PARENT_STUB_INFO */
  "select presence = 'not-present', revnum from base_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_DELETE_LOCK */
  "delete from lock "
  "where repos_id = ?1 and repos_relpath = ?2;",

  /* STMT_UPDATE_BASE_REPO */
  "update base_node set repos_id = ?3 "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* STMT_UPDATE_BASE_CHILDREN_REPO */
  "update base_node set repos_id = ?3 "
  "where repos_id is not null and wc_id = ?1 and parent_relpath like ?2;",

  /* STMT_UPDATE_WORKING_COPYFROM_REPO */
  "update working_node set copyfrom_repos_id = ?3 "
  "where copyfrom_repos_id is not null and wc_id = ?1 and local_relpath = ?2;",

  /* STMT_UPDATE_WORKING_CHILDREN_COPYFROM_REPO */
  "update working_node set copyfrom_repos_id = ?3 "
  "where copyfrom_repos_id is not null and wc_id = ?1 "
  "  and parent_relpath like ?2;",

  /* STMT_UPDATE_LOCK_REPOS_ID */
  "update lock set repos_id = ?3 "
  "where repos_id = ?1 and repos_relpath like ?2;",

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

  /* for temporary allocations */
  apr_pool_t *scratch_pool;

} insert_base_baton_t;


static svn_wc__db_kind_t
word_to_kind(const char *kind)
{
  /* Let's be lazy and fast */
  switch (kind[0])
    {
    case 'f':
      return svn_wc__db_kind_file;
    case 'd':
      return svn_wc__db_kind_dir;
    case 's':
      return kind[1] == 'y' ? svn_wc__db_kind_symlink : svn_wc__db_kind_subdir;
    default:
      /* Given our laziness, do not MALFUNCTION here. */
      return svn_wc__db_kind_unknown;
    }
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
    case svn_wc__db_kind_subdir:
      return "subdir";
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
  switch (presence[0])
    {
    case 'a':
      return svn_wc__db_status_absent;
    case 'e':
      return svn_wc__db_status_excluded;
    case 'i':
      return svn_wc__db_status_incomplete;
    case 'b':
      return svn_wc__db_status_base_deleted;
    default:
      if (strcmp(presence, "not-present") == 0)
        return svn_wc__db_status_not_present;
      /* Do not MALFUNCTION here if presence is not "normal". */
      return svn_wc__db_status_normal;
    }
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
    case svn_wc__db_status_base_deleted:
      return "base-delete";
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


/* Construct a new wcroot_t. The WCROOT_ABSPATH and SDB parameters must
   have lifetime of at least RESULT_POOL.  */
static svn_error_t *
create_wcroot(wcroot_t **wcroot,
              const char *wcroot_abspath,
              svn_sqlite__db_t *sdb,
              apr_int64_t wc_id,
              int format,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  *wcroot = apr_palloc(result_pool, sizeof(**wcroot));

  (*wcroot)->abspath = wcroot_abspath;
  (*wcroot)->sdb = sdb;
  (*wcroot)->wc_id = wc_id;
  (*wcroot)->format = format;

  if (sdb != NULL)
    {
      SVN_ERR(svn_sqlite__read_schema_version(&(*wcroot)->format,
                                              sdb, scratch_pool));
    }

  /* If we construct a wcroot, then we better have a format.  */
  SVN_ERR_ASSERT((*wcroot)->format >= 1);

  return SVN_NO_ERROR;
}


static svn_error_t *
get_pristine_fname(const char **path,
                   svn_wc__db_pdh_t *pdh,
                   const svn_checksum_t *checksum,
                   svn_boolean_t create_subdir,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *base_dir;
  const char *hexdigest = svn_checksum_to_cstring(checksum, scratch_pool);
#ifndef SVN__SKIP_SUBDIR
  char subdir[3] = { 0 };
#endif

  /* ### code is in transition. make sure we have the proper data.  */
  SVN_ERR_ASSERT(pdh->wcroot != NULL);

  /* ### need to fix this to use a symbol for ".svn". we don't need
     ### to use join_many since we know "/" is the separator for
     ### internal canonical paths */
  base_dir = svn_dirent_join(pdh->wcroot->abspath, ".svn/pristine",
                             scratch_pool);

  /* We should have a valid checksum and (thus) a valid digest. */
  SVN_ERR_ASSERT(hexdigest != NULL);

#ifndef SVN__SKIP_SUBDIR
  /* Get the first two characters of the digest, for the subdir. */
  subdir[0] = hexdigest[0];
  subdir[1] = hexdigest[1];

  if (create_subdir)
    {
      const char *subdir_path = svn_dirent_join(base_dir, subdir,
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
                               base_dir,
#ifndef SVN__SKIP_SUBDIR
                               subdir,
#endif
                               hexdigest,
                               NULL);
  return SVN_NO_ERROR;
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


/* Scan from LOCAL_RELPATH upwards through parent nodes until we find a parent
   that has values in the 'repos_id' and 'repos_relpath' columns.  Return
   that information in REPOS_ID and REPOS_RELPATH (either may be NULL). */
static svn_error_t *
scan_upwards_for_repos(apr_int64_t *repos_id,
                       const char **repos_relpath,
                       const wcroot_t *wcroot,
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
                svn_dirent_local_style(local_relpath, scratch_pool));
            }
          else
            {
              err = svn_error_createf(
                SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                _("The node '%s' was not found."),
                svn_dirent_local_style(local_relpath, scratch_pool));
            }

          return svn_error_compose_create(err, svn_sqlite__reset(stmt));
        }

      /* Did we find some non-NULL repository columns? */
      if (!svn_sqlite__column_is_null(stmt, 2))
        {
          /* If one is non-NULL, then so should the other. */
          SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 3));

          if (repos_id)
            *repos_id = svn_sqlite__column_int64(stmt, 2);

          /* Given the node's relpath, append all the segments that
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
            svn_dirent_local_style(local_relpath, scratch_pool));
        }

      /* Strip a path segment off the end, and append it to the suffix
         that we'll use when we finally find a base relpath.  */
      svn_dirent_split(current_relpath, &current_relpath, &current_basename,
                       scratch_pool);
      relpath_suffix = svn_dirent_join(relpath_suffix, current_basename,
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
                const char *path,
                apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const char *format_file_path;

  /* Try reading the format number from the entries file.  */
  format_file_path = svn_wc__adm_child(path, SVN_WC__ADM_ENTRIES, scratch_pool);
  err = svn_io_read_version_file(version, format_file_path, scratch_pool);
  if (err == NULL)
    return SVN_NO_ERROR;
  if (err->apr_err != SVN_ERR_BAD_VERSION_FILE_FORMAT
      && !APR_STATUS_IS_ENOENT(err->apr_err)
      && !APR_STATUS_IS_ENOTDIR(err->apr_err))
    return svn_error_createf(SVN_ERR_WC_MISSING, err, _("'%s' does not exist"),
                             svn_dirent_local_style(path, scratch_pool));
  svn_error_clear(err);

  /* This must be a really old working copy!  Fall back to reading the
     format file.
     
     Note that the format file might not exist in newer working copies
     (format 7 and higher), but in that case, the entries file should
     have contained the format number. */
  format_file_path = svn_wc__adm_child(path, SVN_WC__ADM_FORMAT, scratch_pool);
  err = svn_io_read_version_file(version, format_file_path, scratch_pool);
  if (err == NULL)
    return SVN_NO_ERROR;

  /* Whatever error may have occurred... we can just ignore. This is not
     a working copy directory. Signal the caller.  */
  svn_error_clear(err);

  *version = 0;
  return SVN_NO_ERROR;
}


static svn_wc__db_pdh_t *
get_or_create_pdh(svn_wc__db_t *db,
                  const char *local_dir_abspath,
                  svn_boolean_t create_allowed,
                  apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh = apr_hash_get(db->dir_data,
                                       local_dir_abspath, APR_HASH_KEY_STRING);

  if (pdh == NULL && create_allowed)
    {
      pdh = apr_pcalloc(db->state_pool, sizeof(*pdh));
      pdh->db = db;

      /* Copy the path for the proper lifetime.  */
      pdh->local_abspath = apr_pstrdup(db->state_pool, local_dir_abspath);

      /* We don't know anything about this directory, so we cannot construct
         a wcroot_t for it (yet).  */

      /* ### parent */

      apr_hash_set(db->dir_data, pdh->local_abspath, APR_HASH_KEY_STRING, pdh);
    }

  return pdh;
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
                          const wcroot_t *wcroot,
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
          *local_relpath = svn_dirent_join(dir_relpath,
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
      (*pdh)->db = db;
      (*pdh)->local_abspath = apr_pstrdup(db->state_pool, local_abspath);
    }
  else
    {
      /* The PDH should have been built correctly (so far).  */
      SVN_ERR_ASSERT((*pdh)->db == db);
      SVN_ERR_ASSERT(strcmp((*pdh)->local_abspath, local_abspath) == 0);
    }

  /* Assume that LOCAL_ABSPATH is a directory, and look for the SQLite
     database in the right place. If we find it... great! If not, then
     peel off some components, and try again. */

  while (TRUE)
    {
      svn_error_t *err;

      err = svn_sqlite__open(&sdb,
                             svn_wc__adm_child(local_abspath, "wc.db",
                                               scratch_pool),
                             smode, statements, SVN_WC__VERSION,
                             upgrade_sql, db->state_pool, scratch_pool);
      if (err == NULL)
        break;
      if (err->apr_err != SVN_ERR_SQLITE_ERROR
          && !APR_STATUS_IS_ENOENT(err->apr_err))
        return err;
      svn_error_clear(err);

      /* If we have not moved upwards, then check for a wc-1 working copy.
         Since wc-1 has a .svn in every directory, and we didn't find one
         in the original directory, then we don't have to bother looking
         for more.

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

      svn_sqlite__stmt_t *stmt;
      svn_boolean_t have_row;
      apr_int64_t wc_id;

      /* ### cheat. we know there is just one WORKING_COPY row, and it has a
         ### NULL value for local_abspath. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_WCROOT_NULL));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
      if (!have_row)
        return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                 _("Missing a row in WCROOT for '%s'."),
                                 svn_dirent_local_style(original_abspath,
                                                        scratch_pool));

      SVN_ERR_ASSERT(!svn_sqlite__column_is_null(stmt, 0));
      wc_id = svn_sqlite__column_int64(stmt, 0);

      SVN_ERR(svn_sqlite__reset(stmt));

      /* WCROOT.local_abspath may be NULL when the database is stored
         inside the wcroot, but we know the abspath is this directory
         (ie. where we found it).  */

      SVN_ERR(create_wcroot(&(*pdh)->wcroot,
                            apr_pstrdup(db->state_pool, local_abspath),
                            sdb, wc_id, FORMAT_FROM_SDB,
                            db->state_pool, scratch_pool));
    }
  else
    {
      /* We found a wc-1 working copy directory.  */
      SVN_ERR(create_wcroot(&(*pdh)->wcroot,
                            apr_pstrdup(db->state_pool, local_abspath),
                            NULL, UNKNOWN_WC_ID, wc_format,
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
    *local_relpath = svn_dirent_join(dir_relpath, build_relpath, result_pool);
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
          svn_error_t *err = svn_sqlite__open(&sdb,
                                              svn_wc__adm_child(parent_dir,
                                                                "wc.db",
                                                                scratch_pool),
                                              smode, statements,
                                              SVN_WC__VERSION, upgrade_sql,
                                              db->state_pool, scratch_pool);
          if (err)
            {
              if (err->apr_err != SVN_ERR_SQLITE_ERROR
                  && !APR_STATUS_IS_ENOENT(err->apr_err))
                return err;
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
                  parent_pdh->db = db;
                  parent_pdh->local_abspath = apr_pstrdup(db->state_pool,
                                                          parent_dir);
                }
              else
                {
                  /* The PDH should have been built correctly (so far).  */
                  SVN_ERR_ASSERT(parent_pdh->db == db);
                  SVN_ERR_ASSERT(strcmp(parent_pdh->local_abspath,
                                        parent_dir) == 0);
                }

              SVN_ERR(create_wcroot(&parent_pdh->wcroot,
                                    parent_pdh->local_abspath,
                                    sdb,
                                    1 /* ### hack.  */,
                                    FORMAT_FROM_SDB,
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
          parent_pdh->db = db;
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
                       enum statement_keys stmt_idx,
                       apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  SVN_ERR(svn_sqlite__get_statement(stmt, pdh->wcroot->sdb, stmt_idx));
  SVN_ERR(svn_sqlite__bindf(*stmt, "is", pdh->wcroot->wc_id, local_relpath));

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

  if ((*parent_pdh = child_pdh->parent) != NULL
      && (*parent_pdh)->wcroot != NULL)
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
  SVN_ERR(svn_sqlite__bindf(stmt, "ss", repos_uuid, repos_root_url));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      *repos_id = svn_sqlite__column_int64(stmt, 0);
      return svn_error_return(svn_sqlite__reset(stmt));
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
  return svn_error_return(svn_sqlite__insert(repos_id, stmt));
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

  /* We should not have been called, if the wc has an improper version.  */
  SVN_ERR_ASSERT(pdh->wcroot->format == SVN_WC__VERSION);

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
        svn_dirent_basename(child_relpath, result_pool);

      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  *children = child_names;

  return svn_sqlite__reset(stmt);
}


static void
flush_entries(svn_wc__db_pdh_t *pdh)
{
  if (pdh->adm_access)
    svn_wc__adm_access_set_entries(pdh->adm_access, NULL);
}


static svn_error_t *
close_db(svn_wc__db_t *db,
         apr_pool_t *scratch_pool)
{
  apr_hash_t *roots = apr_hash_make(scratch_pool);
  apr_hash_index_t *hi;

  /* We may have WCROOTs shared between PDHs, so put them all in a hash to
     collapse them, validating along the way. */
  for (hi = apr_hash_first(scratch_pool, db->dir_data); hi;
       hi = apr_hash_next(hi))
    {
      void *val;
      svn_wc__db_pdh_t *pdh;

      apr_hash_this(hi, NULL, NULL, &val);
      pdh = val;
      if (pdh->wcroot == NULL)
        continue;

#ifdef SVN_DEBUG
      /* If two PDH records have the same wcroot_abspath, then they should
         be using the same WCROOT handle.  */
      {
        wcroot_t *existing_wcroot = apr_hash_get(roots,
                                                 pdh->wcroot->abspath,
                                                 APR_HASH_KEY_STRING);
        if (existing_wcroot)
          SVN_ERR_ASSERT(existing_wcroot == pdh->wcroot);
      }
#endif

      apr_hash_set(roots, pdh->wcroot->abspath, APR_HASH_KEY_STRING,
                   pdh->wcroot);
    }

  /* ### it would also be nice to assert that two different wcroot_abspath
     ### values are not sharing the same SDB. If they *do*, then we will
     ### double-close below. That won't cause problems, but it does
     ### represent an internal consistency error.  */

  /* Now close all of the non-duplicate databases. */
  for (hi = apr_hash_first(scratch_pool, roots); hi; hi = apr_hash_next(hi))
    {
      void *val;
      wcroot_t *wcroot;

      apr_hash_this(hi, NULL, NULL, &val);
      wcroot = val;

      if (wcroot->sdb != NULL)
        {
          SVN_ERR(svn_sqlite__close(wcroot->sdb));
          wcroot->sdb = NULL;
          wcroot->wc_id = UNKNOWN_WC_ID;
       }
    }

  return SVN_NO_ERROR;
}


static apr_status_t
close_db_apr(void *data)
{
  svn_wc__db_t *db = data;
  svn_error_t *err;

  err = close_db(db, db->state_pool);
  if (err)
    {
      apr_status_t result = err->apr_err;
      svn_error_clear(err);
      return result;
    }

  return APR_SUCCESS;
}


svn_error_t *
svn_wc__db_open(svn_wc__db_t **db,
                svn_wc__db_openmode_t mode,
                svn_config_t *config,
                apr_pool_t *result_pool,
                apr_pool_t *scratch_pool)
{
  *db = apr_pcalloc(result_pool, sizeof(**db));
  (*db)->mode = mode;
  (*db)->config = config;
  (*db)->dir_data = apr_hash_make(result_pool);
  (*db)->state_pool = result_pool;

  apr_pool_cleanup_register((*db)->state_pool, *db, close_db_apr,
                            apr_pool_cleanup_null);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_close(svn_wc__db_t *db,
                 apr_pool_t *scratch_pool)
{
  apr_status_t result = apr_pool_cleanup_run(db->state_pool, db, close_db_apr);

  if (result == APR_SUCCESS)
    return SVN_NO_ERROR;

  return svn_error_wrap_apr(result, NULL);
}


svn_error_t *
svn_wc__db_init(const char *local_abspath,
                const char *repos_relpath,
                const char *repos_root_url,
                const char *repos_uuid,
                svn_revnum_t initial_rev,
                svn_depth_t depth,
                apr_pool_t *scratch_pool)
{
  svn_sqlite__db_t *sdb;
  svn_sqlite__stmt_t *stmt;
  apr_int64_t repos_id;
  apr_int64_t wc_id;
  insert_base_baton_t ibb;

  SVN_ERR(svn_sqlite__open(&sdb,
                           svn_wc__adm_child(local_abspath, "wc.db",
                                             scratch_pool),
                           svn_sqlite__mode_rwcreate, statements,
                           SVN_WC__VERSION,
                           upgrade_sql, scratch_pool, scratch_pool));

  /* Insert the repository. */
  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid, sdb,
                          scratch_pool));

  /* Insert the wcroot. */
  /* ### Right now, this just assumes wc metadata is being stored locally. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_INSERT_WCROOT));
  SVN_ERR(svn_sqlite__insert(&wc_id, stmt));

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

  ibb.props = NULL;
  ibb.changed_rev = SVN_INVALID_REVNUM;
  ibb.changed_date = 0;
  ibb.changed_author = NULL;

  ibb.children = NULL;
  ibb.depth = depth;
  
  ibb.scratch_pool = scratch_pool;

  SVN_ERR(insert_base_node(&ibb, sdb));

  return svn_sqlite__close(sdb);
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
  SVN_ERR_ASSERT(children != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          pdh->wcroot->sdb, scratch_pool));

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

  ibb.scratch_pool = scratch_pool;

  /* Insert the directory and all its children transactionally.

     Note: old children can stick around, even if they are no longer present
     in this directory's revision.  */
  return svn_sqlite__with_transaction(pdh->wcroot->sdb,
                                      insert_base_node, &ibb);
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
  SVN_ERR_ASSERT(checksum != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          pdh->wcroot->sdb, scratch_pool));

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

  ibb.scratch_pool = scratch_pool;

  /* ### hmm. if this used to be a directory, we should remove children.
     ### or maybe let caller deal with that, if there is a possibility
     ### of a node kind change (rather than eat an extra lookup here).  */

  return insert_base_node(&ibb, pdh->wcroot->sdb);
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
  SVN_ERR_ASSERT(target != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          pdh->wcroot->sdb, scratch_pool));

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

  ibb.scratch_pool = scratch_pool;

  /* ### hmm. if this used to be a directory, we should remove children.
     ### or maybe let caller deal with that, if there is a possibility
     ### of a node kind change (rather than eat an extra lookup here).  */

  return insert_base_node(&ibb, pdh->wcroot->sdb);
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

  SVN_ERR(create_repos_id(&repos_id, repos_root_url, repos_uuid,
                          pdh->wcroot->sdb, scratch_pool));

  ibb.status = status;
  ibb.kind = kind;
  ibb.wc_id = pdh->wcroot->wc_id;
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

  return insert_base_node(&ibb, pdh->wcroot->sdb);
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

  ibb.scratch_pool = scratch_pool;

  return insert_base_node(&ibb, pdh->wcroot->sdb);
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
                         apr_time_t *last_mod_time,
                         svn_depth_t *depth,
                         svn_checksum_t **checksum,
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

  SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                    lock ? STMT_SELECT_BASE_NODE_WITH_LOCK
                                         : STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", pdh->wcroot->wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  if (have_row)
    {
      const char *kind_str = svn_sqlite__column_text(stmt, 5, NULL);
      svn_wc__db_kind_t node_kind;

      SVN_ERR_ASSERT(kind_str != NULL);
      node_kind = word_to_kind(kind_str);

      if (kind)
        {
          if (node_kind == svn_wc__db_kind_subdir)
            *kind = svn_wc__db_kind_dir;
          else
            *kind = node_kind;
        }
      if (status)
        {
          const char *presence = svn_sqlite__column_text(stmt, 4, NULL);

          SVN_ERR_ASSERT(presence != NULL);
          *status = word_to_presence(presence);

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
          *revision = svn_sqlite__column_revnum(stmt, 6);
        }
      if (repos_relpath)
        {
          *repos_relpath = svn_sqlite__column_text(stmt, 3, result_pool);
        }
      if (lock)
        {
          if (svn_sqlite__column_is_null(stmt, 15))
            {
              *lock = NULL;
            }
          else
            {
              *lock = apr_pcalloc(result_pool, sizeof(svn_wc__db_lock_t));
              (*lock)->token = svn_sqlite__column_text(stmt, 15, result_pool);
              if (!svn_sqlite__column_is_null(stmt, 16))
                (*lock)->owner = svn_sqlite__column_text(stmt, 16,
                                                         result_pool);
              if (!svn_sqlite__column_is_null(stmt, 17))
                (*lock)->comment = svn_sqlite__column_text(stmt, 17,
                                                           result_pool);
              if (!svn_sqlite__column_is_null(stmt, 18))
                (*lock)->date = svn_sqlite__column_int64(stmt, 18);
            }
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
            {
              err = fetch_repos_info(repos_root_url, repos_uuid,
                                     pdh->wcroot->sdb,
                                     svn_sqlite__column_int64(stmt, 2),
                                     result_pool);
            }
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
      if (last_mod_time)
        {
          *last_mod_time = svn_sqlite__column_int64(stmt, 14);
        }
      if (depth)
        {
          if (node_kind != svn_wc__db_kind_dir)
            {
              *depth = svn_depth_unknown;
            }
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
            {
              *checksum = NULL;
            }
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

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_SELECT_BASE_PROPS, scratch_pool));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  SVN_ERR(svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                        scratch_pool));
  return svn_sqlite__reset(stmt);
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
    {
      SVN_ERR(svn_sqlite__reset(stmt));
      return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                               _("The node '%s' was not found."),
                               svn_dirent_local_style(local_abspath,
                                                      scratch_pool));
    }

  SVN_ERR(svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                        scratch_pool));
  return svn_error_return(svn_sqlite__reset(stmt));
}


svn_error_t *
svn_wc__db_pristine_get_handle(svn_wc__db_pdh_t **pdh,
                               svn_wc__db_t *db,
                               const char *local_dir_abspath,
                               apr_pool_t *result_pool,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  *pdh = get_or_create_pdh(db, local_dir_abspath, TRUE, scratch_pool);

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
                            apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_add_file(svn_wc__db_t *db,
                       const char *local_abspath,
                       apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_add_symlink(svn_wc__db_t *db,
                          const char *local_abspath,
                          const char *target,
                          apr_pool_t *scratch_pool)
{
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(target != NULL);

  NOT_IMPLEMENTED();
}


svn_error_t *
svn_wc__db_op_set_props(svn_wc__db_t *db,
                        const char *local_abspath,
                        const apr_hash_t *props,
                        apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_UPDATE_ACTUAL_PROPS, scratch_pool));
  SVN_ERR(svn_sqlite__bind_properties(stmt, 3, props, scratch_pool));

  return svn_sqlite__step_done(stmt);
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
svn_wc__db_op_set_changelist(svn_wc__db_t *db,
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
                     apr_time_t *last_mod_time,
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

  /* We should not have been called, if the wc has an improper version.  */
  SVN_ERR_ASSERT(pdh->wcroot->format == SVN_WC__VERSION);

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

              presence_str = svn_sqlite__column_text(stmt_work, 0, NULL);
              work_status = word_to_presence(presence_str);
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
            err = fetch_repos_info(repos_root_url, repos_uuid,
                                   pdh->wcroot->sdb,
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
      if (last_mod_time)
        {
          if (have_work)
            *last_mod_time = svn_sqlite__column_int64(stmt_work, 14);
          else
            *last_mod_time = svn_sqlite__column_int64(stmt_base, 14);
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
            {
              *checksum = NULL;
            }
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
          err = fetch_repos_info(original_root_url, original_uuid,
                                 pdh->wcroot->sdb,
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
      if (lock)
        {
          if (svn_sqlite__column_is_null(stmt_base, 15))
            *lock = NULL;
          else
            {
              *lock = apr_pcalloc(result_pool, sizeof(svn_wc__db_lock_t));
              (*lock)->token = svn_sqlite__column_text(stmt_base, 15,
                                                       result_pool);
              if (!svn_sqlite__column_is_null(stmt_base, 16))
                (*lock)->owner = svn_sqlite__column_text(stmt_base, 16,
                                                         result_pool);
              if (!svn_sqlite__column_is_null(stmt_base, 17))
                (*lock)->comment = svn_sqlite__column_text(stmt_base, 17,
                                                           result_pool);
              if (!svn_sqlite__column_is_null(stmt_base, 18))
                (*lock)->date = svn_sqlite__column_int64(stmt_base, 18);
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

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_SELECT_ALL_PROPS, scratch_pool));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  /* Try ACTUAL, then WORKING and finally BASE. */
  if (!svn_sqlite__column_is_null(stmt, 0))
    SVN_ERR(svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                          scratch_pool));
  else if (!svn_sqlite__column_is_null(stmt, 1))
    SVN_ERR(svn_sqlite__column_properties(props, stmt, 1, result_pool,
                                          scratch_pool));
  else
    SVN_ERR(svn_sqlite__column_properties(props, stmt, 2, result_pool,
                                          scratch_pool));

  return svn_sqlite__reset(stmt);
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

  SVN_ERR(get_statement_for_path(&stmt, db, local_abspath,
                                 STMT_SELECT_PRISTINE_PROPS, scratch_pool));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                             _("The node '%s' was not found."),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  /* Try WORKING, then BASE. */
  if (!svn_sqlite__column_is_null(stmt, 0))
    SVN_ERR(svn_sqlite__column_properties(props, stmt, 0, result_pool,
                                          scratch_pool));
  else
    SVN_ERR(svn_sqlite__column_properties(props, stmt, 1, result_pool,
                                          scratch_pool));

  return svn_sqlite__reset(stmt);
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
  const char *repos_root_url;
  const char *repos_uuid;

  apr_pool_t *scratch_pool;
};


static svn_error_t *
relocate_txn(void *baton, svn_sqlite__db_t *sdb)
{
  struct relocate_baton *rb = baton;
  apr_pool_t *scratch_pool = rb->scratch_pool;
  const char *repos_relpath;
  svn_sqlite__stmt_t *stmt;
  apr_int64_t old_repos_id;
  apr_int64_t new_repos_id;
  svn_boolean_t have_base_node;

  SVN_ERR(create_repos_id(&new_repos_id, rb->repos_root_url, rb->repos_uuid,
                          sdb, scratch_pool));

  /* This function affects all the children of the given local_relpath,
     but the way that it does this is through the repos inheritance mechanism.
     So, we only need to rewrite the repos_id of the given local_relpath,
     as well as any children with a non-null repos_id, as well as various
     repos_id fields in the locks and working_node tables.
   */

  /* Get the existing repos_id of the base node, since we'll need it to
     update a potential lock. */
  /* ### is it faster to fetch fewer columns? */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", rb->wc_id, rb->local_relpath));
  SVN_ERR(svn_sqlite__step(&have_base_node, stmt));
  if (have_base_node)
    {
      old_repos_id = svn_sqlite__column_int64(stmt, 2);
      repos_relpath = svn_sqlite__column_text(stmt, 3, scratch_pool);
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  if (have_base_node)
    {
      /* Update the BASE_NODE.repos_id. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_UPDATE_BASE_REPO));
      SVN_ERR(svn_sqlite__bindf(stmt, "isi", rb->wc_id, rb->local_relpath,
                                new_repos_id));

      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  /* Update a non-NULL WORKING_NODE.copyfrom_repos_id. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                    STMT_UPDATE_WORKING_COPYFROM_REPO));
  SVN_ERR(svn_sqlite__bindf(stmt, "isi", rb->wc_id, rb->local_relpath,
                            new_repos_id));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Update and child working nodes. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                               STMT_UPDATE_WORKING_CHILDREN_COPYFROM_REPO));
  SVN_ERR(svn_sqlite__bindf(stmt, "isi", rb->wc_id,
                            apr_psprintf(scratch_pool, "%s%%",
                                         rb->local_relpath),
                            new_repos_id));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Do a bunch of stuff which is conditional on us actually having a
     base_node in the first place. */
  if (have_base_node)
    {
      /* Update any children which have non-NULL repos_id's */
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_UPDATE_BASE_CHILDREN_REPO));
      SVN_ERR(svn_sqlite__bindf(stmt, "isi", rb->wc_id, rb->local_relpath,
                                new_repos_id));
      SVN_ERR(svn_sqlite__step_done(stmt));

      /* Update any locks for the root or its children. */
      SVN_ERR(svn_sqlite__get_statement(&stmt, sdb,
                                        STMT_UPDATE_LOCK_REPOS_ID));
      SVN_ERR(svn_sqlite__bindf(stmt, "isi",
                                old_repos_id,
                                apr_psprintf(scratch_pool, "%s%%",
                                             repos_relpath),
                                new_repos_id));
      SVN_ERR(svn_sqlite__step_done(stmt));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__db_global_relocate(svn_wc__db_t *db,
                           const char *local_dir_abspath,
                           const char *repos_root_url,
                           const char *repos_uuid,
                           apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  struct relocate_baton rb;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_dir_abspath));
  /* ### assert that we were passed a directory?  */

  SVN_ERR(parse_local_abspath(&pdh, &rb.local_relpath, db, local_dir_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

  rb.wc_id = pdh->wcroot->wc_id;
  rb.repos_root_url = repos_root_url;
  rb.repos_uuid = repos_uuid;
  rb.scratch_pool = scratch_pool;

  return svn_error_return(svn_sqlite__with_transaction(pdh->wcroot->sdb,
                                                       relocate_txn, &rb));
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

  SVN_ERR(scan_upwards_for_repos(&repos_id, &repos_relpath,
                                 pdh->wcroot, local_relpath,
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

  SVN_ERR(scan_upwards_for_repos(&repos_id, &repos_relpath,
                                 pdh->wcroot, local_relpath,
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

  SVN_ERR(scan_upwards_for_repos(&repos_id, repos_relpath,
                                 pdh->wcroot, local_relpath,
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

  while (TRUE)
    {
      svn_sqlite__stmt_t *stmt;
      svn_boolean_t have_row;
      svn_boolean_t presence_is_normal;

      /* ### is it faster to fetch fewer columns? */
      SVN_ERR(svn_sqlite__get_statement(&stmt, pdh->wcroot->sdb,
                                        STMT_SELECT_WORKING_NODE));
      SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                pdh->wcroot->wc_id, current_relpath));
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

          /* This node was added/copied/moved and has an implicit location
             in the repository. We now need to traverse BASE nodes looking
             for repository info.  */
          break;
        }

      presence_is_normal = strcmp("normal",
                                  svn_sqlite__column_text(stmt, 0, NULL)) == 0;

      /* Record information from the starting node.  */
      if (current_abspath == local_abspath)
        {
          svn_wc__db_status_t presence
            = word_to_presence(svn_sqlite__column_text(stmt, 0, NULL));

          /* The starting node should exist normally.  */
          if (presence != svn_wc__db_status_normal)
            {
              svn_error_clear(svn_sqlite__reset(stmt));
              return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
                                       _("Expected node '%s' to be added."),
                                       svn_dirent_local_style(local_abspath,
                                                              scratch_pool));
            }

          /* Provide the default status; we'll override as appropriate. */
          if (status)
            *status = svn_wc__db_status_added;
        }

      /* We want the operation closest to the start node, and then we
         ignore any operations on its ancestors.  */
      if (!found_info
          && presence_is_normal
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
      if (strcmp(current_abspath, pdh->local_abspath) == 0)
        {
          /* The current node is a directory, so move to the parent dir.  */
          SVN_ERR(navigate_to_parent(&pdh, pdh, svn_sqlite__mode_readonly,
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
        *repos_relpath = svn_dirent_join(base_relpath, build_relpath,
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
  svn_wc__db_pdh_t *pdh;

  SVN_ERR_ASSERT(base_del_abspath != NULL);
  SVN_ERR_ASSERT(base_replaced != NULL);
  SVN_ERR_ASSERT(moved_to_abspath != NULL);
  SVN_ERR_ASSERT(work_del_abspath != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* Initialize all the OUT parameters.  */
  *base_del_abspath = NULL;
  *base_replaced = FALSE;  /* becomes TRUE when we know for sure.  */
  *moved_to_abspath = NULL;
  *work_del_abspath = NULL;

  /* Initialize to something that won't denote an important parent/child
     transition.  */
  child_presence = svn_wc__db_status_base_deleted;

  SVN_ERR(parse_local_abspath(&pdh, &current_relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

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
            {
              svn_error_clear(svn_sqlite__reset(stmt));

              return svn_error_createf(SVN_ERR_WC_PATH_NOT_FOUND, NULL,
                                       _("The node '%s' was not found."),
                                       svn_dirent_local_style(local_abspath,
                                                              scratch_pool));
            }

          /* There are no values, so go ahead and reset the stmt now.  */
          SVN_ERR(svn_sqlite__reset(stmt));

          /* No row means no WORKING node at this path, which means we just
             fell off the top of the WORKING tree.

             The child cannot be not-present, as that would imply the
             root of the (added) WORKING subtree was deleted.  */
          SVN_ERR_ASSERT(child_presence != svn_wc__db_status_not_present);

          /* If the child did not have a BASE node associated with it, then
             we're looking at a deletion that occurred within an added tree.
             There is no root of a deleted/replaced BASE tree.

             If the child was base-deleted, then the whole tree is a
             simple (explicit) deletion of the BASE tree.

             If the child was normal, then it is the root of a replacement,
             which means an (implicit) deletion of the BASE tree.

             In both cases, set the root of the operation (if we have not
             already set it as part of a moved-away).  */
          if (child_has_base && *base_del_abspath == NULL)
            *base_del_abspath = apr_pstrdup(result_pool, child_abspath);

          /* We found whatever roots we needed. This BASE node and its
             ancestors are unchanged, so we're done.  */
          break;
        }

      /* We need the presence of the WORKING node. Note that legal values
         are: normal, not-present, base-deleted.  */
      work_presence = word_to_presence(svn_sqlite__column_text(stmt, 1, NULL));

      /* The starting node should be deleted.  */
      if (current_abspath == local_abspath
          && work_presence != svn_wc__db_status_not_present
          && work_presence != svn_wc__db_status_base_deleted)
        return svn_error_createf(SVN_ERR_WC_PATH_UNEXPECTED_STATUS, NULL,
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
            = word_to_presence(svn_sqlite__column_text(stmt, 0, NULL));

          /* Only "normal" and "not-present" are allowed.  */
          SVN_ERR_ASSERT(base_presence == svn_wc__db_status_normal
                         || base_presence == svn_wc__db_status_not_present);

          /* If a BASE node is marked as not-present, then we'll ignore
             it within this function. That status is simply a bookkeeping
             gimmick, not a real node that may have been deleted.  */

          /* If we're looking at a present BASE node, *and* there is a
             WORKING node (present or deleted), then a replacement has
             occurred here or in an ancestor.  */
          if (base_presence == svn_wc__db_status_normal
              && work_presence != svn_wc__db_status_base_deleted)
            {
              *base_replaced = TRUE;
            }
        }

      /* Only grab the nearest ancestor.  */
      if (*moved_to_abspath == NULL
          && !svn_sqlite__column_is_null(stmt, 2 /* moved_to */))
        {
          /* There better be a BASE_NODE (that was moved-away).  */
          SVN_ERR_ASSERT(have_base);

          /* This makes things easy. It's the BASE_DEL_ABSPATH!  */
          *base_del_abspath = apr_pstrdup(result_pool, current_abspath);
          *moved_to_abspath = svn_dirent_join(
                                pdh->wcroot->abspath,
                                svn_sqlite__column_text(stmt, 2, NULL),
                                result_pool);
        }

      if (work_presence == svn_wc__db_status_normal
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
          SVN_ERR(navigate_to_parent(&pdh, pdh, svn_sqlite__mode_readonly,
                                     scratch_pool));
        }
      current_abspath = pdh->local_abspath;
      current_relpath = compute_pdh_relpath(pdh, NULL);
    }

  return SVN_NO_ERROR;
}


/* In the WCROOT associated with DB and LOCAL_ABSPATH, add WORK_ITEM to the
   wcroot's work queue. Use SCRATCH_POOL for all temporary allocations.  */
svn_error_t *
svn_wc__db_wq_add(svn_wc__db_t *db,
                  const char *local_abspath,
                  const svn_skel_t *work_item,
                  apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(work_item != NULL);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  NOT_IMPLEMENTED();
}


/* In the WCROOT associated with DB and LOCAL_ABSPATH, fetch a work item that
   needs to be completed. Its identifier is returned in ID, and the data in
   WORK_ITEM.

   There is no particular ordering to the work items returned by this function.

   If there are no work items to be completed, then ID will be set to zero,
   and WORK_ITEM to NULL.

   RESULT_POOL will be used to allocate WORK_ITEM, and SCRATCH_POOL
   will be used for all temporary allocations.  */
svn_error_t *
svn_wc__db_wq_fetch(apr_uint64_t *id,
                    svn_skel_t **work_item,
                    svn_wc__db_t *db,
                    const char *local_abspath,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR_ASSERT(id != NULL);
  SVN_ERR_ASSERT(work_item != NULL);
  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readonly,
                              scratch_pool, scratch_pool));

  NOT_IMPLEMENTED();
}


/* In the WCROOT associated with DB and LOCAL_ABSPATH, mark work item ID as
   completed. If an error occurs, then it is unknown whether the work item
   has been marked as completed.

   Uses SCRATCH_POOL for all temporary allocations.  */
svn_error_t *
svn_wc__db_wq_completed(svn_wc__db_t *db,
                        const char *local_abspath,
                        apr_uint64_t id,
                        apr_pool_t *scratch_pool)
{
  svn_wc__db_pdh_t *pdh;
  const char *local_relpath;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));
  SVN_ERR_ASSERT(id != 0);

  SVN_ERR(parse_local_abspath(&pdh, &local_relpath, db, local_abspath,
                              svn_sqlite__mode_readwrite,
                              scratch_pool, scratch_pool));

  NOT_IMPLEMENTED();
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

  pdh = get_or_create_pdh(db, local_dir_abspath, FALSE, scratch_pool);

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

      /* If we hit an error examining this directory, then declare this
         directory to not be a working copy.  */
      /* ### for per-dir layouts, the wcroot should be this directory,
         ### so bail if the PDH is a parent (and, thus, local_relpath is
         ### something besides "").  */
      if (err || *local_relpath != '\0')
        {
          if (err && err->apr_err != SVN_ERR_WC_NOT_WORKING_COPY)
            return err;
          svn_error_clear(err);

          /* We might turn this directory into a wcroot later, so let's
             just forget what we (didn't) find. The wcroot is still
             hanging off a parent though.  */
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
  pdh = get_or_create_pdh(db, local_dir_abspath, FALSE, scratch_pool);
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
  pdh = get_or_create_pdh(db, local_dir_abspath, FALSE, scratch_pool);

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

  pdh = get_or_create_pdh(db, local_dir_abspath, TRUE, scratch_pool);

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
  pdh = get_or_create_pdh(db, local_dir_abspath, FALSE, scratch_pool);
  if (pdh != NULL)
    {
      /* We should be closing the correct one, *or* it's already closed.  */
      SVN_ERR_ASSERT_NO_RETURN(pdh->adm_access == adm_access
                               || pdh->adm_access == NULL);
      if (pdh->wcroot)
        {
          /* This assumes a per-dir database. */
          if (pdh->wcroot->sdb)
            {
              SVN_ERR(svn_sqlite__close(pdh->wcroot->sdb));
              pdh->wcroot->sdb = NULL;
            }
          pdh->wcroot = NULL;
        }

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
  pdh = get_or_create_pdh(db, local_dir_abspath, FALSE, scratch_pool);
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
      const void *key;
      void *val;
      const svn_wc__db_pdh_t *pdh;

      apr_hash_this(hi, &key, NULL, &val);
      pdh = val;

      if (pdh->adm_access != NULL)
        apr_hash_set(result, key, APR_HASH_KEY_STRING, pdh->adm_access);
    }

  return result;
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
svn_wc__db_check_node(svn_wc__db_kind_t *kind,
                      svn_wc__db_t *db,
                      const char *local_abspath,
                      apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  err = svn_wc__db_read_info(NULL, kind, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                             NULL, NULL,
                             db, local_abspath, scratch_pool, scratch_pool);

  if (!err)
    return SVN_NO_ERROR;

  if (err->apr_err == SVN_ERR_WC_PATH_NOT_FOUND)
    {
      svn_error_clear(err);
      *kind = svn_wc__db_kind_unknown;
      return SVN_NO_ERROR;
    }

  return svn_error_return(err);
}


svn_error_t *
svn_wc__context_create_with_db(svn_wc_context_t **wc_ctx,
                               svn_config_t *config,
                               svn_wc__db_t *db,
                               apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc_context_create(wc_ctx, config, db->state_pool, scratch_pool));
  SVN_ERR(svn_wc__db_close((*wc_ctx)->db, scratch_pool));

  (*wc_ctx)->db = db;

  return SVN_NO_ERROR;
}
