/*
 * entries.c :  manipulating the administrative `entries' file.
 *
 * ====================================================================
 * Copyright (c) 2000-2009 CollabNet.  All rights reserved.
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

#include <string.h>
#include <assert.h>

#include <apr_strings.h>

#include "svn_error.h"
#include "svn_types.h"
#include "svn_time.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_ctype.h"
#include "svn_string.h"

#include "wc.h"
#include "adm_files.h"
#include "adm_ops.h"
#include "entries.h"
#include "lock.h"
#include "tree_conflicts.h"
#include "wc_db.h"

#include "svn_private_config.h"
#include "private/svn_wc_private.h"
#include "private/svn_sqlite.h"
#include "private/svn_skel.h"

#include "wc-metadata.h"
#include "wc-checks.h"

#define MAYBE_ALLOC(x,p) ((x) ? (x) : apr_pcalloc((p), sizeof(*(x))))

static const char * const upgrade_sql[] = {
  NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL,
  NULL, NULL,
  WC_METADATA_SQL
};

/* This values map to the members of STATEMENTS below, and should be added
   and removed at the same time. */
enum statement_keys {
  STMT_INSERT_REPOSITORY,
  STMT_INSERT_BASE_NODE,
  STMT_INSERT_WORKING_NODE,
  STMT_INSERT_ACTUAL_NODE,
  STMT_SELECT_REPOSITORY,
  STMT_SELECT_WCROOT_NULL,
  STMT_SELECT_ACTUAL_NODE,
  STMT_DELETE_ALL_WORKING,
  STMT_DELETE_ALL_BASE,
  STMT_DELETE_ALL_ACTUAL,
  STMT_DELETE_ALL_LOCK,
  STMT_SELECT_INCOMPLETE_FLAG,
  STMT_SELECT_KEEP_LOCAL_FLAG,
  STMT_SELECT_NOT_PRESENT,
  STMT_SELECT_FILE_EXTERNAL,
  STMT_UPDATE_FILE_EXTERNAL
};

static const char * const statements[] = {
  "insert into repository (root, uuid) "
  "values (?1, ?2);",

  "insert or replace into base_node "
    "(wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, "
     "presence, "
     "revnum, kind, checksum, translated_size, changed_rev, changed_date, "
     "changed_author, depth, last_mod_time, properties, incomplete_children)"
  "values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, "
          "?15, ?16, ?17);",

  "insert or replace into working_node "
    "(wc_id, local_relpath, parent_relpath, presence, kind, "
     "copyfrom_repos_id, "
     "copyfrom_repos_path, copyfrom_revnum, moved_here, moved_to, checksum, "
     "translated_size, changed_rev, changed_date, changed_author, depth, "
     "last_mod_time, properties, keep_local) "
  "values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, "
          "?15, ?16, ?17, ?18, ?19);",

  "insert or replace into actual_node "
    "(wc_id, local_relpath, parent_relpath, properties, conflict_old, "
     "conflict_new, "
     "conflict_working, prop_reject, changelist, text_mod, "
     "tree_conflict_data) "
  "values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);",

  "select id, root from repository where root = ?1;",

  "select id from wcroot where local_abspath is null;",

  "select wc_id, local_relpath, parent_relpath, properties, conflict_old, "
     "conflict_new, "
     "conflict_working, prop_reject, changelist, text_mod, "
     "tree_conflict_data "
  "from actual_node;",

  "delete from working_node;",

  "delete from base_node;",

  "delete from actual_node;",

  "delete from lock;",

  "select incomplete_children from base_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  "select keep_local from working_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  "select 1 from base_node "
  "where wc_id = ?1 and local_relpath = ?2 and presence = 'not-present';",

  "select file_external from base_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  "update base_node set file_external = ?3 "
  "where wc_id = ?1 and local_relpath = ?2;",

  NULL
  };

/* Temporary structures which mirror the tables in wc-metadata.sql.
   For detailed descriptions of each field, see that file. */
typedef struct {
  apr_int64_t wc_id;
  const char *local_relpath;
  apr_int64_t repos_id;
  const char *repos_relpath;
  const char *parent_relpath;
  svn_wc__db_status_t presence;
  svn_revnum_t revision;
  svn_node_kind_t kind;  /* ### should switch to svn_wc__db_kind_t */
  svn_checksum_t *checksum;
  svn_filesize_t translated_size;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  svn_depth_t depth;
  apr_time_t last_mod_time;
  apr_hash_t *properties;
  svn_boolean_t incomplete_children;
} db_base_node_t;

typedef struct {
  apr_int64_t wc_id;
  const char *local_relpath;
  const char *parent_relpath;
  svn_wc__db_status_t presence;
  svn_node_kind_t kind;  /* ### should switch to svn_wc__db_kind_t */
  apr_int64_t copyfrom_repos_id;
  const char *copyfrom_repos_path;
  svn_revnum_t copyfrom_revnum;
  svn_boolean_t moved_here;
  const char *moved_to;
  svn_checksum_t *checksum;
  svn_filesize_t translated_size;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  svn_depth_t depth;
  apr_time_t last_mod_time;
  apr_hash_t *properties;
  svn_boolean_t keep_local;
} db_working_node_t;

typedef struct {
  apr_int64_t wc_id;
  const char *local_relpath;
  const char *parent_relpath;
  apr_hash_t *properties;
  const char *conflict_old;
  const char *conflict_new;
  const char *conflict_working;
  const char *prop_reject;
  const char *changelist;
  /* ### enum for text_mod */
  const char *tree_conflict_data;
} db_actual_node_t;





/* Return the location of the sqlite database containing the entry information
   for PATH in the filesystem.  Allocate in RESULT_POOL. ***/
static const char *
db_path(const char *path,
        apr_pool_t *result_pool)
{
  return svn_wc__adm_child(path, "wc.db", result_pool);
}

static svn_boolean_t
should_create_next_gen(void)
{
  /* ### this is temporary. developers will use this while wc-ng is being
     ### developed. in the future, we will *always* create a next gen wc. */
  return getenv("SVN_ENABLE_NG") != NULL;
}



/*** reading and writing the entries file ***/


static svn_wc_entry_t *
alloc_entry(apr_pool_t *pool)
{
  svn_wc_entry_t *entry = apr_pcalloc(pool, sizeof(*entry));
  entry->revision = SVN_INVALID_REVNUM;
  entry->copyfrom_rev = SVN_INVALID_REVNUM;
  entry->cmt_rev = SVN_INVALID_REVNUM;
  entry->kind = svn_node_none;
  entry->working_size = SVN_WC_ENTRY_WORKING_SIZE_UNKNOWN;
  entry->depth = svn_depth_infinity;
  entry->file_external_path = NULL;
  entry->file_external_peg_rev.kind = svn_opt_revision_unspecified;
  entry->file_external_rev.kind = svn_opt_revision_unspecified;
  return entry;
}


/* If attribute ATTR_NAME appears in hash ATTS, set *ENTRY_FLAG to its
 * boolean value and add MODIFY_FLAG into *MODIFY_FLAGS, else set *ENTRY_FLAG
 * false.  ENTRY_NAME is the name of the WC-entry. */
static svn_error_t *
do_bool_attr(svn_boolean_t *entry_flag,
             apr_uint64_t *modify_flags, apr_uint64_t modify_flag,
             apr_hash_t *atts, const char *attr_name,
             const char *entry_name)
{
  const char *str = apr_hash_get(atts, attr_name, APR_HASH_KEY_STRING);

  *entry_flag = FALSE;
  if (str)
    {
      if (strcmp(str, "true") == 0)
        *entry_flag = TRUE;
      else if (strcmp(str, "false") == 0 || strcmp(str, "") == 0)
        *entry_flag = FALSE;
      else
        return svn_error_createf
          (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
           _("Entry '%s' has invalid '%s' value"),
           (entry_name ? entry_name : SVN_WC_ENTRY_THIS_DIR), attr_name);

      *modify_flags |= modify_flag;
    }
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__atts_to_entry(svn_wc_entry_t **new_entry,
                      apr_uint64_t *modify_flags,
                      apr_hash_t *atts,
                      apr_pool_t *pool)
{
  svn_wc_entry_t *entry = alloc_entry(pool);
  const char *name;

  *modify_flags = 0;

  /* Find the name and set up the entry under that name. */
  name = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_NAME, APR_HASH_KEY_STRING);
  entry->name = name ? apr_pstrdup(pool, name) : SVN_WC_ENTRY_THIS_DIR;

  /* Attempt to set revision (resolve_to_defaults may do it later, too) */
  {
    const char *revision_str
      = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_REVISION, APR_HASH_KEY_STRING);

    if (revision_str)
      {
        entry->revision = SVN_STR_TO_REV(revision_str);
        *modify_flags |= SVN_WC__ENTRY_MODIFY_REVISION;
      }
    else
      entry->revision = SVN_INVALID_REVNUM;
  }

  /* Attempt to set up url path (again, see resolve_to_defaults). */
  {
    entry->url
      = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_URL, APR_HASH_KEY_STRING);

    if (entry->url)
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_URL;
        entry->url = apr_pstrdup(pool, entry->url);
      }
  }

  /* Set up repository root.  Make sure it is a prefix of url. */
  {
    entry->repos = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_REPOS,
                                APR_HASH_KEY_STRING);
    if (entry->repos)
      {
        if (entry->url && ! svn_path_is_ancestor(entry->repos, entry->url))
          return svn_error_createf(SVN_ERR_WC_CORRUPT, NULL,
                                   _("Entry for '%s' has invalid repository "
                                     "root"),
                                   name ? name : SVN_WC_ENTRY_THIS_DIR);
        *modify_flags |= SVN_WC__ENTRY_MODIFY_REPOS;
        entry->repos = apr_pstrdup(pool, entry->repos);
      }
  }

  /* Set up kind. */
  {
    const char *kindstr
      = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_KIND, APR_HASH_KEY_STRING);

    entry->kind = svn_node_none;
    if (kindstr)
      {
        if (strcmp(kindstr, SVN_WC__ENTRIES_ATTR_FILE_STR) == 0)
          entry->kind = svn_node_file;
        else if (strcmp(kindstr, SVN_WC__ENTRIES_ATTR_DIR_STR) == 0)
          entry->kind = svn_node_dir;
        else
          return svn_error_createf
            (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
             _("Entry '%s' has invalid node kind"),
             (name ? name : SVN_WC_ENTRY_THIS_DIR));
        *modify_flags |= SVN_WC__ENTRY_MODIFY_KIND;
      }
  }

  /* Look for a schedule attribute on this entry. */
  {
    const char *schedulestr
      = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_SCHEDULE, APR_HASH_KEY_STRING);

    entry->schedule = svn_wc_schedule_normal;
    if (schedulestr)
      {
        if (strcmp(schedulestr, SVN_WC__ENTRY_VALUE_ADD) == 0)
          entry->schedule = svn_wc_schedule_add;
        else if (strcmp(schedulestr, SVN_WC__ENTRY_VALUE_DELETE) == 0)
          entry->schedule = svn_wc_schedule_delete;
        else if (strcmp(schedulestr, SVN_WC__ENTRY_VALUE_REPLACE) == 0)
          entry->schedule = svn_wc_schedule_replace;
        else if (strcmp(schedulestr, "") == 0)
          entry->schedule = svn_wc_schedule_normal;
        else
          return svn_error_createf
            (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
             _("Entry '%s' has invalid '%s' value"),
             (name ? name : SVN_WC_ENTRY_THIS_DIR),
             SVN_WC__ENTRY_ATTR_SCHEDULE);

        *modify_flags |= SVN_WC__ENTRY_MODIFY_SCHEDULE;
      }
  }

  /* Is this entry in a state of mental torment (conflict)? */
  {
    if ((entry->prejfile
         = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_PREJFILE,
                        APR_HASH_KEY_STRING)))
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_PREJFILE;
        /* Normalize "" (used by the log runner) to NULL */
        entry->prejfile = *(entry->prejfile)
          ? apr_pstrdup(pool, entry->prejfile) : NULL;
      }

    if ((entry->conflict_old
         = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CONFLICT_OLD,
                        APR_HASH_KEY_STRING)))
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_OLD;
        /* Normalize "" (used by the log runner) to NULL */
        entry->conflict_old =
          *(entry->conflict_old)
          ? apr_pstrdup(pool, entry->conflict_old) : NULL;
      }

    if ((entry->conflict_new
         = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CONFLICT_NEW,
                        APR_HASH_KEY_STRING)))
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_NEW;
        /* Normalize "" (used by the log runner) to NULL */
        entry->conflict_new =
          *(entry->conflict_new)
          ? apr_pstrdup(pool, entry->conflict_new) : NULL;
      }

    if ((entry->conflict_wrk
         = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CONFLICT_WRK,
                        APR_HASH_KEY_STRING)))
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_WRK;
        /* Normalize "" (used by the log runner) to NULL */
        entry->conflict_wrk =
          *(entry->conflict_wrk)
          ? apr_pstrdup(pool, entry->conflict_wrk) : NULL;
      }

    if ((entry->tree_conflict_data
         = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_TREE_CONFLICT_DATA,
                        APR_HASH_KEY_STRING)))
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_TREE_CONFLICT_DATA;
        /* Normalize "" (used by the log runner) to NULL */
        entry->tree_conflict_data =
          *(entry->tree_conflict_data)
          ? apr_pstrdup(pool, entry->tree_conflict_data) : NULL;
      }
  }

  /* Is this entry copied? */
  SVN_ERR(do_bool_attr(&entry->copied,
                       modify_flags, SVN_WC__ENTRY_MODIFY_COPIED,
                       atts, SVN_WC__ENTRY_ATTR_COPIED, name));
  {
    const char *revstr;

    entry->copyfrom_url = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_COPYFROM_URL,
                                       APR_HASH_KEY_STRING);
    if (entry->copyfrom_url)
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_URL;
        entry->copyfrom_url = apr_pstrdup(pool, entry->copyfrom_url);
      }

    revstr = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_COPYFROM_REV,
                          APR_HASH_KEY_STRING);
    if (revstr)
      {
        entry->copyfrom_rev = SVN_STR_TO_REV(revstr);
        *modify_flags |= SVN_WC__ENTRY_MODIFY_COPYFROM_REV;
      }
  }

  /* Is this entry deleted? */
  SVN_ERR(do_bool_attr(&entry->deleted,
                       modify_flags, SVN_WC__ENTRY_MODIFY_DELETED,
                       atts, SVN_WC__ENTRY_ATTR_DELETED, name));

  /* Is this entry absent? */
  SVN_ERR(do_bool_attr(&entry->absent,
                       modify_flags, SVN_WC__ENTRY_MODIFY_ABSENT,
                       atts, SVN_WC__ENTRY_ATTR_ABSENT, name));

  /* Is this entry incomplete? */
  SVN_ERR(do_bool_attr(&entry->incomplete,
                       modify_flags, SVN_WC__ENTRY_MODIFY_INCOMPLETE,
                       atts, SVN_WC__ENTRY_ATTR_INCOMPLETE, name));

  /* Should this item be kept in the working copy after deletion? */
  SVN_ERR(do_bool_attr(&entry->keep_local,
                       modify_flags, SVN_WC__ENTRY_MODIFY_KEEP_LOCAL,
                       atts, SVN_WC__ENTRY_ATTR_KEEP_LOCAL, name));

  /* Attempt to set up timestamps. */
  {
    const char *text_timestr;

    text_timestr = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_TEXT_TIME,
                                APR_HASH_KEY_STRING);
    if (text_timestr)
      {
        if (strcmp(text_timestr, SVN_WC__TIMESTAMP_WC) == 0)
          {
            /* Special case:  a magic string that means 'get this value
               from the working copy' -- we ignore it here, trusting
               that the caller of this function know what to do about
               it.  */
          }
        else
          SVN_ERR(svn_time_from_cstring(&entry->text_time, text_timestr,
                                        pool));

        *modify_flags |= SVN_WC__ENTRY_MODIFY_TEXT_TIME;
      }

    /* Note: we do not persist prop_time, so there is no need to attempt
       to parse a new prop_time value from the log. Certainly, on any
       recent working copy, there will not be a log record to alter
       the prop_time value. */
  }

  /* Checksum. */
  {
    entry->checksum = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CHECKSUM,
                                   APR_HASH_KEY_STRING);
    if (entry->checksum)
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CHECKSUM;
        entry->checksum = apr_pstrdup(pool, entry->checksum);
      }
  }

  /* UUID. */
  {
    entry->uuid = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_UUID,
                               APR_HASH_KEY_STRING);
    if (entry->uuid)
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_UUID;
        entry->uuid = apr_pstrdup(pool, entry->uuid);
      }
  }

  /* Setup last-committed values. */
  {
    const char *cmt_datestr, *cmt_revstr;

    cmt_datestr = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CMT_DATE,
                               APR_HASH_KEY_STRING);
    if (cmt_datestr)
      {
        SVN_ERR(svn_time_from_cstring(&entry->cmt_date, cmt_datestr, pool));
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CMT_DATE;
      }
    else
      entry->cmt_date = 0;

    cmt_revstr = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CMT_REV,
                              APR_HASH_KEY_STRING);
    if (cmt_revstr)
      {
        entry->cmt_rev = SVN_STR_TO_REV(cmt_revstr);
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CMT_REV;
      }
    else
      entry->cmt_rev = SVN_INVALID_REVNUM;

    entry->cmt_author = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_CMT_AUTHOR,
                                     APR_HASH_KEY_STRING);
    if (entry->cmt_author)
      {
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CMT_AUTHOR;
        entry->cmt_author = apr_pstrdup(pool, entry->cmt_author);
      }
  }

  /* Lock token. */
  entry->lock_token = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_LOCK_TOKEN,
                                   APR_HASH_KEY_STRING);
  if (entry->lock_token)
    {
      *modify_flags |= SVN_WC__ENTRY_MODIFY_LOCK_TOKEN;
      entry->lock_token = apr_pstrdup(pool, entry->lock_token);
    }

  /* lock owner. */
  entry->lock_owner = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_LOCK_OWNER,
                                   APR_HASH_KEY_STRING);
  if (entry->lock_owner)
    {
      *modify_flags |= SVN_WC__ENTRY_MODIFY_LOCK_OWNER;
      entry->lock_owner = apr_pstrdup(pool, entry->lock_owner);
    }

  /* lock comment. */
  entry->lock_comment = apr_hash_get(atts, SVN_WC__ENTRY_ATTR_LOCK_COMMENT,
                                     APR_HASH_KEY_STRING);
  if (entry->lock_comment)
    {
      *modify_flags |= SVN_WC__ENTRY_MODIFY_LOCK_COMMENT;
      entry->lock_comment = apr_pstrdup(pool, entry->lock_comment);
    }

  /* lock creation date. */
  {
    const char *cdate_str =
      apr_hash_get(atts, SVN_WC__ENTRY_ATTR_LOCK_CREATION_DATE,
                   APR_HASH_KEY_STRING);
    if (cdate_str)
      {
        SVN_ERR(svn_time_from_cstring(&entry->lock_creation_date,
                                      cdate_str, pool));
        *modify_flags |= SVN_WC__ENTRY_MODIFY_LOCK_CREATION_DATE;
      }
  }

  /* Note: if there are attributes for the (deprecated) has_props,
     has_prop_mods, cachable_props, or present_props, then we're just
     going to ignore them. */

  /* Translated size */
  {
    const char *val
      = apr_hash_get(atts,
                     SVN_WC__ENTRY_ATTR_WORKING_SIZE,
                     APR_HASH_KEY_STRING);
    if (val)
      {
        if (strcmp(val, SVN_WC__WORKING_SIZE_WC) == 0)
          {
            /* Special case (same as the timestamps); ignore here
               these will be handled elsewhere */
          }
        else
          /* Cast to off_t; it's safe: we put in an off_t to start with... */
          entry->working_size = (apr_off_t)apr_strtoi64(val, NULL, 0);

        *modify_flags |= SVN_WC__ENTRY_MODIFY_WORKING_SIZE;
      }
  }

  *new_entry = entry;
  return SVN_NO_ERROR;
}


