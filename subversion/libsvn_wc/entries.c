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

#define MAYBE_ALLOC(x,p) ((x) ? (x) : apr_pcalloc((p), sizeof(*(x))))

static const char * const upgrade_sql[] = { NULL, NULL, NULL, NULL, NULL,
  NULL, NULL, NULL, NULL, NULL, NULL, WC_METADATA_SQL };

/* This values map to the members of STATEMENTS below, and should be added
   and removed at the same time. */
enum statement_keys {
  STMT_INSERT_REPOSITORY,
  STMT_INSERT_WCROOT,
  STMT_INSERT_BASE_NODE,
  STMT_INSERT_WORKING_NODE,
  STMT_INSERT_ACTUAL_NODE,
  STMT_SELECT_REPOSITORY,
  STMT_SELECT_WCROOT_NULL,
  STMT_SELECT_REPOSITORY_BY_ID,
  STMT_SELECT_BASE_NODE,
  STMT_SELECT_WORKING_NODE,
  STMT_SELECT_ACTUAL_NODE,
  STMT_DELETE_BASE_NODE,
  STMT_DELETE_WORKING_NODE,
  STMT_DELETE_ACTUAL_NODE,
  STMT_SELECT_BASE_NODE_BY_RELPATH,
  STMT_DELETE_ALL_WORKING,
  STMT_DELETE_ALL_BASE,
  STMT_SELECT_INCOMPLETE_FLAG,
  STMT_SELECT_BASE_NODE_DIR_PRESENT
};

