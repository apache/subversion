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
#include "svn_path.h"
#include "svn_ctype.h"
#include "svn_string.h"

#include "wc.h"
#include "adm_files.h"
#include "adm_ops.h"
#include "entries.h"
#include "lock.h"
#include "tree_conflicts.h"

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
  STMT_INSERT_CHANGELIST,
  STMT_SELECT_REPOSITORY,
  STMT_SELECT_WCROOT_NULL,
  STMT_SELECT_REPOSITORY_BY_ID,
  STMT_SELECT_BASE_NODE,
  STMT_SELECT_WORKING_NODE,
  STMT_SELECT_ACTUAL_NODE,
  STMT_DELETE_BASE_NODE,
  STMT_DELETE_WORKING_NODE,
  STMT_DELETE_ACTUAL_NODE
};

static const char * const statements[] = {
  "insert into repository (root, uuid) "
  "values (?1, ?2);",

  "insert into wcroot (local_abspath) "
  "values (?1);",

  "insert or replace into base_node "
    "(wc_id, local_relpath, repos_id, repos_relpath, parent_id, revnum, "
     "kind, checksum, translated_size, changed_rev, changed_date, "
     "changed_author, depth, last_mod_time, properties, incomplete_children)"
  "values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, "
          "?15, ?16);",

  "insert or replace into working_node "
    "(wc_id, local_relpath, parent_relpath, kind, copyfrom_repos_id, "
     "copyfrom_repos_path, copyfrom_revnum, moved_from, moved_to, checksum, "
     "translated_size, changed_rev, changed_date, changed_author, depth, "
     "last_mod_time, properties, changelist_id, tree_conflict_data) "
  "values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14, "
          "?15, ?16, ?17, ?18, ?19);",

  "insert or replace into actual_node "
    "(wc_id, local_relpath, properties, conflict_old, conflict_new, "
     "conflict_working, prop_reject, changelist_id) "
  "values (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8);",

  "insert or replace into changelist "
    "(wc_id, name) "
  "values (?1, ?2);",

  "select id, root from repository where uuid = ?1;",

  "select id from wcroot where local_abspath is null;",

  "select root, uuid from repository where id = ?1;",

  "select id, wc_id, local_relpath, repos_id, repos_relpath, parent_id, "
    "revnum, kind, checksum, translated_size, changed_rev, changed_date, "
    "changed_author, depth, last_mod_time, properties, incomplete_children "
  "from base_node;",

  "select id, wc_id, local_relpath, parent_relpath, kind, copyfrom_repos_id, "
    "copyfrom_repos_path, copyfrom_revnum, moved_from, moved_to, checksum, "
    "translated_size, changed_rev, changed_date, changed_author, depth, "
    "last_mod_time, properties, changelist_id, tree_conflict_data "
  "from working_node;",

  "select id, wc_id, local_relpath, properties, conflict_old, conflict_new, "
     "conflict_working, prop_reject, changelist_id "
  "from actual_node;",

  "delete from base_node where wc_id = ?1 and local_relpath = ?2;",

  "delete from working_node where wc_id = ?1 and local_relpath = ?2;",

  "delete from actual_node where wc_id = ?1 and local_relpath = ?2;",

  NULL
  };

/* Temporary structures which mirror the tables in wc-metadata.sql.
   For detailed descriptions of each field, see that file. */
typedef struct {
  apr_int64_t id;
  apr_int64_t wc_id;
  const char *local_relpath;
  apr_int64_t repos_id;
  const char *repos_relpath;
  apr_int64_t parent_id;
  svn_revnum_t revision;
  svn_node_kind_t kind;
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
  apr_int64_t id;
  apr_int64_t wc_id;
  const char *local_relpath;
  const char *parent_relpath;
  svn_node_kind_t kind;
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
  apr_int64_t changelist_id;
  const char *tree_conflict_data;
} db_working_node_t;

