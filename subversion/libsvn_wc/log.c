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
#include "wc.h"



/*** Userdata for the callbacks. ***/
struct log_runner
{
  apr_pool_t *pool;
  svn_xml_parser_t *parser;
  svn_stringbuf_t *path;  /* the dir in which this is all happening */
};



/*** The XML handlers. ***/

/* Used by file_xfer_under_path(). */
enum svn_wc__xfer_action {
  svn_wc__xfer_append,
  svn_wc__xfer_cp,
  svn_wc__xfer_mv,
};



/* Copy (or rename, if RENAME is non-zero) NAME to DEST, assuming that
   PATH is the common parent of both locations. 

   If ACTION is 'cp', then do translation IFF either EOL_STR or
   KEYWORDS is non-NULL.  

   When doing translation:

     - setting REPAIR indicates that inconsistent line endings in SRC
       are translated to EOL_STR; else error is returned.  (For more
       info, read the docstring of svn_wc_copy_and_translate).  

     - setting EXPAND indicates a desire to expand keywords, else you
       wish to contract them.  
*/
static svn_error_t *
file_xfer_under_path (svn_stringbuf_t *path,
                      const char *name,
                      const char *dest,
                      enum svn_wc__xfer_action action,
                      const char *eol_str,
                      svn_boolean_t repair,
                      svn_wc_keywords_t *keywords,
                      svn_boolean_t expand,
                      apr_pool_t *pool)
{
  apr_status_t status;
  svn_stringbuf_t *full_from_path, *full_dest_path;

  full_from_path = svn_stringbuf_dup (path, pool);
  full_dest_path = svn_stringbuf_dup (path, pool);

  svn_path_add_component_nts (full_from_path, name);
  svn_path_add_component_nts (full_dest_path, dest);

  switch (action)
    {
    case svn_wc__xfer_append:
      return svn_io_append_file (full_from_path, full_dest_path, pool);
      
    case svn_wc__xfer_cp:
      return svn_wc_copy_and_translate (full_from_path->data,
                                        full_dest_path->data,
                                        eol_str,
                                        repair,
                                        keywords,
                                        expand,
                                        pool);

    case svn_wc__xfer_mv:
      /* Remove read-only flag on destination. */
      SVN_ERR (svn_io_set_file_read_write (full_dest_path->data, TRUE, pool));

      status = apr_file_rename (full_from_path->data,
                              full_dest_path->data, pool);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "file_xfer_under_path: "
                                  "can't move %s to %s",
                                  name, dest);
  }

  return SVN_NO_ERROR;
}



static svn_error_t *
replace_text_base (svn_stringbuf_t *path,
                   const char *name,
                   apr_pool_t *pool)
{
  svn_stringbuf_t *filepath;
  svn_stringbuf_t *tmp_text_base;
  svn_error_t *err;
  enum svn_node_kind kind;
  svn_wc_keywords_t *keywords;

  filepath = svn_stringbuf_dup (path, pool);
  svn_path_add_component_nts (filepath, name);

  tmp_text_base = svn_wc__text_base_path (filepath, 1, pool);
  err = svn_io_check_path (tmp_text_base, &kind, pool);
  if (err)
    return err;

  if (kind == svn_node_none)
    return SVN_NO_ERROR;  /* tolerate mop-up calls gracefully */
  else
    {
      apr_status_t apr_err;
      apr_file_t *ignored;
      svn_boolean_t same;
      svn_stringbuf_t *tmp_wfile, *pdir, *bname;
      enum svn_wc__eol_style eol_style;
      const char *eol_str;

      SVN_ERR (svn_wc__get_eol_style (&eol_style, &eol_str,
                                      filepath->data, pool));
      SVN_ERR (svn_wc__get_keywords (&keywords,
                                     filepath->data, NULL, pool));

      /* In the commit, newlines and keywords may have been
       * canonicalized and/or contracted... Or they may not have
       * been.  It's kind of hard to know.  Here's how we find out:
       *
       *    1. Make a translated tmp copy of the committed text base.
       *    2. Compare it to the working file.
       *    3. If different, rename the tmp file over working file;
       *       else just remove the tmp file.
       *
       * This means we only rewrite the working file if we absolutely
       * have to, which is good because it avoids changing the file's
       * timestamp unless necessary, so editors aren't tempted to
       * reread the file if they don't really need to.
       */
      svn_path_split (filepath, &pdir, &bname, pool);
      tmp_wfile = svn_wc__adm_path (pdir, TRUE, pool, bname->data, NULL);
      
      SVN_ERR (svn_io_open_unique_file (&ignored,
                                        &tmp_wfile,
                                        tmp_wfile->data,
                                        SVN_WC__TMP_EXT,
                                        FALSE,
                                        pool));

      apr_err = apr_file_close (ignored);
      if (! (APR_STATUS_IS_SUCCESS (apr_err)))
        return svn_error_createf
          (apr_err, 0, NULL, pool,
           "replace_text_base: error closing %s", tmp_wfile->data);

      SVN_ERR (svn_wc_copy_and_translate (tmp_text_base->data,
                                          tmp_wfile->data,
                                          eol_str,
                                          FALSE, /* don't repair eol */
                                          keywords,
                                          TRUE, /* expand keywords */
                                          pool));

      SVN_ERR (svn_wc__files_contents_same_p
               (&same, tmp_wfile, filepath, pool));

      if (! same)
        SVN_ERR (svn_io_copy_file (tmp_wfile->data, filepath->data, FALSE,
                                   pool));

      SVN_ERR (svn_io_remove_file (tmp_wfile->data, pool));

      /* Move committed tmp/text-base to real text-base. */
      SVN_ERR (svn_wc__sync_text_base (filepath, pool));
    }

  return SVN_NO_ERROR;
}