static const char * const statements[] = {
  "insert into repository (root, uuid) "
  "values (?1, ?2);",

  "insert into wcroot (local_abspath) "
  "values (?1);",

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
     "copyfrom_repos_path, copyfrom_revnum, moved_from, moved_to, checksum, "
     "translated_size, changed_rev, changed_date, changed_author, depth, "
     "last_mod_time, properties) "
  "values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, "
          "?15, ?16, ?17, ?18);",

  "insert or replace into actual_node "
    "(wc_id, local_relpath, parent_relpath, properties, conflict_old, "
     "conflict_new, "
     "conflict_working, prop_reject, changelist, text_mod, "
     "tree_conflict_data) "
  "values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);",

  "select id, root from repository where uuid = ?1;",

  "select id from wcroot where local_abspath is null;",

  "select root, uuid from repository where id = ?1;",

  "select wc_id, local_relpath, repos_id, repos_relpath, parent_relpath, "
    "presence, revnum, kind, checksum, translated_size, "
    "changed_rev, changed_date, changed_author, depth, last_mod_time, "
    "properties, incomplete_children "
  "from base_node;",

  "select wc_id, local_relpath, parent_relpath, presence, kind, "
    "copyfrom_repos_id, "
    "copyfrom_repos_path, copyfrom_revnum, moved_from, moved_to, checksum, "
    "translated_size, changed_rev, changed_date, changed_author, depth, "
    "last_mod_time, properties "
  "from working_node;",

  "select wc_id, local_relpath, parent_relpath, properties, conflict_old, "
     "conflict_new, "
     "conflict_working, prop_reject, changelist, text_mod, "
     "tree_conflict_data "
  "from actual_node;",

  "delete from base_node where wc_id = ?1 and local_relpath = ?2;",

  "delete from working_node where wc_id = ?1 and local_relpath = ?2;",

  "delete from actual_node where wc_id = ?1 and local_relpath = ?2;",

  "select repos_relpath, root, uuid "
  "from base_node, repository "
  "where local_relpath = ?1 and repository.id = base_node.repos_id;",

  "delete from working_node;",

  "delete from base_node;",

  "select incomplete_children from base_node "
  "where wc_id = ?1 and local_relpath = ?2;",

  /* ### won't work for aggregate database. but won't need it by then. */
  "select 1 from base_node where local_relpath = '';",

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
  apr_size_t translated_size;
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
  const char *moved_from;
  const char *moved_to;
  svn_checksum_t *checksum;
  apr_size_t translated_size;
  svn_revnum_t changed_rev;
  apr_time_t changed_date;
  const char *changed_author;
  svn_depth_t depth;
  apr_time_t last_mod_time;
  apr_hash_t *properties;
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

typedef struct {
  apr_int64_t repos_id;
  const char *repos_relpath;
  const char *lock_token;
  const char *lock_owner;
  const char *lock_comment;
  apr_time_t lock_date;
} db_lock_t;





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
take_from_entry(svn_wc_entry_t *src, svn_wc_entry_t *dst, apr_pool_t *pool)
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


/* Select all the rows from working_node table in WC_DB and put them into
   *NODES allocated in RESULT_POOL. */
static svn_error_t *
fetch_working_nodes(apr_hash_t **nodes,
                    svn_sqlite__db_t *wc_db,
                    apr_pool_t *result_pool,
                    apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  *nodes = apr_hash_make(result_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_SELECT_WORKING_NODE));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      apr_size_t len;
      const void *val;
      db_working_node_t *working_node = apr_pcalloc(result_pool,
                                                    sizeof(*working_node));
      const char *presence;

      working_node->wc_id = svn_sqlite__column_int(stmt, 0);
      working_node->local_relpath = svn_sqlite__column_text(stmt, 1,
                                                            result_pool);
      working_node->parent_relpath = svn_sqlite__column_text(stmt, 2,
                                                             result_pool);

      /* ### only bother with a couple values for now */
      presence = svn_sqlite__column_text(stmt, 3, NULL);
      if (strcmp(presence, "not-present") == 0)
        working_node->presence = svn_wc__db_status_not_present;
      else
        working_node->presence = svn_wc__db_status_normal;

      working_node->kind = svn_node_kind_from_word(
                                     svn_sqlite__column_text(stmt, 4, NULL));

      if (!svn_sqlite__column_is_null(stmt, 5))
        {
          working_node->copyfrom_repos_id = svn_sqlite__column_int(stmt, 5);
          working_node->copyfrom_repos_path = svn_sqlite__column_text(stmt, 6,
                                                                result_pool);
          working_node->copyfrom_revnum = svn_sqlite__column_revnum(stmt, 7);
        }

      if (!svn_sqlite__column_is_null(stmt, 8))
        working_node->moved_from = svn_sqlite__column_text(stmt, 8,
                                                           result_pool);

      if (!svn_sqlite__column_is_null(stmt, 9))
        working_node->moved_to = svn_sqlite__column_text(stmt, 9, result_pool);

      if (!svn_sqlite__column_is_null(stmt, 10))
        {
          const char *digest = svn_sqlite__column_text(stmt, 10, NULL);
          svn_checksum_kind_t kind = (digest[1] == 'm'
                                      ? svn_checksum_md5 : svn_checksum_sha1);
          SVN_ERR(svn_checksum_parse_hex(&working_node->checksum, kind,
                                         digest + 6, result_pool));
          working_node->translated_size = svn_sqlite__column_int(stmt, 11);
        }

      if (!svn_sqlite__column_is_null(stmt, 12))
        {
          working_node->changed_rev = svn_sqlite__column_revnum(stmt, 12);
          working_node->changed_date = svn_sqlite__column_int(stmt, 13);
          working_node->changed_author = svn_sqlite__column_text(stmt, 14,
                                                                 result_pool);
        }

      working_node->depth = svn_depth_from_word(
                                    svn_sqlite__column_text(stmt, 15, NULL));
      working_node->last_mod_time = svn_sqlite__column_int(stmt, 16);

      val = svn_sqlite__column_blob(stmt, 17, &len);
      SVN_ERR(svn_skel__parse_proplist(&working_node->properties,
                                       svn_skel__parse(val, len, scratch_pool),
                                       result_pool));

      apr_hash_set(*nodes, working_node->local_relpath, APR_HASH_KEY_STRING,
                   working_node);
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  return SVN_NO_ERROR;
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


/* This function exists for one purpose: to find the expected future url of
   an entry which is schedule-add.  In a centralized metadata storage
   situation, this is pretty easy, but in the current one-db-per-.svn scenario,
   we need to jump through some hoops, so here it is. */
static svn_error_t *
find_working_add_entry_url_stuffs(const char *adm_access_path,
                                  svn_wc_entry_t *entry,
                                  const char *relative_path,
                                  apr_pool_t *result_pool,
                                  apr_pool_t *scratch_pool)
{
  const char *wc_db_path = db_path(adm_access_path, scratch_pool);
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  svn_sqlite__db_t *wc_db;

  /* Open parent database. */
  SVN_ERR(svn_sqlite__open(&wc_db, wc_db_path, svn_sqlite__mode_readonly,
                           statements,
                           SVN_WC__VERSION_EXPERIMENTAL, upgrade_sql,
                           scratch_pool, scratch_pool));

  /* Check to see if a base_node exists for the directory. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db,
                                    STMT_SELECT_BASE_NODE_BY_RELPATH));
  SVN_ERR(svn_sqlite__bindf(stmt, "s", SVN_WC_ENTRY_THIS_DIR));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));

  /* If so, cat the url with the existing relative path, put that in
     entry->url and return. */
  if (have_row)
    {
      const char *base = svn_sqlite__column_text(stmt, 0, NULL);

      entry->repos = svn_sqlite__column_text(stmt, 1, result_pool);
      entry->uuid = svn_sqlite__column_text(stmt, 2, result_pool);
      entry->url = svn_path_join_many(result_pool, entry->repos, base,
                                      relative_path, NULL);
      return svn_sqlite__reset(stmt);
    }
  SVN_ERR(svn_sqlite__reset(stmt));

  /* If not, move a path segement from adm_access_path to relative_path and
     recurse. */
  return find_working_add_entry_url_stuffs(
                    svn_path_dirname(adm_access_path, scratch_pool),
                    entry,
                    svn_path_join(svn_path_basename(adm_access_path,
                                                    scratch_pool),
                                  relative_path, scratch_pool),
                    result_pool, scratch_pool);
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
  apr_hash_t *working_nodes;
  apr_hash_t *actual_nodes;
  svn_sqlite__db_t *wc_db;
  apr_hash_index_t *hi;
  apr_pool_t *result_pool;
  apr_hash_t *entries;
  const char *wc_db_path;
  svn_wc__db_t *db;
  const char *local_abspath;
  const apr_array_header_t *children;
  int i;
  
  if (svn_wc__adm_wc_format(adm_access) < SVN_WC__WC_NG_VERSION)
    return svn_wc__read_entries_old(adm_access, scratch_pool);

  SVN_ERR(svn_wc__adm_get_db(&db, adm_access, scratch_pool));

  result_pool = svn_wc_adm_access_pool(adm_access);
  entries = apr_hash_make(result_pool);
  wc_db_path = db_path(svn_wc_adm_access_path(adm_access), scratch_pool);

  /* Open the wc.db sqlite database. */
  SVN_ERR(svn_sqlite__open(&wc_db, wc_db_path, svn_sqlite__mode_readonly,
                           statements,
                           SVN_WC__VERSION_EXPERIMENTAL, upgrade_sql,
                           scratch_pool, scratch_pool));

  /* The basic strategy here is to get all the node information from the
     database for the directory in question and convert that to
     svn_wc_entry_t structs.  To do that, we fetch each of the nodes from
     the three node tables into a hash, then iterate over them, linking them
     together as required.

     TODO: A smarter way would be to craft a query using the correct type of
     outer join so that we can get all the nodes in one fell swoop.  However,
     that takes more thought and effort than I'm willing to invest right now.
     We can put it on the stack of future optimizations. */

  SVN_ERR(fetch_working_nodes(&working_nodes, wc_db, scratch_pool,
                              scratch_pool));
  SVN_ERR(fetch_actual_nodes(&actual_nodes, wc_db, scratch_pool, scratch_pool));

  SVN_ERR(svn_dirent_get_absolute(&local_abspath,
                                  svn_wc_adm_access_path(adm_access),
                                  scratch_pool));

  SVN_ERR(svn_wc__db_base_get_children(&children, db,
                                       local_abspath,
                                       result_pool, scratch_pool));

  /* Is the directory also present in the BASE_NODE table? */
  {
    svn_sqlite__stmt_t *stmt;
    svn_boolean_t have_row;

    SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db,
                                      STMT_SELECT_BASE_NODE_DIR_PRESENT));
    SVN_ERR(svn_sqlite__step(&have_row, stmt));
    SVN_ERR(svn_sqlite__reset(stmt));

    if (have_row)
      {
        /* Yup. Found it. Create an entry for this directory. */
        APR_ARRAY_PUSH((apr_array_header_t *)children, const char *) = "";
      }
  }

  for (i = children->nelts; i--; )
    {
      svn_wc__db_kind_t kind;
      svn_wc__db_status_t status;
      const char *repos_relpath;
      svn_checksum_t *checksum;
      svn_filesize_t translated_size;
      const db_working_node_t *working_node;
      const db_actual_node_t *actual_node;
      svn_wc_entry_t *entry = alloc_entry(result_pool);

      entry->name = APR_ARRAY_IDX(children, i, const char *);

      SVN_ERR(svn_wc__db_base_get_info(
                &kind,
                &status,
                &entry->revision,
                &repos_relpath,
                &entry->repos,
                &entry->uuid,
                &entry->cmt_rev,
                &entry->cmt_date,
                &entry->cmt_author,
                &entry->depth,
                &checksum,
                &translated_size,
                db,
                svn_dirent_join(local_abspath, entry->name, scratch_pool),
                result_pool,
                scratch_pool));

      /* ### most of the higher levels seem to want "infinity" for files.
         ### without this, it seems a report with depth=unknown was sent
         ### to the server, which then choked.  */
      if (kind == svn_wc__db_kind_file)
        entry->depth = svn_depth_infinity;

      /* Get any corresponding working and actual nodes, removing them from
         their respective hashs to indicate we've seen them.

         ### these are indexed by local_relpath, which is the same as NAME  */
      working_node = apr_hash_get(working_nodes,
                                  entry->name, APR_HASH_KEY_STRING);
      apr_hash_set(working_nodes, entry->name, APR_HASH_KEY_STRING, NULL);
      actual_node = apr_hash_get(actual_nodes,
                                 entry->name, APR_HASH_KEY_STRING);
      apr_hash_set(actual_nodes, entry->name, APR_HASH_KEY_STRING, NULL);

      if (working_node)
        {
          if (working_node->presence == svn_wc__db_status_not_present)
            entry->schedule = svn_wc_schedule_delete;
          else
            entry->schedule = svn_wc_schedule_replace;
        }
      else
        {
          entry->schedule = svn_wc_schedule_normal;
        }

      entry->url = svn_path_join(entry->repos, repos_relpath, result_pool);

      if (working_node && (working_node->copyfrom_repos_path != NULL))
        entry->copied = TRUE;

      if (checksum)
        entry->checksum = svn_checksum_to_cstring(checksum, result_pool);

      if (actual_node && (actual_node->conflict_old != NULL))
        {
          entry->conflict_old = apr_pstrdup(result_pool,
                                            actual_node->conflict_old);
          entry->conflict_new = apr_pstrdup(result_pool,
                                            actual_node->conflict_new);
          entry->conflict_wrk = apr_pstrdup(result_pool,
                                            actual_node->conflict_working);
        }

      if (actual_node && (actual_node->prop_reject != NULL))
        entry->prejfile = apr_pstrdup(result_pool, actual_node->prop_reject);

      if (actual_node && actual_node->changelist != NULL)
        entry->changelist = apr_pstrdup(result_pool, actual_node->changelist);

      if (actual_node && (actual_node->tree_conflict_data != NULL))
        entry->tree_conflict_data = apr_pstrdup(result_pool,
                                              actual_node->tree_conflict_data);

      if (kind == svn_wc__db_kind_dir)
        entry->kind = svn_node_dir;
      else if (kind == svn_wc__db_kind_file)
        entry->kind = svn_node_file;
      else if (kind == svn_wc__db_kind_symlink)
        entry->kind = svn_node_file;  /* ### no symlink kind */
      else
        entry->kind = svn_node_unknown;

      if (status == svn_wc__db_status_not_present
            && entry->kind == svn_node_unknown)
        entry->deleted = TRUE;

      SVN_ERR(determine_incomplete(&entry->incomplete, wc_db,
                                   1 /* wc_id */, entry->name));

      apr_hash_set(entries, entry->name, APR_HASH_KEY_STRING, entry);
    }

  /* Loop over any additional working nodes. */
  for (hi = apr_hash_first(scratch_pool, working_nodes); hi;
        hi = apr_hash_next(hi))
    {
      const db_working_node_t *working_node;
      const char *rel_path;
      svn_wc_entry_t *entry = alloc_entry(result_pool);

      apr_hash_this(hi, (const void **) &rel_path, NULL,
                    (void **) &working_node);
      entry->name = apr_pstrdup(result_pool, working_node->local_relpath);

      /* This node is in WORKING, but not in BASE, so it must be an add. */
      entry->schedule = svn_wc_schedule_add;

      if (working_node->copyfrom_repos_path != NULL)
        entry->copied = TRUE;

      if (working_node->checksum)
        entry->checksum = svn_checksum_to_cstring(working_node->checksum,
                                                  result_pool);

      SVN_ERR(find_working_add_entry_url_stuffs(
                        entry->name[0] == 0
                            ? svn_path_dirname(svn_wc_adm_access_path(
                                            adm_access), scratch_pool)
                            : svn_wc_adm_access_path(adm_access),
                        entry,
                        entry->name[0] == 0
                            ? svn_path_basename(svn_wc_adm_access_path(
                                                   adm_access), scratch_pool)
                            : entry->name,
                        result_pool, scratch_pool));
      entry->kind = working_node->kind;
      entry->revision = 0;

      apr_hash_set(entries, entry->name, APR_HASH_KEY_STRING, entry);
    }

  /* Fill in any implied fields. */
  SVN_ERR(svn_wc__resolve_to_defaults(entries, result_pool));
  svn_wc__adm_access_set_entries(adm_access, TRUE, entries);

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
      svn_path_split(path, &dir_path, &base_name, pool);
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


svn_error_t *
svn_wc_entries_read(apr_hash_t **entries,
                    svn_wc_adm_access_t *adm_access,
                    svn_boolean_t show_hidden,
                    apr_pool_t *pool)
{
  apr_hash_t *new_entries;

  new_entries = svn_wc__adm_access_entries(adm_access, show_hidden, pool);
  if (! new_entries)
    {
      /* Ask for the deleted entries because most operations request them
         at some stage, getting them now avoids a second file parse. */
      SVN_ERR(read_entries(adm_access, pool));

      new_entries = svn_wc__adm_access_entries(adm_access, show_hidden, pool);
    }

  *entries = new_entries;
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

  /* ### presence */
  if (base_node->presence == svn_wc__db_status_not_present)
    SVN_ERR(svn_sqlite__bind_text(stmt, 6, "not-present"));
  else
    SVN_ERR(svn_sqlite__bind_text(stmt, 6, "normal"));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 7, base_node->revision));

  /* ### kind might be "symlink" or "unknown" */
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

  SVN_ERR(svn_sqlite__bind_int64(stmt, 11, base_node->changed_rev));
  SVN_ERR(svn_sqlite__bind_int64(stmt, 12, base_node->changed_date));
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

  SVN_ERR(svn_sqlite__bind_text(stmt, 5,
                                svn_node_kind_to_word(working_node->kind)));

  if (working_node->copyfrom_repos_id > 0)
    {
      SVN_ERR(svn_sqlite__bind_int64(stmt, 6,
                                     working_node->copyfrom_repos_id));
      SVN_ERR(svn_sqlite__bind_text(stmt, 7,
                                    working_node->copyfrom_repos_path));
      SVN_ERR(svn_sqlite__bind_int64(stmt, 8, working_node->copyfrom_revnum));
    }

  if (working_node->moved_from)
    SVN_ERR(svn_sqlite__bind_text(stmt, 9, working_node->moved_from));

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

  if (working_node->changed_rev != SVN_INVALID_REVNUM)
    {
      SVN_ERR(svn_sqlite__bind_int64(stmt, 13, working_node->changed_rev));
      SVN_ERR(svn_sqlite__bind_int64(stmt, 14, working_node->changed_date));
      SVN_ERR(svn_sqlite__bind_text(stmt, 15, working_node->changed_author));
    }

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
   REPOS_ROOT will all be used for writing ENTRY. */
static svn_error_t *
write_entry(svn_sqlite__db_t *wc_db,
            apr_int64_t wc_id,
            apr_int64_t repos_id,
            const char *repos_root,
            const svn_wc_entry_t *entry,
            const char *name,
            const svn_wc_entry_t *this_dir,
            apr_pool_t *pool)
{
  svn_sqlite__stmt_t *stmt;
  apr_pool_t *scratch_pool = svn_pool_create(pool);
  db_base_node_t *base_node = NULL;
  db_working_node_t *working_node = NULL;
  db_actual_node_t *actual_node = NULL;

  switch (entry->schedule)
    {
      case svn_wc_schedule_normal:
        base_node = MAYBE_ALLOC(base_node, scratch_pool);

        /* We also need to chuck any existing working node. */
        SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db,
                                          STMT_DELETE_WORKING_NODE));
        SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, name));
        SVN_ERR(svn_sqlite__step_done(stmt));
        break;

      case svn_wc_schedule_add:
        working_node = MAYBE_ALLOC(working_node, scratch_pool);

        /* We also need to chuck any existing base node. */
        SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db,
                                          STMT_DELETE_BASE_NODE));
        SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, name));
        SVN_ERR(svn_sqlite__step_done(stmt));
        break;

      case svn_wc_schedule_delete:
      case svn_wc_schedule_replace:
        working_node = MAYBE_ALLOC(working_node, scratch_pool);
        base_node = MAYBE_ALLOC(base_node, scratch_pool);
        break;
    }

  if (entry->copied)
    {
      working_node = MAYBE_ALLOC(working_node, scratch_pool);
      working_node->copyfrom_repos_path = entry->copyfrom_url;
      working_node->copyfrom_revnum = entry->copyfrom_rev;
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

      if (entry->deleted)
        {
          base_node->presence = svn_wc__db_status_not_present;
          base_node->kind = svn_node_unknown;
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
          if (entry->url != NULL)
            {
              base_node->repos_relpath = svn_path_is_child(repos_root,
                                                           entry->url,
                                                           scratch_pool);

              if (base_node->repos_relpath == NULL)
                base_node->repos_relpath = "";
            }
          else
            {
              const char *base_path = svn_path_is_child(repos_root,
                                                        this_dir->url,
                                                        scratch_pool);
              if (base_path == NULL)
                base_path = repos_root;

              base_node->repos_relpath = svn_path_join(base_path, entry->name,
                                                       scratch_pool);
            }
        }

      /* TODO: These values should always be present, if they are missing
         during an upgrade, set a flag, and then ask the user to talk to the
         server. */
      base_node->changed_rev = entry->cmt_rev;
      base_node->changed_date = entry->cmt_date;
      base_node->changed_author = entry->cmt_author == NULL
                                        ? "<unknown.author>"
                                        : entry->cmt_author;

      SVN_ERR(insert_base_node(wc_db, base_node, scratch_pool));
    }

  /* Insert the working node. */
  if (working_node)
    {
      working_node->wc_id = wc_id;
      working_node->local_relpath = name;
      working_node->parent_relpath = "";
      working_node->depth = entry->depth;

      if (entry->kind == svn_node_dir)
        working_node->checksum = NULL;
      else
        SVN_ERR(svn_checksum_parse_hex(&working_node->checksum,
                                       svn_checksum_md5,
                                       entry->checksum, scratch_pool));

      if (entry->schedule == svn_wc_schedule_delete)
        working_node->presence = svn_wc__db_status_not_present;
      else
        {
          working_node->presence = svn_wc__db_status_normal;
          working_node->kind = entry->kind;
        }

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

/* Baton for use with entries_write_body(). */
struct entries_write_txn_baton
{
  apr_hash_t *entries;
  const svn_wc_entry_t *this_dir;
  apr_pool_t *scratch_pool;
};

/* Actually do the sqlite work within a transaction.
   This implements svn_sqlite__transaction_callback_t */
static svn_error_t *
entries_write_body(void *baton,
                   svn_sqlite__db_t *wc_db)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;
  apr_hash_index_t *hi;
  struct entries_write_txn_baton *ewtb = baton;
  apr_pool_t *iterpool = svn_pool_create(ewtb->scratch_pool);
  const char *repos_root;
  apr_int64_t repos_id;
  apr_int64_t wc_id;

  /* Get the repos ID. */
  if (ewtb->this_dir->uuid != NULL)
    {
      SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_SELECT_REPOSITORY));
      SVN_ERR(svn_sqlite__bindf(stmt, "s", ewtb->this_dir->uuid));
      SVN_ERR(svn_sqlite__step(&have_row, stmt));

      if (!have_row)
        return svn_error_createf(SVN_ERR_WC_DB_ERROR, NULL,
                                 _("No REPOSITORY table entry for uuid '%s'"),
                                 ewtb->this_dir->uuid);

      repos_id = svn_sqlite__column_int(stmt, 0);
      repos_root = svn_sqlite__column_text(stmt, 1, ewtb->scratch_pool);
      SVN_ERR(svn_sqlite__reset(stmt));
    }
  else
    {
      repos_id = 0;
      repos_root = NULL;
    }

  /* Remove all WORKING and BASE nodes for this directory, since we're about
     to replace 'em. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_ALL_WORKING));
  SVN_ERR(svn_sqlite__step_done(stmt));
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_ALL_BASE));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Write out "this dir" */
  SVN_ERR(fetch_wc_id(&wc_id, wc_db));
  SVN_ERR(write_entry(wc_db, wc_id, repos_id, repos_root, ewtb->this_dir,
                      SVN_WC_ENTRY_THIS_DIR, ewtb->this_dir,
                      ewtb->scratch_pool));

  for (hi = apr_hash_first(ewtb->scratch_pool, ewtb->entries); hi;
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
      SVN_ERR(write_entry(wc_db, wc_id, repos_id, repos_root,
                          this_entry, key, ewtb->this_dir, iterpool));
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
  struct entries_write_txn_baton ewtb;

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
                           pool, pool));

  /* Do the work in a transaction. */
  ewtb.entries = entries;
  ewtb.this_dir = this_dir;
  ewtb.scratch_pool = pool;
  SVN_ERR(svn_sqlite__with_transaction(wc_db, entries_write_body, &ewtb));

  svn_wc__adm_access_set_entries(adm_access, TRUE, entries);
  svn_wc__adm_access_set_entries(adm_access, FALSE, NULL);

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


/* Actually do the sqlite removal work within a transaction.
   This implements svn_sqlite__transaction_callback_t */
static svn_error_t *
entry_remove_body(void *baton,
                  svn_sqlite__db_t *wc_db)
{
  const char *local_relpath = baton;
  svn_sqlite__stmt_t *stmt;
  apr_int64_t wc_id;

  SVN_ERR(fetch_wc_id(&wc_id, wc_db));

  /* Remove the base node. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Remove the working node. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  /* Remove the actual node. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step_done(stmt));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entry_remove(apr_hash_t *entries,
                     const char *parent_dir,
                     const char *name,
                     apr_pool_t *scratch_pool)
{
  svn_sqlite__db_t *wc_db;
  svn_error_t *err;

  apr_hash_set(entries, name, APR_HASH_KEY_STRING, NULL);

  /* Also remove from the sqlite database. */
  /* Open the wc.db sqlite database. */
  err = svn_sqlite__open(&wc_db, db_path(parent_dir, scratch_pool),
                         svn_sqlite__mode_readwrite, statements,
                         SVN_WC__VERSION_EXPERIMENTAL, upgrade_sql,
                         scratch_pool, scratch_pool);
  if (err == NULL)
    {
      /* Do the work in a transaction, for consistency. */
      SVN_ERR(svn_sqlite__with_transaction(wc_db, entry_remove_body,
                                           /* non-const */ (void *)name));
    }
  else if (APR_STATUS_IS_ENOENT(err->apr_err))
    {
      /* ### fine for now. old-style working copy. */
      svn_error_clear(err);
    }
  else if (err->apr_err == SVN_ERR_SQLITE_ERROR)
    {
      /* ### would be nice to know this is "database not found" or a real
         ### problem... but we don't. for now, just assume *any* db error
         ### means that the database wasn't found. and that is just fine...
         ### it means an old-style working copy. */
      svn_error_clear(err);
    }
  else
    return err;

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
  apr_hash_t *entries, *entries_nohidden;
  svn_boolean_t entry_was_deleted_p = FALSE;

  SVN_ERR_ASSERT(entry);

  /* Load ADM_ACCESS's whole entries file. */
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE, pool));
  SVN_ERR(svn_wc_entries_read(&entries_nohidden, adm_access, FALSE, pool));

  /* Ensure that NAME is valid. */
  if (name == NULL)
    name = SVN_WC_ENTRY_THIS_DIR;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE)
    {
      svn_wc_entry_t *entry_before, *entry_after;
      apr_uint64_t orig_modify_flags = modify_flags;
      svn_wc_schedule_t orig_schedule = entry->schedule;

      /* Keep a copy of the unmodified entry on hand. */
      entry_before = apr_hash_get(entries, name, APR_HASH_KEY_STRING);

      /* If scheduling changes were made, we have a special routine to
         manage those modifications. */
      SVN_ERR(fold_scheduling(entries, name, &modify_flags,
                              &entry->schedule, pool));

      /* Do a bit of self-testing. The "folding" algorithm should do the
       * same whether we give it the normal entries or all entries including
       * "deleted" ones. Check that it does. */
      /* Note: This pointer-comparison will always be true unless
       * undocumented implementation details are in play, so it's not
       * necessarily saying the contents of the two hashes differ. So this
       * check may be invoked redundantly, but that is harmless. */
      if (entries != entries_nohidden)
        {
          SVN_ERR(fold_scheduling(entries_nohidden, name, &orig_modify_flags,
                                  &orig_schedule, pool));

          /* Make certain that both folding operations had the same
             result. */
          SVN_ERR_ASSERT(orig_modify_flags == modify_flags);
          SVN_ERR_ASSERT(orig_schedule == entry->schedule);
        }

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
      if (entries != entries_nohidden)
        SVN_ERR(fold_entry(entries_nohidden, name, modify_flags, entry,
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
svn_wc__tweak_entry(apr_hash_t *entries,
                    const char *name,
                    const char *new_url,
                    const char *repos,
                    svn_revnum_t new_rev,
                    svn_boolean_t allow_removal,
                    svn_boolean_t *write_required,
                    apr_pool_t *pool)
{
  svn_wc_entry_t *entry;

  entry = apr_hash_get(entries, name, APR_HASH_KEY_STRING);
  if (! entry)
    return svn_error_createf(SVN_ERR_ENTRY_NOT_FOUND, NULL,
                             _("No such entry: '%s'"), name);

  if (new_url != NULL
      && (! entry->url || strcmp(new_url, entry->url)))
    {
      *write_required = TRUE;
      entry->url = apr_pstrdup(pool, new_url);
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
          for (hi = apr_hash_first(pool, entries); hi;
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
          *write_required = TRUE;
          entry->repos = apr_pstrdup(pool, repos);
        }
    }

  if ((SVN_IS_VALID_REVNUM(new_rev))
      && (entry->schedule != svn_wc_schedule_add)
      && (entry->schedule != svn_wc_schedule_replace)
      && (entry->copied != TRUE)
      && (entry->revision != new_rev))
    {
      *write_required = TRUE;
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
      *write_required = TRUE;
      apr_hash_set(entries, name, APR_HASH_KEY_STRING, NULL);
    }

  return SVN_NO_ERROR;
}




/* Baton for use with init_body() */
struct init_txn_baton
{
  const char *uuid;
  const char *url;
  const char *repos;
  svn_revnum_t initial_rev;
  svn_depth_t depth;
  apr_pool_t *scratch_pool;
};

/* Actually do the sqlite work within a transaction.
   This implements svn_sqlite__transaction_callback_t */
static svn_error_t *
init_body(void *baton,
          svn_sqlite__db_t *wc_db)
{
  struct init_txn_baton *itb = baton;
  svn_sqlite__stmt_t *stmt;
  apr_int64_t wc_id;
  apr_int64_t repos_id;
  svn_wc_entry_t *entry = alloc_entry(itb->scratch_pool);

  /* Insert the repository. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_INSERT_REPOSITORY));
  SVN_ERR(svn_sqlite__bindf(stmt, "ss", itb->repos, itb->uuid));
  SVN_ERR(svn_sqlite__insert(&repos_id, stmt));

  /* Insert the wcroot. */
  /* TODO: Right now, this just assumes wc metadata is being stored locally. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_INSERT_WCROOT));
  SVN_ERR(svn_sqlite__insert(&wc_id, stmt));

  /* Add an entry for the dir itself.  The directory has no name.  It
     might have a UUID, but otherwise only the revision and default
     ancestry are present as XML attributes, and possibly an
     'incomplete' flag if the revnum is > 0. */

  entry->kind = svn_node_dir;
  entry->url = itb->url;
  entry->revision = itb->initial_rev;
  entry->uuid = itb->uuid;
  entry->repos = itb->repos;
  entry->depth = itb->depth;
  if (itb->initial_rev > 0)
    entry->incomplete = TRUE;

  return write_entry(wc_db, wc_id, repos_id, itb->repos, entry,
                     SVN_WC_ENTRY_THIS_DIR, entry, itb->scratch_pool);
}


svn_error_t *
svn_wc__entries_init(const char *path,
                     const char *uuid,
                     const char *url,
                     const char *repos,
                     svn_revnum_t initial_rev,
                     svn_depth_t depth,
                     apr_pool_t *pool)
{
  svn_node_kind_t kind;
  svn_sqlite__db_t *wc_db;
  const char *wc_db_path = db_path(path, pool);
  struct init_txn_baton itb;

  if (!should_create_next_gen())
    return svn_wc__entries_init_old(path, uuid, url, repos, initial_rev,
                                    depth, pool);

  SVN_ERR_ASSERT(! repos || svn_path_is_ancestor(repos, url));
  SVN_ERR_ASSERT(depth == svn_depth_empty
                 || depth == svn_depth_files
                 || depth == svn_depth_immediates
                 || depth == svn_depth_infinity);

  /* Check that the entries sqlite database does not yet exist. */
  SVN_ERR(svn_io_check_path(wc_db_path, &kind, pool));
  if (kind != svn_node_none)
    return svn_error_createf(SVN_ERR_WC_DB_ERROR, NULL,
                             _("Existing sqlite database found at '%s'"),
                             svn_path_local_style(wc_db_path, pool));

  /* Create the entries database, and start a transaction. */
  SVN_ERR(svn_sqlite__open(&wc_db, wc_db_path, svn_sqlite__mode_rwcreate,
                           statements,
                           SVN_WC__VERSION_EXPERIMENTAL, upgrade_sql,
                           pool, pool));

  /* Do the body of the work within an sqlite transaction. */
  itb.uuid = uuid;
  itb.url = url;
  itb.repos = repos;
  itb.initial_rev = initial_rev;
  itb.depth = depth;
  itb.scratch_pool = pool;
  return svn_sqlite__with_transaction(wc_db, init_body, &itb);
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

  SVN_ERR(walk_callbacks->handle_error
          (dirpath, svn_wc_entries_read(&entries, adm_access, show_hidden,
                                        pool), walk_baton, pool));

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
  SVN_ERR(walk_callbacks->handle_error
          (dirpath,
           walk_callbacks->found_entry(dirpath, dot_entry, walk_baton, pool),
           walk_baton, pool));

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

      entrypath = svn_path_join(dirpath, key, subpool);

      /* Call the "found entry" callback for this entry. (For a directory,
       * this is the first visit: as a child.) */
      if (current_entry->kind == svn_node_file
          || depth >= svn_depth_immediates)
        {
          SVN_ERR(walk_callbacks->handle_error
                  (entrypath,
                   walk_callbacks->found_entry(entrypath, current_entry,
                                               walk_baton, subpool),
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

          SVN_ERR(walk_callbacks->handle_error
                  (entrypath,
                   svn_wc_adm_retrieve(&entry_access, adm_access, entrypath,
                                       subpool),
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
    return walk_callbacks->handle_error
      (path, walk_callbacks->found_entry(path, entry, walk_baton, pool),
       walk_baton, pool);

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
      char *parent_path = svn_path_dirname(path, pool);

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

      svn_path_split(path, &parent_path, &bname, pool);

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