typedef struct {
  apr_int64_t id;
  apr_int64_t wc_id;
  const char *local_relpath;
  apr_hash_t *properties;
  const char *conflict_old;
  const char *conflict_new;
  const char *conflict_working;
  const char *prop_reject;
  apr_int64_t changelist_id;
} db_actual_node_t;

typedef struct {
  apr_int64_t id;
  apr_int64_t wc_id;
  const char *name;
} db_changelist_t;

typedef struct {
  const char *url;
  const char *lock_token;
  const char *lock_owner;
  const char *lock_comment;
  apr_time_t lock_date;
} db_lock_t;



/** Overview **/

/* The administrative `entries' file tracks information about files
   and subdirs within a particular directory.

   See the section on the `entries' file in libsvn_wc/README, for
   concrete information about the XML format.
*/


/*--------------------------------------------------------------- */


/*** Working with the entries sqlite database ***/

/* Return the location of the sqlite database containing the entry information
   for PATH in the filesystem.  Allocate in RESULT_POOL. ***/
static const char *
db_path(const char *path,
        apr_pool_t *result_pool)
{
  return svn_wc__adm_child(path, "wc.db", result_pool);
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

/**
 * Parse the string at *STR as an revision and save the result in
 * *OPT_REV.  After returning successfully, *STR points at next
 * character in *STR where further parsing can be done.
 */
static svn_error_t *
string_to_opt_revision(svn_opt_revision_t *opt_rev,
                       const char **str,
                       apr_pool_t *pool)
{
  const char *s = *str;

  SVN_ERR_ASSERT(opt_rev);

  while (*s && *s != ':')
    ++s;

  /* Should not find a \0. */
  if (!*s)
    return svn_error_createf
      (SVN_ERR_INCORRECT_PARAMS, NULL,
       _("Found an unexpected \\0 in the file external '%s'"), *str);

  if (0 == strncmp(*str, "HEAD:", 5))
    {
      opt_rev->kind = svn_opt_revision_head;
    }
  else
    {
      svn_revnum_t rev;
      const char *endptr;

      SVN_ERR(svn_revnum_parse(&rev, *str, &endptr));
      SVN_ERR_ASSERT(endptr == s);
      opt_rev->kind = svn_opt_revision_number;
      opt_rev->value.number = rev;
    }

  *str = s + 1;

  return SVN_NO_ERROR;
}

/**
 * Given a revision, return a string for the revision, either "HEAD"
 * or a string representation of the revision value.  All other
 * revision kinds return an error.
 */
static svn_error_t *
opt_revision_to_string(const char **str,
                       const char *path,
                       const svn_opt_revision_t *rev,
                       apr_pool_t *pool)
{
  switch (rev->kind)
    {
    case svn_opt_revision_head:
      *str = apr_pstrmemdup(pool, "HEAD", 4);
      break;
    case svn_opt_revision_number:
      *str = apr_itoa(pool, rev->value.number);
      break;
    default:
      return svn_error_createf
        (SVN_ERR_INCORRECT_PARAMS, NULL,
         _("Illegal file external revision kind %d for path '%s'"),
         rev->kind, path);
      break;
    }

  return SVN_NO_ERROR;
}

/* Parse a file external specification in the NULL terminated STR and
   place the path in PATH_RESULT, the peg revision in PEG_REV_RESULT
   and revision number in REV_RESULT.  STR may be NULL, in which case
   PATH_RESULT will be set to NULL and both PEG_REV_RESULT and
   REV_RESULT set to svn_opt_revision_unspecified.

   The format that is read is the same as a working-copy path with a
   peg revision; see svn_opt_parse_path(). */
static svn_error_t *
unserialize_file_external(const char **path_result,
                          svn_opt_revision_t *peg_rev_result,
                          svn_opt_revision_t *rev_result,
                          const char *str,
                          apr_pool_t *pool)
{
  if (str)
    {
      svn_opt_revision_t peg_rev;
      svn_opt_revision_t op_rev;
      const char *s = str;

      SVN_ERR(string_to_opt_revision(&peg_rev, &s, pool));
      SVN_ERR(string_to_opt_revision(&op_rev, &s, pool));

      *path_result = apr_pstrdup(pool, s);
      *peg_rev_result = peg_rev;
      *rev_result = op_rev;
    }
  else
    {
      *path_result = NULL;
      peg_rev_result->kind = svn_opt_revision_unspecified;
      rev_result->kind = svn_opt_revision_unspecified;
    }

  return SVN_NO_ERROR;
}

/* Serialize into STR the file external path, peg revision number and
   the operative revision number into a format that
   unserialize_file_external() can parse.  The format is
     %{peg_rev}:%{rev}:%{path}
   where a rev will either be HEAD or the string revision number.  If
   PATH is NULL then STR will be set to NULL.  This method writes to a
   string instead of a svn_stringbuf_t so that the string can be
   protected by write_str(). */
static svn_error_t *
serialize_file_external(const char **str,
                        const char *path,
                        const svn_opt_revision_t *peg_rev,
                        const svn_opt_revision_t *rev,
                        apr_pool_t *pool)
{
  const char *s;

  if (path)
    {
      const char *s1;
      const char *s2;

      SVN_ERR(opt_revision_to_string(&s1, path, peg_rev, pool));
      SVN_ERR(opt_revision_to_string(&s2, path, rev, pool));

      s = apr_pstrcat(pool, s1, ":", s2, ":", path, NULL);
    }
  else
    s = NULL;

  *str = s;

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


/* Resolve any missing information in ENTRIES by deducing from the
   directory's own entry (which must already be present in ENTRIES). */
static svn_error_t *
resolve_to_defaults(apr_hash_t *entries,
                    apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_wc_entry_t *default_entry
    = apr_hash_get(entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);

  /* First check the dir's own entry for consistency. */
  if (! default_entry)
    return svn_error_create(SVN_ERR_ENTRY_NOT_FOUND,
                            NULL,
                            _("Missing default entry"));

  if (default_entry->revision == SVN_INVALID_REVNUM)
    return svn_error_create(SVN_ERR_ENTRY_MISSING_REVISION,
                            NULL,
                            _("Default entry has no revision number"));

  if (! default_entry->url)
    return svn_error_create(SVN_ERR_ENTRY_MISSING_URL,
                            NULL,
                            _("Default entry is missing URL"));


  /* Then use it to fill in missing information in other entries. */
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      void *val;
      svn_wc_entry_t *this_entry;

      apr_hash_this(hi, NULL, NULL, &val);
      this_entry = val;

      if (this_entry == default_entry)
        /* THIS_DIR already has all the information it can possibly
           have.  */
        continue;

      if (this_entry->kind == svn_node_dir)
        /* Entries that are directories have everything but their
           name, kind, and state stored in the THIS_DIR entry of the
           directory itself.  However, we are disallowing the perusing
           of any entries outside of the current entries file.  If a
           caller wants more info about a directory, it should look in
           the entries file in the directory.  */
        continue;

      if (this_entry->kind == svn_node_file)
        /* For file nodes that do not explicitly have their ancestry
           stated, this can be derived from the default entry of the
           directory in which those files reside.  */
        take_from_entry(default_entry, this_entry, pool);
    }

  return SVN_NO_ERROR;
}

/* Select all the rows from base_node table in WC_DB and put them into *NODES,
   allocated in RESULT_POOL. */
static svn_error_t *
fetch_base_nodes(apr_hash_t **nodes,
                 svn_sqlite__db_t *wc_db,
                 apr_pool_t *scratch_pool,
                 apr_pool_t *result_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  *nodes = apr_hash_make(result_pool);

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_SELECT_BASE_NODE));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  while (have_row)
    {
      apr_size_t len;
      const void *val;
      db_base_node_t *base_node = apr_pcalloc(result_pool,
                                              sizeof(*base_node));

      base_node->wc_id = svn_sqlite__column_int(stmt, 1);
      base_node->local_relpath = svn_sqlite__column_text(stmt, 2, result_pool);

      if (!svn_sqlite__column_is_null(stmt, 3))
        {
          base_node->repos_id = svn_sqlite__column_int(stmt, 3);
          base_node->repos_relpath = svn_sqlite__column_text(stmt, 4,
                                                             result_pool);
        }

      if (!svn_sqlite__column_is_null(stmt, 5))
        base_node->parent_id = svn_sqlite__column_int(stmt, 5);

      base_node->revision = svn_sqlite__column_int(stmt, 6);
      base_node->kind = svn_node_kind_from_word(
                                    svn_sqlite__column_text(stmt, 7, NULL));

      if (!svn_sqlite__column_is_null(stmt, 8))
        {
          const char *digest = svn_sqlite__column_text(stmt, 8, NULL);
          svn_checksum_kind_t kind = (digest[1] == 'm'
                                      ? svn_checksum_md5 : svn_checksum_sha1);
          SVN_ERR(svn_checksum_parse_hex(&base_node->checksum, kind,
                                         digest + 6, result_pool));
        }

      base_node->translated_size = svn_sqlite__column_int(stmt, 9);

      base_node->changed_rev = svn_sqlite__column_int(stmt, 10);
      base_node->changed_date = svn_sqlite__column_int(stmt, 11);
      base_node->changed_author = svn_sqlite__column_text(stmt, 12,
                                                          result_pool);

      base_node->depth = svn_depth_from_word(
                                    svn_sqlite__column_text(stmt, 13, NULL));
      base_node->last_mod_time = svn_sqlite__column_int(stmt, 14);

      val = svn_sqlite__column_blob(stmt, 15, &len);
      SVN_ERR(svn_skel__parse_proplist(&base_node->properties,
                                       svn_skel__parse(val, len, scratch_pool),
                                       result_pool));

      base_node->incomplete_children = svn_sqlite__column_boolean(stmt, 16);

      apr_hash_set(*nodes, base_node->local_relpath, APR_HASH_KEY_STRING,
                   base_node);
      SVN_ERR(svn_sqlite__step(&have_row, stmt));
    }

  return SVN_NO_ERROR;
}