static void
signal_error (struct log_runner *loggy, svn_error_t *err)
{
  svn_xml_signal_bailout (svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                             0,
                                             err,
                                             loggy->pool,
                                             "in directory %s",
                                             loggy->path->data),
                          loggy->parser);
}





/*** Dispatch on the xml opening tag. ***/

static svn_error_t *
log_do_run_cmd (struct log_runner *loggy,
                const char *name,
                const XML_Char **atts)
{
  svn_error_t *err;
  apr_status_t apr_err;
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
      svn_stringbuf_t *infile_path
        = svn_stringbuf_dup (loggy->path, loggy->pool);
      svn_path_add_component_nts (infile_path, infile_name);
      
      apr_err = apr_file_open (&infile, infile_path->data, APR_READ,
                          APR_OS_DEFAULT, loggy->pool);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, loggy->pool,
                                  "error opening %s", infile_path->data);
    }
  
  if (outfile_name)
    {
      svn_stringbuf_t *outfile_path
        = svn_stringbuf_dup (loggy->path, loggy->pool);
      svn_path_add_component_nts (outfile_path, outfile_name);
      
      /* kff todo: always creates and overwrites, currently.
         Could append if file exists... ?  Consider. */
      apr_err = apr_file_open (&outfile, outfile_path->data, 
                          (APR_WRITE | APR_CREATE),
                          APR_OS_DEFAULT, loggy->pool);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, loggy->pool,
                                  "error opening %s", outfile_path->data);
    }
  
  if (errfile_name)
    {
      svn_stringbuf_t *errfile_path
        = svn_stringbuf_dup (loggy->path, loggy->pool);
      svn_path_add_component_nts (errfile_path, errfile_name);
      
      /* kff todo: always creates and overwrites, currently.
         Could append if file exists... ?  Consider. */
      apr_err = apr_file_open (&errfile, errfile_path->data, 
                          (APR_WRITE | APR_CREATE),
                          APR_OS_DEFAULT, loggy->pool);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, loggy->pool,
                                  "error opening %s", errfile_path->data);
    }
  
  err = svn_io_run_cmd (loggy->path->data, name, args, NULL, NULL, FALSE,
                        infile, outfile, errfile, loggy->pool);
  if (err)
     return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
                               "error running %s in %s",
                               name, loggy->path->data);

  /* TODO: Handle status here, or pass it back to caller. */

  return SVN_NO_ERROR;
}


static svn_error_t *
log_do_file_xfer (struct log_runner *loggy,
                  const char *name,
                  enum svn_wc__xfer_action action,
                  const XML_Char **atts)
{
  svn_error_t *err;
  enum svn_wc__eol_style style;
  const char *dest = NULL, *eol_str_val = NULL, *eol_str = NULL;
  const char *revision = NULL, *date = NULL, *author = NULL, *url = NULL;
  const char *repair = NULL, *expand = NULL;
  svn_wc_keywords_t *keywords = NULL;

  /* We have the name (src), and the destination is absolutely required. */
  dest = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_DEST, atts);
  if (! dest)
    return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
                              "missing dest attr in %s", loggy->path->data);

  /* Optional: try to get any other translation-related attributes. */
  revision = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_REVISION, atts);
  date = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_DATE, atts);
  author = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_AUTHOR, atts);
  url = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_URL, atts);
  repair = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_REPAIR, atts);
  expand = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_EXPAND, atts);

  /* Look for an EOL attribute.  Note that we can't pass an actual LF
     or CRLF in the xml, because expat will convert them into a space!
     Thus our xml contains the same codes used in the values of
     svn:eol-style, and thus we use the same parser to get back a real
     eol. */
  eol_str_val = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_EOL_STR, atts);
  if (eol_str_val)
    svn_wc__eol_style_from_value (&style, /* ignored */
                                  &eol_str, eol_str_val); 
  else
    eol_str = NULL;

  /* Conditionally build a keywords structure. */
  if (revision || date || author || url)
    {
      keywords = apr_palloc (loggy->pool, sizeof (*keywords));
      keywords->revision = revision ? 
        svn_string_create (revision, loggy->pool) :
        NULL;
      keywords->date = date ? 
        svn_wc__friendly_date (date, loggy->pool) :
        NULL;
      keywords->author = author ?
        svn_string_create (author, loggy->pool) :
        NULL;
      keywords->url = url ? 
        svn_string_create (url, loggy->pool) :
        NULL;
    }
  err = file_xfer_under_path (loggy->path, name, dest, action, eol_str, 
                              repair ? TRUE : FALSE, 
                              keywords, 
                              expand ? TRUE : FALSE,
                              loggy->pool);
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
  svn_stringbuf_t *full_path;

  full_path = svn_stringbuf_dup (loggy->path, loggy->pool);
  svn_path_add_component_nts (full_path, name);

  SVN_ERR (svn_io_set_file_read_only (full_path->data, FALSE, loggy->pool));

  return SVN_NO_ERROR;
}

/* Remove file NAME in log's CWD. */
static svn_error_t *
log_do_rm (struct log_runner *loggy, const char *name)
{
  apr_status_t apr_err;
  svn_stringbuf_t *full_path;

  full_path = svn_stringbuf_dup (loggy->path, loggy->pool);
  svn_path_add_component_nts (full_path, name);

  apr_err = apr_file_remove (full_path->data, loggy->pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, loggy->pool,
                              "apr_file_remove couldn't remove %s", name);

  return SVN_NO_ERROR;
}


/* Remove file NAME in log's CWD iff it's zero bytes in size. */
static svn_error_t *
log_do_detect_conflict (struct log_runner *loggy,
                        const char *name,
                        const XML_Char **atts)                        
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_finfo_t finfo;
  svn_stringbuf_t *full_path;

  const char *rejfile =
    svn_xml_get_attr_value (SVN_WC_ENTRY_ATTR_REJFILE, atts);

  if (! rejfile)
    return 
      svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
                         "log_do_detect_conflict: no text-rejfile attr in %s",
                         loggy->path->data);


  full_path = svn_stringbuf_dup (loggy->path, loggy->pool);
  svn_path_add_component_nts (full_path, rejfile);

  apr_err = apr_stat (&finfo, full_path->data, APR_FINFO_MIN, loggy->pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, loggy->pool,
                              "log_do_detect_conflict: couldn't stat %s",
                              full_path->data);

  if (finfo.size == 0)
    {
      /* the `patch' program created an empty .rej file.  clean it
         up. */
      apr_err = apr_file_remove (full_path->data, loggy->pool);
      if (apr_err)
        return svn_error_createf (apr_err, 0, NULL, loggy->pool,
                                  "log_do_detect_conflict: couldn't rm %s", 
                                  name);
    }

  else 
    {
      /* size > 0, there must be an actual text conflict.   Mark the
         entry as conflicted! */
      apr_hash_t *atthash = svn_xml_make_att_hash (atts, loggy->pool);

      err = svn_wc__entry_modify
        (loggy->path,
         svn_stringbuf_create (name, loggy->pool),
         (SVN_WC__ENTRY_MODIFY_CONFLICTED | SVN_WC__ENTRY_MODIFY_ATTRIBUTES),
         SVN_INVALID_REVNUM,
         svn_node_none,
         svn_wc_schedule_normal,
         TRUE, FALSE,
         0,
         0,
         NULL,
         atthash, /* contains SVN_WC_ATTR_REJFILE */
         loggy->pool,
         NULL);
    }

  return SVN_NO_ERROR;
}




