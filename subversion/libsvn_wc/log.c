/*
 * log.c:  handle the adm area's log file.
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



#include <string.h>

#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_wc.h"
#include "svn_error.h"
#include "svn_string.h"
#include "svn_xml.h"
#include "svn_pools.h"
#include "svn_io.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"
#include "svn_time.h"
#include "svn_iter.h"

#include "wc.h"
#include "log.h"
#include "props.h"
#include "adm_files.h"
#include "entries.h"
#include "lock.h"
#include "translate.h"
#include "tree_conflicts.h"
#include "workqueue.h"

#include "private/svn_wc_private.h"
#include "private/svn_skel.h"
#include "svn_private_config.h"


/*** Constant definitions for xml generation/parsing ***/

/* Note: every entry in the logfile is either idempotent or atomic.
 * This allows us to remove the entire logfile when every entry in it
 * has been completed -- if you crash in the middle of running a
 * logfile, and then later are running over it again as part of the
 * recovery, a given entry is "safe" in the sense that you can either
 * tell it has already been done (in which case, ignore it) or you can
 * do it again without ill effect.
 *
 * All log commands are self-closing tags with attributes.
 */


/** Log actions. **/

/* Delete lock related fields from the entry SVN_WC__LOG_ATTR_NAME. */
#define SVN_WC__LOG_DELETE_LOCK         "delete-lock"

/* Delete the entry SVN_WC__LOG_ATTR_NAME. */
#define SVN_WC__LOG_DELETE_ENTRY        "delete-entry"
#define SVN_WC__LOG_ATTR_REVISION       "revision"
#define SVN_WC__LOG_ATTR_KIND           "kind"

/* Move file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST. */
#define SVN_WC__LOG_MV                  "mv"

/* Copy file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST, but
   expand any keywords and use any eol-style defined by properties of
   the DEST. */
#define SVN_WC__LOG_CP_AND_TRANSLATE    "cp-and-translate"

/* Set SVN_WC__LOG_ATTR_NAME to have timestamp SVN_WC__LOG_ATTR_TIMESTAMP. */
#define SVN_WC__LOG_SET_TIMESTAMP       "set-timestamp"

/* Add a new tree conflict to the parent entry's tree-conflict-data. */
/* ### rev'd to -2 because we changed the params. developers better not
   ### update across this change if they have stale logs :-)  */
#define SVN_WC__LOG_ADD_TREE_CONFLICT   "add-tree-conflict-2"


/** Log attributes.  See the documentation above for log actions for
    how these are used. **/

#define SVN_WC__LOG_ATTR_NAME           "name"
#define SVN_WC__LOG_ATTR_DEST           "dest"
#define SVN_WC__LOG_ATTR_TIMESTAMP      "timestamp"
#define SVN_WC__LOG_ATTR_DATA           "data"

/* This one is for SVN_WC__LOG_CP_AND_TRANSLATE to indicate a versioned
   path to take its translation properties from */
#define SVN_WC__LOG_ATTR_ARG_2          "arg2"


/*** Userdata for the callbacks. ***/
struct log_runner
{
  svn_wc__db_t *db;
  const char *adm_abspath;

  apr_pool_t *pool; /* cleared before processing each log element */

  svn_xml_parser_t *parser;
};


/* The log body needs to be wrapped in a single, root element to satisfy
   the Expat parser. These two macros provide the start/end wrapprs.  */
#define LOG_START "<wc-log xmlns=\"http://subversion.tigris.org/xmlns\">\n"
#define LOG_END "</wc-log>\n"

/* For log debugging. Generates output about its operation.  */
/* #define DEBUG_LOG */


/* Helper macro for erroring out while running a logfile.

   This is implemented as a macro so that the error created has a useful
   line number associated with it. */
#define SIGNAL_ERROR(loggy, err)                                        \
  svn_xml_signal_bailout(svn_error_createf(                             \
                           SVN_ERR_WC_BAD_ADM_LOG, err,                 \
                           _("In directory '%s'"),                      \
                           svn_dirent_local_style(loggy->adm_abspath,   \
                                                  loggy->pool)),        \
                         loggy->parser)


static svn_error_t *
log_do_file_cp_and_translate(svn_wc__db_t *db,
                             const char *from_abspath,
                             const char *dest_abspath,
                             const char *versioned_abspath,
                             apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  svn_subst_eol_style_t style;
  const char *eol;
  apr_hash_t *keywords;
  svn_boolean_t special;

  err = svn_wc__get_eol_style(&style, &eol, db, versioned_abspath,
                              scratch_pool, scratch_pool);
  if (! err)
    err = svn_wc__get_keywords(&keywords, db, versioned_abspath, NULL,
                               scratch_pool, scratch_pool);
  if (! err)
    err = svn_wc__get_special(&special, db, versioned_abspath,
                              scratch_pool);

  if (! err)
    err = svn_subst_copy_and_translate4(from_abspath, dest_abspath,
                                        eol, TRUE /* repair */,
                                        keywords, TRUE /* expand */,
                                        special,
                                        NULL, NULL, /* ### cancel */
                                        scratch_pool);

  if (err)
    {
      if (!APR_STATUS_IS_ENOENT(err->apr_err))
        return svn_error_return(err);
      svn_error_clear(err);
    }

  return SVN_NO_ERROR;
}


static svn_error_t *
log_do_file_move(const char *from_abspath,
                 const char *dest_abspath,
                 apr_pool_t *scratch_pool)
{
  svn_error_t *err;

  err = svn_io_file_rename(from_abspath, dest_abspath, scratch_pool);

  /* If we got an ENOENT, that's ok;  the move has probably
     already completed in an earlier run of this log.  */
  if (err)
    {
      if (!APR_STATUS_IS_ENOENT(err->apr_err))
        return svn_error_quick_wrap(err, _("Can't move source to dest"));
      svn_error_clear(err);
    }

  return SVN_NO_ERROR;
}


/* Set file NAME in log's CWD to timestamp value in ATTS. */
static svn_error_t *
log_do_file_timestamp(struct log_runner *loggy,
                      const char *name,
                      const char **atts)
{
  apr_time_t timestamp;
  svn_node_kind_t kind;
  const char *local_abspath
    = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);

  const char *timestamp_string
    = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_TIMESTAMP, atts);
  svn_boolean_t is_special;

  if (! timestamp_string)
    return svn_error_createf(SVN_ERR_WC_BAD_ADM_LOG, NULL,
                             _("Missing 'timestamp' attribute in '%s'"),
                             svn_dirent_local_style(loggy->adm_abspath,
                                                    loggy->pool));

  /* Do not set the timestamp on special files. */
  SVN_ERR(svn_io_check_special_path(local_abspath, &kind, &is_special,
                                    loggy->pool));

  if (! is_special)
    {
      SVN_ERR(svn_time_from_cstring(&timestamp, timestamp_string,
                                    loggy->pool));

      SVN_ERR(svn_io_set_file_affected_time(timestamp, local_abspath,
                                            loggy->pool));
    }

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
log_do_delete_lock(struct log_runner *loggy,
                   const char *name)
{
  const char *local_abspath;
  svn_error_t *err;

  local_abspath = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);

  err = svn_wc__db_lock_remove(loggy->db, local_abspath, loggy->pool);
  if (err)
    return svn_error_createf(SVN_ERR_WC_BAD_ADM_LOG, err,
                             _("Error removing lock from entry for '%s'"),
                             name);

  return SVN_NO_ERROR;
}


/* Ben sez:  this log command is (at the moment) only executed by the
   update editor.  It attempts to forcefully remove working data. */
/* Delete a node from version control, and from disk if unmodified.
 * LOCAL_ABSPATH is the name of the file or directory to be deleted.
 * If it is unversioned,
 * do nothing and return no error. Otherwise, delete its WC entry and, if
 * the working version is unmodified, delete it from disk. */