/* Select all the rows from working_node table in WC_DB and put them into
   *NODES allocated in RESULT_POOL. */
static svn_error_t *
fetch_working_nodes(apr_hash_t **nodes,
                    svn_sqlite__db_t *wc_db,
                    apr_pool_t *scratch_pool,
                    apr_pool_t *result_pool)
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

      working_node->wc_id = svn_sqlite__column_int(stmt, 1);
      working_node->local_relpath = svn_sqlite__column_text(stmt, 2,
                                                            result_pool);
      working_node->parent_relpath = svn_sqlite__column_text(stmt, 3,
                                                             result_pool);
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

      if (!svn_sqlite__column_is_null(stmt, 18))
        working_node->changelist_id = svn_sqlite__column_int(stmt, 18);

      if (!svn_sqlite__column_is_null(stmt, 19))
        working_node->tree_conflict_data = svn_sqlite__column_text(stmt, 19,
                                                                result_pool);

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
                   apr_pool_t *scratch_pool,
                   apr_pool_t *result_pool)
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

      actual_node->wc_id = svn_sqlite__column_int(stmt, 1);
      actual_node->local_relpath = svn_sqlite__column_text(stmt, 2,
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
        actual_node->changelist_id = svn_sqlite__column_int(stmt, 8);

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
get_repos_info(const char **repos_root,
               const char **repos_uuid,
               svn_sqlite__db_t *wc_db,
               apr_int64_t repos_id,
               apr_pool_t *result_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_boolean_t have_row;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db,
                                    STMT_SELECT_REPOSITORY_BY_ID));
  SVN_ERR(svn_sqlite__bindf(stmt, "i", repos_id));
  SVN_ERR(svn_sqlite__step(&have_row, stmt));
  if (!have_row)
    return svn_error_createf(SVN_ERR_WC_DB_ERROR, NULL,
                             _("No REPOSITORY table entry for id '%ld'"),
                             repos_id);

  *repos_root = svn_sqlite__column_text(stmt, 0, result_pool);
  *repos_uuid = svn_sqlite__column_text(stmt, 1, result_pool);

  return svn_sqlite__reset(stmt);
}

/* Fill the entries cache in ADM_ACCESS. The full hash cache will be
   populated.  SCRATCH_POOL is used for local memory allocation, the access
   baton pool is used for the cache. */
static svn_error_t *
read_entries(svn_wc_adm_access_t *adm_access,
             apr_pool_t *scratch_pool)
{
  apr_hash_t *base_nodes;
  apr_hash_t *working_nodes;
  apr_hash_t *actual_nodes;
  svn_sqlite__db_t *wc_db;
  apr_hash_index_t *hi;
  const char *repos_root = NULL;
  const char *repos_uuid = NULL;
  apr_pool_t *result_pool = svn_wc_adm_access_pool(adm_access);
  apr_hash_t *entries = apr_hash_make(result_pool);
  const char *wc_db_path = db_path(svn_wc_adm_access_path(adm_access),
                                   scratch_pool);
  
  /* Open the wc.db sqlite database. */
  SVN_ERR(svn_sqlite__open(&wc_db, wc_db_path, svn_sqlite__mode_readwrite,
                           statements, SVN_WC__VERSION, upgrade_sql,
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

  SVN_ERR(fetch_base_nodes(&base_nodes, wc_db, scratch_pool, scratch_pool));
  SVN_ERR(fetch_working_nodes(&working_nodes, wc_db, scratch_pool,
                              scratch_pool));
  SVN_ERR(fetch_actual_nodes(&actual_nodes, wc_db, scratch_pool, scratch_pool));

  for (hi = apr_hash_first(scratch_pool, base_nodes); hi;
        hi = apr_hash_next(hi))
    {
      db_base_node_t *base_node;
      db_working_node_t *working_node;
      db_actual_node_t *actual_node;
      const char *rel_path;
      svn_wc_entry_t *entry = alloc_entry(result_pool);

      apr_hash_this(hi, (const void **) &rel_path, NULL, (void **) &base_node);

      /* Get any corresponding working and actual nodes, removing them from
         their respective hashs to indicate we've seen them. */
      working_node = apr_hash_get(working_nodes, rel_path, APR_HASH_KEY_STRING);
      apr_hash_set(working_nodes, rel_path, APR_HASH_KEY_STRING, NULL);
      actual_node = apr_hash_get(actual_nodes, rel_path, APR_HASH_KEY_STRING);
      apr_hash_set(actual_nodes, rel_path, APR_HASH_KEY_STRING, NULL);

      entry->name = apr_pstrdup(result_pool, base_node->local_relpath);

      if (working_node)
        {
          if (working_node->kind == svn_node_none)
            entry->schedule = svn_wc_schedule_delete;
          else
            entry->schedule = svn_wc_schedule_replace;
        }
      else
        {
          entry->schedule = svn_wc_schedule_normal;
        }

      if (base_node->repos_id)
        {
          if (repos_root == NULL)
            SVN_ERR(get_repos_info(&repos_root, &repos_uuid, wc_db,
                                   base_node->repos_id, result_pool));

          entry->uuid = repos_uuid;
          entry->url = svn_path_join(repos_root, base_node->repos_relpath,
                                     result_pool);
          entry->repos = repos_root;
        }

      if (working_node && (working_node->copyfrom_repos_path != NULL))
        entry->copied = TRUE;

      if (working_node && (working_node->tree_conflict_data != NULL))
        entry->tree_conflict_data = apr_pstrdup(result_pool,
                                             working_node->tree_conflict_data);


      if (base_node->checksum)
        entry->checksum = svn_checksum_to_cstring(base_node->checksum,
                                                  result_pool);

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

      entry->depth = base_node->depth;
      entry->revision = base_node->revision;
      entry->kind = base_node->kind;

      entry->incomplete = base_node->incomplete_children;

      apr_hash_set(entries, entry->name, APR_HASH_KEY_STRING, entry);
    }

  /* Loop over any additional working nodes. */
  for (hi = apr_hash_first(scratch_pool, working_nodes); hi;
        hi = apr_hash_next(hi))
    {
      db_working_node_t *working_node;
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
      entry->kind = working_node->kind;
      entry->revision = 0;

      apr_hash_set(entries, entry->name, APR_HASH_KEY_STRING, entry);
    }

  /* Fill in any implied fields. */
  SVN_ERR(resolve_to_defaults(entries, result_pool));
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
                 db_base_node_t *base_node,
                 apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_stringbuf_t *properties;
  svn_skel_t *skel;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_INSERT_BASE_NODE));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 1, base_node->wc_id));
  SVN_ERR(svn_sqlite__bind_text(stmt, 2, base_node->local_relpath));

  if (base_node->repos_id)
    {
      SVN_ERR(svn_sqlite__bind_int64(stmt, 3, base_node->repos_id));
      SVN_ERR(svn_sqlite__bind_text(stmt, 4, base_node->repos_relpath));
    }

  if (base_node->parent_id)
    SVN_ERR(svn_sqlite__bind_int64(stmt, 5, base_node->parent_id));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 6, base_node->revision));
  SVN_ERR(svn_sqlite__bind_text(stmt, 7,
                                svn_node_kind_to_word(base_node->kind)));

  if (base_node->checksum)
    {
      const char *kind_str = (base_node->checksum->kind == svn_checksum_md5
                              ? "$md5 $" : "$sha1$");
      SVN_ERR(svn_sqlite__bind_text(stmt, 8, apr_pstrcat(scratch_pool,
                    kind_str, svn_checksum_to_cstring(base_node->checksum,
                                                      scratch_pool), NULL)));
    }

  SVN_ERR(svn_sqlite__bind_int64(stmt, 9, base_node->translated_size));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 10, base_node->changed_rev));
  SVN_ERR(svn_sqlite__bind_int64(stmt, 11, base_node->changed_date));
  SVN_ERR(svn_sqlite__bind_text(stmt, 12, base_node->changed_author));

  SVN_ERR(svn_sqlite__bind_text(stmt, 13, svn_depth_to_word(base_node->depth)));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 14, base_node->last_mod_time));

  if (base_node->properties)
    SVN_ERR(svn_skel__unparse_proplist(&skel, base_node->properties,
                                       scratch_pool));
  else
    skel = svn_skel__make_empty_list(scratch_pool);

  properties = svn_skel__unparse(skel, scratch_pool);
  SVN_ERR(svn_sqlite__bind_blob(stmt, 15, properties->data, properties->len));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 16, base_node->incomplete_children));

  /* Execute and reset the insert clause. */
  return svn_sqlite__insert(NULL, stmt);
}