static svn_error_t *
log_do_modify_entry (struct log_runner *loggy,
                     const char *name,
                     const XML_Char **atts)
{
  svn_error_t *err;
  apr_hash_t *ah = svn_xml_make_att_hash (atts, loggy->pool);
  svn_stringbuf_t *sname = svn_stringbuf_create (name, loggy->pool);
  svn_stringbuf_t *tfile = svn_stringbuf_dup (loggy->path, loggy->pool);
  svn_wc_entry_t *entry;
  apr_uint16_t modify_flags;
  svn_stringbuf_t *valuestr;

  /* Convert the attributes into an entry structure. */
  SVN_ERR (svn_wc__atts_to_entry (&entry, &modify_flags, ah, loggy->pool));

  /* Did the log command give us any timestamps?  There are three
     possible scenarios here.  We must check both text_time
     and prop_time for each of the three scenarios.  */

  /* TEXT_TIME: */
  valuestr = apr_hash_get (ah, SVN_WC_ENTRY_ATTR_TEXT_TIME, 
                           APR_HASH_KEY_STRING);

  if ((modify_flags & SVN_WC__ENTRY_MODIFY_TEXT_TIME)
      && (! strcmp (valuestr->data, SVN_WC_TIMESTAMP_WC)))
    {
      enum svn_node_kind tfile_kind;
      apr_time_t text_time;

      if (strcmp (sname->data, SVN_WC_ENTRY_THIS_DIR))
        svn_path_add_component (tfile, sname);
      
      err = svn_io_check_path (tfile, &tfile_kind, loggy->pool);
      if (err)
        return svn_error_createf
          (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
           "error checking path %s", tfile->data);
          
      err = svn_io_file_affected_time (&text_time, tfile, loggy->pool);
      if (err)
        return svn_error_createf
          (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
           "error getting file affected time on %s", tfile->data);

      entry->text_time = text_time;
    }

  /* PROP_TIME: */
  valuestr = apr_hash_get (ah, SVN_WC_ENTRY_ATTR_PROP_TIME, 
                           APR_HASH_KEY_STRING);

  if ((modify_flags & SVN_WC__ENTRY_MODIFY_PROP_TIME)
      && (! strcmp (valuestr->data, SVN_WC_TIMESTAMP_WC)))
    {
      svn_stringbuf_t *pfile;
      enum svn_node_kind pfile_kind;
      apr_time_t prop_time;

      err = svn_wc__prop_path (&pfile, tfile, 0, loggy->pool);
      if (err)
        signal_error (loggy, err);
      
      err = svn_io_check_path (pfile, &pfile_kind, loggy->pool);
      if (err)
        return svn_error_createf
          (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
           "error checking path %s", pfile->data);
      
      err = svn_io_file_affected_time (&prop_time, tfile, loggy->pool);
      if (err)
        return svn_error_createf
          (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
           "error getting file affected time on %s", pfile->data);

      entry->prop_time = prop_time;
    }

  /* Now write the new entry out */
  err = svn_wc__entry_modify (loggy->path,
                              sname,
                              modify_flags,
                              entry->revision,
                              entry->kind,
                              entry->schedule,
                              entry->conflicted,
                              entry->copied,
                              entry->text_time,
                              entry->prop_time,
                              entry->url,
                              entry->attributes,
                              loggy->pool,
                              NULL);

  if (err)
    return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
                              "error merge_syncing entry %s", name);

  return SVN_NO_ERROR;
}


/* Ben sez:  this log command is (at the moment) only executed by the
   update editor.  It attempts to forcefully remove working data. */
static svn_error_t *
log_do_delete_entry (struct log_runner *loggy, const char *name)
{
  svn_wc_entry_t *entry;
  svn_error_t *err = NULL;
  svn_stringbuf_t *sname = svn_stringbuf_create (name, loggy->pool);
  svn_stringbuf_t *full_path = svn_stringbuf_dup (loggy->path, loggy->pool);
  svn_stringbuf_t *this_dir = svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR,
                                              loggy->pool);

  /* Figure out if 'name' is a dir or a file */
  svn_path_add_component (full_path, sname);
  svn_wc_entry (&entry, full_path, loggy->pool);

  if (! entry)
    /* Hmm....this entry is already absent from the revision control
       system.  Chances are good that this item was removed via a
       commit from this working copy.  */
    return SVN_NO_ERROR;

  /* Remove the object from revision control -- whether it's a
     single file or recursive directory removal.  Attempt
     attempt to destroy all working files & dirs too. */
  if (entry->kind == svn_node_dir)
    err = svn_wc_remove_from_revision_control (full_path, this_dir,
                                               TRUE, loggy->pool);
  else if (entry->kind == svn_node_file)
    err = svn_wc_remove_from_revision_control (loggy->path, sname,
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
  const char *revstr
    = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_REVISION, atts);

  if (! revstr)
    return svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
                              "missing revision attr for %s", name);
  else
    {
      svn_stringbuf_t *working_file = NULL;
      svn_stringbuf_t *tmp_base;
      apr_time_t text_time = 0; /* By default, don't override old stamp. */
      apr_time_t prop_time = 0; /* By default, don't override old stamp. */
      enum svn_node_kind kind;
      svn_wc_entry_t *entry;
      svn_stringbuf_t *prop_path, *tmp_prop_path, *prop_base_path;
      svn_stringbuf_t *sname = svn_stringbuf_create (name, loggy->pool);
      svn_boolean_t is_this_dir;

      /* `name' is either a file's basename, or SVN_WC_ENTRY_THIS_DIR. */
      is_this_dir = (strcmp (name, SVN_WC_ENTRY_THIS_DIR)) ? FALSE : TRUE;
      
      /* Determine the actual full path of the affected item so we can
         easily read its entry and check its state. */
      {
        svn_stringbuf_t *full_path;

        full_path = svn_stringbuf_dup (loggy->path, loggy->pool);
        if (! is_this_dir)
          svn_path_add_component (full_path, sname);
        SVN_ERR (svn_wc_entry (&entry, full_path, loggy->pool));
        if ((! is_this_dir) 
            && (entry->kind != svn_node_file))
          return svn_error_createf 
            (SVN_ERR_WC_BAD_ADM_LOG, 0, NULL, loggy->pool,
             "log command for directory '%s' mislocated", name);
      }

      /* What to do if the committed entry was scheduled for deletion: */
      if (entry && (entry->schedule == svn_wc_schedule_delete))
        {
          if (is_this_dir)
            /* Drop a 'killme' file into my own adminstrative dir;
               this signals the svn_wc__run_log() to blow away .svn/
               after its done with this logfile.  */
            SVN_ERR (svn_wc__make_adm_thing (loggy->path,
                                             SVN_WC__ADM_KILLME,
                                             svn_node_file, 
                                             APR_OS_DEFAULT,
                                             0,
                                             loggy->pool));
          else
            /* We can safely remove files from revision control
               without screwing something else up. */
            SVN_ERR (svn_wc_remove_from_revision_control
                     (loggy->path, sname, FALSE, loggy->pool));
        }
               
      else   /* entry not deleted, so mark commited-to-date */
        {
          svn_stringbuf_t *pdir, *basename;
          apr_hash_t *entries;

          if ((entry && (entry->schedule == svn_wc_schedule_replace))
              && is_this_dir)
            {
              apr_hash_index_t *hi;
              
              /* If THIS_DIR has been replaced, all its immmediate
                 children *must* be either marked as {D, A, or R}.
                 Children which are A or R will be reported as individual
                 commit-targets, and thus will be re-visited by
                 log_do_committed().  Children which are marked as D,
                 however, need to be outright removed from revision
                 control.  */
              
              /* Loop over all children entries, look for D markers. */
              SVN_ERR (svn_wc_entries_read (&entries, loggy->path,
                                            loggy->pool));
              
              for (hi = apr_hash_first (loggy->pool, entries); 
                   hi; 
                   hi = apr_hash_next (hi))
                {
                  const void *key;
                  const char *keystring;
                  apr_ssize_t klen;
                  void *val;
                  svn_stringbuf_t *current_entry_name;
                  svn_wc_entry_t *current_entry; 
                  
                  /* Get the next entry */
                  apr_hash_this (hi, &key, &klen, &val);
                  keystring = (const char *) key;
                  current_entry = (svn_wc_entry_t *) val;
                  
                  /* Skip each entry that isn't scheduled for deletion. */
                  if (current_entry->schedule != svn_wc_schedule_delete)
                    continue;
                  
                  /* Get the name of entry, remove from revision control. */
                  current_entry_name = svn_stringbuf_create (keystring,
                                                             loggy->pool);
                  
                  if (current_entry->kind == svn_node_file)
                    SVN_ERR (svn_wc_remove_from_revision_control 
                             (loggy->path,
                              current_entry_name,
                              FALSE, loggy->pool));
                  
                  else if (current_entry->kind == svn_node_dir)
                    {
                      svn_stringbuf_t *parent = svn_stringbuf_dup
                        (loggy->path, loggy->pool);
                      svn_stringbuf_t *thisdir =
                        svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR, 
                                              loggy->pool);
                      svn_path_add_component (parent, current_entry_name);
                      SVN_ERR (svn_wc_remove_from_revision_control
                               (parent, thisdir, FALSE, loggy->pool));
                    }
                }
            }

          if (! is_this_dir)
            {
              /* If we get here, `name' is a file's basename.
                 `basename' is an svn_stringbuf_t version of it.  Check
                 for textual changes. */
              working_file = svn_stringbuf_dup (loggy->path, loggy->pool);
              svn_path_add_component (working_file,
                                      sname);
              tmp_base = svn_wc__text_base_path (working_file, 1, loggy->pool);
              
              err = svn_io_check_path (tmp_base, &kind, loggy->pool);
              if (err)
                return svn_error_createf
                  (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
                   "error checking existence of %s", name);
              
              if (kind == svn_node_file)
                {
                  svn_boolean_t same;

                  err = svn_wc__versioned_file_modcheck (&same,
                                                         working_file,
                                                         tmp_base,
                                                         loggy->pool);
                  if (err)
                    return svn_error_createf 
                      (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
                       "error comparing %s and %s",
                       working_file->data, tmp_base->data);
                  
                  /* What's going on here: the working copy has been
                     copied to tmp/text-base/ during the commit.  That's
                     what `tmp_base' points to.  If we get here, we know
                     the commit was successful, and we need make tmp_base
                     into the real text-base.  *However*, which timestamp
                     do we put on the entry?  It's possible that during
                     the commit the working file may have changed.  If
                     that's the case, use tmp_base's timestamp.  If
                     there's been no local mod, it's okay to use the
                     working file's timestamp. */
                  err = svn_io_file_affected_time 
                    (&text_time, same ? working_file : tmp_base, loggy->pool);
                  if (err)
                    return svn_error_createf 
                      (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
                       "error getting file_affected_time on %s",
                       same ? working_file->data : tmp_base->data);
                  
                  err = replace_text_base (loggy->path, name, loggy->pool);
                  if (err)
                    return svn_error_createf 
                      (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
                       "error replacing text base for %s", name);
                }
            }
              
          /* Now check for property commits. */

          /* Get property file pathnames, depending on whether we're
             examining a file or THIS_DIR */
          SVN_ERR (svn_wc__prop_path 
                   (&prop_path,
                    is_this_dir ? loggy->path : working_file,
                    0 /* not tmp */, loggy->pool));
          
          SVN_ERR (svn_wc__prop_path 
                   (&tmp_prop_path, 
                    is_this_dir ? loggy->path : working_file,
                    1 /* tmp */, loggy->pool));
          
          SVN_ERR (svn_wc__prop_base_path 
                   (&prop_base_path,
                    is_this_dir ? 
                    loggy->path : working_file,
                    0 /* not tmp */, loggy->pool));

          /* Check for existence of tmp_prop_path */
          err = svn_io_check_path (tmp_prop_path, &kind, loggy->pool);
          if (err)
            return svn_error_createf
              (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
               "error checking existence of %s", name);
          
          if (kind == svn_node_file)
            {
              /* Magic inference: if there's a working property file
                 sitting in the tmp area, then we must have committed
                 properties on this file or dir.  Time to sync. */
              
              /* We need to decide which prop-timestamp to use, just
                 like we did with text-time. */             
              svn_boolean_t same;
              apr_status_t status;
              err = svn_wc__files_contents_same_p (&same,
                                                   prop_path,
                                                   tmp_prop_path,
                                                   loggy->pool);
              if (err)
                return svn_error_createf 
                  (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
                   "error comparing %s and %s",
                   prop_path->data, tmp_prop_path->data);

              err = svn_io_file_affected_time 
                (&prop_time, same ? prop_path : tmp_prop_path, loggy->pool);
              if (err)
                return svn_error_createf 
                  (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
                   "error getting file_affected_time on %s",
                   same ? prop_path->data : tmp_prop_path->data);

              /* Remove read-only flag on terminated file. */
              SVN_ERR (svn_io_set_file_read_write (prop_base_path->data,
                                                   TRUE,
                                                   loggy->pool));

              /* Make the tmp prop file the new pristine one. */
              status = apr_file_rename (tmp_prop_path->data,
                                        prop_base_path->data,
                                        loggy->pool);
              if (status)
                return svn_error_createf (status, 0, NULL, loggy->pool,
                                          "error renaming %s to %s",
                                          tmp_prop_path->data,
                                          prop_base_path->data);

              SVN_ERR (svn_io_set_file_read_only (prop_base_path->data,
                                                  FALSE,
                                                  loggy->pool));
            }
          

          /* Files have been moved, and timestamps are found.  Time
             for The Big Merge Sync. */
          err = svn_wc__entry_modify
            (loggy->path,
             sname,
             (SVN_WC__ENTRY_MODIFY_REVISION 
              | SVN_WC__ENTRY_MODIFY_SCHEDULE 
              | SVN_WC__ENTRY_MODIFY_CONFLICTED
              | SVN_WC__ENTRY_MODIFY_COPIED
              | SVN_WC__ENTRY_MODIFY_TEXT_TIME
              | SVN_WC__ENTRY_MODIFY_PROP_TIME
              | SVN_WC__ENTRY_MODIFY_FORCE),
             SVN_STR_TO_REV (revstr),
             svn_node_none,
             svn_wc_schedule_normal,
             FALSE, /* conflicted */
             FALSE, /* copied */
             text_time,
             prop_time,
             NULL, NULL,
             loggy->pool,
             /* Remove all attributes below this comment... */
             SVN_WC_ENTRY_ATTR_REJFILE,
             SVN_WC_ENTRY_ATTR_PREJFILE,
             SVN_WC_ENTRY_ATTR_COPYFROM_URL,
             SVN_WC_ENTRY_ATTR_COPYFROM_REV,
             NULL);
          if (err)
            return svn_error_createf
              (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
               "error merge_syncing %s", name);

          /* Also, if this is a directory, don't forget to reset the
             state in the parent's entry for this directory. */
          if (! is_this_dir)
            return SVN_NO_ERROR;

          svn_path_split (loggy->path, &pdir, &basename, loggy->pool);
          if (svn_path_is_empty (pdir))
            return SVN_NO_ERROR;

          /* Make sure our entry exists in the parent (if the parent
             is even a SVN working copy directory). 

             ### todo:  perhaps some year when our API calls return
             predictable error messages, we can pay attention to the
             difference between "The parent directory is not a SVN
             working copy" and "My God, there's radioactive waste
             dripping on your forehead!" */
          err = svn_wc_entries_read (&entries, pdir, loggy->pool);
          if (! err)
            {
              if (apr_hash_get (entries, basename->data, APR_HASH_KEY_STRING))
                {
                  err = svn_wc__entry_modify
                    (pdir,
                     basename,
                     (SVN_WC__ENTRY_MODIFY_SCHEDULE 
                      | SVN_WC__ENTRY_MODIFY_COPIED
                      | SVN_WC__ENTRY_MODIFY_FORCE),
                     SVN_INVALID_REVNUM,
                     svn_node_dir,
                     svn_wc_schedule_normal,
                     FALSE, /* conflicted */
                     FALSE, /* copied */
                     0,
                     0,
                     NULL, NULL,
                     loggy->pool,
                     SVN_WC_ENTRY_ATTR_COPYFROM_URL,
                     SVN_WC_ENTRY_ATTR_COPYFROM_REV,
                     NULL);
                  if (err)
                    return svn_error_createf
                      (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
                       "error merge_syncing %s", name);
                }
            }
        }
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
          eltname, loggy->path->data));
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
  else if (strcmp (eltname, SVN_WC__LOG_DETECT_CONFLICT) == 0) {
    err = log_do_detect_conflict (loggy, name, atts);
  }
  else if (strcmp (eltname, SVN_WC__LOG_MV) == 0) {
    err = log_do_file_xfer (loggy, name, svn_wc__xfer_mv, atts);
  }
  else if (strcmp (eltname, SVN_WC__LOG_CP) == 0) {
    err = log_do_file_xfer (loggy, name, svn_wc__xfer_cp, atts);
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
                                   loggy->path->data, eltname));
      return;
    }

  if (err)
    signal_error
      (loggy, svn_error_createf
       (SVN_ERR_WC_BAD_ADM_LOG, 0, err, loggy->pool,
        "start_handler: error processing element %s in %s",
        eltname, loggy->path->data));
  
  return;
}



/*** Using the parser to run the log file. ***/

svn_error_t *
svn_wc__run_log (svn_stringbuf_t *path, apr_pool_t *pool)
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
  loggy->path   = path;
  loggy->pool   = pool;
  loggy->parser = parser;
  
  /* Expat wants everything wrapped in a top-level form, so start with
     a ghost open tag. */
  SVN_ERR (svn_xml_parse (parser, log_start, strlen (log_start), 0));

  /* Parse the log file's contents. */
  err = svn_wc__open_adm_file (&f, path, SVN_WC__ADM_LOG, APR_READ, pool);
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
                                  path->data);
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

  /* Remove the logfile;  its commands have been executed. */
  SVN_ERR (svn_wc__remove_adm_file (path, pool, SVN_WC__ADM_LOG, NULL));

  /* Check for a 'killme' file in the administrative area. */
  if (svn_wc__adm_path_exists (path, 0, pool, SVN_WC__ADM_KILLME, NULL))
    {
      /* Blow away the entire administrative dir, and all those below
         it too.  Don't remove any working files, though. */
      svn_stringbuf_t *this_dir = 
        svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR, pool);
      SVN_ERR (svn_wc_remove_from_revision_control (path, this_dir,
                                                    FALSE, pool));
    }
  
  return SVN_NO_ERROR;
}



/*** Recursively do log things. ***/

svn_error_t *
svn_wc_cleanup (svn_stringbuf_t *path,
                apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_t *entries = NULL;
  apr_hash_index_t *hi;
  svn_stringbuf_t *log_path = svn_wc__adm_path (path, 0, pool,
                                                SVN_WC__ADM_LOG, NULL);
  svn_boolean_t locked;
  enum svn_node_kind kind;

  /* Recurse on versioned subdirs first, oddly enough. */
  SVN_ERR (svn_wc_entries_read (&entries, path, pool));

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t keylen;
      void *val;
      svn_wc_entry_t *entry;
      svn_boolean_t is_this_dir = FALSE;

      apr_hash_this (hi, &key, &keylen, &val);
      entry = val;

      if ((keylen == strlen (SVN_WC_ENTRY_THIS_DIR))
          && (strcmp ((char *) key, SVN_WC_ENTRY_THIS_DIR) == 0))
        is_this_dir = TRUE;

      if ((entry->kind == svn_node_dir) && (! is_this_dir))
        {
          /* Recurse */
          svn_stringbuf_t *subdir = svn_stringbuf_dup (path, pool);
          svn_path_add_component (subdir,
                                  svn_stringbuf_create ((char *) key, pool));

          SVN_ERR (svn_wc_cleanup (subdir, pool));
        }
    }

  /* Lock this working copy directory if it isn't already. */
  SVN_ERR (svn_wc__locked (&locked, path, pool));
  if (! locked)
    SVN_ERR (svn_wc__lock (path, 0, pool));

  /* Is there a log?  If so, run it. */
  err = svn_io_check_path (log_path, &kind, pool);
  if (err)
    {
      if (! APR_STATUS_IS_ENOENT(err->apr_err))
        return err;
      else
        svn_error_clear_all (err);
    }
  else if (kind == svn_node_file)
    SVN_ERR (svn_wc__run_log (path, pool));

  /* Cleanup the tmp area of the admin subdir.  The logs have been
     run, so anything left here has no hope of being useful. */
  SVN_ERR (svn_wc__adm_cleanup_tmp_area (path, pool));

  /* Is there a "killme" file?  Remove this directory from revision
     control.  If so, blow away the entire administrative dir, and all
     those below it too.  Don't remove any working files, though. */
  if (svn_wc__adm_path_exists (path, 0, pool, SVN_WC__ADM_KILLME, NULL))
    SVN_ERR (svn_wc_remove_from_revision_control 
             (path, 
              svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR, pool), 
              FALSE, pool));
  
  /* Remove the lock here, making sure that the administrative
     directory still exists after running the log! */
  if (svn_wc__adm_path_exists (path, 0, pool, NULL))
    {
      err = svn_wc__unlock (path, pool);
      if (err && APR_STATUS_IS_ENOENT(err->apr_err))
        svn_error_clear_all (err);
      else if (err)
        return err;
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */

