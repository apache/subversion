/*
 * log.c:  handle the adm area's log file.
 *
 * ====================================================================
 *    Licensed to the Subversion Corporation (SVN Corp.) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The SVN Corp. licenses this file
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

/* Set some attributes on SVN_WC__LOG_ATTR_NAME's entry.  Unmentioned
   attributes are unaffected. */
#define SVN_WC__LOG_MODIFY_ENTRY        "modify-entry"

/* Delete lock related fields from the entry SVN_WC__LOG_ATTR_NAME. */
#define SVN_WC__LOG_DELETE_LOCK         "delete-lock"

/* Delete the entry SVN_WC__LOG_ATTR_NAME. */
#define SVN_WC__LOG_DELETE_ENTRY        "delete-entry"

/* Move file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST. */
#define SVN_WC__LOG_MV                  "mv"

/* Copy file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST, but
   expand any keywords and use any eol-style defined by properties of
   the DEST. */
#define SVN_WC__LOG_CP_AND_TRANSLATE    "cp-and-translate"

/* Remove file SVN_WC__LOG_ATTR_NAME. */
#define SVN_WC__LOG_RM                  "rm"

/* Append file from SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST. */
#define SVN_WC__LOG_APPEND              "append"

/* Make file SVN_WC__LOG_ATTR_NAME readonly */
#define SVN_WC__LOG_READONLY            "readonly"

/* Make file SVN_WC__LOG_ATTR_NAME readonly if needs-lock property is set
   and there is no lock token for the file in the working copy. */
#define SVN_WC__LOG_MAYBE_READONLY "maybe-readonly"

/* Make file SVN_WC__LOG_ATTR_NAME executable if the
   executable property is set. */
#define SVN_WC__LOG_MAYBE_EXECUTABLE "maybe-executable"

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
#define SVN_WC__LOG_ATTR_REVISION       "revision"
#define SVN_WC__LOG_ATTR_TIMESTAMP      "timestamp"
#define SVN_WC__LOG_ATTR_PROPNAME       "propname"
#define SVN_WC__LOG_ATTR_PROPVAL        "propval"
#define SVN_WC__LOG_ATTR_FORMAT         "format"
#define SVN_WC__LOG_ATTR_FORCE          "force"
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



/*** The XML handlers. ***/

/* Used by file_xfer_under_path(). */
enum svn_wc__xfer_action {
  svn_wc__xfer_mv,
  svn_wc__xfer_append,
  svn_wc__xfer_cp_and_translate
};


/* Perform some sort of copy-related ACTION on NAME and DEST:

      svn_wc__xfer_mv:                 do a copy, then remove NAME.
      svn_wc__xfer_append:             append contents of NAME to DEST
      svn_wc__xfer_cp_and_translate:   copy NAME to DEST, doing any eol
                                       and keyword expansion according to
                                       the current property vals of VERSIONED
                                       or, if that's NULL, those of DEST.
*/
static svn_error_t *
file_xfer_under_path(svn_wc__db_t *db,
                     const char *adm_abspath,
                     const char *name,
                     const char *dest,
                     const char *versioned,
                     enum svn_wc__xfer_action action,
                     apr_pool_t *scratch_pool)
{
  svn_error_t *err;
  const char *from_abspath;
  const char *dest_abspath;

  from_abspath = svn_dirent_join(adm_abspath, name, scratch_pool);
  dest_abspath = svn_dirent_join(adm_abspath, dest, scratch_pool);

  switch (action)
    {
    case svn_wc__xfer_append:
      err = svn_io_append_file(from_abspath, dest_abspath, scratch_pool);
      if (err)
        {
          if (!APR_STATUS_IS_ENOENT(err->apr_err))
            return svn_error_return(err);
          svn_error_clear(err);
        }
      break;

    case svn_wc__xfer_cp_and_translate:
      {
        const char *versioned_abspath;
        svn_subst_eol_style_t style;
        const char *eol;
        apr_hash_t *keywords;
        svn_boolean_t special;

        if (versioned)
          versioned_abspath = svn_dirent_join(adm_abspath, versioned,
                                              scratch_pool);
        else
          versioned_abspath = dest_abspath;

          err = svn_wc__get_eol_style(&style, &eol, db, versioned_abspath,
                                      scratch_pool, scratch_pool);
        if (! err)
          err = svn_wc__get_keywords(&keywords, db, versioned_abspath, NULL,
                                     scratch_pool, scratch_pool);
        if (! err)
          err = svn_wc__get_special(&special, db, versioned_abspath,
                                    scratch_pool);

        if (! err)
          err = svn_subst_copy_and_translate3
                (from_abspath, dest_abspath,
                 eol, TRUE,
                 keywords, TRUE,
                 special,
                 scratch_pool);

        if (err)
          {
            if (!APR_STATUS_IS_ENOENT(err->apr_err))
              return svn_error_return(err);
            svn_error_clear(err);
          }

        SVN_ERR(svn_wc__maybe_set_read_only(NULL, db, dest_abspath,
                                            scratch_pool));

        return svn_error_return(svn_wc__maybe_set_executable(
                                              NULL, db, dest_abspath,
                                              scratch_pool));
      }

    case svn_wc__xfer_mv:
      err = svn_io_file_rename(from_abspath, dest_abspath, scratch_pool);

      /* If we got an ENOENT, that's ok;  the move has probably
         already completed in an earlier run of this log.  */
      if (err)
        {
          if (!APR_STATUS_IS_ENOENT(err->apr_err))
            return svn_error_quick_wrap(err, _("Can't move source to dest"));
          svn_error_clear(err);
        }
      break;
    }

  return SVN_NO_ERROR;
}


/* Helper macro for erroring out while running a logfile.

   This is implemented as a macro so that the error created has a useful
   line number associated with it. */
#define SIGNAL_ERROR(loggy, err)                                   \
  svn_xml_signal_bailout                                           \
    (svn_error_createf(SVN_ERR_WC_BAD_ADM_LOG, err,                \
                       _("In directory '%s'"),                     \
                       svn_dirent_local_style(loggy->adm_abspath,  \
                                              loggy->pool)),       \
     loggy->parser)



/*** Dispatch on the xml opening tag. ***/

static svn_error_t *
log_do_file_xfer(struct log_runner *loggy,
                 const char *name,
                 enum svn_wc__xfer_action action,
                 const char **atts)
{
  svn_error_t *err;
  const char *dest = NULL;
  const char *versioned;

  /* We have the name (src), and the destination is absolutely required. */
  dest = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_DEST, atts);
  versioned = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_ARG_2, atts);

  if (! dest)
    return svn_error_createf(SVN_ERR_WC_BAD_ADM_LOG, NULL,
                             _("Missing 'dest' attribute in '%s'"),
                             svn_dirent_local_style(loggy->adm_abspath,
                                                    loggy->pool));

  err = file_xfer_under_path(loggy->db, loggy->adm_abspath, name, dest,
                             versioned, action, loggy->pool);
  if (err)
    SIGNAL_ERROR(loggy, err);

  return SVN_NO_ERROR;
}

/* Make file NAME in log's CWD readonly */
static svn_error_t *
log_do_file_readonly(struct log_runner *loggy,
                     const char *name)
{
  svn_error_t *err;
  const char *local_abspath
    = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);

  err = svn_io_set_file_read_only(local_abspath, FALSE, loggy->pool);
  if (err)
    {
      if (!APR_STATUS_IS_ENOENT(err->apr_err))
        return svn_error_return(err);
      svn_error_clear(err);
    }
  return SVN_NO_ERROR;
}

/* Maybe make file NAME in log's CWD executable */
static svn_error_t *
log_do_file_maybe_executable(struct log_runner *loggy,
                             const char *name)
{
  const char *local_abspath
    = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);

  return svn_error_return(svn_wc__maybe_set_executable(
                                NULL, loggy->db, local_abspath, loggy->pool));
}

/* Maybe make file NAME in log's CWD readonly */
static svn_error_t *
log_do_file_maybe_readonly(struct log_runner *loggy,
                           const char *name)
{
  const char *local_abspath
    = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);

  return svn_wc__maybe_set_read_only(NULL, loggy->db, local_abspath,
                                     loggy->pool);
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


/* Remove file NAME in log's CWD. */
static svn_error_t *
log_do_rm(struct log_runner *loggy, const char *name)
{
  const char *local_abspath
    = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);

  return svn_error_return(
    svn_io_remove_file2(local_abspath, TRUE, loggy->pool));
}


static svn_error_t *
log_do_modify_entry(struct log_runner *loggy,
                    const char *name,
                    const char **atts)
{
  svn_error_t *err;
  apr_hash_t *ah = svn_xml_make_att_hash(atts, loggy->pool);
  const char *local_abspath;
  svn_wc_entry_t *entry;
  apr_uint64_t modify_flags;
  const char *valuestr;

  local_abspath = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);

  /* Convert the attributes into an entry structure. */
  SVN_ERR(svn_wc__atts_to_entry(&entry, &modify_flags, ah, loggy->pool));

  /* svn_wc__atts_to_entry will no-op if the TEXT_TIME timestamp is
     SVN_WC__TIMESTAMP_WC, so look for that case and fill in the proper
     value. */
  valuestr = apr_hash_get(ah, SVN_WC__ENTRY_ATTR_TEXT_TIME,
                          APR_HASH_KEY_STRING);
  if ((modify_flags & SVN_WC__ENTRY_MODIFY_TEXT_TIME)
      && (! strcmp(valuestr, SVN_WC__TIMESTAMP_WC)))
    {
      apr_time_t text_time;

      err = svn_io_file_affected_time(&text_time, local_abspath, loggy->pool);
      if (err)
        return svn_error_createf
          (SVN_ERR_WC_BAD_ADM_LOG, err,
           _("Error getting 'affected time' on '%s'"),
           svn_dirent_local_style(local_abspath, loggy->pool));

      entry->text_time = text_time;
    }

  valuestr = apr_hash_get(ah, SVN_WC__ENTRY_ATTR_WORKING_SIZE,
                          APR_HASH_KEY_STRING);
  if ((modify_flags & SVN_WC__ENTRY_MODIFY_WORKING_SIZE)
      && (! strcmp(valuestr, SVN_WC__WORKING_SIZE_WC)))
    {
      apr_finfo_t finfo;
      const svn_wc_entry_t *tfile_entry;

      err = svn_wc__get_entry(&tfile_entry, loggy->db, local_abspath, TRUE,
                              svn_node_file, FALSE,
                              loggy->pool, loggy->pool);
      if (err)
        SIGNAL_ERROR(loggy, err);

      if (! tfile_entry)
        return SVN_NO_ERROR;

      err = svn_io_stat(&finfo, local_abspath, APR_FINFO_MIN | APR_FINFO_LINK,
                        loggy->pool);
      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        {
          svn_error_clear(err);
          finfo.size = 0;
        }
      else if (err)
        return svn_error_createf
          (SVN_ERR_WC_BAD_ADM_LOG, NULL,
            _("Error getting file size on '%s'"),
            svn_dirent_local_style(local_abspath, loggy->pool));

      entry->working_size = finfo.size;
    }

  /* Handle force flag. */
  valuestr = apr_hash_get(ah, SVN_WC__LOG_ATTR_FORCE,
                          APR_HASH_KEY_STRING);
  if (valuestr && strcmp(valuestr, "true") == 0)
    modify_flags |= SVN_WC__ENTRY_MODIFY_FORCE;

  /* It is possible that we will find a log that has a misordered sequence
     of entry modifications and wcprop modifications. The entry must be
     "not hidden" before wcprops can be installed. The sequence of actions
     will look like:

       1. modify_entry
       2. modify_wcprops
       3. modify_entry(DELETED=FALSE)

     Step 2 will fail if the current node is marked DELETED. r36697 fixes
     the ordering, moving step 3 to the beginning of the sequence. However,
     old logs may still contain the above sequence. To compensate, we will
     attempt to detect the pattern used by step 1, and preemptively clear
     the DELETED flag.

     The misordered entry is written by accumulate_entry_props() in
     update_editor.c. That may modify the CMT_* values and/or the UUID.
     If we see any of those, then we've detected a modify_entry constructed
     by that function. And that means we *just* ran a step 3 (new code)
     or we *will* run a step 3 (too late; old code). In both situations,
     we can safely clear the DELETED flag.

     The UUID modification is *only* performed by that function. The CMT_*
     changes are also performed by process_committed_leaf() in adm_ops.c.
     A just-committed node setting these values will NEVER be DELETED,
     so it is safe to clear the value.  */
  if (modify_flags & (SVN_WC__ENTRY_MODIFY_CMT_REV
                      | SVN_WC__ENTRY_MODIFY_CMT_DATE
                      | SVN_WC__ENTRY_MODIFY_CMT_AUTHOR))
    {
      entry->deleted = FALSE;
      modify_flags |= SVN_WC__ENTRY_MODIFY_DELETED;
    }

  /* Now write the new entry out. Note that we want to always operate
     on the stub if name is not THIS_DIR. This loggy function is intended
     to operate on the data in ADM_ABSPATH, so we do NOT want to reach
     down into a subdir. For entry_modify2(), it is okay to set PARENT_STUB
     to TRUE for files (kind errors are not raised).  */
  err = svn_wc__entry_modify2(loggy->db,
                              svn_dirent_join(loggy->adm_abspath,
                                              name,
                                              loggy->pool),
                              svn_node_unknown,
                              *name != '\0' /* parent_stub */,
                              entry, modify_flags, loggy->pool);
  if (err)
    return svn_error_createf(SVN_ERR_WC_BAD_ADM_LOG, err,
                             _("Error modifying entry for '%s'"), name);

  return SVN_NO_ERROR;
}

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
 * NAME is the name of the file or directory to be deleted, which is a child
 * of the directory represented by LOGGY->adm_access. If it is unversioned,
 * do nothing and return no error. Otherwise, delete its WC entry and, if
 * the working version is unmodified, delete it from disk. */
static svn_error_t *
log_do_delete_entry(struct log_runner *loggy, const char *name)
{
  const char *local_abspath;
  svn_wc__db_kind_t kind;
  svn_boolean_t hidden;
  svn_error_t *err;

  local_abspath = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);

  /* Figure out if 'name' is a dir or a file */
  SVN_ERR(svn_wc__db_read_kind(&kind, loggy->db, local_abspath, TRUE,
                               loggy->pool));

  if (kind == svn_wc__db_kind_unknown)
    return SVN_NO_ERROR; /* Already gone */

  SVN_ERR(svn_wc__db_node_hidden(&hidden, loggy->db, local_abspath,
                                 loggy->pool));

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
                                      loggy->db, local_abspath,
                                      loggy->pool, loggy->pool));
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
              SVN_ERR(svn_wc__entry_remove(loggy->db, local_abspath,
                                           loggy->pool));

              return SVN_NO_ERROR;
            }
        }
      else
        {
          /* Deleting full_path requires that any children it has are
             also locked (issue #3039). */
            SVN_ERR(svn_wc__adm_extend_lock_to_tree(loggy->db,
                                                    local_abspath,
                                                    loggy->pool));
        }
    }

  err = svn_wc__internal_remove_from_revision_control(loggy->db,
                                                      local_abspath,
                                                      TRUE, /* destroy */
                                                      FALSE, /* instant_error*/
                                                      NULL, NULL,
                                                      loggy->pool);

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
  else if (! name)
    {
      SIGNAL_ERROR
        (loggy, svn_error_createf
         (SVN_ERR_WC_BAD_ADM_LOG, NULL,
          _("Log entry missing 'name' attribute (entry '%s' "
            "for directory '%s')"),
          eltname,
          svn_dirent_local_style(loggy->adm_abspath, loggy->pool)));
      return;
    }

  /* Dispatch. */
  if (strcmp(eltname, SVN_WC__LOG_MODIFY_ENTRY) == 0) {
    err = log_do_modify_entry(loggy, name, atts);
  }
  else if (strcmp(eltname, SVN_WC__LOG_DELETE_LOCK) == 0) {
    err = log_do_delete_lock(loggy, name);
  }
  else if (strcmp(eltname, SVN_WC__LOG_DELETE_ENTRY) == 0) {
    err = log_do_delete_entry(loggy, name);
  }
  else if (strcmp(eltname, SVN_WC__LOG_RM) == 0) {
    err = log_do_rm(loggy, name);
  }
  else if (strcmp(eltname, SVN_WC__LOG_MV) == 0) {
    err = log_do_file_xfer(loggy, name, svn_wc__xfer_mv, atts);
  }
  else if (strcmp(eltname, SVN_WC__LOG_CP_AND_TRANSLATE) == 0) {
    err = log_do_file_xfer(loggy, name, svn_wc__xfer_cp_and_translate, atts);
  }
  else if (strcmp(eltname, SVN_WC__LOG_APPEND) == 0) {
    err = log_do_file_xfer(loggy, name, svn_wc__xfer_append, atts);
  }
  else if (strcmp(eltname, SVN_WC__LOG_READONLY) == 0) {
    err = log_do_file_readonly(loggy, name);
  }
  else if (strcmp(eltname, SVN_WC__LOG_MAYBE_READONLY) == 0) {
    err = log_do_file_maybe_readonly(loggy, name);
  }
  else if (strcmp(eltname, SVN_WC__LOG_MAYBE_EXECUTABLE) == 0) {
    err = log_do_file_maybe_executable(loggy, name);
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


svn_error_t *
svn_wc__run_log2(svn_wc__db_t *db,
                 const char *adm_abspath,
                 apr_pool_t *scratch_pool)
{
  /* Verify that we're holding this directory's write lock.  */
  SVN_ERR(svn_wc__write_check(db, adm_abspath, scratch_pool));

  return svn_error_return(svn_wc__wq_run(
                            db, adm_abspath,
                            NULL, NULL,
                            scratch_pool));
}




/*** Log file generation helpers ***/

/* Extend LOG_ACCUM with log operations to do MOVE_COPY_OP to SRC_PATH and
 * DST_PATH.
 *
 * SRC_PATH and DST_PATH are relative to ADM_ABSPATH.
 */
static svn_error_t *
loggy_move_copy_internal(svn_stringbuf_t **log_accum,
                         svn_boolean_t is_move,
                         const char *adm_abspath,
                         const char *src_path, const char *dst_path,
                         apr_pool_t *result_pool,
                         apr_pool_t *scratch_pool)
{
  svn_node_kind_t kind;
  const char *src_abspath = svn_dirent_join(adm_abspath, src_path,
                                            scratch_pool);

  SVN_ERR(svn_io_check_path(src_abspath, &kind, scratch_pool));

  /* Does this file exist? */
  if (kind != svn_node_none)
    {
      svn_xml_make_open_tag(log_accum, result_pool,
                            svn_xml_self_closing,
                            is_move
                              ? SVN_WC__LOG_MV
                              : SVN_WC__LOG_CP_AND_TRANSLATE,
                            SVN_WC__LOG_ATTR_NAME,
                            src_path,
                            SVN_WC__LOG_ATTR_DEST,
                            dst_path,
                            NULL);
    }

  return SVN_NO_ERROR;
}




/* Return the portion of PATH that is relative to the working copy directory
 * ADM_ABSPATH, or SVN_WC_ENTRY_THIS_DIR if PATH is that directory. PATH must
 * not be outside that directory. */
static svn_error_t *
loggy_path(const char **logy_path,
           const char *path,
           const char *adm_abspath,
           apr_pool_t *scratch_pool)
{
  const char *abspath;

  SVN_ERR(svn_dirent_get_absolute(&abspath, path, scratch_pool));
  *logy_path = svn_dirent_is_child(adm_abspath, abspath, NULL);

  if (! (*logy_path))
    {
      if (strcmp(abspath, adm_abspath) == 0) /* same path */
        *logy_path = SVN_WC_ENTRY_THIS_DIR;
      else /* not a child path */
        return svn_error_createf(SVN_ERR_BAD_RELATIVE_PATH, NULL,
                                 _("Path '%s' is not a child of '%s'"),
                                 svn_dirent_local_style(path, scratch_pool),
                                 svn_dirent_local_style(adm_abspath,
                                                        scratch_pool));
    }

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_append(svn_stringbuf_t **log_accum,
                     const char *adm_abspath,
                     const char *src, const char *dst,
                     apr_pool_t *pool)
{
  const char *loggy_path1;
  const char *loggy_path2;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(adm_abspath));

  SVN_ERR(loggy_path(&loggy_path1, src, adm_abspath, pool));
  SVN_ERR(loggy_path(&loggy_path2, dst, adm_abspath, pool));
  svn_xml_make_open_tag(log_accum, pool,
                        svn_xml_self_closing, SVN_WC__LOG_APPEND,
                        SVN_WC__LOG_ATTR_NAME, loggy_path1,
                        SVN_WC__LOG_ATTR_DEST, loggy_path2,
                        NULL);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__loggy_copy(svn_stringbuf_t **log_accum,
                   const char *adm_abspath,
                   const char *src_path, const char *dst_path,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *loggy_path1;
  const char *loggy_path2;

  SVN_ERR(loggy_path(&loggy_path1, src_path, adm_abspath, scratch_pool));
  SVN_ERR(loggy_path(&loggy_path2, dst_path, adm_abspath, scratch_pool));
  return loggy_move_copy_internal(log_accum, FALSE, adm_abspath,
                                  loggy_path1, loggy_path2,
                                  result_pool, scratch_pool);
}

svn_error_t *
svn_wc__loggy_translated_file(svn_stringbuf_t **log_accum,
                              const char *adm_abspath,
                              const char *dst,
                              const char *src,
                              const char *versioned,
                              apr_pool_t *result_pool,
                              apr_pool_t *scratch_pool)
{
  const char *loggy_path1;
  const char *loggy_path2;
  const char *loggy_path3;

  SVN_ERR(loggy_path(&loggy_path1, src, adm_abspath, scratch_pool));
  SVN_ERR(loggy_path(&loggy_path2, dst, adm_abspath, scratch_pool));
  SVN_ERR(loggy_path(&loggy_path3, versioned, adm_abspath, scratch_pool));
  svn_xml_make_open_tag
    (log_accum, result_pool, svn_xml_self_closing,
     SVN_WC__LOG_CP_AND_TRANSLATE,
     SVN_WC__LOG_ATTR_NAME, loggy_path1,
     SVN_WC__LOG_ATTR_DEST, loggy_path2,
     SVN_WC__LOG_ATTR_ARG_2, loggy_path3,
     NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_delete_entry(svn_wc__db_t *db,
                           const char *adm_abspath,
                           const char *path,
                           apr_pool_t *scratch_pool)
{
  const char *loggy_path1;
  svn_stringbuf_t *buf = NULL;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(&buf, scratch_pool, svn_xml_self_closing,
                        SVN_WC__LOG_DELETE_ENTRY,
                        SVN_WC__LOG_ATTR_NAME, loggy_path1,
                        NULL);

  return svn_error_return(svn_wc__wq_add_loggy(db, adm_abspath, buf,
                                               scratch_pool));
}

svn_error_t *
svn_wc__loggy_delete_lock(svn_wc__db_t *db,
                          const char *adm_abspath,
                          const char *path,
                          apr_pool_t *scratch_pool)
{
  const char *loggy_path1;
  svn_stringbuf_t *buf = NULL;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(&buf, scratch_pool, svn_xml_self_closing,
                        SVN_WC__LOG_DELETE_LOCK,
                        SVN_WC__LOG_ATTR_NAME, loggy_path1,
                        NULL);

  return svn_error_return(svn_wc__wq_add_loggy(db, adm_abspath, buf,
                                               scratch_pool));
}


svn_error_t *
svn_wc__loggy_entry_modify(svn_stringbuf_t **log_accum,
                           const char *adm_abspath,
                           const char *path,
                           const svn_wc_entry_t *entry,
                           apr_uint64_t modify_flags,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *loggy_path1;
  apr_hash_t *prop_hash = apr_hash_make(scratch_pool);
  static const char *kind_str[] =
    { "none",
      SVN_WC__ENTRIES_ATTR_FILE_STR,
      SVN_WC__ENTRIES_ATTR_DIR_STR,
      "unknown",
    };
  static const char *schedule_str[] =
    {
      "", /* svn_wc_schedule_normal */
      SVN_WC__ENTRY_VALUE_ADD,
      SVN_WC__ENTRY_VALUE_DELETE,
      SVN_WC__ENTRY_VALUE_REPLACE,
    };


  if (! modify_flags)
    return SVN_NO_ERROR;

#define ADD_ENTRY_ATTR(attr_flag, attr_name, value) \
   if (modify_flags & (attr_flag)) \
     apr_hash_set(prop_hash, (attr_name), APR_HASH_KEY_STRING, value)

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_REVISION,
                 SVN_WC__ENTRY_ATTR_REVISION,
                 apr_psprintf(scratch_pool, "%ld", entry->revision));

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_URL,
                 SVN_WC__ENTRY_ATTR_URL,
                 entry->url);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_KIND,
                 SVN_WC__ENTRY_ATTR_KIND,
                 kind_str[entry->kind]);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_SCHEDULE,
                 SVN_WC__ENTRY_ATTR_SCHEDULE,
                 schedule_str[entry->schedule]);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_COPIED,
                 SVN_WC__ENTRY_ATTR_COPIED,
                 entry->copied ? "true" : "false");

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_DELETED,
                 SVN_WC__ENTRY_ATTR_DELETED,
                 entry->deleted ? "true" : "false");

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_ABSENT,
                 SVN_WC__ENTRY_ATTR_ABSENT,
                 entry->absent ? "true" : "false");

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_COPYFROM_URL,
                 SVN_WC__ENTRY_ATTR_COPYFROM_URL,
                 entry->copyfrom_url);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_COPYFROM_REV,
                 SVN_WC__ENTRY_ATTR_COPYFROM_REV,
                 apr_psprintf(scratch_pool, "%ld", entry->copyfrom_rev));

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_CONFLICT_OLD,
                 SVN_WC__ENTRY_ATTR_CONFLICT_OLD,
                 entry->conflict_old ? entry->conflict_old : "");

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_CONFLICT_NEW,
                 SVN_WC__ENTRY_ATTR_CONFLICT_NEW,
                 entry->conflict_new ? entry->conflict_new : "");

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_CONFLICT_WRK,
                 SVN_WC__ENTRY_ATTR_CONFLICT_WRK,
                 entry->conflict_wrk ? entry->conflict_wrk : "");

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_PREJFILE,
                 SVN_WC__ENTRY_ATTR_PREJFILE,
                 entry->prejfile ? entry->prejfile : "");

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_TEXT_TIME,
                 SVN_WC__ENTRY_ATTR_TEXT_TIME,
                 svn_time_to_cstring(entry->text_time, scratch_pool));

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_CHECKSUM,
                 SVN_WC__ENTRY_ATTR_CHECKSUM,
                 entry->checksum);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_CMT_REV,
                 SVN_WC__ENTRY_ATTR_CMT_REV,
                 apr_psprintf(scratch_pool, "%ld", entry->cmt_rev));

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_CMT_DATE,
                 SVN_WC__ENTRY_ATTR_CMT_DATE,
                 svn_time_to_cstring(entry->cmt_date, scratch_pool));

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_CMT_AUTHOR,
                 SVN_WC__ENTRY_ATTR_CMT_AUTHOR,
                 entry->cmt_author);

  /* Note: LOCK flags are no longer passed to this function.  */

  /* Note: ignoring the (deprecated) has_props, has_prop_mods,
     cachable_props, and present_props fields. */

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_WORKING_SIZE,
                 SVN_WC__ENTRY_ATTR_WORKING_SIZE,
                 apr_psprintf(scratch_pool, "%" APR_OFF_T_FMT,
                              entry->working_size));

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_FORCE,
                 SVN_WC__LOG_ATTR_FORCE,
                 "true");

#undef ADD_ENTRY_ATTR

  if (apr_hash_count(prop_hash) == 0)
    return SVN_NO_ERROR;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  apr_hash_set(prop_hash, SVN_WC__LOG_ATTR_NAME,
               APR_HASH_KEY_STRING, loggy_path1);

  svn_xml_make_open_tag_hash(log_accum, result_pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_MODIFY_ENTRY,
                             prop_hash);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__loggy_move(svn_stringbuf_t **log_accum,
                   const char *adm_abspath,
                   const char *src_path, const char *dst_path,
                   apr_pool_t *result_pool,
                   apr_pool_t *scratch_pool)
{
  const char *loggy_path1;
  const char *loggy_path2;

  SVN_ERR(loggy_path(&loggy_path1, src_path, adm_abspath, scratch_pool));
  SVN_ERR(loggy_path(&loggy_path2, dst_path, adm_abspath, scratch_pool));
  return loggy_move_copy_internal(log_accum, TRUE, adm_abspath,
                                  loggy_path1, loggy_path2,
                                  result_pool, scratch_pool);
}

svn_error_t *
svn_wc__loggy_maybe_set_executable(svn_stringbuf_t **log_accum,
                                   const char *adm_abspath,
                                   const char *path,
                                   apr_pool_t *result_pool,
                                   apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(log_accum,
                        result_pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_MAYBE_EXECUTABLE,
                        SVN_WC__LOG_ATTR_NAME, loggy_path1,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_maybe_set_readonly(svn_stringbuf_t **log_accum,
                                 const char *adm_abspath,
                                 const char *path,
                                 apr_pool_t *result_pool,
                                 apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(log_accum,
                        result_pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_MAYBE_READONLY,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path1,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_set_entry_timestamp_from_wc(svn_stringbuf_t **log_accum,
                                          const char *adm_abspath,
                                          const char *path,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(log_accum,
                        result_pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_MODIFY_ENTRY,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path1,
                        SVN_WC__ENTRY_ATTR_TEXT_TIME,
                        SVN_WC__TIMESTAMP_WC,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_set_entry_working_size_from_wc(svn_stringbuf_t **log_accum,
                                             const char *adm_abspath,
                                             const char *path,
                                             apr_pool_t *result_pool,
                                             apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(log_accum,
                        result_pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_MODIFY_ENTRY,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path1,
                        SVN_WC__ENTRY_ATTR_WORKING_SIZE,
                        SVN_WC__WORKING_SIZE_WC,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_set_readonly(svn_stringbuf_t **log_accum,
                           const char *adm_abspath,
                           const char *path,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(log_accum,
                        result_pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_READONLY,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path1,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_set_timestamp(svn_stringbuf_t **log_accum,
                            const char *adm_abspath,
                            const char *path,
                            const char *timestr,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(log_accum,
                        result_pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_SET_TIMESTAMP,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path1,
                        SVN_WC__LOG_ATTR_TIMESTAMP,
                        timestr,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_remove(svn_stringbuf_t **log_accum,
                     const char *adm_abspath,
                     const char *path,
                     apr_pool_t *result_pool,
                     apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  /* No need to check whether BASE_NAME exists: ENOENT is ignored
     by the log-runner */
  svn_xml_make_open_tag(log_accum, result_pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_RM,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path1,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_add_tree_conflict(svn_stringbuf_t **log_accum,
                                const svn_wc_conflict_description2_t *conflict,
                                apr_pool_t *pool)
{
  const char *victim_basename;
  svn_skel_t *skel;
  const char *conflict_data;

  victim_basename = svn_dirent_basename(conflict->local_abspath, pool);
  SVN_ERR(svn_wc__serialize_conflict(&skel, conflict, pool, pool));
  conflict_data = svn_skel__unparse(skel, pool)->data,
 
  svn_xml_make_open_tag(log_accum, pool, svn_xml_self_closing,
                        SVN_WC__LOG_ADD_TREE_CONFLICT,
                        SVN_WC__LOG_ATTR_NAME,
                        victim_basename,
                        SVN_WC__LOG_ATTR_DATA,
                        conflict_data,
                        NULL);

  return SVN_NO_ERROR;
}


/*** Recursively do log things. ***/

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
  err = svn_wc__db_wclock_set(db, adm_abspath, iterpool);
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
