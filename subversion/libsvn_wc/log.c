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

/* Delete changelist field from the entry SVN_WC__LOG_ATTR_NAME. */
#define SVN_WC__LOG_DELETE_CHANGELIST   "delete-changelist"

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
#define SVN_WC__LOG_ADD_TREE_CONFLICT   "add-tree-conflict"


/* Handle closure after a commit completes successfully:
 *
 *   If SVN/tmp/text-base/SVN_WC__LOG_ATTR_NAME exists, then
 *      compare SVN/tmp/text-base/SVN_WC__LOG_ATTR_NAME with working file
 *         if they're the same, use working file's timestamp
 *         else use SVN/tmp/text-base/SVN_WC__LOG_ATTR_NAME's timestamp
 *      set SVN_WC__LOG_ATTR_NAME's revision to N
 */
#define SVN_WC__LOG_COMMITTED           "committed"

/* On target SVN_WC__LOG_ATTR_NAME, set wc property
   SVN_WC__LOG_ATTR_PROPNAME to value SVN_WC__LOG_ATTR_PROPVAL.  If
   SVN_WC__LOG_ATTR_PROPVAL is absent, then remove the property. */
#define SVN_WC__LOG_MODIFY_WCPROP        "modify-wcprop"


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
  apr_pool_t *result_pool;
  svn_xml_parser_t *parser;
  svn_boolean_t rerun;
  svn_wc_adm_access_t *adm_access;  /* the dir in which all this happens */
  apr_hash_t *tree_conflicts;         /* hash of pointers to
                                         svn_wc_conflict_description2_t. */
  /* Which top-level log element we're on for this logfile.  Some
     callers care whether a failure happened on the first element or
     on some later element (e.g., 'svn cleanup').

     This is initialized to 0 when the log_runner is created, and
     incremented every time start_handler() is called. */
  int count;
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
                     svn_boolean_t rerun,
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
          if (! rerun || ! APR_STATUS_IS_ENOENT(err->apr_err))
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
            if (! rerun || ! APR_STATUS_IS_ENOENT(err->apr_err))
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
          if (! rerun || ! APR_STATUS_IS_ENOENT(err->apr_err))
            return svn_error_quick_wrap(err, _("Can't move source to dest"));
          svn_error_clear(err);
        }
    }

  return SVN_NO_ERROR;
}


/* If new text was committed, then replace the text base for
 * newly-committed file NAME in directory PATH with the new
 * post-commit text base, which is waiting in the adm tmp area in
 * detranslated form.
 *
 * If eol and/or keyword translation would cause the working file to
 * change, then overwrite the working file with a translated copy of
 * the new text base (but only if the translated copy differs from the
 * current working file -- if they are the same, do nothing, to avoid
 * clobbering timestamps unnecessarily).
 *
 * If the executable property is set, the set working file's
 * executable.
 *
 * If the working file was re-translated or had executability set,
 * then set OVERWROTE_WORKING to TRUE.  If the working file isn't
 * touched at all, then set to FALSE.
 *
 * Use SCRATCH_POOL for any temporary allocation.
 */
static svn_error_t *
install_committed_file(svn_boolean_t *overwrote_working,
                       svn_wc__db_t *db,
                       const char *adm_abspath,
                       const char *name,
                       svn_boolean_t remove_executable,
                       svn_boolean_t remove_read_only,
                       apr_pool_t *scratch_pool)
{
  const char *tmp_text_base;
  svn_node_kind_t kind;
  svn_boolean_t same, did_set;
  const char *tmp_wfile;
  svn_boolean_t special;
  const char *file_abspath;

  /* start off assuming that the working file isn't touched. */
  *overwrote_working = FALSE;

  file_abspath = svn_dirent_join(adm_abspath, name, scratch_pool);

  /* In the commit, newlines and keywords may have been
   * canonicalized and/or contracted... Or they may not have
   * been.  It's kind of hard to know.  Here's how we find out:
   *
   *    1. Make a translated tmp copy of the committed text base.
   *       Or, if no committed text base exists (the commit must have
   *       been a propchange only), make a translated tmp copy of the
   *       working file.
   *    2. Compare the translated tmpfile to the working file.
   *    3. If different, copy the tmpfile over working file.
   *
   * This means we only rewrite the working file if we absolutely
   * have to, which is good because it avoids changing the file's
   * timestamp unless necessary, so editors aren't tempted to
   * reread the file if they don't really need to.
   */

  /* Is there a tmp_text_base that needs to be installed?  */
  SVN_ERR(svn_wc__text_base_path(&tmp_text_base, db, file_abspath, TRUE,
                                 scratch_pool));
  SVN_ERR(svn_io_check_path(tmp_text_base, &kind, scratch_pool));

  {
    const char *tmp = (kind == svn_node_file) ? tmp_text_base : file_abspath;

    SVN_ERR(svn_wc__internal_translated_file(&tmp_wfile, tmp, db,
                                             file_abspath,
                                             SVN_WC_TRANSLATE_FROM_NF,
                                             scratch_pool, scratch_pool));

    /* If the translation is a no-op, the text base and the working copy
     * file contain the same content, because we use the same props here
     * as were used to detranslate from working file to text base.
     *
     * In that case: don't replace the working file, but make sure
     * it has the right executable and read_write attributes set.
     */

    SVN_ERR(svn_wc__get_special(&special, db, file_abspath, scratch_pool));
    if (! special && tmp != tmp_wfile)
      SVN_ERR(svn_io_files_contents_same_p(&same, tmp_wfile,
                                           file_abspath, scratch_pool));
    else
      same = TRUE;
  }

  if (! same)
    {
      SVN_ERR(svn_io_file_rename(tmp_wfile, file_abspath, scratch_pool));
      *overwrote_working = TRUE;
    }

  if (remove_executable)
    {
      /* No need to chmod -x on a new file: new files don't have it. */
      if (same)
        SVN_ERR(svn_io_set_file_executable(file_abspath,
                                           FALSE, /* chmod -x */
                                           FALSE, scratch_pool));
      *overwrote_working = TRUE; /* entry needs wc-file's timestamp  */
    }
  else
    {
      /* Set the working file's execute bit if props dictate. */
      SVN_ERR(svn_wc__maybe_set_executable(&did_set, db, file_abspath,
                                           scratch_pool));
      if (did_set)
        /* okay, so we didn't -overwrite- the working file, but we changed
           its timestamp, which is the point of returning this flag. :-) */
        *overwrote_working = TRUE;
    }

  if (remove_read_only)
    {
      /* No need to make a new file read_write: new files already are. */
      if (same)
        SVN_ERR(svn_io_set_file_read_write(file_abspath, FALSE,
                                           scratch_pool));
      *overwrote_working = TRUE; /* entry needs wc-file's timestamp  */
    }
  else
    {
      SVN_ERR(svn_wc__maybe_set_read_only(&did_set, db, file_abspath,
                                          scratch_pool));
      if (did_set)
        /* okay, so we didn't -overwrite- the working file, but we changed
           its timestamp, which is the point of returning this flag. :-) */
        *overwrote_working = TRUE;
    }

  /* Install the new text base if one is waiting. */
  if (kind == svn_node_file)  /* tmp_text_base exists */
    SVN_ERR(svn_wc__sync_text_base(file_abspath, scratch_pool));

  return SVN_NO_ERROR;
}


/* Sometimes, documentation would only confuse matters. */
static apr_status_t
pick_error_code(struct log_runner *loggy)
{
  if (loggy->count <= 1)
    return SVN_ERR_WC_BAD_ADM_LOG_START;
  else
    return SVN_ERR_WC_BAD_ADM_LOG;
}

/* Helper macro for erroring out while running a logfile.

   This is implemented as a macro so that the error created has a useful
   line number associated with it. */
#define SIGNAL_ERROR(loggy, err)                                   \
  svn_xml_signal_bailout                                           \
    (svn_error_createf(pick_error_code(loggy), err,                \
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
    return svn_error_createf(pick_error_code(loggy), NULL,
                             _("Missing 'dest' attribute in '%s'"),
                             svn_dirent_local_style(loggy->adm_abspath,
                                                    loggy->pool));

  err = file_xfer_under_path(loggy->db, loggy->adm_abspath, name, dest,
                             versioned, action, loggy->rerun, loggy->pool);
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
  if (err && loggy->rerun && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    return svn_error_return(err);
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
    return svn_error_createf(pick_error_code(loggy), NULL,
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

  if (loggy->rerun)
    {
      /* When committing a delete the entry might get removed, in
         which case we don't want to reincarnate it.  */
      const svn_wc_entry_t *existing;

      SVN_ERR(svn_wc__get_entry(&existing, loggy->db, local_abspath, TRUE,
                                svn_node_unknown, FALSE,
                                loggy->pool, loggy->pool));
      if (! existing)
        return SVN_NO_ERROR;
    }

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
          (pick_error_code(loggy), err,
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
          (pick_error_code(loggy), NULL,
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
    return svn_error_createf(pick_error_code(loggy), err,
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
    return svn_error_createf(pick_error_code(loggy), err,
                             _("Error removing lock from entry for '%s'"),
                             name);

  return SVN_NO_ERROR;
}

static svn_error_t *
log_do_delete_changelist(struct log_runner *loggy,
                         const char *name)
{
  svn_error_t *err;

  err = svn_wc__db_op_set_changelist(loggy->db,
                                     svn_dirent_join(loggy->adm_abspath, name,
                                                     loggy->pool),
                                     NULL,
                                     loggy->pool);
  if (err)
    return svn_error_createf(pick_error_code(loggy), err,
                             _("Error removing changelist from entry '%s'"),
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

/* Note:  assuming that svn_wc__log_commit() is what created all of
   the <committed...> commands, the `name' attribute will either be a
   file or SVN_WC_ENTRY_THIS_DIR. */
static svn_error_t *
log_do_committed(struct log_runner *loggy,
                 const char *name,
                 const char **atts)
{
  svn_error_t *err;
  apr_pool_t *pool = loggy->pool;
  int is_this_dir = (strcmp(name, SVN_WC_ENTRY_THIS_DIR) == 0);
  const char *rev = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_REVISION, atts);
  svn_boolean_t wc_root, remove_executable = FALSE;
  svn_boolean_t set_read_write = FALSE;
  const char *local_abspath;
  const svn_wc_entry_t *orig_entry;
  svn_wc_entry_t *entry;
  svn_boolean_t prop_mods;
  apr_uint64_t modify_flags = 0;

  /* Determine the actual full path of the affected item. */
  if (! is_this_dir)
    local_abspath = svn_dirent_join(loggy->adm_abspath, name, pool);
  else
    local_abspath = loggy->adm_abspath;

  /*** Perform sanity checking operations ***/

  /* If no new post-commit revision was given us, bail with an error. */
  if (! rev)
    return svn_error_createf(pick_error_code(loggy), NULL,
                             _("Missing 'revision' attribute for '%s'"),
                             name);

  /* Read the entry for the affected item.  If we can't find the
     entry, or if the entry states that our item is not either "this
     dir" or a file kind, perhaps this isn't really the entry our log
     creator was expecting.  */
  SVN_ERR(svn_wc__get_entry(&orig_entry, loggy->db, local_abspath, FALSE,
                            svn_node_unknown, FALSE, pool, pool));

  /* Cannot rerun a commit of a delete since the entry gets changed
     too much; if it's got as far as being in state deleted=true, or
     if it has been removed, then the all the processing has been
     done. */
  if (loggy->rerun && (! orig_entry
                       || (orig_entry->schedule == svn_wc_schedule_normal
                           && orig_entry->deleted)))
    return SVN_NO_ERROR;

  if ((! orig_entry)
      || ((! is_this_dir) && (orig_entry->kind != svn_node_file)))
    return svn_error_createf
      (pick_error_code(loggy), NULL,
       _("Log command for directory '%s' is mislocated"), name);

  entry = svn_wc_entry_dup(orig_entry, pool);

  /*** Handle the committed deletion case ***/

  /* If the committed item was scheduled for deletion, it needs to
     now be removed from revision control.  Once that is accomplished,
     we are finished handling this item.  */
  if (entry->schedule == svn_wc_schedule_delete)
    {
      svn_revnum_t new_rev = SVN_STR_TO_REV(rev);

      /* If we are suppose to delete "this dir", drop a 'killme' file
         into my own administrative dir as a signal for svn_wc__run_log()
         to blow away the administrative area after it is finished
         processing this logfile.  */
      if (is_this_dir)
        {
          /* Bump the revision number of this_dir anyway, so that it
             might be higher than its parent's revnum.  If it's
             higher, then the process that sees KILLME and destroys
             the directory can also place a 'deleted' dir entry in the
             parent. */
          svn_wc_entry_t tmpentry;
          tmpentry.revision = new_rev;
          tmpentry.kind = svn_node_dir;

          SVN_ERR(svn_wc__entry_modify2(loggy->db, local_abspath,
                                        svn_node_dir, FALSE,
                                        &tmpentry,
                                        SVN_WC__ENTRY_MODIFY_REVISION
                                        | SVN_WC__ENTRY_MODIFY_KIND,
                                        pool));

          /* Drop the 'killme' file. */
          err = svn_wc__make_killme(loggy->adm_access, entry->keep_local,
                                    pool);

          if (err)
            {
              if (loggy->rerun && APR_STATUS_IS_EEXIST(err->apr_err))
                svn_error_clear(err);
              else
                return svn_error_return(err);
            }

          return SVN_NO_ERROR;
        }

      /* Else, we're deleting a file, and we can safely remove files
         from revision control without screwing something else up.

         ### We pass NULL, NULL for cancel_func and cancel_baton below.
         ### If they were available, it would be nice to use them. */
      else
        {
          const svn_wc_entry_t *parentry;
          svn_wc_entry_t tmp_entry;

          SVN_ERR(svn_wc__internal_remove_from_revision_control(
                    loggy->db, local_abspath,
                    FALSE, FALSE, NULL, NULL, pool));

          /* If the parent entry's working rev 'lags' behind new_rev... */
          SVN_ERR(svn_wc__get_entry(&parentry, loggy->db, loggy->adm_abspath,
                                    FALSE, svn_node_dir, FALSE, pool, pool));
          if (new_rev > parentry->revision)
            {
              /* ...then the parent's revision is now officially a
                 lie;  therefore, it must remember the file as being
                 'deleted' for a while.  Create a new, uninteresting
                 ghost entry:  */
              tmp_entry.kind = svn_node_file;
              tmp_entry.deleted = TRUE;
              tmp_entry.revision = new_rev;
              SVN_ERR(svn_wc__entry_modify2(loggy->db, local_abspath,
                                            svn_node_file, FALSE,
                                            &tmp_entry,
                                            SVN_WC__ENTRY_MODIFY_REVISION
                                              | SVN_WC__ENTRY_MODIFY_KIND
                                              | SVN_WC__ENTRY_MODIFY_DELETED,
                                            pool));
            }

          return SVN_NO_ERROR;
        }
    }


  /*** Mark the committed item committed-to-date ***/


  /* If "this dir" has been replaced (delete + add), all its
     immmediate children *must* be either scheduled for deletion (they
     were children of "this dir" during the "delete" phase of its
     replacement), added (they are new children of the replaced dir),
     or replaced (they are new children of the replace dir that have
     the same names as children that were present during the "delete"
     phase of the replacement).

     Children which are added or replaced will have been reported as
     individual commit targets, and thus will be re-visited by
     log_do_committed().  Children which were marked for deletion,
     however, need to be outright removed from revision control.  */
  if ((entry->schedule == svn_wc_schedule_replace) && is_this_dir)
    {
      /* Loop over all children entries, look for items scheduled for
         deletion. */
      const apr_array_header_t *children;
      int i;
      apr_pool_t *iterpool = svn_pool_create(pool);

      SVN_ERR(svn_wc__db_read_children(&children, loggy->db,
                                       loggy->adm_abspath, pool, pool));

      for(i = 0; i < children->nelts; i++)
        {
          const char *child_name = APR_ARRAY_IDX(children, i, const char*);
          const char *child_abspath;
          svn_wc__db_kind_t kind;
          svn_boolean_t is_file;
          const svn_wc_entry_t *child_entry;

          apr_pool_clear(iterpool);
          child_abspath = svn_dirent_join(loggy->adm_abspath, child_name,
                                          iterpool);

          SVN_ERR(svn_wc__db_read_kind(&kind, loggy->db, child_abspath, TRUE,
                                       iterpool));

          is_file = (kind == svn_wc__db_kind_file ||
                     kind == svn_wc__db_kind_symlink);

          SVN_ERR(svn_wc__get_entry(&child_entry, loggy->db, child_abspath,
                                    FALSE,
                                    is_file ? svn_node_file : svn_node_dir,
                                    !is_file, iterpool, iterpool));

          if (child_entry->schedule != svn_wc_schedule_delete)
            continue;

          /* ### We pass NULL, NULL for cancel_func and cancel_baton below.
             ### If they were available, it would be nice to use them. */
          SVN_ERR(svn_wc__internal_remove_from_revision_control(
                                    loggy->db, child_abspath,
                                    FALSE, FALSE,
                                    NULL, NULL, iterpool));
        }
    }

  SVN_ERR(svn_wc__props_modified(&prop_mods, loggy->db, local_abspath, pool));
  if (prop_mods)
    {
      if (entry->kind == svn_node_file)
        {
          /* Examine propchanges here before installing the new
             propbase.  If the executable prop was -deleted-, then
             tell install_committed_file() so.

             The same applies to the needs-lock property. */
          int i;
          apr_array_header_t *propchanges;


          SVN_ERR(svn_wc__internal_propdiff(&propchanges, NULL, loggy->db,
                                            local_abspath, pool, pool));
          for (i = 0; i < propchanges->nelts; i++)
            {
              svn_prop_t *propchange
                = &APR_ARRAY_IDX(propchanges, i, svn_prop_t);

              if ((! strcmp(propchange->name, SVN_PROP_EXECUTABLE))
                  && (propchange->value == NULL))
                remove_executable = TRUE;
              else if ((! strcmp(propchange->name, SVN_PROP_NEEDS_LOCK))
                       && (propchange->value == NULL))
                set_read_write = TRUE;
            }
        }

      SVN_ERR(svn_wc__working_props_committed(loggy->db, local_abspath, pool));
  }

  if (entry->kind == svn_node_file)
    {
      svn_boolean_t overwrote_working;
      apr_finfo_t finfo;

      /* Install the new file, which may involve expanding keywords.
         A copy of this file should have been dropped into our `tmp/text-base'
         directory during the commit process.  Part of this process
         involves setting the textual timestamp for this entry.  We'd like
         to just use the timestamp of the working file, but it is possible
         that at some point during the commit, the real working file might
         have changed again.  If that has happened, we'll use the
         timestamp of the copy of this file in `tmp/text-base' (which
         by then will have moved to `text-base'. */

      if ((err = install_committed_file(&overwrote_working, loggy->db,
                                        loggy->adm_abspath, name,
                                        remove_executable, set_read_write,
                                        pool)))
        return svn_error_createf
          (pick_error_code(loggy), err,
           _("Error replacing text-base of '%s'"), name);

      if ((err = svn_io_stat(&finfo, local_abspath,
                             APR_FINFO_MIN | APR_FINFO_LINK, pool)))
        return svn_error_createf(pick_error_code(loggy), err,
                                 _("Error getting 'affected time' of '%s'"),
                                 svn_dirent_local_style(local_abspath, pool));

      /* We will compute and modify the size and timestamp */
      modify_flags |= SVN_WC__ENTRY_MODIFY_WORKING_SIZE
                      | SVN_WC__ENTRY_MODIFY_TEXT_TIME;

      entry->working_size = finfo.size;

      if (overwrote_working)
        entry->text_time = finfo.mtime;
      else
        {
          /* The working copy file hasn't been overwritten, meaning
             we need to decide which timestamp to use. */

          const char *basef;
          apr_finfo_t basef_finfo;

          /* If the working file was overwritten (due to re-translation)
             or touched (due to +x / -x), then use *that* textual
             timestamp instead. */
          SVN_ERR(svn_wc__text_base_path(&basef,
                                         svn_wc__adm_get_db(loggy->adm_access),
                                         local_abspath, FALSE, pool));
          err = svn_io_stat(&basef_finfo, basef, APR_FINFO_MIN | APR_FINFO_LINK,
                            pool);
          if (err)
            return svn_error_createf
              (pick_error_code(loggy), err,
               _("Error getting 'affected time' for '%s'"),
               svn_dirent_local_style(basef, pool));
          else
            {
              svn_boolean_t modified;
              const char *base_abspath;

              SVN_ERR(svn_dirent_get_absolute(&base_abspath, basef, pool));

              /* Verify that the working file is the same as the base file
                 by comparing file sizes, then timestamps and the contents
                 after that. */

              /*###FIXME: if the file needs translation, don't compare
                file-sizes, just compare timestamps and do the rest of the
                hokey pokey. */
              modified = finfo.size != basef_finfo.size;
              if (finfo.mtime != basef_finfo.mtime && ! modified)
                {
                  err = svn_wc__internal_versioned_file_modcheck(&modified,
                                                                 loggy->db,
                                                                 local_abspath,
                                                                 base_abspath,
                                                                 FALSE, pool);
                  if (err)
                    return svn_error_createf
                      (pick_error_code(loggy), err,
                       _("Error comparing '%s' and '%s'"),
                       svn_dirent_local_style(local_abspath, pool),
                       svn_dirent_local_style(basef, pool));
                }
              /* If they are the same, use the working file's timestamp,
                 else use the base file's timestamp. */
              entry->text_time = modified ? basef_finfo.mtime : finfo.mtime;
            }
        }
    }

  /* Files have been moved, and timestamps have been found.  It is now
     time for The Big Entry Modification. Here we set fields in the entry
     to values which we know must be so because it has just been committed. */
  entry->revision = SVN_STR_TO_REV(rev);
  entry->kind = is_this_dir ? svn_node_dir : svn_node_file;
  entry->schedule = svn_wc_schedule_normal;
  entry->copied = FALSE;
  entry->deleted = FALSE;
  entry->copyfrom_url = NULL;
  entry->copyfrom_rev = SVN_INVALID_REVNUM;

  /* We don't reset tree_conflict_data, because it's about conflicts on
     children, not on this node, and it could conceivably be valid to commit
     this node non-recursively while children are still in conflict. */
  if ((err = svn_wc__entry_modify2(loggy->db, local_abspath,
                                   svn_node_unknown, FALSE,
                                   entry,
                                   (modify_flags
                                    | SVN_WC__ENTRY_MODIFY_REVISION
                                    | SVN_WC__ENTRY_MODIFY_KIND
                                    | SVN_WC__ENTRY_MODIFY_SCHEDULE
                                    | SVN_WC__ENTRY_MODIFY_COPIED
                                    | SVN_WC__ENTRY_MODIFY_DELETED
                                    | SVN_WC__ENTRY_MODIFY_COPYFROM_URL
                                    | SVN_WC__ENTRY_MODIFY_COPYFROM_REV
                                    | SVN_WC__ENTRY_MODIFY_FORCE),
                                   pool)))
    return svn_error_createf
      (pick_error_code(loggy), err,
       _("Error modifying entry of '%s'"), name);

  /* Clear out the conflict stuffs.  */
  /* ### this isn't transacted with the above modification, but then again,
     ### this entire function needs some kind of transactional behavior.  */
  SVN_ERR(svn_wc__db_op_mark_resolved(loggy->db, local_abspath,
                                      TRUE, TRUE, FALSE,
                                      pool));

  /* If we aren't looking at "this dir" (meaning we are looking at a
     file), we are finished.  From here on out, it's all about a
     directory's entry in its parent.  */
  if (! is_this_dir)
    return SVN_NO_ERROR;

  /* For directories, we also have to reset the state in the parent's
     entry for this directory, unless the current directory is a `WC
     root' (meaning, our parent directory on disk is not our parent in
     Version Control Land), in which case we're all finished here. */
  SVN_ERR(svn_wc__check_wc_root(&wc_root, NULL, loggy->db, loggy->adm_abspath,
                                pool));
  if (wc_root)
    return SVN_NO_ERROR;

  /* Make sure our entry exists in the parent. */
  {
    const svn_wc_entry_t *dir_entry;

    /* Check if we have a valid record in our parent */
    SVN_ERR(svn_wc__get_entry(&dir_entry, loggy->db, local_abspath,
                              FALSE, svn_node_dir, TRUE, pool, pool));

    /* ### We assume we have the right lock to modify the parent record.

           If this fails for you in the transition to one DB phase, please
           run svn cleanup one level higher. */
    err = svn_wc__entry_modify2(loggy->db, local_abspath, svn_node_dir,
                                TRUE, entry,
                                (SVN_WC__ENTRY_MODIFY_SCHEDULE
                                 | SVN_WC__ENTRY_MODIFY_COPIED
                                 | SVN_WC__ENTRY_MODIFY_DELETED
                                 | SVN_WC__ENTRY_MODIFY_FORCE),
                                pool);

    if (err != NULL)
      return svn_error_createf(pick_error_code(loggy), err,
                               _("Error modifying entry of '%s'"), name);
  }

  return SVN_NO_ERROR;
}


/* See documentation for SVN_WC__LOG_MODIFY_WCPROP. */
static svn_error_t *
log_do_modify_wcprop(struct log_runner *loggy,
                     const char *name,
                     const char **atts)
{
  svn_string_t value;
  const char *propname, *propval;
  const char *local_abspath;

  if (strcmp(name, SVN_WC_ENTRY_THIS_DIR) == 0)
    local_abspath = loggy->adm_abspath;
  else
    local_abspath = svn_dirent_join(loggy->adm_abspath, name, loggy->pool);

  propname = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_PROPNAME, atts);
  propval = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_PROPVAL, atts);

  if (propval)
    {
      value.data = propval;
      value.len = strlen(propval);
    }

  SVN_ERR(svn_wc__wcprop_set(loggy->db, local_abspath,
                             propname, propval ? &value : NULL, loggy->pool));

  return SVN_NO_ERROR;
}

static svn_error_t *
log_do_add_tree_conflict(struct log_runner *loggy,
                         const char **atts)
{
  apr_hash_t *new_conflicts;
  const svn_wc_conflict_description2_t *new_conflict;
  const char *dir_path = svn_wc_adm_access_path(loggy->adm_access);
  apr_hash_index_t *hi;

  /* Convert the text data to a conflict. */
  SVN_ERR(svn_wc__read_tree_conflicts(&new_conflicts,
                            svn_xml_get_attr_value(SVN_WC__LOG_ATTR_DATA, atts),
                                      dir_path, loggy->pool));
  hi = apr_hash_first(loggy->pool, new_conflicts);
  new_conflict = svn_apr_hash_index_val(hi);

  /* Ignore any attempt to re-add an existing tree conflict, as loggy
     operations are idempotent. */
  if (apr_hash_get(loggy->tree_conflicts, 
                   svn_dirent_basename(new_conflict->local_abspath,
                                       loggy->pool),
                   APR_HASH_KEY_STRING) == NULL)
    {
      /* ### should probably grab the pool from the hash, rather than
         ### stored in loggy.  */

      /* Copy the new conflict to the result pool.  Add its pointer to
         the hash of existing conflicts. */
      const svn_wc_conflict_description2_t *duped_conflict =
                        svn_wc__conflict_description2_dup(new_conflict,
                                                          loggy->result_pool);

      /* ### I think this loggy->pool is incorrect.  */
      apr_hash_set(loggy->tree_conflicts,
                   svn_dirent_basename(duped_conflict->local_abspath,
                                       loggy->pool),
                   APR_HASH_KEY_STRING, duped_conflict);
    }

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
         (pick_error_code(loggy), NULL,
          _("Log entry missing 'name' attribute (entry '%s' "
            "for directory '%s')"),
          eltname,
          svn_dirent_local_style(loggy->adm_abspath, loggy->pool)));
      return;
    }

  /* Increment the top-level element count before processing any commands. */
  loggy->count += 1;

  /* Dispatch. */
  if (strcmp(eltname, SVN_WC__LOG_MODIFY_ENTRY) == 0) {
    err = log_do_modify_entry(loggy, name, atts);
  }
  else if (strcmp(eltname, SVN_WC__LOG_DELETE_LOCK) == 0) {
    err = log_do_delete_lock(loggy, name);
  }
  else if (strcmp(eltname, SVN_WC__LOG_DELETE_CHANGELIST) == 0) {
    err = log_do_delete_changelist(loggy, name);
  }
  else if (strcmp(eltname, SVN_WC__LOG_DELETE_ENTRY) == 0) {
    err = log_do_delete_entry(loggy, name);
  }
  else if (strcmp(eltname, SVN_WC__LOG_COMMITTED) == 0) {
    err = log_do_committed(loggy, name, atts);
  }
  else if (strcmp(eltname, SVN_WC__LOG_MODIFY_WCPROP) == 0) {
    err = log_do_modify_wcprop(loggy, name, atts);
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
    err = log_do_add_tree_conflict(loggy, atts);
  }
  else
    {
      SIGNAL_ERROR
        (loggy, svn_error_createf
         (pick_error_code(loggy), NULL,
          _("Unrecognized logfile element '%s' in '%s'"),
          eltname,
          svn_dirent_local_style(loggy->adm_abspath, loggy->pool)));
      return;
    }

  if (err)
    SIGNAL_ERROR
      (loggy, svn_error_createf
       (pick_error_code(loggy), err,
        _("Error processing command '%s' in '%s'"),
        eltname,
        svn_dirent_local_style(loggy->adm_abspath, loggy->pool)));

  return;
}

/* Process the "KILLME" file associated with DIR_ABSPATH: remove the
   administrative area for DIR_ABSPATH and its children, and, if ADM_ONLY
   is FALSE, also remove the contents of the working copy (leaving only
   locally-modified files).  */
static svn_error_t *
handle_killme(svn_wc__db_t *db,
              const char *dir_abspath,
              svn_boolean_t adm_only,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *scratch_pool)
{
  svn_revnum_t original_revision;
  svn_revnum_t parent_revision;
  svn_error_t *err;

  SVN_ERR(svn_wc__db_base_get_info(NULL, NULL, &original_revision,
                                   NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   NULL, NULL, NULL,
                                   db, dir_abspath,
                                   scratch_pool, scratch_pool));
  SVN_ERR(svn_wc__db_read_info(NULL, NULL, &parent_revision,
                               NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL, NULL, NULL, NULL,
                               NULL, NULL, NULL, NULL,
                               db, svn_dirent_dirname(dir_abspath,
                                                      scratch_pool),
                               scratch_pool, scratch_pool));

  /* Blow away the administrative directories, and possibly the working
     copy tree too. */
  err = svn_wc__internal_remove_from_revision_control(
          db, dir_abspath,
          !adm_only /* destroy_wf */, FALSE /* instant_error */,
          cancel_func, cancel_baton,
          scratch_pool);
  if (err && err->apr_err != SVN_ERR_WC_LEFT_LOCAL_MOD)
    return svn_error_return(err);
  svn_error_clear(err);

  /* If revnum of this dir is greater than parent's revnum, then
     recreate 'deleted' entry in parent. */
  if (original_revision > parent_revision)
    {
      svn_wc_entry_t tmp_entry;

      tmp_entry.kind = svn_node_dir;
      tmp_entry.deleted = TRUE;
      tmp_entry.revision = original_revision;
      SVN_ERR(svn_wc__entry_modify2(db, dir_abspath, svn_node_dir, TRUE,
                                    &tmp_entry,
                                    SVN_WC__ENTRY_MODIFY_REVISION
                                      | SVN_WC__ENTRY_MODIFY_KIND
                                      | SVN_WC__ENTRY_MODIFY_DELETED,
                                    scratch_pool));
    }

  return SVN_NO_ERROR;
}


/*** Using the parser to run the log file. ***/

/* Return the filename (with no path components) to use for logfile number
   LOG_NUMBER.  The returned string will be allocated from POOL.

   For log number 0, this will just be SVN_WC__ADM_LOG to maintain
   compatibility with 1.0.x.  Higher numbers have the digits of the
   number appended to SVN_WC__ADM_LOG so that they look like "log.1",
   "log.2", etc. */
static const char *
compute_logfile_path(int log_number, apr_pool_t *result_pool)
{
  if (log_number == 0)
    return SVN_WC__ADM_LOG;
  return apr_psprintf(result_pool, SVN_WC__ADM_LOG ".%d", log_number);
}


/* Run a sequence of log files. */
static svn_error_t *
run_log(svn_wc_adm_access_t *adm_access,
        svn_boolean_t rerun,
        apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
  const char *dir_abspath = svn_wc__adm_access_abspath(adm_access);
  svn_xml_parser_t *parser;
  struct log_runner *loggy;
  char *buf = apr_palloc(scratch_pool, SVN__STREAM_CHUNK_SIZE);
  const char *logfile_path;
  int log_number;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_boolean_t killme, kill_adm_only;

  loggy = apr_pcalloc(scratch_pool, sizeof(*loggy));

  parser = svn_xml_make_parser(loggy, start_handler, NULL, NULL,
                               scratch_pool);

  loggy->db = db;
  loggy->adm_abspath = dir_abspath;
  loggy->adm_access = adm_access;
  loggy->pool = svn_pool_create(scratch_pool);
  loggy->result_pool = svn_pool_create(scratch_pool);
  loggy->parser = parser;
  loggy->rerun = rerun;
  loggy->count = 0;
  loggy->tree_conflicts = apr_hash_make(scratch_pool);

  /* Expat wants everything wrapped in a top-level form, so start with
     a ghost open tag. */
  SVN_ERR(svn_xml_parse(parser, LOG_START, strlen(LOG_START), 0));

  for (log_number = 0; ; log_number++)
    {
      svn_stream_t *stream;
      svn_error_t *err;
      apr_size_t len = SVN__STREAM_CHUNK_SIZE;

      svn_pool_clear(iterpool);
      logfile_path = compute_logfile_path(log_number, iterpool);

      /* Parse the log file's contents. */
      err = svn_wc__open_adm_stream(&stream, dir_abspath, logfile_path,
                                    iterpool, iterpool);
      if (err)
        {
          if (APR_STATUS_IS_ENOENT(err->apr_err))
            {
              svn_error_clear(err);
              break;
            }
          else
            {
              SVN_ERR_W(err, _("Couldn't open log"));
            }
        }

      do {
        SVN_ERR(svn_stream_read(stream, buf, &len));
        SVN_ERR(svn_xml_parse(parser, buf, len, 0));

      } while (len == SVN__STREAM_CHUNK_SIZE);

      SVN_ERR(svn_stream_close(stream));
    }

  /* Pacify Expat with a pointless closing element tag. */
  SVN_ERR(svn_xml_parse(parser, LOG_END, strlen(LOG_END), 1));

  svn_xml_free_parser(parser);

  /* If the logs included tree conflicts, write them to the entry. */
  if (apr_hash_count(loggy->tree_conflicts) > 0)
    {
      apr_hash_index_t *hi;

      for (hi = apr_hash_first(scratch_pool, loggy->tree_conflicts); hi;
            hi = apr_hash_next(hi))
        {
          svn_error_t *err;
          const svn_wc_conflict_description2_t *conflict = 
                                                 svn_apr_hash_index_val(hi);

          svn_pool_clear(iterpool);

          err = svn_wc__db_op_set_tree_conflict(db,
                                                conflict->local_abspath,
                                                conflict, iterpool);
         
          if (err)
            return svn_error_createf(pick_error_code(loggy), err,
                                 _("Error recording tree conflict on '%s'"),
                                 conflict->local_abspath);
        }
    }

  /* Check for a 'killme' file in the administrative area. */
  SVN_ERR(svn_wc__check_killme(adm_access, &killme, &kill_adm_only,
                               iterpool));
  if (killme)
    {
      SVN_ERR(handle_killme(db, dir_abspath, kill_adm_only, NULL, NULL,
                            iterpool));
    }
  else
    {
      for (log_number--; log_number >= 0; log_number--)
        {
          svn_pool_clear(iterpool);
          logfile_path = compute_logfile_path(log_number, iterpool);

          /* No 'killme'?  Remove the logfile; its commands have been
             executed. */
          SVN_ERR(svn_wc__remove_adm_file(dir_abspath, logfile_path,
                                          iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__run_log(svn_wc_adm_access_t *adm_access,
                apr_pool_t *scratch_pool)
{
  return run_log(adm_access, FALSE, scratch_pool);
}

svn_error_t *
svn_wc__run_log2(svn_wc__db_t *db,
                 const char *adm_abspath,
                 apr_pool_t *scratch_pool)
{
  svn_wc_adm_access_t *adm_access;

  adm_access = svn_wc__adm_retrieve_internal2(db, adm_abspath, scratch_pool);

  SVN_ERR_ASSERT(adm_access != NULL); /* A lock MUST exist */

  return run_log(adm_access, FALSE, scratch_pool);
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
svn_wc__loggy_committed(svn_stringbuf_t **log_accum,
                        const char *adm_abspath,
                        const char *path,
                        svn_revnum_t revnum,
                        apr_pool_t *result_pool,
                        apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(adm_abspath));

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(log_accum, result_pool, svn_xml_self_closing,
                        SVN_WC__LOG_COMMITTED,
                        SVN_WC__LOG_ATTR_NAME, loggy_path1,
                        SVN_WC__LOG_ATTR_REVISION,
                        apr_psprintf(scratch_pool, "%ld", revnum),
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
svn_wc__loggy_delete_entry(svn_stringbuf_t **log_accum,
                           const char *adm_abspath,
                           const char *path,
                           apr_pool_t *result_pool,
                           apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(log_accum, result_pool, svn_xml_self_closing,
                        SVN_WC__LOG_DELETE_ENTRY,
                        SVN_WC__LOG_ATTR_NAME, loggy_path1,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_delete_lock(svn_stringbuf_t **log_accum,
                          const char *adm_abspath,
                          const char *path,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(log_accum, result_pool, svn_xml_self_closing,
                        SVN_WC__LOG_DELETE_LOCK,
                        SVN_WC__LOG_ATTR_NAME, loggy_path1,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_delete_changelist(svn_stringbuf_t **log_accum,
                                const char *adm_abspath,
                                const char *path,
                                apr_pool_t *result_pool,
                                apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(log_accum, result_pool, svn_xml_self_closing,
                        SVN_WC__LOG_DELETE_CHANGELIST,
                        SVN_WC__LOG_ATTR_NAME, loggy_path1,
                        NULL);

  return SVN_NO_ERROR;
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
svn_wc__loggy_modify_wcprop(svn_stringbuf_t **log_accum,
                            const char *adm_abspath,
                            const char *path,
                            const char *propname,
                            const char *propval,
                            apr_pool_t *result_pool,
                            apr_pool_t *scratch_pool)
{
  const char *loggy_path1;

  SVN_ERR(loggy_path(&loggy_path1, path, adm_abspath, scratch_pool));
  svn_xml_make_open_tag(log_accum, result_pool, svn_xml_self_closing,
                        SVN_WC__LOG_MODIFY_WCPROP,
                        SVN_WC__LOG_ATTR_NAME, loggy_path1,
                        SVN_WC__LOG_ATTR_PROPNAME, propname,
                        SVN_WC__LOG_ATTR_PROPVAL, propval,
                        NULL);

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
  const char *conflict_data;
  apr_hash_t *conflicts;

  /* ### TODO: implement write_one_tree_conflict(). */
  conflicts = apr_hash_make(pool);
  apr_hash_set(conflicts, conflict->local_abspath, APR_HASH_KEY_STRING,
               conflict);

  SVN_ERR(svn_wc__write_tree_conflicts(&conflict_data, conflicts, pool));

  svn_xml_make_open_tag(log_accum, pool, svn_xml_self_closing,
                        SVN_WC__LOG_ADD_TREE_CONFLICT,
                        SVN_WC__LOG_ATTR_NAME,
                        SVN_WC_ENTRY_THIS_DIR,
                        SVN_WC__LOG_ATTR_DATA,
                        conflict_data,
                        NULL);

  return SVN_NO_ERROR;
}



/*** Helper to write log files ***/

svn_error_t *
svn_wc__write_log(const char *adm_abspath,
                  int log_number,
                  svn_stringbuf_t *log_content,
                  apr_pool_t *scratch_pool)
{
  svn_stream_t *stream;
  const char *temp_file_path;
  const char *logfile_name = compute_logfile_path(log_number, scratch_pool);
  apr_size_t len = log_content->len;

  SVN_ERR(svn_wc__open_adm_writable(&stream, &temp_file_path,
                                    adm_abspath, logfile_name,
                                    scratch_pool, scratch_pool));

  SVN_ERR_W(svn_stream_write(stream, log_content->data, &len),
            apr_psprintf(scratch_pool, _("Error writing log for '%s'"),
                         svn_dirent_local_style(logfile_name, scratch_pool)));

  return svn_wc__close_adm_stream(stream, temp_file_path, adm_abspath,
                                  logfile_name, scratch_pool);
}


svn_error_t *
svn_wc__logfile_present(svn_boolean_t *present,
                        const char *local_abspath,
                        apr_pool_t *scratch_pool)
{
  const char *log_path = svn_wc__adm_child(local_abspath, SVN_WC__ADM_LOG,
                                           scratch_pool);
  svn_node_kind_t kind;

  /* Is the (first) log file present?  */
  SVN_ERR(svn_io_check_path(log_path, &kind, scratch_pool));
  *present = (kind == svn_node_file);

  return SVN_NO_ERROR;
}


/*** Recursively do log things. ***/
static svn_error_t *
cleanup_internal(svn_wc__db_t *db,
                 const char *path,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool);

static svn_error_t *
run_existing_logs(svn_wc_adm_access_t *adm_access,
                  svn_cancel_func_t cancel_func,
                  void *cancel_baton,
                  apr_pool_t *scratch_pool)
{
  svn_wc__db_t *db = svn_wc__adm_get_db(adm_access);
  const apr_array_header_t *children;
  int i;
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  svn_boolean_t killme, kill_adm_only;
  const char *adm_abspath = svn_wc__adm_access_abspath(adm_access);

  /* Recurse on versioned elements first, oddly enough. */
  SVN_ERR(svn_wc__db_read_children(&children, db, adm_abspath,
                                   scratch_pool, iterpool));
  for (i = 0; i < children->nelts; i++)
    {
      const char *name = APR_ARRAY_IDX(children, i, const char *);
      const char *entry_abspath;
      svn_boolean_t hidden;
      svn_wc__db_kind_t kind;

      svn_pool_clear(iterpool);
      entry_abspath = svn_dirent_join(adm_abspath, name, iterpool);

      SVN_ERR(svn_wc__db_node_hidden(&hidden, db, entry_abspath, iterpool));
      if (hidden)
        continue;

      SVN_ERR(svn_wc__db_read_kind(&kind, db, entry_abspath, FALSE, iterpool));

      if (kind == svn_wc__db_kind_dir)
        {
          svn_node_kind_t disk_kind;
          /* Sub-directories */
          SVN_ERR(svn_io_check_path(entry_abspath, &disk_kind, iterpool));
          if (disk_kind == svn_node_dir)
            SVN_ERR(cleanup_internal(db, svn_dirent_join(
                                            svn_wc_adm_access_path(adm_access),
                                            name, iterpool),
                                     cancel_func, cancel_baton,
                                     iterpool));
        }
      else
        {
          /* "." and things that are not directories, check for mods to
             trigger the timestamp repair mechanism.  Since this rewrites
             the entries file for each timestamp fixed it has the potential
             to be slow, perhaps we need something more sophisticated? */
          svn_boolean_t modified;
          SVN_ERR(svn_wc__props_modified(&modified, db, entry_abspath,
                                         iterpool));
          if (kind == svn_wc__db_kind_file || kind == svn_wc__db_kind_symlink)
            SVN_ERR(svn_wc__internal_text_modified_p(&modified, db,
                                        entry_abspath, FALSE, TRUE,
                                        iterpool));
        }
    }

  SVN_ERR(svn_wc__check_killme(adm_access, &killme, &kill_adm_only,
                               iterpool));
  if (killme)
    {
      /* A KILLME indicates that the log has already been run */
      SVN_ERR(handle_killme(db, adm_abspath, kill_adm_only, cancel_func,
                            cancel_baton, iterpool));
    }
  else
    {
      svn_boolean_t present;

      /* In an attempt to maintain consistency between the decisions made in
         this function, and those made in the access baton lock-removal code,
         we use the same test as the lock-removal code. */
      SVN_ERR(svn_wc__logfile_present(&present, adm_abspath, iterpool));
      if (present)
        {
          /* ### rerun the log. why? dunno. missing commentary... */
          SVN_ERR(run_log(adm_access, TRUE, iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}


static svn_error_t *
cleanup_internal(svn_wc__db_t *db,
                 const char *path,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 apr_pool_t *scratch_pool)
{
  svn_wc_adm_access_t *adm_access;

  /* Check cancellation; note that this catches recursive calls too. */
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  /* Lock this working copy directory, or steal an existing lock */
  SVN_ERR(svn_wc__adm_steal_write_lock(&adm_access, db, path,
                                       scratch_pool, scratch_pool));

  /* Recurse and run any existing logs. */
  SVN_ERR(run_existing_logs(adm_access,
                            cancel_func, cancel_baton, scratch_pool));

  /* Cleanup the tmp area of the admin subdir, if running the log has not
     removed it!  The logs have been run, so anything left here has no hope
     of being useful. */
  SVN_ERR(svn_wc__adm_cleanup_tmp_area(adm_access, scratch_pool));

  return svn_wc_adm_close2(adm_access, scratch_pool);
}


/* ### possibly eliminate the WC_CTX parameter? callers really shouldn't
   ### be doing anything *but* running a cleanup, and we need a special
   ### DB anyway. ... *shrug* ... consider later.  */
svn_error_t *
svn_wc_cleanup3(svn_wc_context_t *wc_ctx,
                const char *local_abspath,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t * scratch_pool)
{
  svn_wc__db_t *db;
  int wc_format_version;

  SVN_ERR_ASSERT(svn_dirent_is_absolute(local_abspath));

  /* We need a DB that allows a non-empty work queue (though it *will*
     auto-upgrade). We'll handle everything manually.  */
  SVN_ERR(svn_wc__db_open(&db, svn_wc__db_openmode_readwrite,
                          NULL /* ### config */, TRUE, FALSE,
                          scratch_pool, scratch_pool));

  SVN_ERR(svn_wc__internal_check_wc(&wc_format_version, db, 
                                    local_abspath, scratch_pool));

  /* a "version" of 0 means a non-wc directory */
  if (wc_format_version == 0)
    return svn_error_createf(SVN_ERR_WC_NOT_WORKING_COPY, NULL,
                             _("'%s' is not a working copy directory"),
                             svn_dirent_local_style(local_abspath, 
                                                    scratch_pool));

  if (wc_format_version < SVN_WC__WC_NG_VERSION)
    return svn_error_create(SVN_ERR_WC_UNSUPPORTED_FORMAT, NULL,
                            _("Log format too old, please use "
                              "Subversion 1.6 or earlier"));

  /* ### we really should be running these logs recursively. bleah.
     ### for now, just run them in "this" directory. when we reach the
     ### one-dir layout, then this will work just fine.  */
  if (wc_format_version >= SVN_WC__HAS_WORK_QUEUE)
    SVN_ERR(svn_wc__wq_run(db, local_abspath, cancel_func, cancel_baton,
                           scratch_pool));

  /* We're done with this DB. We can go back to the DB provided in the
     WC_CTX for the old-style log processing.  */
  SVN_ERR(svn_wc__db_close(db));

  return svn_error_return(cleanup_internal(wc_ctx->db, local_abspath, 
                                           cancel_func, cancel_baton, 
                                           scratch_pool));
}
