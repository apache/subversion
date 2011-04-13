/*
 * log.c:  handle the adm area's log file.
 *
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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

#include <apr_pools.h>
#include <apr_strings.h>

#include "svn_wc.h"
#include "svn_error.h"
#include "svn_string.h"
#include "svn_xml.h"
#include "svn_pools.h"
#include "svn_io.h"
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
#include "questions.h"
#include "tree_conflicts.h"

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


/* Upgrade the WC format, both .svn/format and the format number in the
   entries file to SVN_WC__LOG_ATTR_FORMAT. */
#define SVN_WC__LOG_UPGRADE_FORMAT "upgrade-format"

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

/* This one is for SVN_WC__LOG_MERGE. */
#define SVN_WC__LOG_ATTR_ARG_1          "arg1"
/* This one is for SVN_WC__LOG_MERGE
   and optionally SVN_WC__LOG_CP_AND_(DE)TRANSLATE to indicate a versioned
   path to take its translation properties from */
#define SVN_WC__LOG_ATTR_ARG_2          "arg2"
/* The rest are for SVN_WC__LOG_MERGE.  Extend as necessary. */
#define SVN_WC__LOG_ATTR_ARG_3          "arg3"
#define SVN_WC__LOG_ATTR_ARG_4          "arg4"
#define SVN_WC__LOG_ATTR_ARG_5          "arg5"



/* DEPRECATED, left for compat with older format working copies

   Copy file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST. */
#define SVN_WC__LOG_CP                  "cp"

/* DEPRECATED, left for compat with older format working copies

   Copy file SVN_WC__LOG_ATTR_NAME to SVN_WC__LOG_ATTR_DEST, but
   contract any keywords and convert to LF eol, according to
   properties of NAME. */
#define SVN_WC__LOG_CP_AND_DETRANSLATE    "cp-and-detranslate"

/* DEPRECATED, left for compat with pre-v8 format working copies

   A log command which runs svn_wc_merge2().
   See its documentation for details.

   Here is a map of entry-attributes to svn_wc_merge arguments:

         SVN_WC__LOG_NAME         : MERGE_TARGET
         SVN_WC__LOG_ATTR_ARG_1   : LEFT
         SVN_WC__LOG_ATTR_ARG_2   : RIGHT
         SVN_WC__LOG_ATTR_ARG_3   : LEFT_LABEL
         SVN_WC__LOG_ATTR_ARG_4   : RIGHT_LABEL
         SVN_WC__LOG_ATTR_ARG_5   : TARGET_LABEL

   Of course, the three paths should be *relative* to the directory in
   which the log is running, as with all other log commands.  (Usually
   they're just basenames within loggy->path.)
 */
#define SVN_WC__LOG_MERGE        "merge"


/*** Userdata for the callbacks. ***/
struct log_runner
{
  apr_pool_t *pool; /* cleared before processing each log element */
  apr_pool_t *result_pool;
  svn_xml_parser_t *parser;
  svn_boolean_t entries_modified;
  svn_boolean_t wcprops_modified;
  svn_boolean_t rerun;
  svn_wc_adm_access_t *adm_access;  /* the dir in which all this happens */
  const char *diff3_cmd;            /* external diff3 cmd, or null if none */
  svn_boolean_t tree_conflicts_added;
  apr_array_header_t *tree_conflicts; /* array of pointers to
                                         svn_wc_conflict_description_t. */
  /* Which top-level log element we're on for this logfile.  Some
     callers care whether a failure happened on the first element or
     on some later element (e.g., 'svn cleanup').

     This is initialized to 0 when the log_runner is created, and
     incremented every time start_handler() is called. */
  int count;
};



/*** Forward declarations ***/

/* log runner forward declaration used in log_do_merge */
static svn_error_t *
run_log_from_memory(svn_wc_adm_access_t *adm_access,
                    const char *buf,
                    apr_size_t buf_len,
                    svn_boolean_t rerun,
                    const char *diff3_cmd,
                    apr_pool_t *pool);





/*** The XML handlers. ***/

/* Used by file_xfer_under_path(). */
enum svn_wc__xfer_action {
  svn_wc__xfer_cp,
  svn_wc__xfer_mv,
  svn_wc__xfer_append,
  svn_wc__xfer_cp_and_translate,
  svn_wc__xfer_cp_and_detranslate
};


/* Perform some sort of copy-related ACTION on NAME and DEST:

      svn_wc__xfer_cp:                 just do a copy of NAME to DEST.
      svn_wc__xfer_mv:                 do a copy, then remove NAME.
      svn_wc__xfer_append:             append contents of NAME to DEST
      svn_wc__xfer_cp_and_translate:   copy NAME to DEST, doing any eol
                                       and keyword expansion according to
                                       the current property vals of VERSIONED
                                       or, if that's NULL, those of DEST.
      svn_wc__xfer_cp_and_detranslate: copy NAME to DEST, converting to LF
                                       and contracting keywords according to
                                       the current property vals of VERSIONED
                                       or, if that's NULL, those of NAME.

*/
static svn_error_t *
file_xfer_under_path(svn_wc_adm_access_t *adm_access,
                     const char *name,
                     const char *dest,
                     const char *versioned,
                     enum svn_wc__xfer_action action,
                     svn_boolean_t rerun,
                     apr_pool_t *pool)
{
  svn_error_t *err;
  const char *full_from_path, *full_dest_path, *full_versioned_path;

  full_from_path = svn_path_join(svn_wc_adm_access_path(adm_access), name,
                                 pool);
  full_dest_path = svn_path_join(svn_wc_adm_access_path(adm_access), dest,
                                 pool);
  if (versioned)
    full_versioned_path = svn_path_join(svn_wc_adm_access_path(adm_access),
                                        versioned, pool);
  else
    full_versioned_path = NULL; /* Silence GCC uninitialised warning */

  switch (action)
    {
    case svn_wc__xfer_append:
      err = svn_io_append_file(full_from_path, full_dest_path, pool);
      if (err)
        {
          if (! rerun || ! APR_STATUS_IS_ENOENT(err->apr_err))
            return err;
          svn_error_clear(err);
        }
      break;

    case svn_wc__xfer_cp:
      return svn_io_copy_file(full_from_path, full_dest_path, FALSE, pool);

    case svn_wc__xfer_cp_and_translate:
      {
        svn_subst_eol_style_t style;
        const char *eol;
        apr_hash_t *keywords;
        svn_boolean_t special;

        if (! full_versioned_path)
          full_versioned_path = full_dest_path;

        err = svn_wc__get_eol_style(&style, &eol, full_versioned_path,
                                    adm_access, pool);
        if (! err)
          err = svn_wc__get_keywords(&keywords, full_versioned_path,
                                     adm_access, NULL, pool);
        if (! err)
          err = svn_wc__get_special(&special, full_versioned_path, adm_access,
                                    pool);

        if (! err)
          err = svn_subst_copy_and_translate3
                (full_from_path, full_dest_path,
                 eol, TRUE,
                 keywords, TRUE,
                 special,
                 pool);

        if (err)
          {
            if (! rerun || ! APR_STATUS_IS_ENOENT(err->apr_err))
              return err;
            svn_error_clear(err);
          }

        SVN_ERR(svn_wc__maybe_set_read_only(NULL, full_dest_path,
                                            adm_access, pool));

        return svn_wc__maybe_set_executable(NULL, full_dest_path,
                                            adm_access, pool);
      }
    case svn_wc__xfer_cp_and_detranslate:
      {
        const char *tmp_file;

        SVN_ERR(svn_wc_translated_file2
                (&tmp_file,
                 full_from_path,
                 versioned ? full_versioned_path : full_from_path, adm_access,
                 SVN_WC_TRANSLATE_TO_NF
                 | SVN_WC_TRANSLATE_FORCE_COPY,
                 pool));
        return svn_io_file_rename(tmp_file, full_dest_path, pool);
      }

    case svn_wc__xfer_mv:
      err = svn_io_file_rename(full_from_path,
                               full_dest_path, pool);

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
 * Use POOL for any temporary allocation.
 */
static svn_error_t *
install_committed_file(svn_boolean_t *overwrote_working,
                       svn_wc_adm_access_t *adm_access,
                       const char *name,
                       svn_boolean_t remove_executable,
                       svn_boolean_t remove_read_only,
                       apr_pool_t *pool)
{
  const char *filepath;
  const char *tmp_text_base;
  svn_node_kind_t kind;
  svn_boolean_t same, did_set;
  const char *tmp_wfile;
  svn_boolean_t special;

  /* start off assuming that the working file isn't touched. */
  *overwrote_working = FALSE;

  filepath = svn_path_join(svn_wc_adm_access_path(adm_access), name, pool);

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
  tmp_text_base = svn_wc__text_base_path(filepath, 1, pool);
  SVN_ERR(svn_io_check_path(tmp_text_base, &kind, pool));

  {
    const char *tmp = (kind == svn_node_file) ? tmp_text_base : filepath;

    SVN_ERR(svn_wc_translated_file2(&tmp_wfile,
                                    tmp,
                                    filepath, adm_access,
                                    SVN_WC_TRANSLATE_FROM_NF,
                                    pool));

    /* If the translation is a no-op, the text base and the working copy
     * file contain the same content, because we use the same props here
     * as were used to detranslate from working file to text base.
     *
     * In that case: don't replace the working file, but make sure
     * it has the right executable and read_write attributes set.
     */

    SVN_ERR(svn_wc__get_special(&special, filepath, adm_access, pool));
    if (! special && tmp != tmp_wfile)
      SVN_ERR(svn_io_files_contents_same_p(&same, tmp_wfile,
                                           filepath, pool));
    else
      same = TRUE;
  }

  if (! same)
    {
      SVN_ERR(svn_io_file_rename(tmp_wfile, filepath, pool));
      *overwrote_working = TRUE;
    }

  if (remove_executable)
    {
      /* No need to chmod -x on a new file: new files don't have it. */
      if (same)
        SVN_ERR(svn_io_set_file_executable(filepath,
                                           FALSE, /* chmod -x */
                                           FALSE, pool));
      *overwrote_working = TRUE; /* entry needs wc-file's timestamp  */
    }
  else
    {
      /* Set the working file's execute bit if props dictate. */
      SVN_ERR(svn_wc__maybe_set_executable(&did_set, filepath,
                                           adm_access, pool));
      if (did_set)
        /* okay, so we didn't -overwrite- the working file, but we changed
           its timestamp, which is the point of returning this flag. :-) */
        *overwrote_working = TRUE;
    }

  if (remove_read_only)
    {
      /* No need to make a new file read_write: new files already are. */
      if (same)
        SVN_ERR(svn_io_set_file_read_write(filepath, FALSE, pool));
      *overwrote_working = TRUE; /* entry needs wc-file's timestamp  */
    }
  else
    {
      SVN_ERR(svn_wc__maybe_set_read_only(&did_set, filepath,
                                          adm_access, pool));
      if (did_set)
        /* okay, so we didn't -overwrite- the working file, but we changed
           its timestamp, which is the point of returning this flag. :-) */
        *overwrote_working = TRUE;
    }

  /* Install the new text base if one is waiting. */
  if (kind == svn_node_file)  /* tmp_text_base exists */
    SVN_ERR(svn_wc__sync_text_base(filepath, pool));

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
                       svn_path_local_style(svn_wc_adm_access_path \
                                            (loggy->adm_access),   \
                                            loggy->pool)),         \
     loggy->parser)



/*** Dispatch on the xml opening tag. ***/

static svn_error_t *
log_do_merge(struct log_runner *loggy,
             const char *name,
             const char **atts)
{
  const char *left, *right;
  const char *left_label, *right_label, *target_label;
  enum svn_wc_merge_outcome_t merge_outcome;
  svn_stringbuf_t *log_accum = svn_stringbuf_create("", loggy->pool);
  svn_error_t *err;

  /* NAME is the basename of our merge_target.  Pull out LEFT and RIGHT. */
  left = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_ARG_1, atts);
  if (! left)
    return svn_error_createf(pick_error_code(loggy), NULL,
                             _("Missing 'left' attribute in '%s'"),
                             svn_path_local_style
                             (svn_wc_adm_access_path(loggy->adm_access),
                              loggy->pool));
  right = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_ARG_2, atts);
  if (! right)
    return svn_error_createf(pick_error_code(loggy), NULL,
                             _("Missing 'right' attribute in '%s'"),
                             svn_path_local_style
                             (svn_wc_adm_access_path(loggy->adm_access),
                              loggy->pool));

  /* Grab all three labels too.  If non-existent, we'll end up passing
     NULLs to svn_wc_merge, which is fine -- it will use default
     labels. */
  left_label = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_ARG_3, atts);
  right_label = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_ARG_4, atts);
  target_label = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_ARG_5, atts);

  /* Convert the 3 basenames into full paths. */
  left = svn_path_join(svn_wc_adm_access_path(loggy->adm_access), left,
                       loggy->pool);
  right = svn_path_join(svn_wc_adm_access_path(loggy->adm_access), right,
                        loggy->pool);
  name = svn_path_join(svn_wc_adm_access_path(loggy->adm_access), name,
                       loggy->pool);

  /* Now do the merge with our full paths. */
  /* ### TODO: Fill in the left_version and right_version args. */
  err = svn_wc__merge_internal(&log_accum, &merge_outcome,
                               left, NULL, right, NULL, name, NULL,
                               loggy->adm_access, left_label, right_label,
                               target_label, FALSE, loggy->diff3_cmd, NULL,
                               NULL, NULL, NULL, loggy->pool);
  if (err && loggy->rerun && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    if (err)
      return err;

  err = run_log_from_memory(loggy->adm_access,
                            log_accum->data, log_accum->len,
                            loggy->rerun, loggy->diff3_cmd, loggy->pool);
  if (err && loggy->rerun && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    return err;
}


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
                             svn_path_local_style
                             (svn_wc_adm_access_path(loggy->adm_access),
                              loggy->pool));

  err = file_xfer_under_path(loggy->adm_access, name, dest, versioned,
                             action, loggy->rerun, loggy->pool);
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
  const char *full_path
    = svn_path_join(svn_wc_adm_access_path(loggy->adm_access), name,
                    loggy->pool);

  err = svn_io_set_file_read_only(full_path, FALSE, loggy->pool);
  if (err && loggy->rerun && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    return err;
}

/* Maybe make file NAME in log's CWD executable */
static svn_error_t *
log_do_file_maybe_executable(struct log_runner *loggy,
                             const char *name)
{
  const char *full_path
    = svn_path_join(svn_wc_adm_access_path(loggy->adm_access), name,
                    loggy->pool);

  return svn_wc__maybe_set_executable(NULL, full_path, loggy->adm_access,
                                     loggy->pool);
}

/* Maybe make file NAME in log's CWD readonly */
static svn_error_t *
log_do_file_maybe_readonly(struct log_runner *loggy,
                           const char *name)
{
  const char *full_path
    = svn_path_join(svn_wc_adm_access_path(loggy->adm_access), name,
                    loggy->pool);

  return svn_wc__maybe_set_read_only(NULL, full_path, loggy->adm_access,
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
  const char *full_path
    = svn_path_join(svn_wc_adm_access_path(loggy->adm_access), name,
                    loggy->pool);

  const char *timestamp_string
    = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_TIMESTAMP, atts);
  svn_boolean_t is_special;

  if (! timestamp_string)
    return svn_error_createf(pick_error_code(loggy), NULL,
                             _("Missing 'timestamp' attribute in '%s'"),
                             svn_path_local_style
                             (svn_wc_adm_access_path(loggy->adm_access),
                              loggy->pool));

  /* Do not set the timestamp on special files. */
  SVN_ERR(svn_io_check_special_path(full_path, &kind, &is_special,
                                    loggy->pool));

  if (! is_special)
    {
      SVN_ERR(svn_time_from_cstring(&timestamp, timestamp_string,
                                    loggy->pool));

      SVN_ERR(svn_io_set_file_affected_time(timestamp, full_path,
                                            loggy->pool));
    }

  return SVN_NO_ERROR;
}


/* Remove file NAME in log's CWD. */
static svn_error_t *
log_do_rm(struct log_runner *loggy, const char *name)
{
  const char *full_path
    = svn_path_join(svn_wc_adm_access_path(loggy->adm_access),
                    name, loggy->pool);

  svn_error_t *err =
    svn_io_remove_file(full_path, loggy->pool);

  if (err && APR_STATUS_IS_ENOENT(err->apr_err))
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    return err;
}




static svn_error_t *
log_do_modify_entry(struct log_runner *loggy,
                    const char *name,
                    const char **atts)
{
  svn_error_t *err;
  apr_hash_t *ah = svn_xml_make_att_hash(atts, loggy->pool);
  const char *tfile;
  svn_wc_entry_t *entry;
  apr_uint64_t modify_flags;
  const char *valuestr;

  if (loggy->rerun)
    {
      /* When committing a delete the entry might get removed, in
         which case we don't want to reincarnate it.  */
      const svn_wc_entry_t *existing;
      const char *path
        = svn_path_join(svn_wc_adm_access_path(loggy->adm_access), name,
                        loggy->pool);
      SVN_ERR(svn_wc_entry(&existing, path, loggy->adm_access, TRUE,
                           loggy->pool));
      if (! existing)
        return SVN_NO_ERROR;
    }

  /* Convert the attributes into an entry structure. */
  SVN_ERR(svn_wc__atts_to_entry(&entry, &modify_flags, ah, loggy->pool));

  /* Make TFILE the path of the thing being modified.  */
  tfile = svn_path_join(svn_wc_adm_access_path(loggy->adm_access),
                        strcmp(name, SVN_WC_ENTRY_THIS_DIR) ? name : "",
                        loggy->pool);

  /* svn_wc__atts_to_entry will no-op if the TEXT_TIME timestamp is
     SVN_WC__TIMESTAMP_WC, so look for that case and fill in the proper
     value. */
  valuestr = apr_hash_get(ah, SVN_WC__ENTRY_ATTR_TEXT_TIME,
                          APR_HASH_KEY_STRING);
  if ((modify_flags & SVN_WC__ENTRY_MODIFY_TEXT_TIME)
      && (! strcmp(valuestr, SVN_WC__TIMESTAMP_WC)))
    {
      apr_time_t text_time;

      err = svn_io_file_affected_time(&text_time, tfile, loggy->pool);
      if (err)
        return svn_error_createf
          (pick_error_code(loggy), err,
           _("Error getting 'affected time' on '%s'"),
           svn_path_local_style(tfile, loggy->pool));

      entry->text_time = text_time;
    }

  valuestr = apr_hash_get(ah, SVN_WC__ENTRY_ATTR_WORKING_SIZE,
                          APR_HASH_KEY_STRING);
  if ((modify_flags & SVN_WC__ENTRY_MODIFY_WORKING_SIZE)
      && (! strcmp(valuestr, SVN_WC__WORKING_SIZE_WC)))
    {
      apr_finfo_t finfo;
      const svn_wc_entry_t *tfile_entry;

      err = svn_wc_entry(&tfile_entry, tfile, loggy->adm_access,
                         FALSE, loggy->pool);

      if (err)
        SIGNAL_ERROR(loggy, err);

      if (! tfile_entry)
        return SVN_NO_ERROR;

      err = svn_io_stat(&finfo, tfile, APR_FINFO_MIN | APR_FINFO_LINK,
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
            svn_path_local_style(tfile, loggy->pool));

      entry->working_size = finfo.size;
    }


  /* Handle force flag. */
  valuestr = apr_hash_get(ah, SVN_WC__LOG_ATTR_FORCE,
                          APR_HASH_KEY_STRING);
  if (valuestr && strcmp(valuestr, "true") == 0)
    modify_flags |= SVN_WC__ENTRY_MODIFY_FORCE;

  /* Now write the new entry out */
  err = svn_wc__entry_modify(loggy->adm_access, name,
                             entry, modify_flags, FALSE, loggy->pool);
  if (err)
    return svn_error_createf(pick_error_code(loggy), err,
                             _("Error modifying entry for '%s'"), name);
  loggy->entries_modified = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
log_do_delete_lock(struct log_runner *loggy,
                   const char *name)
{
  svn_error_t *err;
  svn_wc_entry_t entry;

  entry.lock_token = entry.lock_comment = entry.lock_owner = NULL;
  entry.lock_creation_date = 0;

  /* Now write the new entry out */
  err = svn_wc__entry_modify(loggy->adm_access, name,
                             &entry,
                             SVN_WC__ENTRY_MODIFY_LOCK_TOKEN
                             | SVN_WC__ENTRY_MODIFY_LOCK_OWNER
                             | SVN_WC__ENTRY_MODIFY_LOCK_COMMENT
                             | SVN_WC__ENTRY_MODIFY_LOCK_CREATION_DATE,
                             FALSE, loggy->pool);
  if (err)
    return svn_error_createf(pick_error_code(loggy), err,
                             _("Error removing lock from entry for '%s'"),
                             name);
  loggy->entries_modified = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
log_do_delete_changelist(struct log_runner *loggy,
                         const char *name)
{
  svn_error_t *err;
  svn_wc_entry_t entry;

  entry.changelist = NULL;

  /* Now write the new entry out */
  err = svn_wc__entry_modify(loggy->adm_access, name,
                             &entry,
                             SVN_WC__ENTRY_MODIFY_CHANGELIST,
                             FALSE, loggy->pool);
  if (err)
    return svn_error_createf(pick_error_code(loggy), err,
                             _("Error removing changelist from entry '%s'"),
                             name);
  loggy->entries_modified = TRUE;

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
  svn_wc_adm_access_t *adm_access;
  const svn_wc_entry_t *entry;
  svn_error_t *err = SVN_NO_ERROR;
  const char *full_path
    = svn_path_join(svn_wc_adm_access_path(loggy->adm_access), name,
                    loggy->pool);

  /* Figure out if 'name' is a dir or a file */
  SVN_ERR(svn_wc_adm_probe_retrieve(&adm_access, loggy->adm_access, full_path,
                                    loggy->pool));
  SVN_ERR(svn_wc_entry(&entry, full_path, adm_access, FALSE, loggy->pool));

  if (! entry)
    /* Hmm....this entry is already absent from the revision control
       system.  Chances are good that this item was removed via a
       commit from this working copy.  */
    return SVN_NO_ERROR;

  /* Remove the object from revision control -- whether it's a
     single file or recursive directory removal.  Attempt
     to destroy all working files & dirs too.

     ### We pass NULL, NULL for cancel_func and cancel_baton below.
     ### If they were available, it would be nice to use them. */
  if (entry->kind == svn_node_dir)
    {
      svn_wc_adm_access_t *ignored;

      /* If we get the right kind of error, it means the directory is
         already missing, so all we need to do is delete its entry in
         the parent directory. */
      err = svn_wc_adm_retrieve(&ignored, adm_access, full_path, loggy->pool);
      if (err)
        {
          if (err->apr_err == SVN_ERR_WC_NOT_LOCKED)
            {
              apr_hash_t *entries;

              svn_error_clear(err);
              err = SVN_NO_ERROR;

              if (entry->schedule != svn_wc_schedule_add)
                {
                  SVN_ERR(svn_wc_entries_read(&entries, loggy->adm_access,
                                              TRUE, loggy->pool));
                  svn_wc__entry_remove(entries, name);
                  SVN_ERR(svn_wc__entries_write(entries, loggy->adm_access,
                                                loggy->pool));
                }
            }
          else
            {
              return err;
            }
        }
      else
        {
          /* Deleting full_path requires that any children it has are
             also locked (issue #3039). */
          SVN_ERR(svn_wc__adm_extend_lock_to_tree(adm_access, loggy->pool));
          err = svn_wc_remove_from_revision_control(adm_access,
                                                    SVN_WC_ENTRY_THIS_DIR,
                                                    TRUE, /* destroy */
                                                    FALSE, /* instant_error */
                                                    NULL, NULL,
                                                    loggy->pool);
        }
    }
  else if (entry->kind == svn_node_file)
    {
      err = svn_wc_remove_from_revision_control(loggy->adm_access, name,
                                                TRUE, /* destroy */
                                                FALSE, /* instant_error */
                                                NULL, NULL,
                                                loggy->pool);
    }

  if (err && err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD)
    {
      svn_error_clear(err);
      return SVN_NO_ERROR;
    }
  else
    {
      return err;
    }
}

static svn_error_t *
remove_deleted_entry(void *baton, const void *key,
                     apr_ssize_t klen, void *val, apr_pool_t *pool)
{
  struct log_runner *loggy = baton;
  const char *base_name;
  const char *pdir;
  const svn_wc_entry_t *cur_entry = val;
  svn_wc_adm_access_t *entry_access;

  /* Skip each entry that isn't scheduled for deletion. This gracefully
     includes excluded item. */
  if (cur_entry->schedule != svn_wc_schedule_delete)
    return SVN_NO_ERROR;

  /* Determine what arguments to hand to our removal function,
     and let BASE_NAME double as an "ok" flag to run that function. */
  base_name = NULL;
  if (cur_entry->kind == svn_node_file)
    {
      pdir = svn_wc_adm_access_path(loggy->adm_access);
      base_name = apr_pstrdup(pool, key);
      entry_access = loggy->adm_access;
    }
  else if (cur_entry->kind == svn_node_dir)
    {
      pdir = svn_path_join(svn_wc_adm_access_path(loggy->adm_access),
                           key, pool);
      base_name = SVN_WC_ENTRY_THIS_DIR;
      SVN_ERR(svn_wc_adm_retrieve(&entry_access, loggy->adm_access,
                                  pdir, pool));
    }

  /* ### We pass NULL, NULL for cancel_func and cancel_baton below.
     ### If they were available, it would be nice to use them. */
  if (base_name)
    SVN_ERR(svn_wc_remove_from_revision_control
            (entry_access, base_name, FALSE, FALSE,
             NULL, NULL, pool));

  return SVN_NO_ERROR;
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
  const char *full_path;
  const char *pdir, *base_name;
  apr_hash_t *entries;
  const svn_wc_entry_t *orig_entry;
  svn_wc_entry_t *entry;
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t prop_mods;
  apr_uint64_t modify_flags = 0;

  /* Determine the actual full path of the affected item. */
  if (! is_this_dir)
    full_path = svn_path_join(svn_wc_adm_access_path(loggy->adm_access),
                              name, pool);
  else
    full_path = apr_pstrdup(pool, svn_wc_adm_access_path(loggy->adm_access));

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
  SVN_ERR(svn_wc_adm_probe_retrieve(&adm_access, loggy->adm_access, full_path,
                                    pool));
  SVN_ERR(svn_wc_entry(&orig_entry, full_path, adm_access, TRUE, pool));

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

          SVN_ERR(svn_wc__entry_modify
                  (loggy->adm_access, NULL, &tmpentry,
                   SVN_WC__ENTRY_MODIFY_REVISION | SVN_WC__ENTRY_MODIFY_KIND,
                   FALSE, pool));
          loggy->entries_modified = TRUE;

          /* Drop the 'killme' file. */
          err = svn_wc__make_killme(loggy->adm_access, entry->keep_local,
                                    pool);

          if (err)
            {
              if (loggy->rerun && APR_STATUS_IS_EEXIST(err->apr_err))
                svn_error_clear(err);
              else
                return err;
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

          SVN_ERR(svn_wc_remove_from_revision_control(loggy->adm_access,
                                                      name, FALSE, FALSE,
                                                      NULL, NULL,
                                                      pool));

          /* If the parent entry's working rev 'lags' behind new_rev... */
          SVN_ERR(svn_wc_entry(&parentry,
                               svn_wc_adm_access_path(loggy->adm_access),
                               loggy->adm_access,
                               FALSE, pool));
          if (new_rev > parentry->revision)
            {
              /* ...then the parent's revision is now officially a
                 lie;  therefore, it must remember the file as being
                 'deleted' for a while.  Create a new, uninteresting
                 ghost entry:  */
              tmp_entry.kind = svn_node_file;
              tmp_entry.deleted = TRUE;
              tmp_entry.revision = new_rev;
              SVN_ERR(svn_wc__entry_modify
                      (loggy->adm_access, name, &tmp_entry,
                       SVN_WC__ENTRY_MODIFY_REVISION
                       | SVN_WC__ENTRY_MODIFY_KIND
                       | SVN_WC__ENTRY_MODIFY_DELETED,
                       FALSE, pool));
              loggy->entries_modified = TRUE;
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
      SVN_ERR(svn_wc_entries_read(&entries, loggy->adm_access, TRUE, pool));
      SVN_ERR(svn_iter_apr_hash(NULL, entries,
                                remove_deleted_entry, loggy, pool));
    }

  SVN_ERR(svn_wc_props_modified_p(&prop_mods, full_path, loggy->adm_access,
                                  pool));
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


          SVN_ERR(svn_wc_get_prop_diffs(&propchanges, NULL,
                                        full_path, loggy->adm_access, pool));
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

      SVN_ERR(svn_wc__working_props_committed(full_path, loggy->adm_access,
                                              FALSE, pool));
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

      if ((err = install_committed_file
           (&overwrote_working, loggy->adm_access, name,
            remove_executable, set_read_write, pool)))
        return svn_error_createf
          (pick_error_code(loggy), err,
           _("Error replacing text-base of '%s'"), name);

      if ((err = svn_io_stat(&finfo, full_path,
                             APR_FINFO_MIN | APR_FINFO_LINK, pool)))
        return svn_error_createf(pick_error_code(loggy), err,
                                 _("Error getting 'affected time' of '%s'"),
                                 svn_path_local_style(full_path, pool));

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
          basef = svn_wc__text_base_path(full_path, 0, pool);
          err = svn_io_stat(&basef_finfo, basef, APR_FINFO_MIN | APR_FINFO_LINK,
                            pool);
          if (err)
            return svn_error_createf
              (pick_error_code(loggy), err,
               _("Error getting 'affected time' for '%s'"),
               svn_path_local_style(basef, pool));
          else
            {
              svn_boolean_t modified;

              /* Verify that the working file is the same as the base file
                 by comparing file sizes, then timestamps and the contents
                 after that. */

              /*###FIXME: if the file needs translation, don't compare
                file-sizes, just compare timestamps and do the rest of the
                hokey pokey. */
              modified = finfo.size != basef_finfo.size;
              if (finfo.mtime != basef_finfo.mtime && ! modified)
                {
                  err = svn_wc__versioned_file_modcheck(&modified, full_path,
                                                        loggy->adm_access,
                                                        basef, FALSE, pool);
                  if (err)
                    return svn_error_createf
                      (pick_error_code(loggy), err,
                       _("Error comparing '%s' and '%s'"),
                       svn_path_local_style(full_path, pool),
                       svn_path_local_style(basef, pool));
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
  entry->conflict_old = NULL;
  entry->conflict_new = NULL;
  entry->conflict_wrk = NULL;
  entry->prejfile = NULL;
  entry->copyfrom_url = NULL;
  entry->copyfrom_rev = SVN_INVALID_REVNUM;
  entry->has_prop_mods = FALSE;

  /* We don't reset tree_conflict_data, because it's about conflicts on
     children, not on this node, and it could conceivably be valid to commit
     this node non-recursively while children are still in conflict. */
  if ((err = svn_wc__entry_modify(loggy->adm_access, name, entry,
                                  (modify_flags
                                   | SVN_WC__ENTRY_MODIFY_REVISION
                                   | SVN_WC__ENTRY_MODIFY_KIND
                                   | SVN_WC__ENTRY_MODIFY_SCHEDULE
                                   | SVN_WC__ENTRY_MODIFY_COPIED
                                   | SVN_WC__ENTRY_MODIFY_DELETED
                                   | SVN_WC__ENTRY_MODIFY_CONFLICT_OLD
                                   | SVN_WC__ENTRY_MODIFY_CONFLICT_NEW
                                   | SVN_WC__ENTRY_MODIFY_CONFLICT_WRK
                                   | SVN_WC__ENTRY_MODIFY_PREJFILE
                                   | SVN_WC__ENTRY_MODIFY_COPYFROM_URL
                                   | SVN_WC__ENTRY_MODIFY_COPYFROM_REV
                                   | SVN_WC__ENTRY_MODIFY_HAS_PROP_MODS
                                   | SVN_WC__ENTRY_MODIFY_FORCE),
                                  FALSE, pool)))
    return svn_error_createf
      (pick_error_code(loggy), err,
       _("Error modifying entry of '%s'"), name);
  loggy->entries_modified = TRUE;

  /* If we aren't looking at "this dir" (meaning we are looking at a
     file), we are finished.  From here on out, it's all about a
     directory's entry in its parent.  */
  if (! is_this_dir)
    return SVN_NO_ERROR;

  /* For directories, we also have to reset the state in the parent's
     entry for this directory, unless the current directory is a `WC
     root' (meaning, our parent directory on disk is not our parent in
     Version Control Land), in which case we're all finished here. */
  SVN_ERR(svn_wc_is_wc_root(&wc_root,
                            svn_wc_adm_access_path(loggy->adm_access),
                            loggy->adm_access,
                            pool));
  if (wc_root)
    return SVN_NO_ERROR;

  /* Make sure our entry exists in the parent. */
  {
    svn_wc_adm_access_t *paccess;
    svn_boolean_t unassociated = FALSE;

    svn_path_split(svn_wc_adm_access_path(loggy->adm_access), &pdir,
                   &base_name, pool);

    err = svn_wc_adm_retrieve(&paccess, loggy->adm_access, pdir, pool);
    if (err && (err->apr_err == SVN_ERR_WC_NOT_LOCKED))
      {
        svn_error_clear(err);
        SVN_ERR(svn_wc_adm_open3(&paccess, NULL, pdir, TRUE, 0,
                                 NULL, NULL, pool));
        unassociated = TRUE;
      }
    else if (err)
      return err;

    SVN_ERR(svn_wc_entries_read(&entries, paccess, FALSE, pool));
    if (apr_hash_get(entries, base_name, APR_HASH_KEY_STRING))
      {
        if ((err = svn_wc__entry_modify(paccess, base_name, entry,
                                        (SVN_WC__ENTRY_MODIFY_SCHEDULE
                                         | SVN_WC__ENTRY_MODIFY_COPIED
                                         | SVN_WC__ENTRY_MODIFY_DELETED
                                         | SVN_WC__ENTRY_MODIFY_FORCE),
                                        TRUE, pool)))
          return svn_error_createf(pick_error_code(loggy), err,
                                   _("Error modifying entry of '%s'"), name);
      }

    if (unassociated)
      SVN_ERR(svn_wc_adm_close2(paccess, pool));
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
  const char *propname, *propval, *path;

  if (strcmp(name, SVN_WC_ENTRY_THIS_DIR) == 0)
    path = svn_wc_adm_access_path(loggy->adm_access);
  else
    path = svn_path_join(svn_wc_adm_access_path(loggy->adm_access),
                         name, loggy->pool);

  propname = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_PROPNAME, atts);
  propval = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_PROPVAL, atts);

  if (propval)
    {
      value.data = propval;
      value.len = strlen(propval);
    }

  SVN_ERR(svn_wc__wcprop_set(propname, propval ? &value : NULL,
                             path, loggy->adm_access, FALSE, loggy->pool));

  loggy->wcprops_modified = TRUE;

  return SVN_NO_ERROR;
}

static svn_error_t *
log_do_upgrade_format(struct log_runner *loggy,
                      const char **atts)
{
  const char *fmtstr = svn_xml_get_attr_value(SVN_WC__LOG_ATTR_FORMAT, atts);
  int fmt;
  const char *path
    = svn_wc__adm_child(svn_wc_adm_access_path(loggy->adm_access),
                        SVN_WC__ADM_FORMAT, loggy->pool);

  if (! fmtstr || (fmt = atoi(fmtstr)) == 0)
    return svn_error_create(pick_error_code(loggy), NULL,
                            _("Invalid 'format' attribute"));

  /* Remove the .svn/format file, if it exists. */
  svn_error_clear(svn_io_remove_file(path, loggy->pool));

  /* The nice thing is that, just by setting this flag, the entries file will
     be rewritten in the desired format. */
  loggy->entries_modified = TRUE;
  /* Reading the entries file will support old formats, even if this number
     is updated. */
  svn_wc__adm_set_wc_format(loggy->adm_access, fmt);

  return SVN_NO_ERROR;
}


static svn_error_t *
log_do_add_tree_conflict(struct log_runner *loggy,
                         const char **atts)
{
  apr_array_header_t *new_conflicts;
  const svn_wc_conflict_description_t *new_conflict;
  const char *dir_path = svn_wc_adm_access_path(loggy->adm_access);

  /* Convert the text data to a conflict. */
  SVN_ERR(svn_wc__read_tree_conflicts(&new_conflicts,
                            svn_xml_get_attr_value(SVN_WC__LOG_ATTR_DATA, atts),
                                      dir_path, loggy->pool));
  new_conflict = APR_ARRAY_IDX(new_conflicts, 0,
                               svn_wc_conflict_description_t *);

  /* Ignore any attempt to re-add an existing tree conflict, as loggy
     operations are idempotent. */
  if (! svn_wc__tree_conflict_exists(loggy->tree_conflicts,
                                     svn_path_basename(new_conflict->path,
                                                       loggy->pool),
                                     loggy->pool))
    {
      /* Copy the new conflict to the result pool.  Add its pointer to
         the array of existing conflicts. */
      APR_ARRAY_PUSH(loggy->tree_conflicts,
                     const svn_wc_conflict_description_t *) =
                        svn_wc__conflict_description_dup(new_conflict,
                                                         loggy->result_pool);

      loggy->tree_conflicts_added = TRUE;
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
  else if (! name && strcmp(eltname, SVN_WC__LOG_UPGRADE_FORMAT) != 0)
    {
      SIGNAL_ERROR
        (loggy, svn_error_createf
         (pick_error_code(loggy), NULL,
          _("Log entry missing 'name' attribute (entry '%s' "
            "for directory '%s')"),
          eltname,
          svn_path_local_style(svn_wc_adm_access_path(loggy->adm_access),
                               loggy->pool)));
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
  else if (strcmp(eltname, SVN_WC__LOG_MERGE) == 0) {
    err = log_do_merge(loggy, name, atts);
  }
  else if (strcmp(eltname, SVN_WC__LOG_MV) == 0) {
    err = log_do_file_xfer(loggy, name, svn_wc__xfer_mv, atts);
  }
  else if (strcmp(eltname, SVN_WC__LOG_CP) == 0) {
    err = log_do_file_xfer(loggy, name, svn_wc__xfer_cp, atts);
  }
  else if (strcmp(eltname, SVN_WC__LOG_CP_AND_TRANSLATE) == 0) {
    err = log_do_file_xfer(loggy, name, svn_wc__xfer_cp_and_translate, atts);
  }
  else if (strcmp(eltname, SVN_WC__LOG_CP_AND_DETRANSLATE) == 0) {
    err = log_do_file_xfer(loggy, name, svn_wc__xfer_cp_and_detranslate, atts);
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
  else if (strcmp(eltname, SVN_WC__LOG_UPGRADE_FORMAT) == 0) {
    err = log_do_upgrade_format(loggy, atts);
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
          svn_path_local_style(svn_wc_adm_access_path(loggy->adm_access),
                               loggy->pool)));
      return;
    }

  if (err)
    SIGNAL_ERROR
      (loggy, svn_error_createf
       (pick_error_code(loggy), err,
        _("Error processing command '%s' in '%s'"),
        eltname,
        svn_path_local_style(svn_wc_adm_access_path(loggy->adm_access),
                             loggy->pool)));

  return;
}

/* Process the "KILLME" file in ADM_ACCESS: remove the administrative area
   for ADM_ACCESS and its children, and, if ADM_ONLY is false, also remove
   the contents of the working copy (leaving only locally-modified files). */
static svn_error_t *
handle_killme(svn_wc_adm_access_t *adm_access,
              svn_boolean_t adm_only,
              svn_cancel_func_t cancel_func,
              void *cancel_baton,
              apr_pool_t *pool)
{
  const svn_wc_entry_t *thisdir_entry, *parent_entry;
  svn_wc_entry_t tmp_entry;
  svn_error_t *err;
  SVN_ERR(svn_wc_entry(&thisdir_entry,
                       svn_wc_adm_access_path(adm_access), adm_access,
                       FALSE, pool));

  /* Blow away the administrative directories, and possibly the working
     copy tree too. */
  err = svn_wc_remove_from_revision_control(adm_access,
                                            SVN_WC_ENTRY_THIS_DIR,
                                            !adm_only, /* destroy */
                                            FALSE, /* no instant err */
                                            cancel_func, cancel_baton,
                                            pool);
  if (err && err->apr_err != SVN_ERR_WC_LEFT_LOCAL_MOD)
    return err;
  svn_error_clear(err);

  /* If revnum of this dir is greater than parent's revnum, then
     recreate 'deleted' entry in parent. */
  {
    const char *parent, *bname;
    svn_wc_adm_access_t *parent_access;

    svn_path_split(svn_wc_adm_access_path(adm_access), &parent, &bname, pool);
    SVN_ERR(svn_wc_adm_retrieve(&parent_access, adm_access, parent, pool));
    SVN_ERR(svn_wc_entry(&parent_entry, parent, parent_access, FALSE, pool));

    if (thisdir_entry->revision > parent_entry->revision)
      {
        tmp_entry.kind = svn_node_dir;
        tmp_entry.deleted = TRUE;
        tmp_entry.revision = thisdir_entry->revision;
        SVN_ERR(svn_wc__entry_modify(parent_access, bname, &tmp_entry,
                                     SVN_WC__ENTRY_MODIFY_REVISION
                                     | SVN_WC__ENTRY_MODIFY_KIND
                                     | SVN_WC__ENTRY_MODIFY_DELETED,
                                     TRUE, pool));
      }
  }
  return SVN_NO_ERROR;
}


/*** Using the parser to run the log file. ***/

/* Determine the log file that should be used for a given number. */
const char *
svn_wc__logfile_path(int log_number,
                     apr_pool_t *pool)
{
  return apr_psprintf(pool, SVN_WC__ADM_LOG "%s",
                      (log_number == 0) ? ""
                      : apr_psprintf(pool, ".%d", log_number));
}

/* Run a series of log-instructions from a memory block of length BUF_LEN
   at BUF. RERUN and DIFF3_CMD are passed in the log baton to the
   log runner callbacks.

   Allocations are done in POOL.
*/
static svn_error_t *
run_log_from_memory(svn_wc_adm_access_t *adm_access,
                    const char *buf,
                    apr_size_t buf_len,
                    svn_boolean_t rerun,
                    const char *diff3_cmd,
                    apr_pool_t *pool)
{
  struct log_runner *loggy;
  svn_xml_parser_t *parser;
  /* kff todo: use the tag-making functions here, now. */
  const char *log_start
    = "<wc-log xmlns=\"http://subversion.tigris.org/xmlns\">\n";
  const char *log_end
    = "</wc-log>\n";

  loggy = apr_pcalloc(pool, sizeof(*loggy));
  loggy->adm_access = adm_access;
  loggy->pool = svn_pool_create(pool);
  loggy->result_pool = svn_pool_create(pool);
  loggy->parser = svn_xml_make_parser(loggy, start_handler,
                                      NULL, NULL, pool);
  loggy->entries_modified = FALSE;
  loggy->wcprops_modified = FALSE;
  loggy->rerun = rerun;
  loggy->diff3_cmd = diff3_cmd;
  loggy->count = 0;
  loggy->tree_conflicts_added = FALSE;
  loggy->tree_conflicts = NULL;

  parser = loggy->parser;
  /* Expat wants everything wrapped in a top-level form, so start with
     a ghost open tag. */
  SVN_ERR(svn_xml_parse(parser, log_start, strlen(log_start), 0));

  SVN_ERR(svn_xml_parse(parser, buf, buf_len, 0));

  /* Pacify Expat with a pointless closing element tag. */
  return svn_xml_parse(parser, log_end, strlen(log_end), 1);
}


/* #define RERUN_LOG_FILES */
/* Run a sequence of log files. */
static svn_error_t *
run_log(svn_wc_adm_access_t *adm_access,
        svn_boolean_t rerun,
        const char *diff3_cmd,
        apr_pool_t *pool)
{
  svn_xml_parser_t *parser;
  struct log_runner *loggy = apr_pcalloc(pool, sizeof(*loggy));
  char *buf = apr_palloc(pool, SVN__STREAM_CHUNK_SIZE);
  const char *logfile_path;
  int log_number;
  apr_pool_t *iterpool = svn_pool_create(pool);
  svn_boolean_t killme, kill_adm_only;
  const svn_wc_entry_t *entry;

  /* kff todo: use the tag-making functions here, now. */
  const char *log_start
    = "<wc-log xmlns=\"http://subversion.tigris.org/xmlns\">\n";
  const char *log_end
    = "</wc-log>\n";

  /* #define RERUN_LOG_FILES to test that rerunning log files works */
#ifdef RERUN_LOG_FILES
  int rerun_counter = 2;
 rerun:
#endif

  parser = svn_xml_make_parser(loggy, start_handler, NULL, NULL, pool);
  loggy->adm_access = adm_access;
  loggy->pool = svn_pool_create(pool);
  loggy->result_pool = svn_pool_create(pool);
  loggy->parser = parser;
  loggy->entries_modified = FALSE;
  loggy->wcprops_modified = FALSE;
  loggy->rerun = rerun;
  loggy->diff3_cmd = diff3_cmd;
  loggy->count = 0;
  loggy->tree_conflicts_added = FALSE;

  /* Populate the tree conflict array with the existing tree conflicts. */
  SVN_ERR(svn_wc_entry(&entry, svn_wc_adm_access_path(adm_access), adm_access,
                       TRUE, pool));
  SVN_ERR(svn_wc__read_tree_conflicts(&(loggy->tree_conflicts),
                                      entry->tree_conflict_data,
                                      svn_wc_adm_access_path(adm_access),
                                      pool));

  /* Expat wants everything wrapped in a top-level form, so start with
     a ghost open tag. */
  SVN_ERR(svn_xml_parse(parser, log_start, strlen(log_start), 0));

  for (log_number = 0; ; log_number++)
    {
      svn_stream_t *stream;
      svn_error_t *err;
      apr_size_t len = SVN__STREAM_CHUNK_SIZE;

      svn_pool_clear(iterpool);
      logfile_path = svn_wc__logfile_path(log_number, iterpool);

      /* Parse the log file's contents. */
      err = svn_wc__open_adm_stream(&stream,
                                    svn_wc_adm_access_path(adm_access),
                                    logfile_path,
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
  SVN_ERR(svn_xml_parse(parser, log_end, strlen(log_end), 1));

  svn_xml_free_parser(parser);

  /* If the logs included tree conflicts, write them to the entry. */
  if (loggy->tree_conflicts_added)
    {
      svn_wc_entry_t tmp_entry;
      svn_error_t *err;

      SVN_ERR(svn_wc__write_tree_conflicts(&tmp_entry.tree_conflict_data,
                                           loggy->tree_conflicts, pool));

      err = svn_wc__entry_modify(adm_access, SVN_WC_ENTRY_THIS_DIR,
                                 &tmp_entry,
                                 SVN_WC__ENTRY_MODIFY_TREE_CONFLICT_DATA,
                                 FALSE, pool);
      if (err)
        return svn_error_createf(pick_error_code(loggy), err,
                                 _("Error recording tree conflicts in '%s'"),
                                 svn_wc_adm_access_path(adm_access));

      loggy->entries_modified = TRUE;
    }

#ifdef RERUN_LOG_FILES
  rerun = TRUE;
  if (--rerun_counter)
    goto rerun;
#endif

  if (loggy->entries_modified)
    {
      apr_hash_t *entries;
      SVN_ERR(svn_wc_entries_read(&entries, adm_access, TRUE, pool));
      SVN_ERR(svn_wc__entries_write(entries, adm_access, pool));
    }
  if (loggy->wcprops_modified)
    SVN_ERR(svn_wc__wcprops_flush(adm_access, pool));

  /* Check for a 'killme' file in the administrative area. */
  SVN_ERR(svn_wc__check_killme(adm_access, &killme, &kill_adm_only, pool));
  if (killme)
    {
      SVN_ERR(handle_killme(adm_access, kill_adm_only, NULL, NULL, pool));
    }
  else
    {
      for (log_number--; log_number >= 0; log_number--)
        {
          svn_pool_clear(iterpool);
          logfile_path = svn_wc__logfile_path(log_number, iterpool);

          /* No 'killme'?  Remove the logfile; its commands have been
             executed. */
          SVN_ERR(svn_wc__remove_adm_file(adm_access, logfile_path, iterpool));
        }
    }

  svn_pool_destroy(iterpool);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__run_log(svn_wc_adm_access_t *adm_access,
                const char *diff3_cmd,
                apr_pool_t *pool)
{
  return run_log(adm_access, FALSE, diff3_cmd, pool);
}

svn_error_t *
svn_wc__rerun_log(svn_wc_adm_access_t *adm_access,
                  const char *diff3_cmd,
                  apr_pool_t *pool)
{
  return run_log(adm_access, TRUE, diff3_cmd, pool);
}



/*** Log file generation helpers ***/

/* Extend LOG_ACCUM with log operations to do MOVE_COPY_OP to SRC_PATH and
 * DST_PATH.
 *
 * SRC_PATH and DST_PATH are relative to ADM_ACCESS.
 */
static svn_error_t *
loggy_move_copy_internal(svn_stringbuf_t **log_accum,
                         svn_boolean_t is_move,
                         svn_wc_adm_access_t *adm_access,
                         const char *src_path, const char *dst_path,
                         apr_pool_t *pool)
{
  svn_node_kind_t kind;
  const char *full_src = svn_path_join(svn_wc_adm_access_path(adm_access),
                                       src_path, pool);

  SVN_ERR(svn_io_check_path(full_src, &kind, pool));

  /* Does this file exist? */
  if (kind != svn_node_none)
    {
      svn_xml_make_open_tag(log_accum, pool,
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
 * to which ADM_ACCESS belongs, or SVN_WC_ENTRY_THIS_DIR if PATH is that
 * directory. PATH must not be outside that directory. */
static const char *
loggy_path(const char *path,
           svn_wc_adm_access_t *adm_access)
{
  const char *adm_path = svn_wc_adm_access_path(adm_access);
  const char *local_path = svn_path_is_child(adm_path, path, NULL);

  if (! local_path && strcmp(path, adm_path) == 0)
    local_path = SVN_WC_ENTRY_THIS_DIR;

  return local_path;
}

svn_error_t *
svn_wc__loggy_append(svn_stringbuf_t **log_accum,
                     svn_wc_adm_access_t *adm_access,
                     const char *src, const char *dst,
                     apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum,
                        pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_APPEND,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path(src, adm_access),
                        SVN_WC__LOG_ATTR_DEST,
                        loggy_path(dst, adm_access),
                        NULL);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__loggy_committed(svn_stringbuf_t **log_accum,
                        svn_wc_adm_access_t *adm_access,
                        const char *path, svn_revnum_t revnum,
                        apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum, pool, svn_xml_self_closing,
                        SVN_WC__LOG_COMMITTED,
                        SVN_WC__LOG_ATTR_NAME, loggy_path(path, adm_access),
                        SVN_WC__LOG_ATTR_REVISION,
                        apr_psprintf(pool, "%ld", revnum),
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_copy(svn_stringbuf_t **log_accum,
                   svn_wc_adm_access_t *adm_access,
                   const char *src_path, const char *dst_path,
                   apr_pool_t *pool)
{
  return loggy_move_copy_internal(log_accum, FALSE, adm_access,
                                  loggy_path(src_path, adm_access),
                                  loggy_path(dst_path, adm_access),
                                  pool);
}

svn_error_t *
svn_wc__loggy_translated_file(svn_stringbuf_t **log_accum,
                              svn_wc_adm_access_t *adm_access,
                              const char *dst,
                              const char *src,
                              const char *versioned,
                              apr_pool_t *pool)
{
  svn_xml_make_open_tag
    (log_accum, pool, svn_xml_self_closing,
     SVN_WC__LOG_CP_AND_TRANSLATE,
     SVN_WC__LOG_ATTR_NAME, loggy_path(src, adm_access),
     SVN_WC__LOG_ATTR_DEST, loggy_path(dst, adm_access),
     SVN_WC__LOG_ATTR_ARG_2, loggy_path(versioned, adm_access),
     NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_delete_entry(svn_stringbuf_t **log_accum,
                           svn_wc_adm_access_t *adm_access,
                           const char *path,
                           apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum, pool, svn_xml_self_closing,
                        SVN_WC__LOG_DELETE_ENTRY,
                        SVN_WC__LOG_ATTR_NAME, loggy_path(path, adm_access),
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_delete_lock(svn_stringbuf_t **log_accum,
                          svn_wc_adm_access_t *adm_access,
                          const char *path,
                          apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum, pool, svn_xml_self_closing,
                        SVN_WC__LOG_DELETE_LOCK,
                        SVN_WC__LOG_ATTR_NAME, loggy_path(path, adm_access),
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_delete_changelist(svn_stringbuf_t **log_accum,
                                svn_wc_adm_access_t *adm_access,
                                const char *path,
                                apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum, pool, svn_xml_self_closing,
                        SVN_WC__LOG_DELETE_CHANGELIST,
                        SVN_WC__LOG_ATTR_NAME, loggy_path(path, adm_access),
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_entry_modify(svn_stringbuf_t **log_accum,
                           svn_wc_adm_access_t *adm_access,
                           const char *path,
                           svn_wc_entry_t *entry,
                           apr_uint64_t modify_flags,
                           apr_pool_t *pool)
{
  apr_hash_t *prop_hash = apr_hash_make(pool);
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
                 apr_psprintf(pool, "%ld", entry->revision));

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_URL,
                 SVN_WC__ENTRY_ATTR_URL,
                 entry->url);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_REPOS,
                 SVN_WC__ENTRY_ATTR_REPOS,
                 entry->repos);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_UUID,
                 SVN_WC__ENTRY_ATTR_UUID,
                 entry->uuid);

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

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_INCOMPLETE,
                 SVN_WC__ENTRY_ATTR_INCOMPLETE,
                 entry->incomplete ? "true" : "false");

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_COPYFROM_URL,
                 SVN_WC__ENTRY_ATTR_COPYFROM_URL,
                 entry->copyfrom_url);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_COPYFROM_REV,
                 SVN_WC__ENTRY_ATTR_COPYFROM_REV,
                 apr_psprintf(pool, "%ld", entry->copyfrom_rev));

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
                 svn_time_to_cstring(entry->text_time, pool));

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_CHECKSUM,
                 SVN_WC__ENTRY_ATTR_CHECKSUM,
                 entry->checksum);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_CMT_REV,
                 SVN_WC__ENTRY_ATTR_CMT_REV,
                 apr_psprintf(pool, "%ld", entry->cmt_rev));

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_CMT_DATE,
                 SVN_WC__ENTRY_ATTR_CMT_DATE,
                 svn_time_to_cstring(entry->cmt_date, pool));

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_CMT_AUTHOR,
                 SVN_WC__ENTRY_ATTR_CMT_AUTHOR,
                 entry->cmt_author);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_LOCK_TOKEN,
                 SVN_WC__ENTRY_ATTR_LOCK_TOKEN,
                 entry->lock_token);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_LOCK_OWNER,
                 SVN_WC__ENTRY_ATTR_LOCK_OWNER,
                 entry->lock_owner);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_LOCK_COMMENT,
                 SVN_WC__ENTRY_ATTR_LOCK_COMMENT,
                 entry->lock_comment);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_LOCK_CREATION_DATE,
                 SVN_WC__ENTRY_ATTR_LOCK_CREATION_DATE,
                 svn_time_to_cstring(entry->lock_creation_date, pool));

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_HAS_PROPS,
                 SVN_WC__ENTRY_ATTR_HAS_PROPS,
                 entry->has_props ? "true" : "false");

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_HAS_PROP_MODS,
                 SVN_WC__ENTRY_ATTR_HAS_PROP_MODS,
                 entry->has_prop_mods ? "true" : "false");

  /* Note: ignoring the (deprecated) cachable_props field. */

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_PRESENT_PROPS,
                 SVN_WC__ENTRY_ATTR_PRESENT_PROPS,
                 entry->present_props);

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_WORKING_SIZE,
                 SVN_WC__ENTRY_ATTR_WORKING_SIZE,
                 apr_psprintf(pool, "%" APR_OFF_T_FMT,
                              entry->working_size));
  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_FORCE,
                 SVN_WC__LOG_ATTR_FORCE,
                 "true");

  ADD_ENTRY_ATTR(SVN_WC__ENTRY_MODIFY_TREE_CONFLICT_DATA,
                 SVN_WC__ENTRY_ATTR_TREE_CONFLICT_DATA,
                 entry->tree_conflict_data ? entry->tree_conflict_data : "");

#undef ADD_ENTRY_ATTR

  if (apr_hash_count(prop_hash) == 0)
    return SVN_NO_ERROR;

  apr_hash_set(prop_hash, SVN_WC__LOG_ATTR_NAME,
               APR_HASH_KEY_STRING, loggy_path(path, adm_access));

  svn_xml_make_open_tag_hash(log_accum, pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_MODIFY_ENTRY,
                             prop_hash);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__loggy_modify_wcprop(svn_stringbuf_t **log_accum,
                            svn_wc_adm_access_t *adm_access,
                            const char *path,
                            const char *propname,
                            const char *propval,
                            apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum, pool, svn_xml_self_closing,
                        SVN_WC__LOG_MODIFY_WCPROP,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path(path, adm_access),
                        SVN_WC__LOG_ATTR_PROPNAME,
                        propname,
                        SVN_WC__LOG_ATTR_PROPVAL,
                        propval,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_move(svn_stringbuf_t **log_accum,
                   svn_wc_adm_access_t *adm_access,
                   const char *src_path, const char *dst_path,
                   apr_pool_t *pool)
{
  return loggy_move_copy_internal(log_accum, TRUE, adm_access,
                                  loggy_path(src_path, adm_access),
                                  loggy_path(dst_path, adm_access),
                                  pool);
}

svn_error_t *
svn_wc__loggy_maybe_set_executable(svn_stringbuf_t **log_accum,
                                   svn_wc_adm_access_t *adm_access,
                                   const char *path,
                                   apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum,
                        pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_MAYBE_EXECUTABLE,
                        SVN_WC__LOG_ATTR_NAME, loggy_path(path, adm_access),
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_maybe_set_readonly(svn_stringbuf_t **log_accum,
                                 svn_wc_adm_access_t *adm_access,
                                 const char *path,
                                 apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum,
                        pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_MAYBE_READONLY,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path(path, adm_access),
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_set_entry_timestamp_from_wc(svn_stringbuf_t **log_accum,
                                          svn_wc_adm_access_t *adm_access,
                                          const char *path,
                                          apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum,
                        pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_MODIFY_ENTRY,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path(path, adm_access),
                        SVN_WC__ENTRY_ATTR_TEXT_TIME,
                        SVN_WC__TIMESTAMP_WC,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_set_entry_working_size_from_wc(svn_stringbuf_t **log_accum,
                                                svn_wc_adm_access_t *adm_access,
                                                const char *path,
                                                apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum,
                        pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_MODIFY_ENTRY,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path(path, adm_access),
                        SVN_WC__ENTRY_ATTR_WORKING_SIZE,
                        SVN_WC__TIMESTAMP_WC,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_set_readonly(svn_stringbuf_t **log_accum,
                           svn_wc_adm_access_t *adm_access,
                           const char *path,
                           apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum,
                        pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_READONLY,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path(path, adm_access),
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_set_timestamp(svn_stringbuf_t **log_accum,
                            svn_wc_adm_access_t *adm_access,
                            const char *path,
                            const char *timestr,
                            apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum,
                        pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_SET_TIMESTAMP,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path(path, adm_access),
                        SVN_WC__LOG_ATTR_TIMESTAMP,
                        timestr,
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_remove(svn_stringbuf_t **log_accum,
                     svn_wc_adm_access_t *adm_access,
                     const char *path,
                     apr_pool_t *pool)
{
  /* No need to check whether BASE_NAME exists: ENOENT is ignored
     by the log-runner */
  svn_xml_make_open_tag(log_accum, pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_RM,
                        SVN_WC__LOG_ATTR_NAME,
                        loggy_path(path, adm_access),
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_upgrade_format(svn_stringbuf_t **log_accum,
                             svn_wc_adm_access_t *adm_access,
                             int format,
                             apr_pool_t *pool)
{
  svn_xml_make_open_tag(log_accum, pool,
                        svn_xml_self_closing,
                        SVN_WC__LOG_UPGRADE_FORMAT,
                        SVN_WC__LOG_ATTR_FORMAT,
                        apr_itoa(pool, format),
                        NULL);

  return SVN_NO_ERROR;
}

svn_error_t *
svn_wc__loggy_add_tree_conflict(svn_stringbuf_t **log_accum,
                                const svn_wc_conflict_description_t *conflict,
                                svn_wc_adm_access_t *adm_access,
                                apr_pool_t *pool)
{
  const char *conflict_data;
  apr_array_header_t *conflicts;

  /* ### TODO: implement write_one_tree_conflict(). */
  conflicts = apr_array_make(pool, 1,
      sizeof(svn_wc_conflict_description_t *));
  APR_ARRAY_PUSH(conflicts, const svn_wc_conflict_description_t *) = conflict;

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
svn_wc__write_log(svn_wc_adm_access_t *adm_access,
                  int log_number, svn_stringbuf_t *log_content,
                  apr_pool_t *pool)
{
  svn_stream_t *stream;
  const char *temp_file_path;
  const char *logfile_name = svn_wc__logfile_path(log_number, pool);
  const char *adm_path = svn_wc_adm_access_path(adm_access);
  apr_size_t len = log_content->len;

  SVN_ERR(svn_wc__open_adm_writable(&stream, &temp_file_path,
                                    adm_path, logfile_name, pool, pool));

  SVN_ERR_W(svn_stream_write(stream, log_content->data, &len),
            apr_psprintf(pool, _("Error writing log for '%s'"),
                         svn_path_local_style(logfile_name, pool)));

  return svn_wc__close_adm_stream(stream, temp_file_path, adm_path,
                                  logfile_name, pool);
}


/*** Recursively do log things. ***/

svn_error_t *
svn_wc_cleanup(const char *path,
               svn_wc_adm_access_t *optional_adm_access,
               const char *diff3_cmd,
               svn_cancel_func_t cancel_func,
               void *cancel_baton,
               apr_pool_t *pool)
{
  return svn_wc_cleanup2(path, diff3_cmd, cancel_func, cancel_baton, pool);
}


static svn_error_t *
cleanup_internal(const char *path,
                 const char *diff3_cmd,
                 svn_cancel_func_t cancel_func,
                 void *cancel_baton,
                 svn_boolean_t already_recursing,
                 apr_pool_t *pool)
{
  apr_hash_t *entries = NULL;
  apr_hash_index_t *hi;
  svn_node_kind_t kind;
  svn_wc_adm_access_t *adm_access;
  svn_boolean_t cleanup;
  int wc_format_version;
  apr_pool_t *subpool;
  svn_boolean_t killme, kill_adm_only;

  /* Check cancellation; note that this catches recursive calls too. */
  if (cancel_func)
    SVN_ERR(cancel_func(cancel_baton));

  SVN_ERR(svn_wc_check_wc(path, &wc_format_version, pool));

  /* a "version" of 0 means a non-wc directory */
  if (wc_format_version == 0)
    {
      /* If we've been asked to cleanup a non-working copy, bail out.
       * But if this directory is simply found missing during recursion,
       * silently ignore it. */
      if (already_recursing)
        return SVN_NO_ERROR;
      else
        return svn_error_createf
          (SVN_ERR_WC_NOT_DIRECTORY, NULL,
           _("'%s' is not a working copy directory"),
           svn_path_local_style(path, pool));
    }

  /* Lock this working copy directory, or steal an existing lock */
  SVN_ERR(svn_wc__adm_steal_write_lock(&adm_access, NULL, path, pool));

  /* Recurse on versioned elements first, oddly enough. */
  SVN_ERR(svn_wc_entries_read(&entries, adm_access, FALSE, pool));
  subpool = svn_pool_create(pool);
  for (hi = apr_hash_first(pool, entries); hi; hi = apr_hash_next(hi))
    {
      const void *key;
      void *val;
      const svn_wc_entry_t *entry;
      const char *entry_path;

      svn_pool_clear(subpool);
      apr_hash_this(hi, &key, NULL, &val);
      entry = val;
      entry_path = svn_path_join(path, key, subpool);

      if (entry->kind == svn_node_dir
          && strcmp(key, SVN_WC_ENTRY_THIS_DIR) != 0)
        {
          /* Sub-directories */
          SVN_ERR(svn_io_check_path(entry_path, &kind, subpool));
          if (kind == svn_node_dir)
            SVN_ERR(cleanup_internal(entry_path, diff3_cmd,
                                     cancel_func, cancel_baton,
                                     TRUE, subpool));
        }
      else
        {
          /* "." and things that are not directories, check for mods to
             trigger the timestamp repair mechanism.  Since this rewrites
             the entries file for each timestamp fixed it has the potential
             to be slow, perhaps we need something more sophisticated? */
          svn_boolean_t modified;
          SVN_ERR(svn_wc_props_modified_p(&modified, entry_path,
                                          adm_access, subpool));
          if (entry->kind == svn_node_file)
            SVN_ERR(svn_wc_text_modified_p(&modified, entry_path, FALSE,
                                           adm_access, subpool));
        }
    }
  svn_pool_destroy(subpool);

  SVN_ERR(svn_wc__check_killme(adm_access, &killme, &kill_adm_only, pool));

  if (killme)
    {
      /* A KILLME indicates that the log has already been run */
      SVN_ERR(handle_killme(adm_access, kill_adm_only, cancel_func,
                            cancel_baton, pool));
    }
  else
    {
      /* In an attempt to maintain consistency between the decisions made in
         this function, and those made in the access baton lock-removal code,
         we use the same test as the lock-removal code. */
      SVN_ERR(svn_wc__adm_is_cleanup_required(&cleanup, adm_access, pool));
      if (cleanup)
        SVN_ERR(svn_wc__rerun_log(adm_access, diff3_cmd, pool));
    }

  /* Cleanup the tmp area of the admin subdir, if running the log has not
     removed it!  The logs have been run, so anything left here has no hope
     of being useful. */
  SVN_ERR(svn_wc__adm_cleanup_tmp_area(adm_access, pool));

  return svn_wc_adm_close2(adm_access, pool);
}

svn_error_t *
svn_wc_cleanup2(const char *path,
                const char *diff3_cmd,
                svn_cancel_func_t cancel_func,
                void *cancel_baton,
                apr_pool_t *pool)
{
  return cleanup_internal(path, diff3_cmd, cancel_func, cancel_baton,
                          FALSE, pool);

}