/* Is the entry in a 'hidden' state in the sense of the 'show_hidden'
 * switches on svn_wc_entries_read(), svn_wc_walk_entries*(), etc.? */
static svn_boolean_t
entry_is_hidden(const svn_wc_entry_t *entry)
{
  return ((entry->deleted && entry->schedule != svn_wc_schedule_add)
          || entry->absent);
}


/* Use entry SRC to fill in blank portions of entry DST.  SRC itself
   may not have any blanks, of course.
   Typically, SRC is a parent directory's own entry, and DST is some
   child in that directory. */
static void
take_from_entry(const svn_wc_entry_t *src,
                svn_wc_entry_t *dst,
                apr_pool_t *pool)
{
  /* Inherits parent's revision if doesn't have a revision of one's
     own, unless this is a subdirectory. */
  if ((dst->revision == SVN_INVALID_REVNUM) && (dst->kind != svn_node_dir))
    dst->revision = src->revision;

  /* Inherits parent's url if doesn't have a url of one's own. */
  if (! dst->url)
    dst->url = svn_path_url_add_component2(src->url, dst->name, pool);

  if (! dst->repos)
    dst->repos = src->repos;

  if ((! dst->uuid)
      && (! ((dst->schedule == svn_wc_schedule_add)
             || (dst->schedule == svn_wc_schedule_replace))))
    {
      dst->uuid = src->uuid;
    }
}


/* Select all the rows from actual_node table in WC_DB and put them into
   *NODES allocated in RESULT_POOL. */
static svn_error_t *
fetch_actual_nodes(apr_hash_t **nodes,
                   svn_sqlite__db_t *wc_db,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  *nodes = apr_hash_make(result_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_SELECT_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      db_actual_node_t *actual_node = apr_pcalloc(result_pool,
                                                  sizeof(*actual_node));

      actual_node->wc_id = svn_sqlite__column_int(stmt, 0);
      actual_node->local_relpath = svn_sqlite__column_text(stmt, 1,
                                                           result_pool);
      actual_node->parent_relpath = svn_sqlite__column_text(stmt, 2,
                                                            result_pool);

      if (!svn_sqlite__column_is_null(stmt, 3))
        {
          apr_size_t len;
          const void *val;

          val = svn_sqlite__column_blob(stmt, 3, &len);
          SVN_ERR(svn_skel__parse_proplist(&actual_node->properties,
                                           svn_skel__parse(val, len,
                                                           scratch_pool),
                                           result_pool));
        }

      if (!svn_sqlite__column_is_null(stmt, 4))
        {
          actual_node->conflict_old = svn_sqlite__column_text(stmt, 4,
                                                              result_pool);
          actual_node->conflict_new = svn_sqlite__column_text(stmt, 5,
                                                              result_pool);
          actual_node->conflict_working = svn_sqlite__column_text(stmt, 6,
                                                                  result_pool);
        }

      if (!svn_sqlite__column_is_null(stmt, 7))
        actual_node->prop_reject = svn_sqlite__column_text(stmt, 7,
                                                           result_pool);

      if (!svn_sqlite__column_is_null(stmt, 8))
        actual_node->changelist = svn_sqlite__column_text(stmt, 8,
                                                          result_pool);

      /* ### column 9 is text_mod */

      if (!svn_sqlite__column_is_null(stmt, 10))
        actual_node->tree_conflict_data = svn_sqlite__column_text(stmt, 10,
                                                                  result_pool);

      apr_hash_set(*nodes, actual_node->local_relpath, APR_HASH_KEY_STRING,
                   actual_node);
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  return SVN_NO_ERROR;
}

static svn_error_t *
fetch_wc_id(apr_int64_t *wc_id, svn_sqlite__db_t *wc_db)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_SELECT_WCROOT_NULL));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_create(SVN_ERR_WC_DB_ERROR, NULL, _("No WC table entry"));
  *wc_id = svn_sqlite__column_int(stmt, 0);
  return svn_sqlite__reset(stmt);
}


static svn_error_t *
determine_keep_local(svn_boolean_t *keep_local,
                     svn_sqlite__db_t *sdb,
                     apr_int64_t wc_id,
                     const char *local_relpath)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_KEEP_LOCAL_FLAG));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_row(stmt));

  *keep_local = svn_sqlite__column_boolean(stmt, 0);

  return svn_sqlite__reset(stmt);
}