static svn_error_t *
insert_working_node(svn_sqlite__db_t *wc_db,
                    db_working_node_t *working_node,
                    apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_stringbuf_t *properties;
  svn_skel_t *skel;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_INSERT_WORKING_NODE));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 1, working_node->wc_id));
  SVN_ERR(svn_sqlite__bind_text(stmt, 2, working_node->local_relpath));
  SVN_ERR(svn_sqlite__bind_text(stmt, 3, working_node->parent_relpath));
  SVN_ERR(svn_sqlite__bind_text(stmt, 4,
                                svn_node_kind_to_word(working_node->kind)));

  if (working_node->copyfrom_repos_id > 0)
    {
      SVN_ERR(svn_sqlite__bind_int64(stmt, 5,
                                     working_node->copyfrom_repos_id));
      SVN_ERR(svn_sqlite__bind_text(stmt, 6,
                                    working_node->copyfrom_repos_path));
      SVN_ERR(svn_sqlite__bind_int64(stmt, 7, working_node->copyfrom_revnum));
    }

  if (working_node->moved_from)
    SVN_ERR(svn_sqlite__bind_text(stmt, 8, working_node->moved_from));

  if (working_node->moved_to)
    SVN_ERR(svn_sqlite__bind_text(stmt, 9, working_node->moved_to));

  if (working_node->checksum)
    {
      const char *kind_str = (working_node->checksum->kind == svn_checksum_md5
                              ? "$md5 $" : "$sha1$");
      SVN_ERR(svn_sqlite__bind_text(stmt, 10, apr_pstrcat(scratch_pool,
                    kind_str, svn_checksum_to_cstring(working_node->checksum,
                                                      scratch_pool), NULL)));
      SVN_ERR(svn_sqlite__bind_int64(stmt, 11, working_node->translated_size));
    }

  if (working_node->changed_rev != SVN_INVALID_REVNUM)
    {
      SVN_ERR(svn_sqlite__bind_int64(stmt, 12, working_node->changed_rev));
      SVN_ERR(svn_sqlite__bind_int64(stmt, 13, working_node->changed_date));
      SVN_ERR(svn_sqlite__bind_text(stmt, 14, working_node->changed_author));
    }

  SVN_ERR(svn_sqlite__bind_text(stmt, 15,
                                svn_depth_to_word(working_node->depth)));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 16, working_node->last_mod_time));

  if (working_node->properties)
    SVN_ERR(svn_skel__unparse_proplist(&skel, working_node->properties,
                                       scratch_pool));
  else
    skel = svn_skel__make_empty_list(scratch_pool);

  properties = svn_skel__unparse(skel, scratch_pool);
  SVN_ERR(svn_sqlite__bind_blob(stmt, 17, properties->data, properties->len));

  if (working_node->changelist_id > 0)
    SVN_ERR(svn_sqlite__bind_int64(stmt, 18, working_node->changelist_id));

  if (working_node->tree_conflict_data)
    SVN_ERR(svn_sqlite__bind_text(stmt, 19, working_node->tree_conflict_data));

  /* Execute and reset the insert clause. */
  return svn_sqlite__insert(NULL, stmt);
}

static svn_error_t *
insert_actual_node(svn_sqlite__db_t *wc_db,
                   db_actual_node_t *actual_node,
                   apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;
  svn_stringbuf_t *properties;
  svn_skel_t *skel;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_INSERT_ACTUAL_NODE));

  SVN_ERR(svn_sqlite__bind_int64(stmt, 1, actual_node->wc_id));
  SVN_ERR(svn_sqlite__bind_text(stmt, 2, actual_node->local_relpath));

  if (actual_node->properties)
    SVN_ERR(svn_skel__unparse_proplist(&skel, actual_node->properties,
                                       scratch_pool));
  else
    skel = svn_skel__make_empty_list(scratch_pool);

  properties = svn_skel__unparse(skel, scratch_pool);
  SVN_ERR(svn_sqlite__bind_blob(stmt, 3, properties->data, properties->len));

  if (actual_node->conflict_old)
    {
      SVN_ERR(svn_sqlite__bind_text(stmt, 4, actual_node->conflict_old));
      SVN_ERR(svn_sqlite__bind_text(stmt, 5, actual_node->conflict_new));
      SVN_ERR(svn_sqlite__bind_text(stmt, 6, actual_node->conflict_working));
    }

  if (actual_node->prop_reject)
    SVN_ERR(svn_sqlite__bind_text(stmt, 7, actual_node->prop_reject));

  if (actual_node->changelist_id > 0)
    SVN_ERR(svn_sqlite__bind_int64(stmt, 8, actual_node->changelist_id));

  /* Execute and reset the insert clause. */
  return svn_sqlite__insert(NULL, stmt);
}

static svn_error_t *
insert_changelist(svn_sqlite__db_t *wc_db,
                  db_changelist_t *changelist,
                  apr_int64_t *changelist_id,
                  apr_pool_t *scratch_pool)
{
  svn_sqlite__stmt_t *stmt;

  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_INSERT_CHANGELIST));

  SVN_ERR(svn_sqlite__bindf(stmt, "is", (apr_int64_t) changelist->wc_id,
                            changelist->name));

  /* Execute and reset the insert clause. */
  return svn_sqlite__insert(changelist_id, stmt);
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
  svn_boolean_t got_row;
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
        SVN_ERR(svn_sqlite__step(&got_row, stmt));
        SVN_ERR(svn_sqlite__reset(stmt));
        break;

      case svn_wc_schedule_add:
        working_node = MAYBE_ALLOC(working_node, scratch_pool);

        /* We also need to chuck any existing base node. */
        SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db,
                                          STMT_DELETE_BASE_NODE));
        SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, name));
        SVN_ERR(svn_sqlite__step(&got_row, stmt));
        SVN_ERR(svn_sqlite__reset(stmt));
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

  if (entry->deleted)
    working_node = MAYBE_ALLOC(working_node, scratch_pool);

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
      db_changelist_t changelist;
      apr_int64_t changelist_id;

      changelist.wc_id = wc_id;
      changelist.name = entry->changelist;

      SVN_ERR(insert_changelist(wc_db, &changelist, &changelist_id,
                                scratch_pool));

      if (working_node)
        working_node->changelist_id = changelist_id;
      if (actual_node)
        actual_node->changelist_id = changelist_id;
    }

  if (entry->tree_conflict_data)
    {
      working_node = MAYBE_ALLOC(working_node, scratch_pool);
      working_node->tree_conflict_data = entry->tree_conflict_data;
    }

  /* Insert the base node. */
  if (base_node)
    {
      base_node->wc_id = wc_id;
      base_node->local_relpath = name;
      base_node->kind = entry->kind;
      base_node->revision = entry->revision;
      base_node->depth = entry->depth;

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
                                        ? "<unknown author>"
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
        working_node->kind = svn_node_none;
      else
        working_node->kind = entry->kind;

      SVN_ERR(insert_working_node(wc_db, working_node, scratch_pool));
    }

  /* Insert the actual node. */
  if (actual_node)
    {
      actual_node->wc_id = wc_id;
      actual_node->local_relpath = name;

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
                           svn_sqlite__mode_readwrite, statements,
                           SVN_WC__VERSION, upgrade_sql, pool, pool));

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
  svn_boolean_t got_row;  /* Meaningless when doing a delete. */
  apr_int64_t wc_id;

  SVN_ERR(fetch_wc_id(&wc_id, wc_db));

  /* Remove the base node. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_BASE_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&got_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  /* Remove the working node. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_WORKING_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&got_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  /* Remove the actual node. */
  SVN_ERR(svn_sqlite__get_statement(&stmt, wc_db, STMT_DELETE_ACTUAL_NODE));
  SVN_ERR(svn_sqlite__bindf(stmt, "is", wc_id, local_relpath));
  SVN_ERR(svn_sqlite__step(&got_row, stmt));
  SVN_ERR(svn_sqlite__reset(stmt));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entry_remove(apr_hash_t *entries,
                     const char *parent_dir,
                     const char *name,
                     apr_pool_t *scratch_pool)
{
  svn_sqlite__db_t *wc_db;

  apr_hash_set(entries, name, APR_HASH_KEY_STRING, NULL);

  /* Also remove from the sqlite database. */
  /* Open the wc.db sqlite database. */
  SVN_ERR(svn_sqlite__open(&wc_db, db_path(parent_dir, scratch_pool),
                           svn_sqlite__mode_readwrite, statements,
                           SVN_WC__VERSION, upgrade_sql,
                           scratch_pool, scratch_pool));

  /* Do the work in a transaction, for consistency. */
  SVN_ERR(svn_sqlite__with_transaction(wc_db, entry_remove_body,
                                       (void *) name));

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




/*** Initialization of the entries file. ***/

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
                           statements, SVN_WC__VERSION, upgrade_sql, pool,
                           pool));

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
