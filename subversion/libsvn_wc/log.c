/*
 * log.c:  handle the adm area's log file.
 *
 * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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
#include <apr_thread_proc.h>

#include "svn_wc.h"
#include "svn_error.h"
#include "svn_string.h"
#include "svn_xml.h"
#include "svn_pools.h"
#include "svn_io.h"

#include "wc.h"
#include "log.h"
#include "adm_files.h"
#include "entries.h"
#include "translate.h"
#include "questions.h"


/*** Userdata for the callbacks. ***/
struct log_runner
{
  apr_pool_t *pool;
  svn_xml_parser_t *parser;
  svn_wc_adm_access_t *adm_access;  /* the dir in which this is all happening */
};



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
                                       the current property vals of DEST.
      svn_wc__xfer_cp_and_detranslate: copy NAME to DEST, converting to LF
                                       and contracting keywords according to
                                       the current property vals of NAME.
*/
static svn_error_t *
file_xfer_under_path (const char *path,
                      const char *name,
                      const char *dest,
                      enum svn_wc__xfer_action action,
                      apr_pool_t *pool)
{
  svn_error_t *err;
  const char *full_from_path, *full_dest_path;

  full_from_path = svn_path_join (path, name, pool);
  full_dest_path = svn_path_join (path, dest, pool);

  switch (action)
    {
    case svn_wc__xfer_append:
      return svn_io_append_file (full_from_path, full_dest_path, pool);
      
    case svn_wc__xfer_cp:
      return svn_io_copy_file (full_from_path, full_dest_path, FALSE, pool);

    case svn_wc__xfer_cp_and_translate:
      {
        /* This action is currently only used by svn_wc_install_file,
           when loggily translating some text-base (NAME) to a working
           file (DEST). */

        /* Get eol-style and keywords from the current properties of DEST. */
        svn_wc_keywords_t *keywords;
        const char *eol_str;
        enum svn_wc__eol_style style;
        svn_boolean_t toggled;

        SVN_ERR (svn_wc__get_keywords (&keywords, full_dest_path, NULL, pool));
        SVN_ERR (svn_wc__get_eol_style (&style, &eol_str,
                                        full_dest_path, pool));

        SVN_ERR (svn_wc_copy_and_translate (full_from_path,
                                            full_dest_path,
                                            eol_str,
                                            TRUE,
                                            keywords,
                                            TRUE,
                                            pool));

        /* After copying, toggle the executable bit if props dictate. */
        return svn_wc__maybe_toggle_working_executable_bit 
          (&toggled,
           full_dest_path,
           pool);
      }

    case svn_wc__xfer_cp_and_detranslate:
      {
        /* I don't think this has any callers yet, but my conscience
           says that this option should be here anyway. :-) */
        svn_wc_keywords_t *keywords;
        SVN_ERR (svn_wc__get_keywords (&keywords, full_from_path, NULL, pool));
        
        return svn_wc_copy_and_translate (full_from_path,
                                          full_dest_path,
                                          "\n", /* repository-normal EOL */
                                          TRUE,  
                                          keywords,
                                          FALSE, /* contract keywords */
                                          pool);
      }

    case svn_wc__xfer_mv:
      /* Remove read-only flag on destination. */
      SVN_ERR (svn_io_set_file_read_write (full_dest_path, TRUE, pool));

      err = svn_io_file_rename (full_from_path,
                                full_dest_path, pool);

      /* If we got an ENOENT, that's ok;  the move has probably
         already completed in an earlier run of this log.  */
      if (err && (! APR_STATUS_IS_ENOENT(err->apr_err)))
        return svn_error_quick_wrap (err,
                                     "file_xfer_under_path: "
                                     "can't move from to dest");
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
 * If the executable property is set, the appropriately toggle the
 * working file's executability.
 *
 * If the working file was re-translated or had permissions toggled,
 * then set OVERWROTE_WORKING to TRUE.  If the working file isn't
 * touched at all, then set to FALSE.
 * 
 * Use POOL for any temporary allocation.
 */
static svn_error_t *
install_committed_file (svn_boolean_t *overwrote_working,
                        const char *path,
                        const char *name,
                        apr_pool_t *pool)
{
  const char *filepath;
  const char *tmp_text_base;
  enum svn_node_kind kind;
  svn_wc_keywords_t *keywords;
  apr_status_t apr_err;
  apr_file_t *ignored;
  svn_boolean_t same, toggled;
  const char *tmp_wfile, *pdir, *bname;
  enum svn_wc__eol_style eol_style;
  const char *eol_str;

  /* start off assuming that the working file isn't touched. */
  *overwrote_working = FALSE;

  filepath = svn_path_join (path, name, pool);

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

  /* start off getting the latest translation prop values. */
  SVN_ERR (svn_wc__get_eol_style (&eol_style, &eol_str, filepath, pool));
  SVN_ERR (svn_wc__get_keywords (&keywords, filepath, NULL, pool));

  svn_path_split_nts (filepath, &pdir, &bname, pool);
  tmp_wfile = svn_wc__adm_path (pdir, TRUE, pool, bname, NULL);
  
  SVN_ERR (svn_io_open_unique_file (&ignored, &tmp_wfile,
                                    tmp_wfile, SVN_WC__TMP_EXT,
                                    FALSE, pool));
  apr_err = apr_file_close (ignored);
  if (apr_err)
    return svn_error_createf
      (apr_err, 0, NULL, pool,
       "install_committed_file: error closing %s", tmp_wfile);

  /* Is there a tmp_text_base that needs to be installed?  */
  tmp_text_base = svn_wc__text_base_path (filepath, 1, pool);
  SVN_ERR (svn_io_check_path (tmp_text_base, &kind, pool));

  if (kind == svn_node_file)
    SVN_ERR (svn_wc_copy_and_translate (tmp_text_base,
                                        tmp_wfile,
                                        eol_str,
                                        FALSE, /* don't repair eol */
                                        keywords,
                                        TRUE, /* expand keywords */
                                        pool));
  else
    SVN_ERR (svn_wc_copy_and_translate (filepath,
                                        tmp_wfile,
                                        eol_str,
                                        FALSE, /* don't repair eol */
                                        keywords,
                                        TRUE, /* expand keywords */
                                        pool));

  SVN_ERR (svn_wc__files_contents_same_p (&same, tmp_wfile, filepath, pool));
  
  if (! same)
    {
      SVN_ERR (svn_io_copy_file (tmp_wfile, filepath, FALSE, pool));
      *overwrote_working = TRUE;
    }

  SVN_ERR (svn_io_remove_file (tmp_wfile, pool));

  /* Toggle the working file's execute bit if props dictate. */
  SVN_ERR (svn_wc__maybe_toggle_working_executable_bit (&toggled,
                                                        filepath, pool));
  if (toggled)
    /* okay, so we didn't -overwrite- the working file, but we changed
       its timestamp, which is the point of returning this flag. :-) */
    *overwrote_working = TRUE;

  /* Install the new text base if one is waiting. */
  if (kind == svn_node_file)  /* tmp_text_base exists */
    SVN_ERR (svn_wc__sync_text_base (filepath, pool));

  return SVN_NO_ERROR;
}


static void
signal_error (struct log_runner *loggy, svn_error_t *err)
{
  svn_xml_signal_bailout
    (svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                        0,
                        err,
                        loggy->pool,
                        "in directory %s",
                        svn_wc_adm_access_path (loggy->adm_access)),
     loggy->parser);
}





/*** Dispatch on the xml opening tag. ***/

static svn_error_t *
log_do_run_cmd (struct log_runner *loggy,
                const char *name,
                const XML_Char **atts)
{
  svn_error_t *err;
  const char
    *infile_name,
    *outfile_name,
    *errfile_name;
  apr_file_t
    *infile = NULL,
    *outfile = NULL,
    *errfile = NULL;
  const char *args[10];
  
  args[0] = name;
  /* Grab the arguments.
     You want ugly?  I'll give you ugly... */
  args[1] = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_1, atts);
  args[2] = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_2, atts);
  args[3] = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_3, atts);
  args[4] = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_4, atts);
  args[5] = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_5, atts);
  args[6] = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_6, atts);
  args[7] = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_7, atts);
  args[8] = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_8, atts);
  args[9] = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_9, atts);
  
  /* Grab the input and output, if any. */
  infile_name = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_INFILE, atts);;
  outfile_name = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_OUTFILE, atts);;
  errfile_name = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ERRFILE, atts);;
  
  if (infile_name)
    {
      const char *infile_path
        = svn_path_join (svn_wc_adm_access_path (loggy->adm_access),
                         infile_name, loggy->pool);
      
      SVN_ERR_W (svn_io_file_open (&infile, infile_path, APR_READ,
                                   APR_OS_DEFAULT, loggy->pool),
                 "error opening infile");
    }
  
  if (outfile_name)
    {
      const char *outfile_path
        = svn_path_join (svn_wc_adm_access_path (loggy->adm_access),
                         outfile_name, loggy->pool);
      
      /* kff todo: always creates and overwrites, currently.
         Could append if file exists... ?  Consider. */
      SVN_ERR_W (svn_io_file_open (&outfile, outfile_path,
                                   (APR_WRITE | APR_CREATE),
                                   APR_OS_DEFAULT, loggy->pool),
                 "error opening outfile");
    }
  
  if (errfile_name)
    {
      const char *errfile_path
        = svn_path_join (svn_wc_adm_access_path (loggy->adm_access),
                         errfile_name, loggy->pool);
      
      /* kff todo: always creates and overwrites, currently.
         Could append if file exists... ?  Consider. */
       SVN_ERR_W (svn_io_file_open (&errfile, errfile_path,
                                    (APR_WRITE | APR_CREATE),
                                    APR_OS_DEFAULT, loggy->pool),
                  "error opening errfile");
    }
  
  err = svn_io_run_cmd (svn_wc_adm_access_path (loggy->adm_access),
                        name, args, NULL, NULL, FALSE,
                        infile, outfile, errfile, loggy->pool);
  if (err)
     return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
                               "error running %s in %s",
                               name,
                               svn_wc_adm_access_path (loggy->adm_access));

  /* TODO: Handle status here, or pass it back to caller. */

  return SVN_NO_ERROR;
}