static svn_error_t *
determine_incomplete(svn_boolean_t *incomplete,
                     svn_sqlite__db_t *sdb,
                     apr_int64_t wc_id,
                     const char *local_relpath)
{
  svn_sqlite__stmt_t *stmt;

  /* ### very soon, we need to figure out an algorithm to determine
     ### this value, rather than use a recorded value. the wc should be
     ### able to *know* when it is incomplete rather than try to maintain
     ### a flag (which could be wrong).  */

  /* Check to see if a base_node exists for the directory. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, sdb, STMT_SELECT_INCOMPLETE_FLAG));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_row(stmt));

  *incomplete = svn_sqlite__column_boolean(stmt, 0);

  return svn_sqlite__reset(stmt);
}


/* Fill the entries cache in ADM_ACCESS. The full hash cache will be
   populated.  SCRATCH_POOL is used for local memory allocation, the access
   baton pool is used for the cache. */
static svn_error_t *
read_entries(svn_wc_adm_access_t *adm_access,
             apr_pool_t *scratch_pool)
{
  apr_hash_t *actual_nodes;
  svn_sqlite__db_t *wc_db;
  apr_pool_t *result_pool;
  apr_hash_t *entries;
  const char *wc_db_path;
  svn_wc__db_t *db;
  const char *local_abspath;
  const apr_array_header_t *children;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  int i;
  svn_boolean_t parent_copied = FALSE;
  
  if (svn_wc__adm_wc_format(adm_access) < SVN_WC__WC_NG_VERSION)
    return svn_wc__read_entries_old(adm_access, scratch_pool);

  SVN_ERR(svn_wc__adm_get_db(&db, adm_access, scratch_pool));

  result_pool = svn_wc_adm_access_pool(adm_access);
  entries = apr_hash_make(result_pool);

  /* ### need database to determine: incomplete, keep_local, ACTUAL info.  */
  wc_db_path = db_path(svn_wc_adm_access_path(adm_access), scratch_pool);

  /* Open the wc.db sqlite database. */
  SVN_ERR(svn_sqlite__open(&wc_db, wc_db_path, svn_sqlite__mode_readonly,
                           statements,
                           SVN_WC__VERSION_EXPERIMENTAL, upgrade_sql,
                           scratch_pool, scratch_pool));

  /* ### some of the data is not in the wc_db interface. grab it manually.
     ### trim back the columns fetched?  */
  SVN_ERR(fetch_actual_nodes(&actual_nodes, wc_db, scratch_pool, scratch_pool));

  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  svn_wc_adm_access_path(adm_access),
                                  scratch_pool));

  SVN_ERR(svn_wc__db_read_children(&children, db,
                                   local_abspath,
                                   result_pool, scratch_pool));

  APR_ARRAY_PUSH((apr_array_header_t *)children, const char *) = "";

  /* Note that this loop order causes "this dir" to be processed first.
     This is necessary because we may need to access the directory's
     entry during processing of "added" nodes.  */
  for (i = children->nelts; i--; )
    {
      svn_wc__db_kind_t kind;
      svn_wc__db_status_t status;
      svn_wc__db_lock_t *lock;
      const char *repos_relpath;
      svn_checksum_t *checksum;
      svn_filesize_t translated_size;
      svn_wc_entry_t *entry = alloc_entry(result_pool);
      const char *entry_abspath;
      const char *original_repos_relpath;
      const char *original_root_url;
      svn_boolean_t base_shadowed;

      svn_pool_clear(iterpool);

      entry->name = APR_ARRAY_IDX(children, i, const char *);

      entry_abspath = svn_dirent_join(local_abspath, entry->name, iterpool);

      SVN_ERR(svn_wc__db_read_info(
                &status,
                &kind,
                &entry->revision,
                &repos_relpath,
                &entry->repos,
                &entry->uuid,
                &entry->cmt_rev,
                &entry->cmt_date,
                &entry->cmt_author,
                &entry->text_time,
                &entry->depth,
                &checksum,
                &translated_size,
                NULL,
                &entry->changelist,
                &original_repos_relpath,
                &original_root_url,
                NULL,
                &entry->copyfrom_rev,
                NULL,
                NULL,
                &base_shadowed,
                &lock,
                db,
                entry_abspath,
                result_pool,
                iterpool));

      if (status == svn_wc__db_status_normal)
        {
          svn_boolean_t have_row = FALSE;

          /* Ugh. During a checkout, it is possible that we are constructing
             a subdirectory "over" a not-present directory. The read_info()
             will return information out of the wc.db in the subdir. We
             need to detect this situation and create a DELETED entry
             instead.  */
          if (kind == svn_wc__db_kind_dir)
            {
                svn_sqlite__stmt_t *stmt;

                SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db,
                                                  STMT_SELECT_NOT_PRESENT));
                SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                          (apr_uint64_t)1 /* wc_id */,
                                          entry->name));
                SVN_ERR(svn_sqlite__step(&have_row, stmt));
                SVN_ERR(svn_sqlite__reset(stmt));
            }

          if (have_row)
            {
              /* Just like a normal "not-present" node: schedule=normal
                 and DELETED.  */
              entry->schedule = svn_wc_schedule_normal;
              entry->deleted = TRUE;
            }
          else
            {
              /* Plain old BASE node.  */
              entry->schedule = svn_wc_schedule_normal;

              /* Grab inherited repository information, if necessary. */
              if (repos_relpath == NULL)
                {
                  SVN_ERR(svn_wc__db_scan_base_repos(&repos_relpath,
                                                     &entry->repos,
                                                     &entry->uuid,
                                                     db,
                                                     entry_abspath,
                                                     result_pool,
                                                     iterpool));
                }

              /* ### hacky hacky  */
              SVN_ERR(determine_incomplete(&entry->incomplete, wc_db,
                                           1 /* wc_id */, entry->name));
            }
        }
      else if (status == svn_wc__db_status_deleted
               || status == svn_wc__db_status_obstructed_delete)
        {
          /* ### we don't have to worry about moves, so this is a delete. */
          entry->schedule = svn_wc_schedule_delete;

          /* ### keep_local (same hack as determine_incomplete) */
          SVN_ERR(determine_keep_local(&entry->keep_local, wc_db,
                                       1 /* wc_id */, entry->name));
        }
      else if (status == svn_wc__db_status_added
               || status == svn_wc__db_status_obstructed_add)
        {
          svn_wc__db_status_t work_status;
          svn_revnum_t original_revision;

          /* For child nodes, pick up the parent's revision.  */
          if (*entry->name != '\0')
            {
              const svn_wc_entry_t *parent_entry;

              assert(entry->revision == SVN_INVALID_REVNUM);

              parent_entry = apr_hash_get(entries, "", 0);
              entry->revision = parent_entry->revision;
            }

          if (base_shadowed)
            {
              svn_wc__db_status_t base_status;

              /* ### mystery: make the rev same as BASE. */
              SVN_ERR(svn_wc__db_base_get_info(&base_status, NULL,
                                               &entry->revision,
                                               NULL, NULL, NULL,
                                               NULL, NULL, NULL,
                                               NULL, NULL, NULL,
                                               NULL, NULL, NULL,
                                               db, entry_abspath,
                                               iterpool, iterpool));

              if (base_status == svn_wc__db_status_not_present)
                {
                  /* ### the underlying node is DELETED in this revision.  */
                  entry->deleted = TRUE;

                  /* This is an add since there isn't a node to replace.  */
                  entry->schedule = svn_wc_schedule_add;
                }
              else
                entry->schedule = svn_wc_schedule_replace;
            }
          else
            {
              /* ### if this looks like a plain old add, then rev=0.  */
              if (!SVN_IS_VALID_REVNUM(entry->copyfrom_rev)
                  && !SVN_IS_VALID_REVNUM(entry->cmt_rev))
                entry->revision = 0;

              if (status == svn_wc__db_status_obstructed_add)
                entry->revision = SVN_INVALID_REVNUM;

              /* ### when we're reading a directory that is not present,
                 ### then it must be "normal" rather than "add".  */
              if (*entry->name == '\0'
                  && status == svn_wc__db_status_obstructed_add)
                entry->schedule = svn_wc_schedule_normal;
              else
                entry->schedule = svn_wc_schedule_add;
            }

          SVN_ERR(svn_wc__db_scan_working(&work_status,
                                          NULL,
                                          &repos_relpath,
                                          &entry->repos,
                                          &entry->uuid,
                                          NULL, NULL, NULL, &original_revision,
                                          NULL,
                                          db,
                                          entry_abspath,
                                          result_pool,
                                          iterpool));

          if (!SVN_IS_VALID_REVNUM(entry->cmt_rev)
              && original_repos_relpath == NULL)
            {
              /* There is NOT a last-changed revision (last-changed date and
                 author may be unknown, but we can always check the rev).
                 The absence of a revision implies this node was added WITHOUT
                 any history. Avoid the COPIED checks in the else block.  */
              /* ### scan_working may need to be updated to avoid returning
                 ### status_copied in this case.  */
            }
          else if (work_status == svn_wc__db_status_copied)
            {
              entry->copied = TRUE;

              /* If this is the parent directory, then record its copy
                 status. This will be used for schedule-delete children.  */
              if (*entry->name == '\0')
                parent_copied = TRUE;

              /* If this is a child of a copied subtree, then it should be
                 schedule_normal.  */
              if (original_repos_relpath == NULL)
                {
                  /* ### what if there is a BASE node under there? */
                  entry->schedule = svn_wc_schedule_normal;
                }

              /* ### copied nodes need to mirror their copyfrom_rev */
              entry->revision = original_revision;
            }

          /* Does this node have copyfrom_* information?  */
          if (original_repos_relpath != NULL)
            {
              const char *parent_abspath;
              svn_boolean_t set_copyfrom = TRUE;
              svn_error_t *err;
              const char *op_root_abspath;
              const char *parent_repos_relpath;
              const char *parent_root_url;

              SVN_ERR_ASSERT(work_status == svn_wc__db_status_copied);

              /* When we insert entries into the database, we will construct
                 additional copyfrom records for mixed-revision copies. The
                 old entries would simply record the different revision in
                 the entry->revision field. That is not available within
                 wc-ng, so additional copies are made (see the logic inside
                 write_entry()). However, when reading these back *out* of
                 the database, the additional copies look like new "Added"
                 nodes rather than a simple mixed-rev working copy.

                 That would be a behavior change if we did not compensate.
                 If there is copyfrom information for this node, then the
                 code below looks at the parent to detect if it *also* has
                 copyfrom information, and if the copyfrom_url would align
                 properly. If it *does*, then we omit storing copyfrom_url
                 and copyfrom_rev, and simply leave the mixed-rev value
                 that was stored into entry->revision by the code above.  */

              /* Get the copyfrom information from our parent.

                 Note that the parent could be added/copied/moved-here. There
                 is no way for it to be deleted/moved-away and have *this*
                 node appear as copied.  */
              parent_abspath = svn_dirent_dirname(entry_abspath, iterpool);
              err = svn_wc__db_scan_working(NULL,
                                            &op_root_abspath,
                                            NULL, NULL, NULL,
                                            &parent_repos_relpath,
                                            &parent_root_url,
                                            NULL, NULL,
                                            NULL,
                                            db,
                                            parent_abspath,
                                            iterpool, iterpool);
              if (err)
                {
                  if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
                    return err;
                  svn_error_clear(err);
                }
              else if (parent_root_url != NULL
                       && strcmp(original_root_url, parent_root_url) == 0)
                {
                  const char *relpath_to_entry = svn_dirent_is_child(
                    op_root_abspath, entry_abspath, NULL);
                  const char *entry_repos_relpath = svn_uri_join(
                    parent_repos_relpath, relpath_to_entry, iterpool);

                  /* The copyfrom roots matched. Now we look to see if the
                     copyfrom path of the parent would align with our own
                     path (meaning this copyfrom was inserted for mixed-rev
                     purposes and can be eliminated without changing the
                     implied copyfrom path).  */
                  if (strcmp(entry_repos_relpath, original_repos_relpath) == 0)
                    {
                      set_copyfrom = FALSE;

                      /* Reset a couple fields, to their proper states when
                         no copyfrom information is present.  */
                      entry->copyfrom_rev = SVN_INVALID_REVNUM;
                      entry->schedule = svn_wc_schedule_normal;
                    }
                }

              if (set_copyfrom)
                entry->copyfrom_url =
                  svn_path_url_add_component2(original_root_url,
                                              original_repos_relpath,
                                              result_pool);
            }
        }
      else if (status == svn_wc__db_status_not_present)
        {
          /* ### buh. 'deleted' nodes are actually supposed to be
             ### schedule "normal" since we aren't going to actually *do*
             ### anything to this node at commit time.  */
          entry->schedule = svn_wc_schedule_normal;
          entry->deleted = TRUE;
        }
      else if (status == svn_wc__db_status_obstructed)
        {
          /* ### set some values that should (hopefully) let this directory
             ### be usable.  */
          entry->revision = SVN_INVALID_REVNUM;
        }
      else
        {
          /* One of the not-present varieties. Skip this node.  */
          SVN_ERR_ASSERT(status == svn_wc__db_status_absent
                         || status == svn_wc__db_status_excluded
                         || status == svn_wc__db_status_incomplete);
          continue;
        }

      /* ### higher levels want repos information about deleted nodes, even
         ### tho they are not "part of" a repository any more.  */
      if (entry->schedule == svn_wc_schedule_delete)
        {
          svn_error_t *err;

          /* Get the information from the underlying BASE node.  */
          err = svn_wc__db_base_get_info(NULL, &kind,
                                         &entry->revision,
                                         NULL, NULL, NULL,
                                         &entry->cmt_rev,
                                         &entry->cmt_date,
                                         &entry->cmt_author,
                                         &entry->text_time,
                                         &entry->depth,
                                         &checksum,
                                         &entry->working_size,
                                         NULL,
                                         NULL,
                                         db,
                                         entry_abspath,
                                         result_pool,
                                         iterpool);
          if (err)
            {
              svn_wc__db_status_t refined_status;
              const char *op_root_abspath;
              const char *parent_repos_relpath;
              const char *parent_abspath;

              if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
                return err;

              /* No base node?  This is a deleted child of a copy, so we
                 need to scan through the working tree, potentially twice */
              svn_error_clear(err);

              SVN_ERR(svn_wc__db_scan_working(&refined_status,
                                              &op_root_abspath,
                                              &repos_relpath,
                                              NULL, NULL, NULL,
                                              NULL, NULL, NULL, NULL,
                                              db,
                                              entry_abspath,
                                              result_pool,
                                              iterpool));

              /* Reconstruct this from the parent's information. */
              SVN_ERR_ASSERT(repos_relpath == NULL);
              SVN_ERR_ASSERT(refined_status == svn_wc__db_status_deleted);
              parent_abspath = svn_dirent_dirname(op_root_abspath, iterpool);

              SVN_ERR(svn_wc__db_scan_working(NULL, NULL,
                                              &parent_repos_relpath,
                                              &entry->repos,
                                              &entry->uuid,
                                              NULL, NULL, NULL,
                                              NULL, NULL,
                                              db,
                                              parent_abspath,
                                              result_pool,
                                              iterpool));

              /* Now glue it all together */
              repos_relpath = svn_uri_join(
                                   parent_repos_relpath,
                                   svn_dirent_is_child(parent_abspath,
                                                       entry_abspath,
                                                       NULL),
                                   iterpool);
            }
          else
            {
              SVN_ERR(svn_wc__db_scan_base_repos(&repos_relpath,
                                                 &entry->repos,
                                                 &entry->uuid,
                                                 db,
                                                 entry_abspath,
                                                 result_pool,
                                                 iterpool));
            }

          /* For child nodes without a revision, pick up the parent's
             revision.  */
          if (!SVN_IS_VALID_REVNUM(entry->revision) && *entry->name != '\0')
            {
              const svn_wc_entry_t *parent_entry;

              parent_entry = apr_hash_get(entries, "", 0);
              entry->revision = parent_entry->revision;
            }

          /* Hold on tight. This is where it gets REALLY confusing.

             If we have changed_* information for the node which is being
             deleted, then it doesn't matter whether we are COPIED or not.
             Presumably because we know what revision needs to be deleted.

             If we do NOT have changed_* information, then if the parent
             was COPIED, then we are also COPIED. For child nodes, detection
             is easy -- we dropped off the value into PARENT_COPIED. For
             "this dir", we have a lot more work to see if any ancestors
             were copied, and (thus) making us a COPIED child.  */
          if (SVN_IS_VALID_REVNUM(entry->cmt_rev))
            {
              /* Since we have change information, this is not COPIED.  */
            }
          else if (*entry->name != '\0')
            {
              /* Child nodes simply inherit the parent's COPIED flag.  */
              entry->copied = parent_copied;
            }
          else
            {
              const char *current_abspath = local_abspath;

              /* As long as CURRENT_ABSPATH refers to a deleted node, we
                 will scan up the tree looking for a copy. We will either
                 find this node is a deleted child of a copied subtree,
                 or it is part of a deleted subtree.  */
              while (TRUE)
                {
                  const char *parent_abspath;
                  svn_wc__db_status_t parent_status;
                  const char *op_root_abspath;

                  /* Back up to this directory's parent. We know it is version-
                     controlled since we are schedule-delete (you can't schedule
                     the wcroot for deletion!). We then need to figure out what
                     is going on with that node.  */
                  parent_abspath = svn_dirent_dirname(current_abspath,
                                                      iterpool);
                  err = svn_wc__db_scan_working(&parent_status,
                                                &op_root_abspath,
                                                NULL, NULL, NULL,
                                                NULL, NULL, NULL, NULL,
                                                NULL,
                                                db,
                                                parent_abspath,
                                                iterpool, iterpool);
                  if (err)
                    {
                      if (err->apr_err != SVN_ERR_WC_PATH_NOT_FOUND)
                        return err;
                      svn_error_clear(err);

                      /* The parent is not in the WORKING tree, meaning it is
                         unmodified. That implies CURRENT_ABSPATH is the root
                         of a deleted subtree, so it is certainly not copied.
                         Nothing more to do.  */
                      break;
                    }
                  if (parent_status == svn_wc__db_status_copied
                      || parent_status == svn_wc__db_status_moved_dst)
                    {
                      /* The parent is copied/moved here, so CURRENT_ABSPATH
                         is the root of a deleted subtree. Mark everything as
                         COPIED.

                         Note: MOVED_DST is a concept foreign to this old
                         interface, but it is best represented as if a copy
                         had occurred, so we'll model it that way to old
                         clients.  */
                      entry->copied = parent_copied = TRUE;
                      break;
                    }
                  if (parent_status == svn_wc__db_status_added)
                    {
                      /* Whoops. CURRENT_ABSPATH is scheduled for deletion,
                         yet the parent is scheduled for addition. This can
                         occur when a subtree is deleted, and then nodes
                         are added *later*. Since the parent is a simple add,
                         then nothing has been copied. Nothing more to do.

                         Note: if a subtree is added, *then* deletions are
                         made, the nodes should simply be removed from
                         version control.  */
                      break;
                    }

                  /* Reaching here, the only valid response for the parent
                     is that it has been deleted/moved-away.  */
                  SVN_ERR_ASSERT(
                    parent_status == svn_wc__db_status_deleted
                    || parent_status == svn_wc__db_status_moved_src);

                  /* OP_ROOT_ABSPATH is the root of the deletion/move. We
                     now need to examine what happened to *its* parent.
                     That root may be a deleted subtree within a copied
                     subtree. We gotta go all the way up to the top.

                     Set CURRENT_ABSPATH to this new root. We'll loop
                     around to examine its parent.  */
                  current_abspath = op_root_abspath;
                }
            }
        }

      /* ### default to the infinite depth if we don't know it. */
      if (entry->depth == svn_depth_unknown)
        entry->depth = svn_depth_infinity;

      if (kind == svn_wc__db_kind_dir)
        entry->kind = svn_node_dir;
      else if (kind == svn_wc__db_kind_file)
        entry->kind = svn_node_file;
      else if (kind == svn_wc__db_kind_symlink)
        entry->kind = svn_node_file;  /* ### no symlink kind */
      else
        entry->kind = svn_node_unknown;

      SVN_ERR_ASSERT(repos_relpath != NULL
                     || entry->schedule == svn_wc_schedule_delete
                     || status == svn_wc__db_status_obstructed
                     || status == svn_wc__db_status_obstructed_delete
                     );
      if (repos_relpath)
        entry->url = svn_path_url_add_component2(entry->repos,
                                                 repos_relpath,
                                                 result_pool);

      if (checksum)
        entry->checksum = svn_checksum_to_cstring(checksum, result_pool);

     if (lock)
       {
         entry->lock_token = lock->token;
         entry->lock_owner = lock->owner;
         entry->lock_comment = lock->comment;
         entry->lock_creation_date = lock->date;
       }

      /* ### there may be an ACTUAL_NODE to grab info from.  really, this
         ### should probably only exist for added/copied files, but it
         ### seems to always be needed. Just do so, for now.  */
      if (TRUE)
        {
          const db_actual_node_t *actual_node;

          actual_node = apr_hash_get(actual_nodes,
                                     entry->name, APR_HASH_KEY_STRING);
          if (actual_node)
            {
              if (actual_node->conflict_old != NULL)
                {
                  entry->conflict_old =
                    apr_pstrdup(result_pool, actual_node->conflict_old);
                  entry->conflict_new =
                    apr_pstrdup(result_pool, actual_node->conflict_new);
                  entry->conflict_wrk =
                    apr_pstrdup(result_pool, actual_node->conflict_working);
                }

              if (actual_node->prop_reject != NULL)
                entry->prejfile =
                  apr_pstrdup(result_pool, actual_node->prop_reject);

              if (actual_node->tree_conflict_data != NULL)
                entry->tree_conflict_data =
                  apr_pstrdup(result_pool, actual_node->tree_conflict_data);
            }
        }

      /* Let's check for a file external.
         ### right now this is ugly, since we have no good way querying
         ### for a file external OR retrieving properties.  ugh.  */
      if (entry->kind == svn_node_file)
        {
          svn_sqlite__stmt_t *stmt;
          svn_boolean_t have_row;

          SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db,
                                            STMT_SELECT_FILE_EXTERNAL));
          SVN_ERR(svn_sqlite__bindf(stmt, "is",
                                    (apr_uint64_t)1 /* wc_id */,
                                    entry->name));
          SVN_ERR(svn_sqlite__step(&have_row, stmt));

          if (!svn_sqlite__column_is_null(stmt, 0))
            {
              SVN_ERR(svn_wc__unserialize_file_external(
                            &entry->file_external_path,
                            &entry->file_external_peg_rev,
                            &entry->file_external_rev,
                            svn_sqlite__column_text(stmt, 0, NULL),
                            result_pool));

            }

          SVN_ERR(svn_sqlite__reset(stmt));
        }

      entry->working_size = translated_size;

      apr_hash_set(entries, entry->name, APR_HASH_KEY_STRING, entry);
    }

  svn_wc__adm_access_set_entries(adm_access, entries);

  SVN_ERR(svn_sqlite__close(wc_db));
  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

/* For non-directory PATHs full entry information is obtained by reading
 * the entries for the parent directory of PATH and then extracting PATH's
 * entry.  If PATH is a directory then only abrieviated information is
 * available in the parent directory, more complete information is
 * available by reading the entries for PATH itself.
 *
 * Note: There is one bit of information about directories that is only
 * available in the parent directory, that is the "deleted" state.  If PATH
 * is a versioned directory then the "deleted" state information will not
 * be returned in ENTRY.  This means some bits of the code (e.g. revert)
 * need to obtain it by directly extracting the directory entry from the
 * parent directory's entries.  I wonder if this function should handle
 * that?
 */
svn_error_t *
svn_wc_entry(const svn_wc_entry_t **entry,
             const char *path,
             svn_wc_adm_access_t *adm_access,
             svn_boolean_t show_hidden,
             apr_pool_t *pool)
{
  const char *entry_name;
  svn_wc_adm_access_t *dir_access;

  SVN_ERR(svn_wc__adm_retrieve_internal(&dir_access, adm_access, path, pool));
  if (! dir_access)
    {
      const char *dir_path, *base_name;
      svn_dirent_split(path, &dir_path, &base_name, pool);
      SVN_ERR(svn_wc__adm_retrieve_internal(&dir_access, adm_access, dir_path,
                                            pool));
      entry_name = base_name;
    }
  else
    entry_name = SVN_WC_ENTRY_THIS_DIR;

  if (dir_access)
    {
      apr_hash_t *entries;
      SVN_ERR(svn_wc_entries_read(&entries, dir_access, show_hidden, pool));
      *entry = apr_hash_get(entries, entry_name, APR_HASH_KEY_STRING);
    }
  else
    *entry = NULL;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entry_versioned_internal(const svn_wc_entry_t **entry,
                                 const char *path,
                                 svn_wc_adm_access_t *adm_access,
                                 svn_boolean_t show_hidden,
                                 const char *caller_filename,
                                 int caller_lineno,
                                 apr_pool_t *pool)
{
  SVN_ERR(svn_wc_entry(entry, path, adm_access, show_hidden, pool));

  if (! *entry)
    {
      svn_error_t *err
        = svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                            _("'%s' is not under version control"),
                            svn_path_local_style(path, pool));

      err->file = caller_filename;
      err->line = caller_lineno;
      return err;
    }

  return SVN_NO_ERROR;
}


/* Prune the deleted entries from the cached entries in ADM_ACCESS, and
   return that collection.  SCRATCH_POOL is used for local, short
   term, memory allocation, RESULT_POOL for permanent stuff.  */
static apr_hash_t *
prune_deleted(apr_hash_t *entries_all,
              apr_pool_t *result_pool,
              apr_pool_t *scratch_pool)
{
  apr_hash_index_t *hi;
  apr_hash_t *entries;

  if (!entries_all)
    return NULL;

  /* I think it will be common for there to be no deleted entries, so
     it is worth checking for that case as we can optimise it. */
  for (hi = apr_hash_first(scratch_pool, entries_all);
       hi;
       hi = apr_hash_next(hi))
    {
      void *val;
      const svn_wc_entry_t *entry;
      apr_hash_this(hi, NULL, NULL, &val);
      entry = val;
      if ((entry->deleted
           && (entry->schedule != svn_wc_schedule_add)
           && (entry->schedule != svn_wc_schedule_replace))
          || entry->absent || (entry->depth == svn_depth_exclude))
        break;
    }

  if (! hi)
    {
      /* There are no deleted entries, so we can use the full hash */
      return  entries_all;
    }

  /* Construct pruned hash without deleted entries */
  entries = apr_hash_make(result_pool);
  for (hi = apr_hash_first(scratch_pool, entries_all);
       hi;
       hi = apr_hash_next(hi))
    {
      void *val;
      const void *key;
      const svn_wc_entry_t *entry;

      apr_hash_this(hi, &key, NULL, &val);
      entry = val;
      if (((entry->deleted == FALSE) && (entry->absent == FALSE)
           && (entry->depth != svn_depth_exclude))
          || (entry->schedule == svn_wc_schedule_add)
          || (entry->schedule == svn_wc_schedule_replace))
        {
          apr_hash_set(entries, key, APR_HASH_KEY_STRING, entry);
        }
    }

  return entries;
}

svn_error_t *
svn_wc_entries_read(apr_hash_t **entries,
                    svn_wc_adm_access_t *adm_access,
                    svn_boolean_t show_hidden,
                    apr_pool_t *pool)
{
  apr_hash_t *new_entries;

  new_entries = svn_wc__adm_access_entries(adm_access, pool);
  if (! new_entries)
    {
      /* Ask for the deleted entries because most operations request them
         at some stage, getting them now avoids a second file parse. */
      SVN_ERR(read_entries(adm_access, pool));

      new_entries = svn_wc__adm_access_entries(adm_access, pool);
    }

  if (show_hidden)
    *entries = new_entries;
  else
    *entries = prune_deleted(new_entries, svn_wc_adm_access_pool(adm_access),
                             pool);

  return SVN_NO_ERROR;
}


static svn_error_t *
insert_base_node(svn_sqlite__db_t *wc_db,
                 const db_base_node_t *base_node,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_INSERT_BASE_NODE));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 1, base_node->wc_id));
  SVN_ERR(svn_sqlite__bind_text(stmt, 2, base_node->local_relpath));

  if (base_node->repos_id)
    {
      SVN_ERR(svn_sqlite__bind_int64(stmt, 3, base_node->repos_id));
      SVN_ERR(svn_sqlite__bind_text(stmt, 4, base_node->repos_relpath));
    }

  if (base_node->parent_relpath)
    SVN_ERR(svn_sqlite__bind_text(stmt, 5, base_node->parent_relpath));

  if (base_node->presence == svn_wc__db_status_not_present)
    SVN_ERR(svn_sqlite__bind_text(stmt, 6, "not-present"));
  else if (base_node->presence == svn_wc__db_status_normal)
    SVN_ERR(svn_sqlite__bind_text(stmt, 6, "normal"));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 7, base_node->revision));

  /* ### in per-subdir operation, if we're about to write a directory and
     ### it is *not* "this dir", then we're writing a row in the parent
     ### directory about the child. note that in the kind.  */
  /* ### kind might be "symlink" or "unknown" */
  if (base_node->kind == svn_node_dir && *base_node->local_relpath != '\0')
    SVN_ERR(svn_sqlite__bind_text(stmt, 8, "subdir"));
  else
    SVN_ERR(svn_sqlite__bind_text(stmt, 8,
                                  svn_node_kind_to_word(base_node->kind)));

  if (base_node->checksum)
    {
      const char *kind_str = (base_node->checksum->kind == svn_checksum_md5
                              ? "$md5 $" : "$sha1$");
      SVN_ERR(svn_sqlite__bind_text(stmt, 9, apr_pstrcat(scratch_pool,
                    kind_str, svn_checksum_to_cstring(base_node->checksum,
                                                      scratch_pool), NULL)));
    }

  SVN_ERR(svn_sqlite__bind_int64(stmt, 10, base_node->translated_size));

  /* ### strictly speaking, changed_rev should be valid for present nodes. */
  if (SVN_IS_VALID_REVNUM(base_node->changed_rev))
    SVN_ERR(svn_sqlite__bind_int64(stmt, 11, base_node->changed_rev));
  if (base_node->changed_date)
    SVN_ERR(svn_sqlite__bind_int64(stmt, 12, base_node->changed_date));
  if (base_node->changed_author)
    SVN_ERR(svn_sqlite__bind_text(stmt, 13, base_node->changed_author));

  SVN_ERR(svn_sqlite__bind_text(stmt, 14, svn_depth_to_word(base_node->depth)));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 15, base_node->last_mod_time));

  if (base_node->properties)
    {
      svn_skel_t *skel;
      svn_stringbuf_t *properties;

      SVN_ERR(svn_skel__unparse_proplist(&skel, base_node->properties,
                                         scratch_pool));
      properties = svn_skel__unparse(skel, scratch_pool);
      SVN_ERR(svn_sqlite__bind_blob(stmt, 16, properties->data,
                                    properties->len));
    }

  SVN_ERR(svn_sqlite__bind_int64(stmt, 17, base_node->incomplete_children));

  /* Execute and reset the insert clause. */
  return svn_sqlite__insert(NULL, stmt);
}

static svn_error_t *
insert_working_node(svn_sqlite__db_t *wc_db,
                    const db_working_node_t *working_node,
                    apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_stringbuf_t *properties;
  svn_skel_t *skel;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_INSERT_WORKING_NODE));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 1, working_node->wc_id));
  SVN_ERR(svn_sqlite__bind_text(stmt, 2, working_node->local_relpath));
  SVN_ERR(svn_sqlite__bind_text(stmt, 3, working_node->parent_relpath));

  /* ### need rest of values */
  if (working_node->presence == svn_wc__db_status_normal)
    SVN_ERR(svn_sqlite__bind_text(stmt, 4, "normal"));
  else if (working_node->presence == svn_wc__db_status_not_present)
    SVN_ERR(svn_sqlite__bind_text(stmt, 4, "not-present"));

  /* ### in per-subdir operation, if we're about to write a directory and
     ### it is *not* "this dir", then we're writing a row in the parent
     ### directory about the child. note that in the kind.  */
  if (working_node->kind == svn_node_dir
      && *working_node->local_relpath != '\0')
    SVN_ERR(svn_sqlite__bind_text(stmt, 5, "subdir"));
  else
    SVN_ERR(svn_sqlite__bind_text(stmt, 5,
                                  svn_node_kind_to_word(working_node->kind)));

  if (working_node->copyfrom_repos_path)
    {
      SVN_ERR(svn_sqlite__bind_int64(stmt, 6,
                                     working_node->copyfrom_repos_id));
      SVN_ERR(svn_sqlite__bind_text(stmt, 7,
                                    working_node->copyfrom_repos_path));
      SVN_ERR(svn_sqlite__bind_int64(stmt, 8, working_node->copyfrom_revnum));
    }

  if (working_node->moved_here)
    SVN_ERR(svn_sqlite__bind_int(stmt, 9, working_node->moved_here));

  if (working_node->moved_to)
    SVN_ERR(svn_sqlite__bind_text(stmt, 10, working_node->moved_to));

  if (working_node->checksum)
    {
      const char *kind_str = (working_node->checksum->kind == svn_checksum_md5
                              ? "$md5 $" : "$sha1$");
      SVN_ERR(svn_sqlite__bind_text(stmt, 11, apr_pstrcat(scratch_pool,
                    kind_str, svn_checksum_to_cstring(working_node->checksum,
                                                      scratch_pool), NULL)));
      SVN_ERR(svn_sqlite__bind_int64(stmt, 12, working_node->translated_size));
    }

  if (SVN_IS_VALID_REVNUM(working_node->changed_rev))
    SVN_ERR(svn_sqlite__bind_int64(stmt, 13, working_node->changed_rev));
  if (working_node->changed_date)
    SVN_ERR(svn_sqlite__bind_int64(stmt, 14, working_node->changed_date));
  if (working_node->changed_author)
    SVN_ERR(svn_sqlite__bind_text(stmt, 15, working_node->changed_author));

  SVN_ERR(svn_sqlite__bind_text(stmt, 16,
                                svn_depth_to_word(working_node->depth)));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 17, working_node->last_mod_time));

  if (working_node->properties)
    SVN_ERR(svn_skel__unparse_proplist(&skel, working_node->properties,
                                       scratch_pool));
  else
    skel = svn_skel__make_empty_list(scratch_pool);

  properties = svn_skel__unparse(skel, scratch_pool);
  SVN_ERR(svn_sqlite__bind_blob(stmt, 18, properties->data, properties->len));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 19, working_node->keep_local));

  /* Execute and reset the insert clause. */
  return svn_sqlite__insert(NULL, stmt);
}

static svn_error_t *
insert_actual_node(svn_sqlite__db_t *wc_db,
                   const db_actual_node_t *actual_node,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_INSERT_ACTUAL_NODE));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 1, actual_node->wc_id));
  SVN_ERR(svn_sqlite__bind_text(stmt, 2, actual_node->local_relpath));
  SVN_ERR(svn_sqlite__bind_text(stmt, 3, actual_node->parent_relpath));

  if (actual_node->properties)
    {
      svn_skel_t *skel;
      svn_stringbuf_t *properties;

      SVN_ERR(svn_skel__unparse_proplist(&skel, actual_node->properties,
                                         scratch_pool));
      properties = svn_skel__unparse(skel, scratch_pool);
      SVN_ERR(svn_sqlite__bind_blob(stmt, 4, properties->data,
                                    properties->len));
    }

  if (actual_node->conflict_old)
    {
      SVN_ERR(svn_sqlite__bind_text(stmt, 5, actual_node->conflict_old));
      SVN_ERR(svn_sqlite__bind_text(stmt, 6, actual_node->conflict_new));
      SVN_ERR(svn_sqlite__bind_text(stmt, 7, actual_node->conflict_working));
    }

  if (actual_node->prop_reject)
    SVN_ERR(svn_sqlite__bind_text(stmt, 8, actual_node->prop_reject));

  if (actual_node->changelist)
    SVN_ERR(svn_sqlite__bind_text(stmt, 9, actual_node->changelist));

  /* ### column 10 is text_mod */

  if (actual_node->tree_conflict_data)
    SVN_ERR(svn_sqlite__bind_text(stmt, 11, actual_node->tree_conflict_data));

  /* Execute and reset the insert clause. */
  return svn_sqlite__insert(NULL, stmt);
}


/* Write the information for ENTRY to WC_DB.  The WC_ID, REPOS_ID and
   REPOS_ROOT will all be used for writing ENTRY.
   ### transitioning from straight sql to using the wc_db APIs.  For the
   ### time being, we'll need both parameters. */
static svn_error_t *
write_entry(svn_wc__db_t *db,
            svn_sqlite__db_t *wc_db,
            apr_int64_t wc_id,
            apr_int64_t repos_id,
            const char *repos_root,
            const svn_wc_entry_t *entry,
            const char *name,
            const char *entry_abspath,
            const svn_wc_entry_t *this_dir,
            apr_pool_t *pool)
{
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  db_base_node_t *base_node = NULL;
  db_working_node_t *working_node = NULL;
  db_actual_node_t *actual_node = NULL;

  switch (entry->schedule)
    {
      case svn_wc_schedule_normal:
        if (entry->copied)
          working_node = MAYBE_ALLOC(working_node, scratch_pool);
        else
          base_node = MAYBE_ALLOC(base_node, scratch_pool);
        break;

      case svn_wc_schedule_add:
        working_node = MAYBE_ALLOC(working_node, scratch_pool);
        break;

      case svn_wc_schedule_delete:
        working_node = MAYBE_ALLOC(working_node, scratch_pool);
        if (!this_dir->copied)
          base_node = MAYBE_ALLOC(base_node, scratch_pool);
        /* ### what about a deleted BASE tree, with a copy over the top,
           ### followed by a delete? there should be a base node then...  */
        break;

      case svn_wc_schedule_replace:
        working_node = MAYBE_ALLOC(working_node, scratch_pool);
        base_node = MAYBE_ALLOC(base_node, scratch_pool);
        break;
    }

  /* Something deleted in this revision means there should always be a
     BASE node to indicate the not-present node.  */
  if (entry->deleted)
    {
      base_node = MAYBE_ALLOC(base_node, scratch_pool);
    }

  if (entry->copied)
    {
      /* Make sure we get a WORKING_NODE inserted. The copyfrom information
         will occur here or on a parent, as appropriate.  */
      working_node = MAYBE_ALLOC(working_node, scratch_pool);

      if (entry->copyfrom_url)
        {
          const char *relative_url;

          working_node->copyfrom_repos_id = repos_id;
          relative_url = svn_uri_is_child(repos_root, entry->copyfrom_url,
                                          NULL);
          if (relative_url == NULL)
            working_node->copyfrom_repos_path = "";
          else
            {
              /* copyfrom_repos_path is NOT a URI. decode into repos path.  */
              working_node->copyfrom_repos_path =
                svn_path_uri_decode(relative_url, scratch_pool);
            }
          working_node->copyfrom_revnum = entry->copyfrom_rev;
        }
      else
        {
          const char *parent_abspath = svn_dirent_dirname(entry_abspath,
                                                          scratch_pool);
          const char *op_root_abspath;
          const char *original_repos_relpath;
          svn_revnum_t original_revision;
          svn_error_t *err;

          /* The parent will *always* have info in the WORKING tree, since
             we've been designated as COPIED but do not have our own
             COPYFROM information. Therefore, our parent or a more distant
             ancestor has that information. Grab the data.  */
          err = svn_wc__db_scan_working(
                    NULL,
                    &op_root_abspath,
                    NULL, NULL, NULL,
                    &original_repos_relpath, NULL, NULL, &original_revision,
                    NULL,
                    db,
                    parent_abspath,
                    scratch_pool, scratch_pool);

          /* We could be reading the entries while in a transitional state
             during an add/copy operation. The scan_working *does* throw
             errors sometimes. So clear anything that may come out of it,
             and perform the copyfrom construction only when it looks like
             we have a good/real set of return values.  */
          svn_error_clear(err);

          /* We may have been copied from a mixed-rev working copy. We need
             to simulate additional copies around revision changes. The old
             code could separately store the revision, but NG needs to create
             copies at each change.  */
          if (err == NULL
              && op_root_abspath != NULL
              && original_repos_relpath != NULL
              && SVN_IS_VALID_REVNUM(original_revision)
              /* above is valid result testing. below is the key test.  */
              && original_revision != entry->revision)
            {
              const char *relpath_to_entry = svn_dirent_is_child(
                op_root_abspath, entry_abspath, NULL);
              const char *new_copyfrom_relpath = svn_uri_join(
                original_repos_relpath, relpath_to_entry, scratch_pool);

              working_node->copyfrom_repos_id = repos_id;
              working_node->copyfrom_repos_path = new_copyfrom_relpath;
              working_node->copyfrom_revnum = entry->revision;
            }
        }
    }

  if (entry->keep_local)
    {
      SVN_ERR_ASSERT(working_node != NULL);
      SVN_ERR_ASSERT(entry->schedule == svn_wc_schedule_delete);
      working_node->keep_local = TRUE;
    }

  if (entry->absent)
    {
      /* TODO: Adjust kinds to absent kinds. */
    }

  if (entry->incomplete)
    {
      base_node = MAYBE_ALLOC(base_node, scratch_pool);
      base_node->incomplete_children = TRUE;
    }

  if (entry->conflict_old)
    {
      actual_node = MAYBE_ALLOC(actual_node, scratch_pool);
      actual_node->conflict_old = entry->conflict_old;
      actual_node->conflict_new = entry->conflict_new;
      actual_node->conflict_working = entry->conflict_wrk;
    }

  if (entry->prejfile)
    {
      actual_node = MAYBE_ALLOC(actual_node, scratch_pool);
      actual_node->prop_reject = entry->prejfile;
    }

  if (entry->changelist)
    {
      actual_node = MAYBE_ALLOC(actual_node, scratch_pool);
      actual_node->changelist = entry->changelist;
    }

  /* ### set the text_mod value? */

  if (entry->tree_conflict_data)
    {
      actual_node = MAYBE_ALLOC(actual_node, scratch_pool);
      actual_node->tree_conflict_data = entry->tree_conflict_data;
    }

  if (entry->file_external_path != NULL)
    {
      base_node = MAYBE_ALLOC(base_node, scratch_pool);
    }

  /* Insert the base node. */
  if (base_node)
    {
      base_node->wc_id = wc_id;
      base_node->local_relpath = name;
      if (*name == '\0')
        base_node->parent_relpath = NULL;
      else
        base_node->parent_relpath = "";
      base_node->revision = entry->revision;
      base_node->depth = entry->depth;
      base_node->last_mod_time = entry->text_time;
      base_node->translated_size = entry->working_size;

      if (entry->deleted)
        {
          base_node->presence = svn_wc__db_status_not_present;
          /* ### should be svn_node_unknown, but let's store what we have. */
          base_node->kind = entry->kind;
        }
      else
        base_node->kind = entry->kind;

      if (entry->kind == svn_node_dir)
        base_node->checksum = NULL;
      else
        SVN_ERR(svn_checksum_parse_hex(&base_node->checksum, svn_checksum_md5,
                                       entry->checksum, scratch_pool));

      if (repos_root)
        {
          base_node->repos_id = repos_id;

          /* repos_relpath is NOT a URI. decode as appropriate.  */
          if (entry->url != NULL)
            {
              const char *relative_url = svn_uri_is_child(repos_root,
                                                          entry->url,
                                                          scratch_pool);

              if (relative_url == NULL)
                base_node->repos_relpath = "";
              else
                base_node->repos_relpath = svn_path_uri_decode(relative_url,
                                                               scratch_pool);
            }
          else
            {
              const char *base_path = svn_uri_is_child(repos_root,
                                                       this_dir->url,
                                                       scratch_pool);
              if (base_path == NULL)
                base_node->repos_relpath = entry->name;
              else
                base_node->repos_relpath =
                  svn_dirent_join(svn_path_uri_decode(base_path, scratch_pool),
                                  entry->name,
                                  scratch_pool);
            }
        }

      /* TODO: These values should always be present, if they are missing
         during an upgrade, set a flag, and then ask the user to talk to the
         server.

         Note: cmt_rev is the distinguishing value. The others may be 0 or
         NULL if the corresponding revprop has been deleted.  */
      base_node->changed_rev = entry->cmt_rev;
      base_node->changed_date = entry->cmt_date;
      base_node->changed_author = entry->cmt_author;

      SVN_ERR(insert_base_node(wc_db, base_node, scratch_pool));

      /* We have to insert the lock after the base node, because the node
         must exist to lookup various bits of repos related information for
         the abs path. */
      if (entry->lock_token)
        {
          svn_wc__db_lock_t lock;

          lock.token = entry->lock_token;
          lock.owner = entry->lock_owner;
          lock.comment = entry->lock_comment;
          lock.date = entry->lock_creation_date;

          SVN_ERR(svn_wc__db_lock_add(db, entry_abspath, &lock, scratch_pool));
        }

      /* Now, update the file external information.
         ### This is a hack!  */
      if (entry->file_external_path)
        {
          svn_sqlite__stmt_t *stmt;
          const char *str;

          SVN_ERR(svn_wc__serialize_file_external(&str,
                                                  entry->file_external_path,
                                                  &entry->file_external_peg_rev,
                                                  &entry->file_external_rev,
                                                  scratch_pool));
       
          SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db,
                                            STMT_UPDATE_FILE_EXTERNAL));
          SVN_ERR(svn_sqlite__bindf(stmt, "iss",
                                    (apr_uint64_t)1 /* wc_id */,
                                    entry->name,
                                    str));
          SVN_ERR(svn_sqlite__step_done(stmt));
        }
    }

  /* Insert the working node. */
  if (working_node)
    {
      working_node->wc_id = wc_id;
      working_node->local_relpath = name;
      working_node->parent_relpath = "";
      working_node->depth = entry->depth;
      working_node->changed_rev = SVN_INVALID_REVNUM;
      working_node->last_mod_time = entry->text_time;
      working_node->translated_size = entry->working_size;

      if (entry->kind == svn_node_dir)
        working_node->checksum = NULL;
      else
        SVN_ERR(svn_checksum_parse_hex(&working_node->checksum,
                                       svn_checksum_md5,
                                       entry->checksum, scratch_pool));

      if (entry->schedule == svn_wc_schedule_delete)
        {
          working_node->presence = svn_wc__db_status_not_present;

          /* ### should be svn_node_unknown, but let's store what we have. */
          working_node->kind = entry->kind;
        }
      else
        {
          working_node->presence = svn_wc__db_status_normal;
          working_node->kind = entry->kind;
        }

      /* These should generally be unset for added and deleted files,
         and contain whatever information we have for copied files. Let's
         just store whatever we have.

         Note: cmt_rev is the distinguishing value. The others may be 0 or
         NULL if the corresponding revprop has been deleted.  */
      working_node->changed_rev = entry->cmt_rev;
      working_node->changed_date = entry->cmt_date;
      working_node->changed_author = entry->cmt_author;

      SVN_ERR(insert_working_node(wc_db, working_node, scratch_pool));
    }

  /* Insert the actual node. */
  if (actual_node)
    {
      actual_node->wc_id = wc_id;
      actual_node->local_relpath = name;
      actual_node->parent_relpath = "";

      SVN_ERR(insert_actual_node(wc_db, actual_node, scratch_pool));
    }

  svn_pool_destroy(scratch_pool);

  return SVN_NO_ERROR;
}

/* Actually do the sqlite work within a transaction.
   This implements svn_sqlite__transaction_callback_t */
static svn_error_t *
entries_write_body(svn_sqlite__db_t *wc_db,
                   apr_hash_t *entries,
                   svn_wc_adm_access_t *adm_access,
                   const svn_wc_entry_t *this_dir,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_hash_index_t *hi;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  const char *repos_root;
  apr_int64_t repos_id;
  apr_int64_t wc_id;
  const char *local_abspath;
  svn_wc__db_t *db;

  SVN_ERR(svn_wc__adm_get_db(&db, adm_access, scratch_pool));
  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  svn_wc_adm_access_path(adm_access),
                                  scratch_pool));

  /* Get the repos ID. */
  if (this_dir->uuid != NULL)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_SELECT_REPOSITORY));
      SVN_ERR(svn_sqlite__bindf(stmt, "s", this_dir->repos));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (have_row)
        {
          repos_id = svn_sqlite__column_int(stmt, 0);
          repos_root = svn_sqlite__column_text(stmt, 1, scratch_pool);
        }
      else
        {
          SVN_ERR(svn_sqlite__reset(stmt));

          /* Insert a new row in the REPOSITORY table for using this new,
             and currently unknown, repository.

             ### does this need to be done on a per-entry basis instead of
             ### the per-directory way we do it now?  me thinks yes...
             ###
             ### when do we harvest repository entries which no longer have
             ### any members?  */
          SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db,
                                            STMT_INSERT_REPOSITORY));
          SVN_ERR(svn_sqlite__bindf(stmt, "ss", this_dir->repos,
                                    this_dir->uuid));
          SVN_ERR(svn_sqlite__insert(&repos_id, stmt));
          repos_root = this_dir->repos;
        }

      SVN_ERR(svn_sqlite__reset(stmt));
    }
  else
    {
      repos_id = 0;
      repos_root = NULL;
    }

  /* Remove all WORKING, BASE and ACTUAL nodes for this directory, as well
     as locks, since we're about to replace 'em. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_ALL_WORKING));
  SVN_ERR(svn_sqlite__step_done(stmt));
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_ALL_BASE));
  SVN_ERR(svn_sqlite__step_done(stmt));
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_ALL_ACTUAL));
  SVN_ERR(svn_sqlite__step_done(stmt));
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_ALL_LOCK));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Write out "this dir" */
  SVN_ERR(fetch_wc_id(&wc_id, wc_db));
  SVN_ERR(write_entry(db, wc_db, wc_id, repos_id, repos_root, this_dir,
                      SVN_WC_ENTRY_THIS_DIR, local_abspath,
                      this_dir, scratch_pool));

  for (hi = apr_hash_first(scratch_pool, entries); hi;
        hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const svn_wc_entry_t *this_entry;

      svn_pool_clear(iterpool);

      /* Get the entry and make sure its attributes are up-to-date. */
      apr_hash_this(hi, &key, NULL, &val);
      this_entry = val;

      /* Don't rewrite the "this dir" entry! */
      if (strcmp(key, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      /* Write the entry. */
      SVN_ERR(write_entry(db, wc_db, wc_id, repos_id, repos_root,
                          this_entry, key,
                          svn_dirent_join(local_abspath, this_entry->name,
                                          iterpool),
                          this_dir, iterpool));
    }

  svn_pool_destroy(iterpool);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entries_write(apr_hash_t *entries,
                      svn_wc_adm_access_t *adm_access,
                      apr_pool_t *pool)
{
  svn_sqlite__db_t *wc_db;
  const svn_wc_entry_t *this_dir;
  apr_pool_t *scratch_pool = svn_pool_create(pool);

  if (svn_wc__adm_wc_format(adm_access) < SVN_WC__WC_NG_VERSION)
    return svn_wc__entries_write_old(entries, adm_access, pool);

  SVN_ERR(svn_wc__adm_write_check(adm_access, pool));

  /* Get a copy of the "this dir" entry for comparison purposes. */
  this_dir = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR,
                          APR_HASH_KEY_STRING);

  /* If there is no "this dir" entry, something is wrong. */
  if (! this_dir)
    return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                             _("No default entry in directory '%s'"),
                             svn_path_local_style
                             (svn_wc_adm_access_path(adm_access), pool));

  /* Open the wc.db sqlite database. */
  SVN_ERR(svn_sqlite__open(&wc_db,
                           db_path(svn_wc_adm_access_path(adm_access), pool),
                           svn_sqlite__mode_readwrite,
                           statements,
                           SVN_WC__VERSION_EXPERIMENTAL, upgrade_sql,
                           scratch_pool, scratch_pool));

  /* Write the entries. */
  SVN_ERR(entries_write_body(wc_db, entries, adm_access, this_dir,
                             scratch_pool));

  svn_wc__adm_access_set_entries(adm_access, entries);

  svn_pool_destroy(scratch_pool);
  return SVN_NO_ERROR;
}


/* Update an entry NAME in ENTRIES, according to the combination of
   entry data found in ENTRY and masked by MODIFY_FLAGS. If the entry
   already exists, the requested changes will be folded (merged) into
   the entry's existing state.  If the entry doesn't exist, the entry
   will be created with exactly those properties described by the set
   of changes. Also cleanups meaningless fields combinations.

   The SVN_WC__ENTRY_MODIFY_FORCE flag is ignored.

   POOL may be used to allocate memory referenced by ENTRIES.
 */
static svn_error_t *
fold_entry(apr_hash_t *entries,
           const char *name,
           apr_uint64_t modify_flags,
           const svn_wc_entry_t *entry,
           apr_pool_t *pool)
{
  svn_wc_entry_t *cur_entry
    = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

  SVN_ERR_ASSERT(name != NULL);

  if (! cur_entry)
    cur_entry = alloc_entry(pool);

  /* Name (just a safeguard here, really) */
  if (! cur_entry->name)
    cur_entry->name = apr_pstrdup(pool, name);

  /* Revision */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_REVISION)
    cur_entry->revision = entry->revision;

  /* Ancestral URL in repository */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_URL)
    cur_entry->url = entry->url ? apr_pstrdup(pool, entry->url) : NULL;

  /* Repository root */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_REPOS)
    cur_entry->repos = entry->repos ? apr_pstrdup(pool, entry->repos) : NULL;

  /* Kind */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_KIND)
    cur_entry->kind = entry->kind;

  /* Schedule */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE)
    cur_entry->schedule = entry->schedule;

  /* Checksum */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CHECKSUM)
    cur_entry->checksum = entry->checksum
      ? apr_pstrdup(pool, entry->checksum)
                          : NULL;

  /* Copy-related stuff */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_COPIED)
    cur_entry->copied = entry->copied;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_COPYFROM_URL)
    cur_entry->copyfrom_url = entry->copyfrom_url
      ? apr_pstrdup(pool, entry->copyfrom_url)
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_COPYFROM_REV)
    cur_entry->copyfrom_rev = entry->copyfrom_rev;

  /* Deleted state */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_DELETED)
    cur_entry->deleted = entry->deleted;

  /* Absent state */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_ABSENT)
    cur_entry->absent = entry->absent;

  /* Incomplete state */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_INCOMPLETE)
    cur_entry->incomplete = entry->incomplete;

  /* Text/prop modification times */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_TEXT_TIME)
    cur_entry->text_time = entry->text_time;

  /* Conflict stuff */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CONFLICT_OLD)
    cur_entry->conflict_old = entry->conflict_old
      ? apr_pstrdup(pool, entry->conflict_old)
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CONFLICT_NEW)
    cur_entry->conflict_new = entry->conflict_new
      ? apr_pstrdup(pool, entry->conflict_new)
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CONFLICT_WRK)
    cur_entry->conflict_wrk = entry->conflict_wrk
      ? apr_pstrdup(pool, entry->conflict_wrk)
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_PREJFILE)
    cur_entry->prejfile = entry->prejfile
      ? apr_pstrdup(pool, entry->prejfile)
                          : NULL;

  /* Last-commit stuff */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CMT_REV)
    cur_entry->cmt_rev = entry->cmt_rev;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CMT_DATE)
    cur_entry->cmt_date = entry->cmt_date;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CMT_AUTHOR)
    cur_entry->cmt_author = entry->cmt_author
      ? apr_pstrdup(pool, entry->cmt_author)
                            : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_UUID)
    cur_entry->uuid = entry->uuid
      ? apr_pstrdup(pool, entry->uuid)
                            : NULL;

  /* Lock token */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_LOCK_TOKEN)
    cur_entry->lock_token = (entry->lock_token
                             ? apr_pstrdup(pool, entry->lock_token)
                             : NULL);

  /* Lock owner */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_LOCK_OWNER)
    cur_entry->lock_owner = (entry->lock_owner
                             ? apr_pstrdup(pool, entry->lock_owner)
                             : NULL);

  /* Lock comment */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_LOCK_COMMENT)
    cur_entry->lock_comment = (entry->lock_comment
                               ? apr_pstrdup(pool, entry->lock_comment)
                               : NULL);

  /* Lock creation date */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_LOCK_CREATION_DATE)
    cur_entry->lock_creation_date = entry->lock_creation_date;

  /* Changelist */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CHANGELIST)
    cur_entry->changelist = (entry->changelist
                             ? apr_pstrdup(pool, entry->changelist)
                             : NULL);

  /* has-props, prop-mods, cachable-props, and present-props are deprecated,
     so we do not copy them. */

  if (modify_flags & SVN_WC__ENTRY_MODIFY_KEEP_LOCAL)
    cur_entry->keep_local = entry->keep_local;

  /* Note that we don't bother to fold entry->depth, because it is
     only meaningful on the this-dir entry anyway. */

  /* Tree conflict data. */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_TREE_CONFLICT_DATA)
    cur_entry->tree_conflict_data = entry->tree_conflict_data
      ? apr_pstrdup(pool, entry->tree_conflict_data)
                              : NULL;

  /* Absorb defaults from the parent dir, if any, unless this is a
     subdir entry. */
  if (cur_entry->kind != svn_node_dir)
    {
      svn_wc_entry_t *default_entry
        = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
      if (default_entry)
        take_from_entry(default_entry, cur_entry, pool);
    }

  /* Cleanup meaningless fields */

  /* ### svn_wc_schedule_delete is the minimal value. We need it because it's
     impossible to NULLify copyfrom_url with log-instructions.

     Note that I tried to find the smallest collection not to clear these
     fields for, but this condition still fails the test suite:

     !(entry->schedule == svn_wc_schedule_add
       || entry->schedule == svn_wc_schedule_replace
       || (entry->schedule == svn_wc_schedule_normal && entry->copied)))

  */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE
      && entry->schedule == svn_wc_schedule_delete)
    {
      cur_entry->copied = FALSE;
      cur_entry->copyfrom_rev = SVN_INVALID_REVNUM;
      cur_entry->copyfrom_url = NULL;
    }

  if (modify_flags & SVN_WC__ENTRY_MODIFY_WORKING_SIZE)
    cur_entry->working_size = entry->working_size;

  /* keep_local makes sense only when we are going to delete directory. */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE
      && entry->schedule != svn_wc_schedule_delete)
    {
      cur_entry->keep_local = FALSE;
    }

  /* File externals. */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_FILE_EXTERNAL)
    {
      cur_entry->file_external_path = (entry->file_external_path
                                       ? apr_pstrdup(pool,
                                                     entry->file_external_path)
                                       : NULL);
      cur_entry->file_external_peg_rev = entry->file_external_peg_rev;
      cur_entry->file_external_rev = entry->file_external_rev;
    }

  /* Make sure the entry exists in the entries hash.  Possibly it
     already did, in which case this could have been skipped, but what
     the heck. */
  apr_hash_set(entries, cur_entry->name, APR_HASH_KEY_STRING, cur_entry);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entry_remove(apr_hash_t *entries,
                     svn_wc_adm_access_t *adm_access,
                     const char *name,
                     svn_boolean_t write_to_disk,
                     apr_pool_t *scratch_pool)
{
  if (entries == NULL)
    {
      assert(write_to_disk);
      SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE, scratch_pool));
    }

  apr_hash_set(entries, name, APR_HASH_KEY_STRING, NULL);

  if (write_to_disk)
    SVN_ERR(svn_wc__entries_write(entries, adm_access, scratch_pool));

  return SVN_NO_ERROR;
}


/* Our general purpose intelligence module for handling a scheduling
   change to a single entry.

   Given an entryname NAME in ENTRIES, examine the caller's requested
   scheduling change in *SCHEDULE and the current state of the entry.
   *MODIFY_FLAGS should have the 'SCHEDULE' flag set (else do nothing) and
   may have the 'FORCE' flag set (in which case do nothing).
   Determine the final schedule for the entry. Output the result by doing
   none or any or all of: delete the entry from *ENTRIES, change *SCHEDULE
   to the new schedule, remove the 'SCHEDULE' change flag from
   *MODIFY_FLAGS.

   POOL is used for local allocations only, calling this function does not
   use POOL to allocate any memory referenced by ENTRIES.
 */
static svn_error_t *
fold_scheduling(apr_hash_t *entries,
                const char *name,
                apr_uint64_t *modify_flags,
                svn_wc_schedule_t *schedule,
                apr_pool_t *pool)
{
  svn_wc_entry_t *entry, *this_dir_entry;

  /* If we're not supposed to be bothering with this anyway...return. */
  if (! (*modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE))
    return SVN_NO_ERROR;

  /* Get the current entry */
  entry = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

  /* If we're not merging in changes, the requested schedule is the final
     schedule. */
  if (*modify_flags & SVN_WC__ENTRY_MODIFY_FORCE)
    return SVN_NO_ERROR;

  /* The only operation valid on an item not already in revision
     control is addition. */
  if (! entry)
    {
      if (*schedule == svn_wc_schedule_add)
        return SVN_NO_ERROR;
      else
        return
          svn_error_createf(SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
                            _("'%s' is not under version control"),
                            name);
    }

  /* Get the default entry */
  this_dir_entry = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR,
                                APR_HASH_KEY_STRING);

  /* At this point, we know the following things:

     1. There is already an entry for this item in the entries file
        whose existence is either _normal or _added (or about to
        become such), which for our purposes mean the same thing.

     2. We have been asked to merge in a state change, not to
        explicitly set the state.  */

  /* Here are some cases that are parent-directory sensitive.
     Basically, we make sure that we are not allowing versioned
     resources to just sorta dangle below directories marked for
     deletion. */
  if ((entry != this_dir_entry)
      && (this_dir_entry->schedule == svn_wc_schedule_delete))
    {
      if (*schedule == svn_wc_schedule_add)
        return
          svn_error_createf(SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
                            _("Can't add '%s' to deleted directory; "
                              "try undeleting its parent directory first"),
                            name);
      if (*schedule == svn_wc_schedule_replace)
        return
          svn_error_createf(SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
                            _("Can't replace '%s' in deleted directory; "
                              "try undeleting its parent directory first"),
                            name);
    }

  if (entry->absent && (*schedule == svn_wc_schedule_add))
    {
      return svn_error_createf
        (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
         _("'%s' is marked as absent, so it cannot be scheduled for addition"),
         name);
    }

  switch (entry->schedule)
    {
    case svn_wc_schedule_normal:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
          /* Normal is a trivial no-op case. Reset the
             schedule modification bit and move along. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;


        case svn_wc_schedule_delete:
        case svn_wc_schedule_replace:
          /* These are all good. */
          return SVN_NO_ERROR;


        case svn_wc_schedule_add:
          /* You can't add something that's already been added to
             revision control... unless it's got a 'deleted' state */
          if (! entry->deleted)
            return
              svn_error_createf
              (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
               _("Entry '%s' is already under version control"), name);
        }
      break;

    case svn_wc_schedule_add:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
        case svn_wc_schedule_add:
        case svn_wc_schedule_replace:
          /* These are all no-op cases.  Normal is obvious, as is add.
               ### The 'add' case is not obvious: above, we throw an error if
               ### already versioned, so why not here too?
             Replace on an entry marked for addition breaks down to
             (add + (delete + add)), which resolves to just (add), and
             since this entry is already marked with (add), this too
             is a no-op. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;


        case svn_wc_schedule_delete:
          /* Not-yet-versioned item being deleted.  If the original
             entry was not marked as "deleted", then remove the entry.
             Else, return the entry to a 'normal' state, preserving
               ### What does it mean for an entry be schedule-add and
               ### deleted at once, and why change schedule to normal?
             the "deleted" flag.  Check that we are not trying to
             remove the SVN_WC_ENTRY_THIS_DIR entry as that would
             leave the entries file in an invalid state. */
          SVN_ERR_ASSERT(entry != this_dir_entry);
          if (! entry->deleted)
            apr_hash_set(entries, name, APR_HASH_KEY_STRING, NULL);
          else
            *schedule = svn_wc_schedule_normal;
          return SVN_NO_ERROR;
        }
      break;

    case svn_wc_schedule_delete:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
          /* Reverting a delete results in normal */
          return SVN_NO_ERROR;

        case svn_wc_schedule_delete:
          /* These are no-op cases. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;


        case svn_wc_schedule_add:
          /* Re-adding an entry marked for deletion?  This is really a
             replace operation. */
          *schedule = svn_wc_schedule_replace;
          return SVN_NO_ERROR;


        case svn_wc_schedule_replace:
          /* Replacing an item marked for deletion breaks down to
             (delete + (delete + add)), which might deserve a warning,
             but whatever. */
          return SVN_NO_ERROR;

        }
      break;

    case svn_wc_schedule_replace:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
          /* Reverting replacements results normal. */
          return SVN_NO_ERROR;

        case svn_wc_schedule_add:
          /* Adding a to-be-replaced entry breaks down to ((delete +
             add) + add) which might deserve a warning, but we'll just
             no-op it. */
        case svn_wc_schedule_replace:
          /* Replacing a to-be-replaced entry breaks down to ((delete
             + add) + (delete + add)), which is insane!  Make up your
             friggin' mind, dude! :-)  Well, we'll no-op this one,
             too. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;


        case svn_wc_schedule_delete:
          /* Deleting a to-be-replaced entry breaks down to ((delete +
             add) + delete) which resolves to a flat deletion. */
          *schedule = svn_wc_schedule_delete;
          return SVN_NO_ERROR;

        }
      break;

    default:
      return
        svn_error_createf
        (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
         _("Entry '%s' has illegal schedule"), name);
    }
  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc__entry_modify(svn_wc_adm_access_t *adm_access,
                     const char *name,
                     svn_wc_entry_t *entry,
                     apr_uint64_t modify_flags,
                     svn_boolean_t do_sync,
                     apr_pool_t *pool)
{
  apr_hash_t *entries;
  svn_boolean_t entry_was_deleted_p = FALSE;

  SVN_ERR_ASSERT(entry);

  /* Load ADM_ACCESS's whole entries file. */
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE, pool));

  /* Ensure that NAME is valid. */
  if (name == NULL)
    name = SVN_WC_ENTRY_THIS_DIR;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE)
    {
      const svn_wc_entry_t *entry_before;
      const svn_wc_entry_t *entry_after;

      /* Keep a copy of the unmodified entry on hand. */
      entry_before = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

      /* If scheduling changes were made, we have a special routine to
         manage those modifications. */
      SVN_ERR(fold_scheduling(entries, name, &modify_flags,
                              &entry->schedule, pool));

      /* Special case:  fold_state_changes() may have actually REMOVED
         the entry in question!  If so, don't try to fold_entry, as
         this will just recreate the entry again. */
      entry_after = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

      /* Note if this entry was deleted above so we don't accidentally
         re-add it in the following steps. */
      if (entry_before && (! entry_after))
        entry_was_deleted_p = TRUE;
    }

  /* If the entry wasn't just removed from the entries hash, fold the
     changes into the entry. */
  if (! entry_was_deleted_p)
    {
      SVN_ERR(fold_entry(entries, name, modify_flags, entry,
                         svn_wc_adm_access_pool(adm_access)));
    }

  /* Sync changes to disk. */
  if (do_sync)
    SVN_ERR(svn_wc__entries_write(entries, adm_access, pool));

  return SVN_NO_ERROR;
}


svn_wc_entry_t *
svn_wc_entry_dup(const svn_wc_entry_t *entry, apr_pool_t *pool)
{
  svn_wc_entry_t *dupentry = apr_palloc(pool, sizeof(*dupentry));

  /* Perform a trivial copy ... */
  *dupentry = *entry;

  /* ...and then re-copy stuff that needs to be duped into our pool. */
  if (entry->name)
    dupentry->name = apr_pstrdup(pool, entry->name);
  if (entry->url)
    dupentry->url = apr_pstrdup(pool, entry->url);
  if (entry->repos)
    dupentry->repos = apr_pstrdup(pool, entry->repos);
  if (entry->uuid)
    dupentry->uuid = apr_pstrdup(pool, entry->uuid);
  if (entry->copyfrom_url)
    dupentry->copyfrom_url = apr_pstrdup(pool, entry->copyfrom_url);
  if (entry->conflict_old)
    dupentry->conflict_old = apr_pstrdup(pool, entry->conflict_old);
  if (entry->conflict_new)
    dupentry->conflict_new = apr_pstrdup(pool, entry->conflict_new);
  if (entry->conflict_wrk)
    dupentry->conflict_wrk = apr_pstrdup(pool, entry->conflict_wrk);
  if (entry->prejfile)
    dupentry->prejfile = apr_pstrdup(pool, entry->prejfile);
  if (entry->checksum)
    dupentry->checksum = apr_pstrdup(pool, entry->checksum);
  if (entry->cmt_author)
    dupentry->cmt_author = apr_pstrdup(pool, entry->cmt_author);
  if (entry->lock_token)
    dupentry->lock_token = apr_pstrdup(pool, entry->lock_token);
  if (entry->lock_owner)
    dupentry->lock_owner = apr_pstrdup(pool, entry->lock_owner);
  if (entry->lock_comment)
    dupentry->lock_comment = apr_pstrdup(pool, entry->lock_comment);
  if (entry->changelist)
    dupentry->changelist = apr_pstrdup(pool, entry->changelist);

  /* NOTE: we do not dup cachable_props or present_props since they
     are deprecated. Use "" to indicate "nothing cachable or cached". */
  dupentry->cachable_props = "";
  dupentry->present_props = "";

  if (entry->tree_conflict_data)
    dupentry->tree_conflict_data = apr_pstrdup(pool,
                                               entry->tree_conflict_data);
  if (entry->file_external_path)
    dupentry->file_external_path = apr_pstrdup(pool,
                                               entry->file_external_path);
  return dupentry;
}


svn_error_t *
svn_wc__tweak_entry(svn_wc_adm_access_t *adm_access,
                    apr_hash_t *entries,
                    const char *name,
                    const char *new_url,
                    const char *repos,
                    svn_revnum_t new_rev,
                    svn_boolean_t allow_removal,
                    svn_boolean_t write_to_disk,
                    apr_pool_t *scratch_pool)
{
  apr_pool_t *state_pool = svn_wc_adm_access_pool(adm_access);
  svn_wc_entry_t *entry;

  if (entries == NULL)
    {
      assert(write_to_disk);
      SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE, scratch_pool));
    }

  entry = apr_hash_get(entries, name, APR_HASH_KEY_STRING);
  if (! entry)
    return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                             _("No such entry: '%s'"), name);

  if (new_url != NULL
      && (! entry->url || strcmp(new_url, entry->url)))
    {
      entry->url = apr_pstrdup(state_pool, new_url);
    }

  if (repos != NULL
      && (! entry->repos || strcmp(repos, entry->repos))
      && entry->url
      && svn_path_is_ancestor(repos, entry->url))
    {
      svn_boolean_t set_repos = TRUE;

      /* Setting the repository root on THIS_DIR will make files in this
         directory inherit that property.  So to not make the WC corrupt,
         we have to make sure that the repos root is valid for such entries as
         well.  Note that this shouldn't happen in normal circumstances. */
      if (strcmp(entry->name, SVN_WC_ENTRY_THIS_DIR) == 0)
        {
          apr_hash_index_t *hi;
          for (hi = apr_hash_first(scratch_pool, entries); hi;
               hi = apr_hash_next(hi))
            {
              void *value;
              const svn_wc_entry_t *child_entry;

              apr_hash_this(hi, NULL, NULL, &value);
              child_entry = value;

              if (! child_entry->repos && child_entry->url
                  && ! svn_path_is_ancestor(repos, child_entry->url))
                {
                  set_repos = FALSE;
                  break;
                }
            }
        }

      if (set_repos)
        {
          entry->repos = apr_pstrdup(state_pool, repos);
        }
    }

  if ((SVN_IS_VALID_REVNUM(new_rev))
      && (entry->schedule != svn_wc_schedule_add)
      && (entry->schedule != svn_wc_schedule_replace)
      && (entry->copied != TRUE)
      && (entry->revision != new_rev))
    {
      entry->revision = new_rev;
    }

  /* As long as this function is only called as a helper to
     svn_wc__do_update_cleanup, then it's okay to remove any entry
     under certain circumstances:

     If the entry is still marked 'deleted', then the server did not
     re-add it.  So it's really gone in this revision, thus we remove
     the entry.

     If the entry is still marked 'absent' and yet is not the same
     revision as new_rev, then the server did not re-add it, nor
     re-absent it, so we can remove the entry.

     ### This function cannot always determine whether removal is
     ### appropriate, hence the ALLOW_REMOVAL flag.  It's all a bit of a
     ### mess. */
  if (allow_removal
      && (entry->deleted || (entry->absent && entry->revision != new_rev)))
    {
      apr_hash_set(entries, name, APR_HASH_KEY_STRING, NULL);
    }

  if (write_to_disk)
    SVN_ERR(svn_wc__entries_write(entries, adm_access, scratch_pool));

  return SVN_NO_ERROR;
}




svn_error_t *
svn_wc__entries_init(const char *path,
                     const char *uuid,
                     const char *url,
                     const char *repos_root,
                     svn_revnum_t initial_rev,
                     svn_depth_t depth,
                     apr_pool_t *pool)
{
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  const char *abspath;
  const char *repos_relpath;

  if (!should_create_next_gen())
    return svn_wc__entries_init_old(path, uuid, url, repos_root, initial_rev,
                                    depth, pool);

  SVN_ERR_ASSERT(! repos_root || svn_path_is_ancestor(repos_root, url));
  SVN_ERR_ASSERT(depth == svn_depth_empty
                 || depth == svn_depth_files
                 || depth == svn_depth_immediates
                 || depth == svn_depth_infinity);

  /* Initialize the db for this directory. */
  SVN_ERR(svn_dirent_get_absolute(&abspath, path, scratch_pool));
  repos_relpath = svn_uri_is_child(repos_root, url, scratch_pool);
  SVN_ERR(svn_wc__db_init(abspath, repos_relpath == NULL ? ""
                            : svn_path_uri_decode(repos_relpath, scratch_pool),
                          repos_root, uuid, initial_rev, depth, scratch_pool));

  svn_pool_destroy(scratch_pool);
  return SVN_NO_ERROR;
}


/*--------------------------------------------------------------- */

/*** Generic Entry Walker */


/* A recursive entry-walker, helper for svn_wc_walk_entries3().
 *
 * For this directory (DIRPATH, ADM_ACCESS), call the "found_entry" callback
 * in WALK_CALLBACKS, passing WALK_BATON to it. Then, for each versioned
 * entry in this directory, call the "found entry" callback and then recurse
 * (if it is a directory and if DEPTH allows).
 *
 * If SHOW_HIDDEN is true, include entries that are in a 'deleted' or
 * 'absent' state (and not scheduled for re-addition), else skip them.
 *
 * Call CANCEL_FUNC with CANCEL_BATON to allow cancellation.
 */
static svn_error_t *
walker_helper(const char *dirpath,
              svn_wc_adm_access_t *adm_access,
              const svn_wc_entry_callbacks2_t *walk_callbacks,
              void *walk_baton,
              svn_depth_t depth,
              svn_boolean_t show_hidden,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create(pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_wc_entry_t *dot_entry;
  svn_error_t *err;

  err = svn_wc_entries_read(&entries, adm_access, show_hidden, pool);

  if (err)
    SVN_ERR(walk_callbacks->handle_error(dirpath, err, walk_baton, pool));

  /* As promised, always return the '.' entry first. */
  dot_entry = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR,
                           APR_HASH_KEY_STRING);
  if (! dot_entry)
    return walk_callbacks->handle_error
      (dirpath, svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                                  _("Directory '%s' has no THIS_DIR entry"),
                                  svn_path_local_style(dirpath, pool)),
       walk_baton, pool);

  /* Call the "found entry" callback for this directory as a "this dir"
   * entry. Note that if this directory has been reached by recusrion, this
   * is the second visit as it will already have been visited once as a
   * child entry of its parent. */

  err = walk_callbacks->found_entry(dirpath, dot_entry, walk_baton, pool);


  if(err)
    SVN_ERR(walk_callbacks->handle_error(dirpath, err, walk_baton, pool));

  if (depth == svn_depth_empty)
    return SVN_NO_ERROR;

  /* Loop over each of the other entries. */
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const svn_wc_entry_t *current_entry;
      const char *entrypath;

      svn_pool_clear(subpool);

      /* See if someone wants to cancel this operation. */
      if (cancel_func)
        SVN_ERR(cancel_func(cancel_baton));

      apr_hash_this(hi, &key, NULL, &val);
      current_entry = val;

      /* Skip the "this dir" entry. */
      if (strcmp(current_entry->name, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      entrypath = svn_dirent_join(dirpath, key, subpool);

      /* Call the "found entry" callback for this entry. (For a directory,
       * this is the first visit: as a child.) */
      if (current_entry->kind == svn_node_file
          || depth >= svn_depth_immediates)
        {
          err = walk_callbacks->found_entry(entrypath, current_entry,
                                            walk_baton, subpool);

          if (err)
            SVN_ERR(walk_callbacks->handle_error(entrypath, err,
                                                 walk_baton, pool));
        }

      /* Recurse into this entry if appropriate. */
      if (current_entry->kind == svn_node_dir
          && !entry_is_hidden(current_entry)
          && depth >= svn_depth_immediates)
        {
          svn_wc_adm_access_t *entry_access;
          svn_depth_t depth_below_here = depth;

          if (depth == svn_depth_immediates)
            depth_below_here = svn_depth_empty;

          err = svn_wc_adm_retrieve(&entry_access, adm_access, entrypath,
                                    subpool);
          
          if (err)
            SVN_ERR(walk_callbacks->handle_error(entrypath, err,
                                                 walk_baton, pool));

          if (entry_access)
            SVN_ERR(walker_helper(entrypath, entry_access,
                                  walk_callbacks, walk_baton,
                                  depth_below_here, show_hidden,
                                  cancel_func, cancel_baton,
                                  subpool));
        }
    }

  svn_pool_destroy(subpool);
  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__walker_default_error_handler(const char *path,
                                     svn_error_t *err,
                                     void *walk_baton,
                                     apr_pool_t *pool)
{
  return err;
}


/* The public API. */
svn_error_t *
svn_wc_walk_entries3(const char *path,
                     svn_wc_adm_access_t *adm_access,
                     const svn_wc_entry_callbacks2_t *walk_callbacks,
                     void *walk_baton,
                     svn_depth_t depth,
                     svn_boolean_t show_hidden,
                     svn_cancel_func_t cancel_func,
                     void *cancel_baton,
                     apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;

  SVN_ERR(svn_wc_entry(&entry, path, adm_access, show_hidden, pool));

  if (! entry)
    return walk_callbacks->handle_error
      (path, svn_error_createf(SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                               _("'%s' is not under version control"),
                               svn_path_local_style(path, pool)),
       walk_baton, pool);

  if (entry->kind == svn_node_file || entry->depth == svn_depth_exclude)
    {
      svn_error_t *err = walk_callbacks->found_entry(path, entry, walk_baton,
                                                     pool);

      if (err)
        return walk_callbacks->handle_error(path, err, walk_baton, pool);

      return SVN_NO_ERROR;
    }

  else if (entry->kind == svn_node_dir)
    return walker_helper(path, adm_access, walk_callbacks, walk_baton,
                         depth, show_hidden, cancel_func, cancel_baton, pool);

  else
    return walk_callbacks->handle_error
      (path, svn_error_createf(SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                               _("'%s' has an unrecognized node kind"),
                               svn_path_local_style(path, pool)),
       walk_baton, pool);
}


/* A baton for use with visit_tc_too_callbacks. */
typedef struct visit_tc_too_baton_t
  {
    svn_wc_adm_access_t *adm_access;
    const svn_wc_entry_callbacks2_t *callbacks;
    void *baton;
    const char *target;
    svn_depth_t depth;
  } visit_tc_too_baton_t;

/* An svn_wc_entry_callbacks2_t callback function.
 *
 * Call the user's "found entry" callback
 * WALK_BATON->callbacks->found_entry(), passing it PATH, ENTRY and
 * WALK_BATON->baton. Then call it once for each unversioned tree-conflicted
 * child of this entry, passing it the child path, a null "entry", and
 * WALK_BATON->baton. WALK_BATON is of type (visit_tc_too_baton_t *).
 */
static svn_error_t *
visit_tc_too_found_entry(const char *path,
                         const svn_wc_entry_t *entry,
                         void *walk_baton,
                         apr_pool_t *pool)
{
  struct visit_tc_too_baton_t *baton = walk_baton;
  svn_boolean_t check_children;

  /* Call the entry callback for this entry. */
  SVN_ERR(baton->callbacks->found_entry(path, entry, baton->baton, pool));

  if (entry->kind != svn_node_dir || entry_is_hidden(entry))
    return SVN_NO_ERROR;

  /* If this is a directory, we may need to also visit any unversioned
   * children that are tree conflict victims. However, that should not
   * happen when we've already reached the requested depth. */

  switch (baton->depth){
    case svn_depth_empty:
      check_children = FALSE;
      break;

    /* Since svn_depth_files only visits files and this is a directory,
     * we have to be at the target. Just verify that anyway: */
    case svn_depth_files:
    case svn_depth_immediates:
      /* Check if this already *is* an immediate child, in which
       * case we shouldn't descend further. */
      check_children = (strcmp(baton->target, path) == 0);
      break;

    case svn_depth_infinity:
    case svn_depth_exclude:
    case svn_depth_unknown:
      check_children = TRUE;
      break;
  };

  if (check_children)
    {
      /* We're supposed to check the children of this directory. However,
       * in case of svn_depth_files, don't visit directories. */

      svn_wc_adm_access_t *adm_access = NULL;
      apr_array_header_t *conflicts;
      int i;

      /* Loop through all the tree conflict victims */
      SVN_ERR(svn_wc__read_tree_conflicts(&conflicts,
                                          entry->tree_conflict_data, path,
                                          pool));

      if (conflicts->nelts > 0)
        SVN_ERR(svn_wc_adm_retrieve(&adm_access, baton->adm_access, path,
                                    pool));

      for (i = 0; i < conflicts->nelts; i++)
        {
          svn_wc_conflict_description_t *conflict
            = APR_ARRAY_IDX(conflicts, i, svn_wc_conflict_description_t *);
          const svn_wc_entry_t *child_entry;

          if ((conflict->node_kind == svn_node_dir)
              && (baton->depth == svn_depth_files))
            continue;

          /* If this victim is not in this dir's entries ... */
          SVN_ERR(svn_wc_entry(&child_entry, conflict->path, adm_access,
                               TRUE, pool));
          if (!child_entry || child_entry->deleted)
            {
              /* Found an unversioned tree conflict victim. Call the "found
               * entry" callback with a null "entry" parameter. */
              SVN_ERR(baton->callbacks->found_entry(conflict->path, NULL,
                                                    baton->baton, pool));
            }
        }
    }

  return SVN_NO_ERROR;
}

/* An svn_wc_entry_callbacks2_t callback function.
 *
 * If the error ERR is because this PATH is an unversioned tree conflict
 * victim, call the user's "found entry" callback
 * WALK_BATON->callbacks->found_entry(), passing it this PATH, a null
 * "entry" parameter, and WALK_BATON->baton. Otherwise, forward this call
 * to the user's "handle error" callback
 * WALK_BATON->callbacks->handle_error().
 */
static svn_error_t *
visit_tc_too_error_handler(const char *path,
                           svn_error_t *err,
                           void *walk_baton,
                           apr_pool_t *pool)
{
  struct visit_tc_too_baton_t *baton = walk_baton;

  /* If this is an unversioned tree conflict victim, call the "found entry"
   * callback. This can occur on the root node of the walk; we do not expect
   * to reach such a node by recursion. */
  if (err && (err->apr_err == SVN_ERR_UNVERSIONED_RESOURCE))
    {
      svn_wc_adm_access_t *adm_access;
      svn_wc_conflict_description_t *conflict;
      char *parent_path = svn_dirent_dirname(path, pool);

      /* See if there is any tree conflict on this path. */
      SVN_ERR(svn_wc_adm_retrieve(&adm_access, baton->adm_access, parent_path,
                                  pool));
      SVN_ERR(svn_wc__get_tree_conflict(&conflict, path, adm_access, pool));

      /* If so, don't regard it as an error but call the "found entry"
       * callback with a null "entry" parameter. */
      if (conflict)
        {
          svn_error_clear(err);
          err = NULL;

          SVN_ERR(baton->callbacks->found_entry(conflict->path, NULL,
                                                baton->baton, pool));
        }
    }

  /* Call the user's error handler for this entry. */
  return baton->callbacks->handle_error(path, err, baton->baton, pool);
}

/* Callbacks used by svn_wc_walk_entries_and_tc(). */
static const svn_wc_entry_callbacks2_t
visit_tc_too_callbacks =
  {
    visit_tc_too_found_entry,
    visit_tc_too_error_handler
  };

svn_error_t *
svn_wc__walk_entries_and_tc(const char *path,
                            svn_wc_adm_access_t *adm_access,
                            const svn_wc_entry_callbacks2_t *walk_callbacks,
                            void *walk_baton,
                            svn_depth_t depth,
                            svn_cancel_func_t cancel_func,
                            void *cancel_baton,
                            apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_adm_access_t *path_adm_access;
  const svn_wc_entry_t *entry;

  /* If there is no adm_access, there are no nodes to visit, not even 'path'
   * because it can't be in conflict. */
  if (adm_access == NULL)
    return SVN_NO_ERROR;

  /* Is 'path' versioned? Set path_adm_access accordingly. */
  /* First: Get item's adm access (meaning parent's if it's a file). */
  err = svn_wc_adm_probe_retrieve(&path_adm_access, adm_access, path, pool);
  if (err && err->apr_err == SVN_ERR_WC_NOT_LOCKED)
    {
      /* Item is unversioned and doesn't have a versioned parent so there is
       * nothing to walk. */
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else if (err)
    return err;
  /* If we can get the item's entry then it is versioned. */
  err = svn_wc_entry(&entry, path, path_adm_access, TRUE, pool);
  if (err)
    {
      svn_error_clear(err);
      /* Indicate that it is unversioned. */
      entry = NULL;
    }

  /* If this path is versioned, do a tree walk, else perhaps call the
   * "unversioned tree conflict victim" callback directly. */
  if (entry)
    {
      /* Versioned, so use the regular entries walker with callbacks that
       * make it also visit unversioned tree conflict victims. */
      visit_tc_too_baton_t visit_tc_too_baton;

      visit_tc_too_baton.adm_access = adm_access;
      visit_tc_too_baton.callbacks = walk_callbacks;
      visit_tc_too_baton.baton = walk_baton;
      visit_tc_too_baton.target = path;
      visit_tc_too_baton.depth = depth;

      SVN_ERR(svn_wc_walk_entries3(path, path_adm_access,
                                   &visit_tc_too_callbacks, &visit_tc_too_baton,
                                   depth, TRUE /*show_hidden*/,
                                   cancel_func, cancel_baton, pool));
    }
  else
    {
      /* Not locked, so assume unversioned. If it is a tree conflict victim,
       * call the "found entry" callback with a null "entry" parameter. */
      svn_wc_conflict_description_t *conflict;

      SVN_ERR(svn_wc__get_tree_conflict(&conflict, path, adm_access, pool));
      if (conflict)
        SVN_ERR(walk_callbacks->found_entry(path, NULL, walk_baton, pool));
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_mark_missing_deleted(const char *path,
                            svn_wc_adm_access_t *parent,
                            apr_pool_t *pool)
{
  svn_node_kind_t pkind;

  SVN_ERR(svn_io_check_path(path, &pkind, pool));

  if (pkind == svn_node_none)
    {
      const char *parent_path, *bname;
      svn_wc_adm_access_t *adm_access;
      svn_wc_entry_t newent;

      newent.deleted = TRUE;
      newent.schedule = svn_wc_schedule_normal;

      svn_dirent_split(path, &parent_path, &bname, pool);

      SVN_ERR(svn_wc_adm_retrieve(&adm_access, parent, parent_path, pool));
      return svn_wc__entry_modify(adm_access, bname, &newent,
                                   (SVN_WC__ENTRY_MODIFY_DELETED
                                    | SVN_WC__ENTRY_MODIFY_SCHEDULE
                                    | SVN_WC__ENTRY_MODIFY_FORCE),
                                   TRUE, /* sync right away */ pool);
    }
  else
    return svn_error_createf(SVN_ERR_WC_PATH_FOUND, NULL,
                             _("Unexpectedly found '%s': "
                               "path is marked 'missing'"),
                             svn_path_local_style(path, pool));
}