static svn_error_t *
basic_delete_entry(svn_wc__db_t *db,
                   const char *local_abspath,
                   apr_pool_t *scratch_pool)
{
  svn_wc__db_kind_t kind;
  svn_boolean_t hidden;
  svn_error_t *err;

  /* Figure out if 'name' is a dir or a file */
  SVN_ERR(svn_wc__db_read_kind(&kind, db, local_abspath, TRUE, scratch_pool));
  if (kind == svn_wc__db_kind_unknown)
    return SVN_NO_ERROR; /* Already gone */

  SVN_ERR(svn_wc__db_node_hidden(&hidden, db, local_abspath, scratch_pool));
  if (hidden)
    return SVN_NO_ERROR;

  /* Remove the object from revision control -- whether it's a
     single file or recursive directory removal.  Attempt
     to destroy all working files & dirs too.

     ### We pass NULL, NULL for cancel_func and cancel_baton below.
     ### If they were available, it would be nice to use them. */
  if (kind == svn_wc__db_kind_dir)
    {
      svn_wc__db_status_t status;

      SVN_ERR(svn_wc__db_read_info(&status, NULL, NULL, NULL, NULL, NULL,
                                      NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                      NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                                      NULL, NULL, NULL, NULL,
                                   db, local_abspath,
                                   scratch_pool, scratch_pool));
      if (status == svn_wc__db_status_obstructed ||
          status == svn_wc__db_status_obstructed_add ||
          status == svn_wc__db_status_obstructed_delete)
        {
          /* Removing a missing wcroot is easy, just remove its parent entry
             ### BH: I can't tell why we don't use this for adds.
                     We might want to remove WC obstructions?

             We don't have a missing status in the final version of WC-NG,
             so why bother researching its history.
          */
          if (status != svn_wc__db_status_obstructed_add)
            {
              SVN_ERR(svn_wc__db_temp_op_remove_entry(db, local_abspath,
                                                      scratch_pool));

              return SVN_NO_ERROR;
            }
        }
    }

  err = svn_wc__internal_remove_from_revision_control(db,
                                                      local_abspath,
                                                      TRUE, /* destroy */
                                                      FALSE, /* instant_error*/
                                                      NULL, NULL,
                                                      scratch_pool);

  if (err && err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    {
      return svn_error_return(err);
    }
}


static svn_error_t *
log_do_delete_entry(struct log_runner *loggy,
                    const char *name,
                    svn_revnum_t revision,
                    svn_node_kind_t kind)
{
  const char *local_abspath;
  const char *repos_relpath, *repos_root, *repos_uuid;

  local_abspath = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);

  if (SVN_IS_VALID_REVNUM(revision))
    SVN_ERR(svn_wc__db_scan_base_repos(&repos_relpath, &repos_root,
                                       &repos_uuid, loggy->db, local_abspath,
                                       loggy->pool, loggy->pool));

  SVN_ERR(basic_delete_entry(loggy->db, local_abspath, loggy->pool));

  if (SVN_IS_VALID_REVNUM(revision))
    {
      SVN_ERR(svn_wc__db_base_add_absent_node(loggy->db,
                                              local_abspath,
                                              repos_relpath,
                                              repos_root,
                                              repos_uuid,
                                              revision,
                                              kind == svn_node_dir 
                                                   ? svn_wc__db_kind_dir
                                                   : svn_wc__db_kind_file,
                                              svn_wc__db_status_not_present,
                                              NULL,
                                              NULL,
                                              loggy->pool));
    }

  return SVN_NO_ERROR;
}

/* */
static svn_error_t *
log_do_add_tree_conflict(struct log_runner *loggy,
                         const char *victim_basename,
                         const char **atts)
{
  svn_skel_t *skel;
  const char *raw_conflict;
  const svn_wc_conflict_description2_t *new_conflict;
  svn_error_t *err;

  /* Convert the text data to a conflict. */
  raw_conflict = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_DATA, atts);
  skel = svn_skel__parse(raw_conflict, strlen(raw_conflict), loggy->pool);
  SVN_ERR(svn_wc__deserialize_conflict(&new_conflict,
                                       skel,
                                       loggy->adm_abspath,
                                       loggy->pool, loggy->pool));

  err = svn_wc__db_op_set_tree_conflict(loggy->db,
                                        new_conflict->local_abspath,
                                        new_conflict,
                                        loggy->pool);
  if (err)
    return svn_error_createf(SVN_ERR_WC_BAD_ADM_LOG, err,
                             _("Error recording tree conflict on '%s'"),
                             new_conflict->local_abspath);

  return SVN_NO_ERROR;
}

/* */
static void
start_handler(void *userData, const char *eltname, const char **atts)
{
  svn_error_t *err = SVN_NO_ERROR;
  struct log_runner *loggy = userData;

  /* Most elements use the `name' attribute, so grab it now. */
  const char *name = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_NAME, atts);

  /* Clear the per-log-item pool. */
  svn_pool_clear(loggy->pool);

  if (strcmp(eltname, "wc-log") == 0)   /* ignore expat pacifier */
    return;

  /* The NAME attribute should be present.  */
  SVN_ERR_ASSERT_NO_RETURN(name != NULL);

#ifdef DEBUG_LOG
  SVN_DBG(("start_handler: name='%s'\n", eltname));
#endif

  /* Dispatch. */
  if (strcmp(eltname, SVN_WC__LOG_DELETE_LOCK) == 0) {
    err = log_do_delete_lock(loggy, name);
  }
  else if (strcmp(eltname, SVN_WC__LOG_DELETE_ENTRY) == 0) {
    const char *attr;
    svn_revnum_t revision;
    svn_node_kind_t kind;

    attr = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_REVISION, atts);
    revision = SVN_STR_TO_REV(attr);
    attr = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_KIND, atts);
    if (strcmp(attr, "dir") == 0)
      kind = svn_node_dir;
    else
      kind = svn_node_file;
    err = log_do_delete_entry(loggy, name, revision, kind);
  }
  else if (strcmp(eltname, SVN_WC__LOG_MV) == 0) {
    const char *dest = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_DEST, atts);
    const char *from_abspath;
    const char *dest_abspath;

    SVN_ERR_ASSERT_NO_RETURN(dest != NULL);
    from_abspath = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);
    dest_abspath = svn_dirent_join(loggy->adm_abspath, dest, loggy->pool);
    err = log_do_file_move(from_abspath, dest_abspath, loggy->pool);
  }
  else if (strcmp(eltname, SVN_WC__LOG_CP_AND_TRANSLATE) == 0) {
    const char *dest = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_DEST, atts);
    const char *versioned = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_ARG_2,
                                                   atts);
    const char *from_abspath;
    const char *dest_abspath;
    const char *versioned_abspath;

    SVN_ERR_ASSERT_NO_RETURN(dest != NULL);
    SVN_ERR_ASSERT_NO_RETURN(versioned != NULL);
    from_abspath = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);
    dest_abspath = svn_dirent_join(loggy->adm_abspath, dest, loggy->pool);
    versioned_abspath = svn_dirent_join(loggy->adm_abspath, versioned,
                                        loggy->pool);
    err = log_do_file_cp_and_translate(loggy->db, from_abspath, dest_abspath,
                                       versioned_abspath, loggy->pool);
  }
  else if (strcmp(eltname, SVN_WC__LOG_SET_TIMESTAMP) == 0) {
    err = log_do_file_timestamp(loggy, name, atts);
  }
  else if (strcmp(eltname, SVN_WC__LOG_ADD_TREE_CONFLICT) == 0) {
    err = log_do_add_tree_conflict(loggy, name, atts);
  }
  else
    {
      SIGNAL_ERROR
        (loggy, svn_error_createf
         (SVN_ERR_WC_BAD_ADM_LOG, NULL,
          _("Unrecognized logfile element '%s' in '%s'"),
          eltname,
          svn_dirent_local_style(loggy->adm_abspath, loggy->pool)));
      return;
    }

  if (err)
    SIGNAL_ERROR
      (loggy, svn_error_createf
       (SVN_ERR_WC_BAD_ADM_LOG, err,
        _("Error processing command '%s' in '%s'"),
        eltname,
        svn_dirent_local_style(loggy->adm_abspath, loggy->pool)));

  return;
}



/*** Using the parser to run the log file. ***/


/* Run a sequence of log files. */
svn_error_t *
svn_wc__run_xml_log(svn_wc__db_t *db,
                    const char *adm_abspath,
                    const char *log_contents,
                    apr_size_t log_len,
                    apr_pool_t *scratch_pool)
{
  svn_xml_parser_t *parser;
  struct log_runner *loggy;

  loggy = apr_pcalloc(scratch_pool, sizeof(*loggy));

  parser = svn_xml_make_parser(loggy, start_handler, NULL, NULL,
                               scratch_pool);

  loggy->db = db;
  loggy->adm_abspath = adm_abspath;
  loggy->pool = svn_pool_create(scratch_pool);
  loggy->parser = parser;

  /* Expat wants everything wrapped in a top-level form, so start with
     a ghost open tag. */
  SVN_ERR(svn_xml_parse(parser, LOG_START, strlen(LOG_START), 0));

  SVN_ERR(svn_xml_parse(parser, log_contents, log_len, 0));

  /* Pacify Expat with a pointless closing element tag. */
  SVN_ERR(svn_xml_parse(parser, LOG_END, strlen(LOG_END), 1));

  svn_xml_free_parser(parser);

  return SVN_NO_ERROR;
}


/* Return (in *RELPATH) the portion of ABSPATH that is relative to the
   working copy directory ADM_ABSPATH, or SVN_WC_ENTRY_THIS_DIR if ABSPATH
   is that directory. ABSPATH must within ADM_ABSPATH.  */
static svn_error_t *
loggy_path(const char **relpath,
           const char *abspath,
           const char *adm_abspath,
           apr_pool_t *scratch_pool)
{
  *relpath = svn_dirent_is_child(adm_abspath, abspath, NULL);

  if (*relpath == NULL)
    {
      SVN_ERR_ASSERT(strcmp(abspath, adm_abspath) == 0);

      *relpath = "";
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__loggy_translated_file(svn_skel_t **work_item,
                              svn_wc__db_t *db,
                              const char *adm_abspath,
                              const char *dst_abspath,
                              const char *src_abspath,
                              const char *versioned_abspath,
                              apr_pool_t *result_pool)
{
  const char *loggy_path1;
  const char *loggy_path2;
  const char *loggy_path3;
  svn_stringbuf_t *log_accum = NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(adm_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(versioned_abspath));

  SVN_ERR(loggy_path(&loggy_path1, src_abspath, adm_abspath, result_pool));
  SVN_ERR(loggy_path(&loggy_path2, dst_abspath, adm_abspath, result_pool));
  SVN_ERR(loggy_path(&loggy_path3, versioned_abspath, adm_abspath,
                     result_pool));

  svn_xml_make_open_tag(&log_accum, result_pool, svn_xml_self_closing,
                        SVN_WC__LOG_CP_AND_TRANSLATE,
                        SVN_WC__LOG_ATTR_NAME, loggy_path1,
                        SVN_WC__LOG_ATTR_DEST, loggy_path2,
                        SVN_WC__LOG_ATTR_ARG_2, loggy_path3,
                        NULL);

  return svn_error_return(svn_wc__wq_build_loggy(work_item,
                                                 db, adm_abspath, log_accum,
                                                 result_pool));
}

svn_error_t *
svn_wc__loggy_delete_entry(svn_skel_t **work_item,
                           svn_wc__db_t *db,
                           const char *adm_abspath,
                           const char *local_abspath,
                           svn_revnum_t revision,
                           svn_wc__db_kind_t kind,
                           apr_pool_t *result_pool)
{
  const char *loggy_path1;
  svn_stringbuf_t *log_accum = NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(loggy_path(&loggy_path1, local_abspath, adm_abspath, result_pool));
  svn_xml_make_open_tag(&log_accum, result_pool, svn_xml_self_closing,
                        SVN_WC__LOG_DELETE_ENTRY,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path1,
                        SVN_WC__LOG_ATTR_REVISION,
                        apr_psprintf(result_pool, "%ld", revision),
                        SVN_WC__LOG_ATTR_KIND,
                        kind == svn_wc__db_kind_dir ? "dir" : "file",
                        NULL);

  return svn_error_return(svn_wc__wq_build_loggy(work_item,
                                                 db, adm_abspath, log_accum,
                                                 result_pool));
}

svn_error_t *
svn_wc__loggy_delete_lock(svn_skel_t **work_item,
                          svn_wc__db_t *db,
                          const char *adm_abspath,
                          const char *local_abspath,
                          apr_pool_t *result_pool)
{
  const char *loggy_path1;
  svn_stringbuf_t *log_accum = NULL;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(loggy_path(&loggy_path1, local_abspath, adm_abspath, result_pool));
  svn_xml_make_open_tag(&log_accum, result_pool, svn_xml_self_closing,
                        SVN_WC__LOG_DELETE_LOCK,
                        SVN_WC__LOG_ATTR_NAME, loggy_path1,
                        NULL);

  return svn_error_return(svn_wc__wq_build_loggy(work_item,
                                                 db, adm_abspath, log_accum,
                                                 result_pool));
}


svn_error_t *
svn_wc__loggy_move(svn_skel_t **work_item,
                   svn_wc__db_t *db,
                   const char *adm_abspath,
                   const char *src_abspath,
                   const char *dst_abspath,
                   apr_pool_t *result_pool)
{
  svn_stringbuf_t *log_accum = NULL;
  const char *loggy_path1;
  const char *loggy_path2;
  svn_node_kind_t kind;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(src_abspath));
  SVN_ERR_ASSERT(svn_dirent_is_absolute(dst_abspath));

  SVN_ERR(loggy_path(&loggy_path1, src_abspath, adm_abspath, result_pool));
  SVN_ERR(loggy_path(&loggy_path2, dst_abspath, adm_abspath, result_pool));

  SVN_ERR(svn_io_check_path(src_abspath, &kind, result_pool));

  /* ### idiocy of the old world. the file better exist, if we're asking
     ### to do some work with it.  */
  SVN_ERR_ASSERT(kind != svn_node_none);

  svn_xml_make_open_tag(&log_accum, result_pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_MV,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path1,
                        SVN_WC__LOG_ATTR_DEST,
                        loggy_path2,
                        NULL);

  return svn_error_return(svn_wc__wq_build_loggy(work_item,
                                                 db, adm_abspath, log_accum,
                                                 result_pool));
}


svn_error_t *
svn_wc__loggy_set_timestamp(svn_skel_t **work_item,
                            svn_wc__db_t *db,
                            const char *adm_abspath,
                            const char *local_abspath,
                            const char *timestr,
                            apr_pool_t *result_pool)
{
  svn_stringbuf_t *log_accum = NULL;
  const char *loggy_path1;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  SVN_ERR(loggy_path(&loggy_path1, local_abspath, adm_abspath, result_pool));
  svn_xml_make_open_tag(&log_accum,
                        result_pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_SET_TIMESTAMP,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path1,
                        SVN_WC__LOG_ATTR_TIMESTAMP,
                        timestr,
                        NULL);

  return svn_error_return(svn_wc__wq_build_loggy(work_item,
                                                 db, adm_abspath, log_accum,
                                                 result_pool));
}


svn_error_t *
svn_wc__loggy_add_tree_conflict(svn_skel_t **work_item,
                                svn_wc__db_t *db,
                                const char *adm_abspath,
                                const svn_wc_conflict_description2_t *conflict,
                                apr_pool_t *result_pool)
{
  svn_stringbuf_t *log_accum = NULL;
  const char *victim_basename;
  svn_skel_t *skel;
  const char *conflict_data;

  victim_basename = svn_dirent_basename(conflict->local_abspath, result_pool);
  SVN_ERR(svn_wc__serialize_conflict(&skel, conflict,
                                     result_pool, result_pool));
  conflict_data = svn_skel__unparse(skel, result_pool)->data,

  svn_xml_make_open_tag(&log_accum, result_pool, svn_xml_self_closing,
                        SVN_WC__LOG_ADD_TREE_CONFLICT,
                        SVN_WC__LOG_ATTR_NAME,
                        victim_basename,
                        SVN_WC__LOG_ATTR_DATA,
                        conflict_data,
                        NULL);

  return svn_error_return(svn_wc__wq_build_loggy(work_item,
                                                 db, adm_abspath, log_accum,
                                                 result_pool));
}


/*** Recursively do log things. ***/

/* */
static svn_error_t *
can_be_cleaned(int *wc_format,
               svn_wc__db_t *db,
               const char *local_abspath,
               apr_pool_t *scratch_pool)
{
  SVN_ERR(svn_wc__internal_check_wc(wc_format, db,
                                    local_abspath, scratch_pool));

  /* a "version" of 0 means a non-wc directory */
  if (*wc_format == 0)
    return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                             _("'%s' is not a working copy directory"),
                             svn_dirent_local_style(local_abspath,
                                                    scratch_pool));

  if (*wc_format < SVN_WC__WC_NG_VERSION)
    return svn_error_create(SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
                            _("Log format too old, please use "
                              "Subversion 1.6 or earlier"));

  return SVN_NO_ERROR;
}


/* */
static svn_error_t *
cleanup_internal(svn_wc__db_t *db,
                 const char *adm_abspath,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  int wc_format;
  svn_error_t *err;
  const apr_array_header_t *children;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  /* Check cancellation; note that this catches recursive calls too. */
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  /* Can we even work with this directory?  */
  SVN_ERR(can_be_cleaned(&wc_format, db, adm_abspath, iterpool));

  /* Lock this working copy directory, or steal an existing lock */
  err = svn_wc__db_wclock_set(db, adm_abspath, 0, iterpool);
  if (err && err->apr_err == SVN_ERR_WC_LOCKED)
    svn_error_clear(err);
  else if (err)
    return svn_error_return(err);
  SVN_ERR(svn_wc__db_temp_mark_locked(db, adm_abspath, iterpool));

  /* Run our changes before the subdirectories. We may not have to recurse
     if we blow away a subdir.  */
  if (wc_format >= SVN_WC__HAS_WORK_QUEUE)
    SVN_ERR(svn_wc__wq_run(db, adm_abspath, cancel_func, cancel_baton,
                           iterpool));

  /* Recurse on versioned, existing subdirectories.  */
  SVN_ERR(svn_wc__db_read_children(&children, db, adm_abspath,
                                   scratch_pool, iterpool));
  for (i = 0; i < children->nelts; i++)
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);
      const char *entry_abspath;
      svn_wc__db_kind_t kind;

      svn_pool_clear(iterpool);
      entry_abspath = svn_dirent_join(adm_abspath, name, iterpool);

      SVN_ERR(svn_wc__db_read_kind(&kind, db, entry_abspath, FALSE, iterpool));

      if (kind == svn_wc__db_kind_dir)
        {
          svn_node_kind_t disk_kind;

          SVN_ERR(svn_io_check_path(entry_abspath, &disk_kind, iterpool));
          if (disk_kind == svn_node_dir)
            SVN_ERR(cleanup_internal(db, entry_abspath,
                                     cancel_func, cancel_baton,
                                     iterpool));
        }
    }

  /* Cleanup the tmp area of the admin subdir, if running the log has not
     removed it!  The logs have been run, so anything left here has no hope
     of being useful. */
  SVN_ERR(svn_wc__adm_cleanup_tmp_area(db, adm_abspath, iterpool));

  /* All done, toss the lock */
  SVN_ERR(svn_wc__db_wclock_remove(db, adm_abspath, iterpool));

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


/* ### possibly eliminate the WC_CTX parameter? callers really shouldn't
   ### be doing anything *but* running a cleanup, and we need a special
   ### DB anyway. ... *shrug* ... consider later.  */
svn_error_t *
svn_wc_cleanup3(svn_wc_context_t *wc_ctx,
                const char *local_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* We need a DB that allows a non-empty work queue (though it *will*
     auto-upgrade). We'll handle everything manually.  */
  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readwrite,
                          NULL /* ### config */, TRUE, FALSE,
                          scratch_pool, scratch_pool));

  SVN_ERR(cleanup_internal(db, local_abspath, cancel_func, cancel_baton,
                           scratch_pool));

  /* We're done with this DB, so proactively close it.  */
  SVN_ERR(svn_wc__db_close(db));

  return SVN_NO_ERROR;
}