static svn_error_t *
log_do_merge (struct log_runner *loggy,
              const char *name,
              const XML_Char **atts)
{
  const char *left, *right;
  const char *left_label, *right_label, *target_label;
  svn_error_t *err;
  apr_pool_t *subpool = svn_pool_create (loggy->pool);

  /* NAME is the basename of our merge_target.  Pull out LEFT and RIGHT. */
  left = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_1, atts);
  if (! left)
    return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
                              "missing 'left' attr in %s",
                              svn_wc_adm_access_path (loggy->adm_access));
  right = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_2, atts);
  if (! right)
    return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
                              "missing 'right' attr in %s",
                              svn_wc_adm_access_path (loggy->adm_access));

  /* Grab all three labels too.  If non-existent, we'll end up passing
     NULLs to svn_wc_merge, which is fine -- it will use default
     labels. */
  left_label = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_3, atts);
  right_label = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_4, atts);
  target_label = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_ARG_5, atts);

  /* Convert the 3 basenames into full paths. */
  left = svn_path_join (svn_wc_adm_access_path (loggy->adm_access), left,
                        subpool);
  right = svn_path_join (svn_wc_adm_access_path (loggy->adm_access), right,
                         subpool);
  name = svn_path_join (svn_wc_adm_access_path (loggy->adm_access), name,
                        subpool);
  
  /* Now do the merge with our full paths. */
  err = svn_wc_merge (left, right, name, loggy->adm_access,
                      left_label, right_label, target_label,
                      subpool);

  if (err && (err->apr_err != SVN_ERR_WC_CONFLICT))
    /* Got a *real* error. */
    return svn_error_quick_wrap 
      (err, "svn_wc_merge() returned an unexpected error");

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


static svn_error_t *
log_do_file_xfer (struct log_runner *loggy,
                  const char *name,
                  enum svn_wc__xfer_action action,
                  const XML_Char **atts)
{
  svn_error_t *err;
  const char *dest = NULL;

  /* We have the name (src), and the destination is absolutely required. */
  dest = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_DEST, atts);
  if (! dest)
    return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
                              "missing dest attr in %s",
                              svn_wc_adm_access_path (loggy->adm_access));

  err = file_xfer_under_path (svn_wc_adm_access_path (loggy->adm_access),
                              name, dest, action, loggy->pool);
  if (err)
    signal_error (loggy, err);

  return SVN_NO_ERROR;
}

/* Make file NAME in log's CWD readonly */
static svn_error_t *
log_do_file_readonly (struct log_runner *loggy,
                      const char *name,
                      enum svn_wc__xfer_action action,
                      const XML_Char **atts)
{
  const char *full_path
    = svn_path_join (svn_wc_adm_access_path (loggy->adm_access), name,
                     loggy->pool);

  SVN_ERR (svn_io_set_file_read_only (full_path, FALSE, loggy->pool));

  return SVN_NO_ERROR;
}

/* Remove file NAME in log's CWD. */
static svn_error_t *
log_do_rm (struct log_runner *loggy, const char *name)
{
  const char *full_path
    = svn_path_join (svn_wc_adm_access_path (loggy->adm_access),
                     name, loggy->pool);

  SVN_ERR (svn_io_remove_file (full_path, loggy->pool));

  return SVN_NO_ERROR;
}




static svn_error_t *
log_do_modify_entry (struct log_runner *loggy,
                     const char *name,
                     const XML_Char **atts)
{
  svn_error_t *err;
  apr_hash_t *ah = svn_xml_make_att_hash (atts, loggy->pool);
  const char *tfile;
  svn_wc_entry_t *entry;
  apr_uint32_t modify_flags;
  const char *valuestr;

  /* Convert the attributes into an entry structure. */
  SVN_ERR (svn_wc__atts_to_entry (&entry, &modify_flags, ah, loggy->pool));

  /* Make TFILE the path of the thing being modified.  */
  tfile = svn_path_join (svn_wc_adm_access_path (loggy->adm_access),
                         strcmp (name, SVN_WC_ENTRY_THIS_DIR) ? name : "",
                         loggy->pool);
      
  /* Did the log command give us any timestamps?  There are three
     possible scenarios here.  We must check both text_time
     and prop_time for each of the three scenarios.  */

  /* TEXT_TIME: */
  valuestr = apr_hash_get (ah, SVN_WC__ENTRY_ATTR_TEXT_TIME,
                           APR_HASH_KEY_STRING);

  if ((modify_flags & SVN_WC__ENTRY_MODIFY_TEXT_TIME)
      && (! strcmp (valuestr, SVN_WC_TIMESTAMP_WC)))
    {
      enum svn_node_kind tfile_kind;
      apr_time_t text_time;

      err = svn_io_check_path (tfile, &tfile_kind, loggy->pool);
      if (err)
        return svn_error_createf
          (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
           "error checking path `%s'", tfile);
          
      err = svn_io_file_affected_time (&text_time, tfile, loggy->pool);
      if (err)
        return svn_error_createf
          (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
           "error getting file affected time on `%s'", tfile);

      entry->text_time = text_time;
    }

  /* PROP_TIME: */
  valuestr = apr_hash_get (ah, SVN_WC__ENTRY_ATTR_PROP_TIME, 
                           APR_HASH_KEY_STRING);

  if ((modify_flags & SVN_WC__ENTRY_MODIFY_PROP_TIME)
      && (! strcmp (valuestr, SVN_WC_TIMESTAMP_WC)))
    {
      const char *pfile;
      enum svn_node_kind pfile_kind;
      apr_time_t prop_time;

      err = svn_wc__prop_path (&pfile, tfile, 0, loggy->pool);
      if (err)
        signal_error (loggy, err);
      
      err = svn_io_check_path (pfile, &pfile_kind, loggy->pool);
      if (err)
        return svn_error_createf
          (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
           "error checking path `%s'", pfile);
      
      err = svn_io_file_affected_time (&prop_time, pfile, loggy->pool);
      if (err)
        return svn_error_createf
          (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
           "error getting file affected time on `%s'", pfile);

      entry->prop_time = prop_time;
    }

  /* Now write the new entry out */
  err = svn_wc__entry_modify (loggy->adm_access, name,
                              entry, modify_flags, loggy->pool);
  if (err)
    return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
                              "error merge_syncing entry `%s'", name);

  return SVN_NO_ERROR;
}


/* Ben sez:  this log command is (at the moment) only executed by the
   update editor.  It attempts to forcefully remove working data. */
static svn_error_t *
log_do_delete_entry (struct log_runner *loggy, const char *name)
{
  svn_wc_entry_t *entry;
  svn_error_t *err = NULL;
  const char *full_path
    = svn_path_join (svn_wc_adm_access_path (loggy->adm_access), name,
                     loggy->pool);

  /* Figure out if 'name' is a dir or a file */
  svn_wc_entry (&entry, full_path, FALSE, loggy->pool);

  if (! entry)
    /* Hmm....this entry is already absent from the revision control
       system.  Chances are good that this item was removed via a
       commit from this working copy.  */
    return SVN_NO_ERROR;

  /* Remove the object from revision control -- whether it's a
     single file or recursive directory removal.  Attempt
     attempt to destroy all working files & dirs too. */
  if (entry->kind == svn_node_dir)
    {
      svn_wc_adm_access_t *adm_access;
      SVN_ERR (svn_wc_adm_retrieve (&adm_access, loggy->adm_access, full_path,
                                    loggy->pool));
      err = svn_wc_remove_from_revision_control (adm_access,
                                                 SVN_WC_ENTRY_THIS_DIR,
                                                 TRUE, loggy->pool);
    }
  else if (entry->kind == svn_node_file)
    err = svn_wc_remove_from_revision_control (loggy->adm_access, name,
                                               TRUE, loggy->pool);
    
  /* It's possible that locally modified files were left behind during
     the removal.  That's okay;  just check for this special case. */
  if (err && (err->apr_err == SVN_ERR_WC_LEFT_LOCAL_MOD))
    svn_error_clear_all (err);
  else if (err)
    return err;

  /* (## Perhaps someday have the client print a warning that "locally
     modified files were not deleted" ??) */    

  return SVN_NO_ERROR;
}

/* Note:  assuming that svn_wc__log_commit() is what created all of
   the <committed...> commands, the `name' attribute will either be a
   file or SVN_WC_ENTRY_THIS_DIR. */
static svn_error_t *
log_do_committed (struct log_runner *loggy,
                  const char *name,
                  const XML_Char **atts)
{
  svn_error_t *err;
  apr_pool_t *pool = loggy->pool; 
  int is_this_dir = (strcmp (name, SVN_WC_ENTRY_THIS_DIR) == 0);
  const char *rev = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_REVISION, atts);
  svn_boolean_t wc_root, overwrote_working = FALSE, remove_executable = FALSE;
  const char *full_path;
  const char *pdir, *base_name;
  apr_hash_t *entries;
  svn_wc_entry_t *entry;
  apr_time_t text_time = 0; /* By default, don't override old stamp. */
  apr_time_t prop_time = 0; /* By default, don't override old stamp. */
  svn_node_kind_t kind;

  /* Determine the actual full path of the affected item. */
  if (! is_this_dir)
    full_path = svn_path_join (svn_wc_adm_access_path (loggy->adm_access),
                               name, pool);
  else
    full_path = apr_pstrdup (pool, svn_wc_adm_access_path (loggy->adm_access));

  /*** Perform sanity checking operations ***/

  /* If no new post-commit revision was given us, bail with an error. */
  if (! rev)
    return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, pool,
                              "missing revision attr for %s", name);
      
  /* Read the entry for the affected item.  If we can't find the
     entry, or if the entry states that our item is not either "this
     dir" or a file kind, perhaps this isn't really the entry our log
     creator was expecting.  */
  SVN_ERR (svn_wc_entry (&entry, full_path, TRUE, pool));
  if ((! entry) || ((! is_this_dir) && (entry->kind != svn_node_file)))
    return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, pool,
                              "log command for dir '%s' is mislocated", name);


  /*** Handle the committed deletion case ***/

  /* If the committed item was scheduled for deletion, it needs to
     now be removed from revision control.  Once that is accomplished,
     we are finished handling this item.  */
  if (entry->schedule == svn_wc_schedule_delete)
    {
      svn_revnum_t new_rev = SVN_STR_TO_REV(rev);

      /* If we are suppose to delete "this dir", drop a 'killme' file
         into my own adminstrative dir as a signal for svn_wc__run_log() 
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

          SVN_ERR (svn_wc__entry_modify
                   (loggy->adm_access, NULL, &tmpentry,
                    SVN_WC__ENTRY_MODIFY_REVISION | SVN_WC__ENTRY_MODIFY_KIND,
                    pool));

          /* Drop the 'killme' file. */
          return svn_wc__make_adm_thing (loggy->adm_access, SVN_WC__ADM_KILLME,
                                         svn_node_file, APR_OS_DEFAULT,
                                         0, pool);

        }

      /* Else, we're deleting a file, and we can safely remove files
         from revision control without screwing something else up. */
      else
        {         
          svn_wc_entry_t *parentry, *tmpentry;

          SVN_ERR (svn_wc_remove_from_revision_control (loggy->adm_access,
                                                        name, FALSE, pool));
          
          /* If the parent entry's working rev 'lags' behind new_rev... */
          SVN_ERR (svn_wc_entry (&parentry,
                                 svn_wc_adm_access_path (loggy->adm_access),
                                 TRUE, pool));
          if (new_rev > parentry->revision)
            {
              /* ...then the parent's revision is now officially a
                 lie;  therefore, it must remember the file as being
                 'deleted' for a while.  Create a new, uninteresting
                 ghost entry:  */
              tmpentry = apr_pcalloc (pool, sizeof(*tmpentry));
              tmpentry->kind = svn_node_file;
              tmpentry->deleted = TRUE;
              tmpentry->revision = new_rev;
              SVN_ERR (svn_wc__entry_modify
                       (loggy->adm_access, name, tmpentry,
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
      apr_hash_index_t *hi;
              
      /* Loop over all children entries, look for items scheduled for
         deletion. */
      SVN_ERR (svn_wc_entries_read (&entries,
                                    svn_wc_adm_access_path (loggy->adm_access),
                                    TRUE, pool));
      for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          apr_ssize_t klen;
          void *val;
          svn_wc_entry_t *cur_entry; 
          svn_wc_adm_access_t *entry_access;
                  
          /* Get the next entry */
          apr_hash_this (hi, &key, &klen, &val);
          cur_entry = (svn_wc_entry_t *) val;
                  
          /* Skip each entry that isn't scheduled for deletion. */
          if (cur_entry->schedule != svn_wc_schedule_delete)
            continue;
          
          /* Determine what arguments to hand to our removal function,
             and let BASE_NAME double as an "ok" flag to run that function. */
          base_name = NULL;
          if (cur_entry->kind == svn_node_file)
            {
              pdir = svn_wc_adm_access_path (loggy->adm_access);
              base_name = apr_pstrdup (pool, key);
              entry_access = loggy->adm_access;
            }
          else if (cur_entry->kind == svn_node_dir)
            {
              pdir = svn_path_join (svn_wc_adm_access_path (loggy->adm_access),
                                    key, pool);
              base_name = SVN_WC_ENTRY_THIS_DIR;
              SVN_ERR (svn_wc_adm_retrieve (&entry_access, loggy->adm_access,
                                            pdir, pool));
            }

          if (base_name)
            SVN_ERR (svn_wc_remove_from_revision_control 
                     (entry_access, base_name, FALSE, pool));
        }
    }


  /* For file commit items, we need to "install" the user's working
     file as the new `text-base' in the administrative area.  A copy
     of this file should have been dropped into our `tmp/text-base'
     directory during the commit process.  Part of this process
     involves setting the textual timestamp for this entry.  We'd like
     to just use the timestamp of the working file, but it is possible that
     at some point during the commit, the real working file might have
     changed again.  If that has happened, we'll use the timestamp of
     the copy of this file in `tmp/text-base'. */
  if (! is_this_dir)
    {
      const char *wf = full_path, *tmpf;

      /* Make sure our working file copy is present in the temp area. */
      tmpf = svn_wc__text_base_path (wf, 1, pool);
      if ((err = svn_io_check_path (tmpf, &kind, pool)))
        return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, err, pool,
                                  "error checking existence: %s", name);
      if (kind == svn_node_file)
        {
          svn_boolean_t same;
          const char *chosen;

          /* Verify that the working file is the same as the tmpf file. */
          if ((err = svn_wc__versioned_file_modcheck (&same, wf, tmpf, pool)))
            return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, err, pool,
                                      "error comparing `%s' and `%s'",
                                      wf, tmpf);

          /* If they are the same, use the working file's timestamp,
             else use the tmpf file's timestamp. */
          chosen = same ? wf : tmpf;

          /* Get the timestamp from our chosen file. */
          if ((err = svn_io_file_affected_time (&text_time, chosen, pool)))
            return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, err, pool,
                                      "error getting affected time: %s",
                                      chosen);
        }
    }
              
  /* Now check for property commits.  If a property commit occured, a
     copy of the "working" property file should have been dumped in
     the admistrative `tmp' area.  We'll let that tmpfile's existence
     be a signal that we need to do post-commit property processing.
     Also, we have to again decide which timestamp to use (see the
     text-time case above).  */
  {
    const char *wf, *tmpf, *basef;

    /* Get property file pathnames (not from the `tmp' area) depending
       on whether we're examining a file or THIS_DIR */
    
    /* ### Logic check: if is_this_dir, then full_path is the same
       as loggy->adm_access->path, I think.  In which case we don't need the
       inline conditionals below... */
    
    SVN_ERR (svn_wc__prop_path
             (&wf,
              is_this_dir
              ? svn_wc_adm_access_path (loggy->adm_access) : full_path,
              0, pool));
    SVN_ERR (svn_wc__prop_base_path
             (&basef,
              is_this_dir
              ? svn_wc_adm_access_path (loggy->adm_access) : full_path,
              0, pool));
    
    /* If this file was replaced in the commit, then we definitely
       need to begin by removing any old residual prop-base file.  */
    if (entry->schedule == svn_wc_schedule_replace)
      {
        svn_node_kind_t kinder;
        SVN_ERR (svn_io_check_path (basef, &kinder, pool));
        if (kinder == svn_node_file)
          SVN_ERR (svn_io_remove_file (basef, pool));
      }

    SVN_ERR (svn_wc__prop_path
             (&tmpf,
              is_this_dir
              ? svn_wc_adm_access_path (loggy->adm_access) : full_path,
              1, pool));
    if ((err = svn_io_check_path (tmpf, &kind, pool)))
      return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, err, pool,
                                "error checking existence: %s", name);
    if (kind == svn_node_file)
      {
        svn_boolean_t same;
        const char *chosen;
        
        /* We need to decide which prop-timestamp to use, just like we
           did with text-time above. */
        if ((err = svn_wc__files_contents_same_p (&same, wf, tmpf, pool)))
          return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, err, pool,
                                    "error comparing `%s' and `%s'",
                                    wf, tmpf);

        /* If they are the same, use the working file's timestamp,
           else use the tmp_base file's timestamp. */
        chosen = same ? wf : tmpf;

        /* Get the timestamp of our chosen file. */
        if ((err = svn_io_file_affected_time (&prop_time, chosen, pool)))
          return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, err, pool,
                                    "error getting affected time: %s",
                                    chosen);

        /* Examine propchanges here before installing the new
           propbase.  If the executable prop was -deleted-, then set a
           flag that will remind us to run -x after our call to
           install_committed_file(). */
        if (! is_this_dir)
          {
            int i;
            apr_array_header_t *propchanges;
            SVN_ERR (svn_wc_get_prop_diffs (&propchanges, NULL,
                                            full_path, pool));
            for (i = 0; i < propchanges->nelts; i++)
              {
                svn_prop_t *propchange
                  = &APR_ARRAY_IDX (propchanges, i, svn_prop_t);
                
                if ((! strcmp (propchange->name, SVN_PROP_EXECUTABLE))
                    && (propchange->value == NULL))
                  {
                    remove_executable = TRUE;
                    break;
                  }
              }                
          }

        /* Make the tmp prop file the new pristine one.  Note that we
           have to temporarily set the file permissions for writability. */
        SVN_ERR (svn_io_set_file_read_write (basef, TRUE, pool));
        SVN_ERR (svn_io_file_rename (tmpf, basef, pool));
        SVN_ERR (svn_io_set_file_read_only (basef, FALSE, pool));
      }
  }   

  /* Timestamps have been decided on, and prop-base has been installed
     if necessary.  Now we install the new text-base (if present), and
     possibly re-translate the working file. */
  if (! is_this_dir)
    {
      /* We need to remove the `add' schedule flag before expanding
         keywords, since the URL keyword is sensitive to schedule
         flags.  It won't expand if it thinks an entry is scheduled
         for addition, because such an entry doesn't yet have a URL. */
      entry->schedule = svn_wc_schedule_normal;
      if ((err = svn_wc__entry_modify
           (loggy->adm_access, name, entry,
            SVN_WC__ENTRY_MODIFY_SCHEDULE | SVN_WC__ENTRY_MODIFY_FORCE, pool)))
        return svn_error_createf
          (SVN_ERR_WC_BAD_ADM_LOG, 0, err, pool,
           "error modifying entry: %s", name);

      /* Okay, NOW install the new file, which may involve expanding
         keywords. */
      if ((err = install_committed_file
           (&overwrote_working, svn_wc_adm_access_path (loggy->adm_access),
            name, pool)))
        return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, err, pool,
                                  "error replacing text-base: %s", name);

      /* The previous call will have run +x if the executable property
         was added or already present.  But if this property was
         -removed-, (detected earlier), then run -x here on the new
         working file.  */
      if (remove_executable)
        {
          SVN_ERR (svn_io_set_file_executable (full_path,
                                               FALSE, /* chmod -x */
                                               FALSE, pool));
          overwrote_working = TRUE; /* entry needs wc-file's timestamp  */
        }
      
      /* If the working file was overwritten (due to re-translation)
         or touched (due to +x / -x), then use *that* textual
         timestamp instead. */
      if (overwrote_working)
        if ((err = svn_io_file_affected_time (&text_time, full_path, pool)))
          return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, err, pool,
                                    "error getting affected time: %s",
                                    full_path);
    }
    
  /* Files have been moved, and timestamps have been found.  It is now
     fime for The Big Entry Modification. */
  entry->revision = SVN_STR_TO_REV (rev);
  entry->kind = is_this_dir ? svn_node_dir : svn_node_file;
  entry->schedule = svn_wc_schedule_normal;
  entry->copied = FALSE;
  entry->deleted = FALSE;
  entry->text_time = text_time;
  entry->prop_time = prop_time;
  entry->conflict_old = NULL;
  entry->conflict_new = NULL;
  entry->conflict_wrk = NULL;
  entry->prejfile = NULL;
  entry->copyfrom_url = NULL;
  entry->copyfrom_rev = SVN_INVALID_REVNUM;
  if ((err = svn_wc__entry_modify (loggy->adm_access, name, entry,
                                   (SVN_WC__ENTRY_MODIFY_REVISION 
                                    | SVN_WC__ENTRY_MODIFY_SCHEDULE 
                                    | SVN_WC__ENTRY_MODIFY_COPIED
                                    | SVN_WC__ENTRY_MODIFY_DELETED
                                    | SVN_WC__ENTRY_MODIFY_COPYFROM_URL
                                    | SVN_WC__ENTRY_MODIFY_COPYFROM_REV
                                    | SVN_WC__ENTRY_MODIFY_CONFLICT_OLD
                                    | SVN_WC__ENTRY_MODIFY_CONFLICT_NEW
                                    | SVN_WC__ENTRY_MODIFY_CONFLICT_WRK
                                    | SVN_WC__ENTRY_MODIFY_PREJFILE
                                    | SVN_WC__ENTRY_MODIFY_TEXT_TIME
                                    | SVN_WC__ENTRY_MODIFY_PROP_TIME
                                    | SVN_WC__ENTRY_MODIFY_FORCE),
                                   pool)))
    return svn_error_createf
      (SVN_ERR_WC_BAD_ADM_LOG, 0, err, pool,
       "error modifying entry: %s", name);

  /* If we aren't looking at "this dir" (meaning we are looking at a
     file), we are finished.  From here on out, it's all about a
     directory's entry in its parent.  */
  if (! is_this_dir)
    return SVN_NO_ERROR;

  /* For directories, we also have to reset the state in the parent's
     entry for this directory, unless the current directory is a `WC
     root' (meaning, our parent directory on disk is not our parent in
     Version Control Land), in which case we're all finished here. */
  SVN_ERR (svn_wc_is_wc_root (&wc_root,
                              svn_wc_adm_access_path (loggy->adm_access),
                              pool));
  if (wc_root)
    return SVN_NO_ERROR;

  /* Make sure our entry exists in the parent (if the parent is even a
     SVN working copy directory). */
  svn_path_split_nts (svn_wc_adm_access_path (loggy->adm_access), &pdir,
                      &base_name, pool);
  SVN_ERR (svn_wc_entries_read (&entries, pdir, FALSE, pool));
  if (apr_hash_get (entries, base_name, APR_HASH_KEY_STRING))
    {
      svn_wc_adm_access_t *parent_access;
      SVN_ERR (svn_wc_adm_retrieve (&parent_access, loggy->adm_access, pdir,
                                    pool));
      if ((err = svn_wc__entry_modify (parent_access, base_name, entry,
                                       (SVN_WC__ENTRY_MODIFY_SCHEDULE 
                                        | SVN_WC__ENTRY_MODIFY_COPIED
                                        | SVN_WC__ENTRY_MODIFY_DELETED
                                        | SVN_WC__ENTRY_MODIFY_FORCE),
                                       pool)))
        return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, err, pool,
                                  "error merge_syncing %s", name);
    }

  return SVN_NO_ERROR;
}


static void
start_handler (void *userData, const XML_Char *eltname, const XML_Char **atts)
{
  svn_error_t *err = NULL;
  struct log_runner *loggy = userData;

  /* All elements use the `name' attribute, so grab it now. */
  const char *name = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_NAME, atts);

  if (strcmp (eltname, "wc-log") == 0)   /* ignore expat pacifier */
    return;
  else if (! name)
    {
      signal_error
        (loggy, svn_error_createf 
         (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
          "log entry missing name attribute (entry %s for dir %s)",
          eltname, svn_wc_adm_access_path (loggy->adm_access)));
      return;
    }
  
  /* Dispatch. */
  if (strcmp (eltname, SVN_WC__LOG_RUN_CMD) == 0) {
    err = log_do_run_cmd (loggy, name, atts);
  }
  else if (strcmp (eltname, SVN_WC__LOG_MODIFY_ENTRY) == 0) {
    err = log_do_modify_entry (loggy, name, atts);
  }
  else if (strcmp (eltname, SVN_WC__LOG_DELETE_ENTRY) == 0) {
    err = log_do_delete_entry (loggy, name);
  }
  else if (strcmp (eltname, SVN_WC__LOG_COMMITTED) == 0) {
    err = log_do_committed (loggy, name, atts);
  }
  else if (strcmp (eltname, SVN_WC__LOG_RM) == 0) {
    err = log_do_rm (loggy, name);
  }
  else if (strcmp (eltname, SVN_WC__LOG_MERGE) == 0) {
    err = log_do_merge (loggy, name, atts);
  }
  else if (strcmp (eltname, SVN_WC__LOG_MV) == 0) {
    err = log_do_file_xfer (loggy, name, svn_wc__xfer_mv, atts);
  }
  else if (strcmp (eltname, SVN_WC__LOG_CP) == 0) {
    err = log_do_file_xfer (loggy, name, svn_wc__xfer_cp, atts);
  }
  else if (strcmp (eltname, SVN_WC__LOG_CP_AND_TRANSLATE) == 0) {
    err = log_do_file_xfer (loggy, name,svn_wc__xfer_cp_and_translate, atts);
  }
  else if (strcmp (eltname, SVN_WC__LOG_CP_AND_DETRANSLATE) == 0) {
    err = log_do_file_xfer (loggy, name,svn_wc__xfer_cp_and_detranslate, atts);
  }
  else if (strcmp (eltname, SVN_WC__LOG_APPEND) == 0) {
    err = log_do_file_xfer (loggy, name, svn_wc__xfer_append, atts);
  }
  else if (strcmp (eltname, SVN_WC__LOG_READONLY) == 0) {
    err = log_do_file_readonly (loggy, name, svn_wc__xfer_append, atts);
  }
  else
    {
      signal_error
        (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                   0,
                                   NULL,
                                   loggy->pool,
                                   "unrecognized logfile element in %s: `%s'",
                                   svn_wc_adm_access_path (loggy->adm_access),
                                   eltname));
      return;
    }

  if (err)
    signal_error
      (loggy, svn_error_createf
       (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
        "start_handler: error processing command '%s' in %s",
        eltname, svn_wc_adm_access_path (loggy->adm_access)));
  
  return;
}



/*** Using the parser to run the log file. ***/

svn_error_t *
svn_wc__run_log (svn_wc_adm_access_t *adm_access, apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  svn_xml_parser_t *parser;
  struct log_runner *loggy = apr_pcalloc (pool, sizeof (*loggy));
  char buf[BUFSIZ];
  apr_size_t buf_len;
  apr_file_t *f = NULL;

  /* kff todo: use the tag-making functions here, now. */
  const char *log_start
    = "<wc-log xmlns=\"http://subversion.tigris.org/xmlns\">\n";
  const char *log_end
    = "</wc-log>\n";

  parser = svn_xml_make_parser (loggy, start_handler, NULL, NULL, pool);
  loggy->adm_access = adm_access;
  loggy->pool = pool;
  loggy->parser = parser;
  
  /* Expat wants everything wrapped in a top-level form, so start with
     a ghost open tag. */
  SVN_ERR (svn_xml_parse (parser, log_start, strlen (log_start), 0));

  /* Parse the log file's contents. */
  err = svn_wc__open_adm_file (&f, svn_wc_adm_access_path (adm_access),
                               SVN_WC__ADM_LOG, APR_READ, pool);
  if (err)
    return svn_error_quick_wrap (err, "svn_wc__run_log: couldn't open log.");
  
  do {
    buf_len = sizeof (buf);

    apr_err = apr_file_read (f, buf, &buf_len);
    if (apr_err && !APR_STATUS_IS_EOF(apr_err))
      {
        apr_file_close (f);
        return svn_error_createf (apr_err, 0, NULL, pool,
                                 "error reading adm log file in %s",
                                  svn_wc_adm_access_path (adm_access));
      }

    err = svn_xml_parse (parser, buf, buf_len, 0);
    if (err)
      {
        apr_file_close (f);
        return err;
      }

    if (APR_STATUS_IS_EOF(apr_err))
      {
        /* Not an error, just means we're done. */
        apr_file_close (f);
        break;
      }
  } while (apr_err == APR_SUCCESS);

  /* Pacify Expat with a pointless closing element tag. */
  SVN_ERR (svn_xml_parse (parser, log_end, strlen (log_end), 1));

  svn_xml_free_parser (parser);

  /* Check for a 'killme' file in the administrative area. */
  if (svn_wc__adm_path_exists (svn_wc_adm_access_path (adm_access), 0, pool,
                               SVN_WC__ADM_KILLME, NULL))
    {
      svn_wc_entry_t *thisdir_entry, *parent_entry, *tmpentry;
      SVN_ERR (svn_wc_entry (&thisdir_entry,
                             svn_wc_adm_access_path (adm_access), FALSE, pool));

      /* Blow away the entire directory, and all those below it too. */
      SVN_ERR (svn_wc_remove_from_revision_control (adm_access,
                                                    SVN_WC_ENTRY_THIS_DIR,
                                                    TRUE, pool));

      /* If revnum of this dir is greater than parent's revnum, then
         recreate 'deleted' entry in parent. */
      {
        const char *parent, *bname;
        svn_path_split_nts (svn_wc_adm_access_path (adm_access), &parent,
                            &bname, pool);
        SVN_ERR (svn_wc_entry (&parent_entry, parent, FALSE, pool));
        
        if (thisdir_entry->revision > parent_entry->revision)
          {
            svn_wc_adm_access_t *parent_access;
            SVN_ERR (svn_wc_adm_retrieve (&parent_access, adm_access, parent,
                                          pool));
            tmpentry = apr_pcalloc (pool, sizeof(*tmpentry));
            tmpentry->kind = svn_node_dir;
            tmpentry->deleted = TRUE;
            tmpentry->revision = thisdir_entry->revision;
            SVN_ERR (svn_wc__entry_modify (parent_access, bname, tmpentry,
                                           SVN_WC__ENTRY_MODIFY_REVISION
                                           | SVN_WC__ENTRY_MODIFY_KIND
                                           | SVN_WC__ENTRY_MODIFY_DELETED,
                                           pool));            
          }
      }
    }
  else
    {
      /* No 'killme'?  Remove the logfile;  its commands have been executed. */
      SVN_ERR (svn_wc__remove_adm_file (svn_wc_adm_access_path (adm_access),
                                        pool, SVN_WC__ADM_LOG, NULL));
    }

  return SVN_NO_ERROR;
}



/*** Recursively do log things. ***/

svn_error_t *
svn_wc_cleanup (const char *path,
                svn_wc_adm_access_t *optional_adm_access,
                apr_pool_t *pool)
{
  apr_hash_t *entries = NULL;
  apr_hash_index_t *hi;
  const char *log_path = svn_wc__adm_path (path, 0, pool,
                                           SVN_WC__ADM_LOG, NULL);
  enum svn_node_kind kind;
  svn_wc_adm_access_t *adm_access;
  int is_wc;

  SVN_ERR (svn_wc_check_wc (path, &is_wc, pool));
  if (! is_wc)
    return svn_error_createf
      (SVN_ERR_WC_NOT_DIRECTORY, 0, NULL, pool,
       "svn_wc_cleanup: %s is not a working copy directory", path);

  /* Lock this working copy directory, or steal an existing lock */
  SVN_ERR (svn_wc__adm_steal_write_lock (&adm_access, optional_adm_access, 
                                         path, pool));

  /* Recurse on versioned subdirs first, oddly enough. */
  SVN_ERR (svn_wc_entries_read (&entries, path, FALSE, pool));

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      svn_wc_entry_t *entry;

      apr_hash_this (hi, &key, NULL, &val);
      entry = val;

      if ((entry->kind == svn_node_dir)
          && (strcmp (key, SVN_WC_ENTRY_THIS_DIR) != 0))
        {
          /* Recurse */
          const char *subdir = svn_path_join (path, key, pool);
          SVN_ERR (svn_wc_cleanup (subdir, adm_access, pool));
        }
    }

  /* Is there a log?  If so, run it. */
  SVN_ERR (svn_io_check_path (log_path, &kind, pool));
  if (kind == svn_node_file)
    SVN_ERR (svn_wc__run_log (adm_access, pool));

  /* Cleanup the tmp area of the admin subdir, if running the log has not
     removed it!  The logs have been run, so anything left here has no hope
     of being useful. */
  if (svn_wc__adm_path_exists (path, 0, pool, NULL))
    SVN_ERR (svn_wc__adm_cleanup_tmp_area (adm_access, pool));

  if (! optional_adm_access)
    SVN_ERR (svn_wc_adm_close (adm_access));

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */

