/*
 * log.c:  handle the adm area's log file.
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */



#include <apr_pools.h>
#include <apr_strings.h>
#include "svn_wc.h"
#include "svn_error.h"
#include "svn_xml.h"
#include "wc.h"



/*** Userdata for the callbacks. ***/
struct log_runner
{
  apr_pool_t *pool;
  svn_xml_parser_t *parser;
  svn_string_t *path;  /* the dir in which this is all happening */
};



/*** The XML handlers. ***/

static svn_error_t *
merge_text (svn_string_t *path,
            const char *name,
            const char *saved_mods,
            apr_pool_t *pool)
{
  svn_string_t *filepath;
  svn_error_t *err;
  void *diff;

  filepath = svn_string_dup (path, pool);
  svn_path_add_component_nts (filepath, name, svn_path_local_style);

  /* Get the local edits. */
  err = svn_wc__get_local_changes (svn_wc__gnudiff_differ,
                                   &diff,
                                   filepath,
                                   pool);
  if (err)
    return err;

  /* Merge local edits into the updated version. */
  err = svn_wc__merge_local_changes (svn_wc__gnudiff_patcher,
                                     diff,
                                     filepath,
                                     pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


/* Move a file NAME to DEST, assuming that PATH is the common parent
   of both locations.  */
static svn_error_t *
move_file (svn_string_t *path,
           const char *name,
           const char *dest,
           apr_pool_t *pool)
{
  apr_status_t status;
  svn_string_t *full_from_path, *full_dest_path;

  full_from_path = svn_string_dup (path, pool);
  full_dest_path = svn_string_dup (path, pool);

  svn_path_add_component_nts (full_from_path, name, svn_path_local_style);
  svn_path_add_component_nts (full_dest_path, dest, svn_path_local_style);

  status = apr_rename_file (full_from_path->data, full_dest_path->data, pool);
  if (status)
    return svn_error_createf (status, 0, NULL, pool,
                              "move_file:  can't move %s to %s",
                              name, dest);

  return SVN_NO_ERROR;
}



static svn_error_t *
replace_text_base (svn_string_t *path,
                   const char *name,
                   apr_pool_t *pool)
{
  svn_string_t *filepath;
  svn_string_t *tmp_text_base;
  svn_error_t *err;
  enum svn_node_kind kind;

  filepath = svn_string_dup (path, pool);
  svn_path_add_component_nts (filepath, name, svn_path_local_style);

  tmp_text_base = svn_wc__text_base_path (filepath, 1, pool);
  err = svn_io_check_path (tmp_text_base, &kind, pool);
  if (err)
    return err;

  if (kind == svn_invalid_kind)
    return SVN_NO_ERROR;  /* tolerate mop-up calls gracefully */
  else
    return svn_wc__sync_text_base (filepath, pool);
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


static svn_error_t *
remove_from_version_control (struct log_runner *loggy, svn_string_t *name)
{
  svn_error_t *err;
  apr_hash_t *entries = NULL;
  
  /* Remove this entry from the entries file. */
  err = svn_wc__entries_read (&entries, loggy->path, loggy->pool);
  if (err)
    return err;
  svn_wc__entry_remove (entries, name);
  err = svn_wc__entries_write (entries, loggy->path, loggy->pool);
  if (err)
    return err;
  
  /* Remove its text-base copy, if any, and conditionally remove
     working file too. */
  {
    svn_string_t *file_full_path;
    svn_string_t *text_base_path;
    enum svn_node_kind kind;
    
    file_full_path = svn_string_dup (loggy->path, loggy->pool);
    svn_path_add_component (file_full_path, name, svn_path_local_style);
    text_base_path
      = svn_wc__text_base_path (file_full_path, 0, loggy->pool);
    err = svn_io_check_path (text_base_path, &kind, loggy->pool);
    if (err && (err->apr_err == APR_ENOENT))
      return SVN_NO_ERROR;
    else if (err)
      return err;
    
    /* Else we have a text-base copy, so use it. */

    if (kind == svn_file_kind)
      {
        apr_status_t apr_err;
        svn_boolean_t same;
        
        {
          /* Aha!  There is a text-base file still around.  Use it
             to check if the working file is modified; if wf is not
             modified, we should remove it too. */
          err = svn_wc__files_contents_same_p (&same,
                                               file_full_path,
                                               text_base_path,
                                               loggy->pool);
          if (err && (err->apr_err != APR_ENOENT))
            return err;
          else if (! err)
            {
              apr_err = apr_remove_file (file_full_path->data,
                                         loggy->pool);
              if (apr_err)
                return svn_error_createf
                  (apr_err, 0, NULL,
                   loggy->pool,
                   "log.c:start_handler() (SVN_WC__LOG_DELETE_ENTRY): "
                   "error removing file %s",
                   file_full_path->data);
            }
        }
        
        apr_err = apr_remove_file (text_base_path->data, loggy->pool);
        if (apr_err)
          return svn_error_createf
            (apr_err, 0, NULL,
             loggy->pool,
             "log.c:start_handler() (SVN_WC__LOG_DELETE_ENTRY): "
             "error removing file %s",
             text_base_path->data);
      }
  }

  return SVN_NO_ERROR;
}


static void
start_handler (void *userData, const XML_Char *eltname, const XML_Char **atts)
{
  struct log_runner *loggy = userData;
  svn_error_t *err = NULL;

  /* Most elements have a name attribute, so try to grab one now. */
  const char *name = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_NAME, atts);

  if (strcmp (eltname, SVN_WC__LOG_MERGE_TEXT) == 0)
    {
      const char *saved_mods = svn_xml_get_attr_value ("saved-mods", atts);

      if (! name)
        return signal_error
          (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                     0,
                                     NULL,
                                     loggy->pool,
                                     "missing name attr in %s",
                                     loggy->path->data));
      else
        /* Note that saved_mods is allowed to be null. */
        err = merge_text (loggy->path, name, saved_mods, loggy->pool);
    }
  else if (strcmp (eltname, SVN_WC__LOG_REPLACE_TEXT_BASE) == 0)
    {
      if (! name)
        return signal_error
          (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                     0,
                                     NULL,
                                     loggy->pool,
                                     "missing name attr in %s",
                                     loggy->path->data));
      else
        err = replace_text_base (loggy->path, name, loggy->pool);
    }
  else if (strcmp (eltname, SVN_WC__LOG_MV) == 0)
    {
      /* Grab a "dest" attribute as well. */
      const char *dest = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_DEST, atts);

      if (! name)
        return signal_error
          (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                     0,
                                     NULL,
                                     loggy->pool,
                                     "missing name attr in %s",
                                     loggy->path->data));
      else if (! dest)
        return signal_error
          (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                     0,
                                     NULL,
                                     loggy->pool,
                                     "missing dest attr in %s",
                                     loggy->path->data));
      else
        err = move_file (loggy->path, name, dest, loggy->pool);
    }
  else if (strcmp (eltname, SVN_WC__LOG_DELETE_ENTRY) == 0)
    {
      svn_string_t *sname; 

      if (! name)
        return signal_error
          (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                     0,
                                     NULL,
                                     loggy->pool,
                                     "missing name attr in %s",
                                     loggy->path->data));

      sname = svn_string_create (name, loggy->pool);

      err = remove_from_version_control (loggy, sname);
      if (err)
        return signal_error (loggy, err);
    }
  else if (strcmp (eltname, SVN_WC__LOG_SET_VERSION) == 0)
    {
      const char *verstr 
        = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_VERSION, atts);
      
      if (! name)
        return signal_error
          (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                     0,
                                     NULL,
                                     loggy->pool,
                                     "missing name attr in %s",
                                     loggy->path->data));
      else if (! verstr)
        return signal_error
          (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                     0,
                                     NULL,
                                     loggy->pool,
                                     "missing version attr for %s",
                                     name));
      else
        {
          apr_time_t timestamp = 0;
          svn_string_t *sname = svn_string_create (name, loggy->pool);
          svn_string_t *working_file
            = svn_string_dup (loggy->path, loggy->pool);
          svn_path_add_component (working_file,
                                  sname,
                                  svn_path_local_style);

          err = svn_wc__file_affected_time (&timestamp,
                                            working_file,
                                            loggy->pool);
          if (err)
            return signal_error (loggy, svn_error_createf
                                 (SVN_ERR_WC_BAD_ADM_LOG,
                                  0,
                                  NULL,
                                  loggy->pool,
                                  "error discovering file affected time on %s",
                                  name));
          
          err = svn_wc__entry_merge_sync (loggy->path,
                                          sname,
                                          atoi (verstr),
                                          svn_file_kind,
                                          0,
                                          timestamp,
                                          loggy->pool,
                                          NULL);
          if (err)
            return signal_error (loggy, svn_error_createf 
                                 (SVN_ERR_WC_BAD_ADM_LOG,
                                  0,
                                  NULL,
                                  loggy->pool,
                                  "error merge_syncing entry %s",
                                  name));
        }
    }
  else if (strcmp (eltname, SVN_WC__LOG_COMMITTED) == 0)
    {
      const char *verstr
        = svn_xml_get_attr_value (SVN_WC__LOG_ATTR_VERSION, atts);

      if (! name)
        return signal_error
          (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                     0,
                                     NULL,
                                     loggy->pool,
                                     "missing name attr in %s",
                                     loggy->path->data));
      else if (! verstr)
        return signal_error
          (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                     0,
                                     NULL,
                                     loggy->pool,
                                     "missing version attr for %s",
                                     name));
      else
        {
          svn_string_t *working_file;
          svn_string_t *tmp_base;
          apr_time_t timestamp = 0; /* By default, don't override old stamp. */
          enum svn_node_kind kind;
          svn_string_t *sname = svn_string_create (name, loggy->pool);
          apr_hash_t *entries = NULL;
          svn_wc__entry_t *entry;

          err = svn_wc__entries_read (&entries, loggy->path, loggy->pool);
          if (err)
            return signal_error (loggy, err);
          
          entry = apr_hash_get (entries, sname->data, sname->len);
          if (entry && (entry->flags & SVN_WC__ENTRY_DELETE))
            {
              err = remove_from_version_control (loggy, sname);
              if (err)
                return signal_error (loggy, err);
            }
          else   /* entry not being deleted, so mark commited-to-date */
            {
              working_file = svn_string_dup (loggy->path, loggy->pool);
              svn_path_add_component (working_file,
                                      sname,
                                      svn_path_local_style);
              tmp_base = svn_wc__text_base_path (working_file, 1, loggy->pool);
              
              err = svn_io_check_path (tmp_base, &kind, loggy->pool);
              if (err)
                return signal_error (loggy, svn_error_createf
                                     (SVN_ERR_WC_BAD_ADM_LOG,
                                      0,
                                      NULL,
                                      loggy->pool,
                                      "error checking existence of %s",
                                      name));
              
              if (kind == svn_file_kind)
                {
                  svn_boolean_t same;
                  err = svn_wc__files_contents_same_p (&same,
                                                       working_file,
                                                       tmp_base,
                                                       loggy->pool);
                  if (err)
                    return signal_error (loggy, svn_error_createf 
                                         (SVN_ERR_WC_BAD_ADM_LOG,
                                          0,
                                          NULL,
                                          loggy->pool,
                                          "error comparing %s and %s",
                                          working_file->data, tmp_base->data));
                  
                  err = svn_wc__file_affected_time (&timestamp,
                                                    same ? working_file : tmp_base,
                                                    loggy->pool);
                  if (err)
                    return signal_error
                      (loggy, svn_error_createf 
                       (SVN_ERR_WC_BAD_ADM_LOG,
                        0,
                        NULL,
                        loggy->pool,
                        "error getting file_affected_time on %s",
                        same ? working_file->data : tmp_base->data));
                  
                  err = replace_text_base (loggy->path, name, loggy->pool);
                  if (err)
                    return signal_error (loggy, svn_error_createf 
                                         (SVN_ERR_WC_BAD_ADM_LOG,
                                          0,
                                          NULL,
                                          loggy->pool,
                                          "error replacing text base for %s",
                                          name));
                }
              
              /* Else the SVN/tmp/text-base/ file didn't exist.  Whatever; we
                 can ignore and move on. */
              err = svn_wc__entry_merge_sync (loggy->path,
                                              sname,
                                              atoi (verstr),
                                              svn_file_kind,
                                              SVN_WC__ENTRY_CLEAR,
                                              timestamp,
                                              loggy->pool,
                                              NULL);
              if (err)
                return signal_error
                  (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                             0,
                                             NULL,
                                             loggy->pool,
                                             "error merge_syncing %s",
                                             name));
            }
        }
    }
  else if (strcmp (eltname, "wc-log") == 0)
    /* ignore the expat pacifier */ ;
  else
    return signal_error
      (loggy, svn_error_createf (SVN_ERR_WC_BAD_ADM_LOG,
                                 0,
                                 NULL,
                                 loggy->pool,
                                 "unrecognized logfile element in %s: `%s'",
                                 loggy->path->data, eltname));


  if (err)
    svn_xml_signal_bailout (err, loggy->parser);
}



/*** Using the parser to run the log file. ***/

svn_error_t *
svn_wc__run_log (svn_string_t *path, apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  svn_xml_parser_t *parser;
  struct log_runner *loggy = apr_pcalloc (pool, sizeof (*loggy));
  char buf[BUFSIZ];
  apr_ssize_t buf_len;
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
  err = svn_xml_parse (parser, log_start, strlen (log_start), 0);
  if (err)
    return err;

  /* Parse the log file's contents. */
  err = svn_wc__open_adm_file (&f, path, SVN_WC__ADM_LOG, APR_READ, pool);
  if (err)
    return err;
  
  do {
    buf_len = sizeof (buf);

    apr_err = apr_read (f, buf, &buf_len);
    if (apr_err && (apr_err != APR_EOF))
      {
        apr_close (f);
        return svn_error_createf (apr_err, 0, NULL, pool,
                                 "error reading adm log file in %s",
                                  path->data);
      }

    err = svn_xml_parse (parser, buf, buf_len, 0);
    if (err)
      {
        apr_close (f);
        return err;
      }

    if (apr_err == APR_EOF)
      {
        /* Not an error, just means we're done. */
        apr_close (f);
        break;
      }
  } while (apr_err == APR_SUCCESS);

  /* Pacify Expat with a pointless closing element tag. */
  err = svn_xml_parse (parser, log_end, strlen (log_end), 1);
  if (err)
    return err;

  svn_xml_free_parser (parser);

  err = svn_wc__remove_adm_file (path, pool, SVN_WC__ADM_LOG, NULL);

  return err;
}



/*** Recursively do log things. ***/

svn_error_t *
svn_wc__cleanup (svn_string_t *path,
                 apr_hash_t *targets,
                 svn_boolean_t bail_on_lock,
                 apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_t *entries = NULL;
  apr_hash_index_t *hi;
  svn_boolean_t care_about_this_dir = 0;

  /* Recurse on versioned subdirs first, oddly enough. */
  err = svn_wc__entries_read (&entries, path, pool);
  if (err)
    return err;

  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t keylen;
      void *val;
      svn_wc__entry_t *entry;

      apr_hash_this (hi, &key, &keylen, &val);
      entry = val;

      if ((keylen == strlen (SVN_WC__ENTRIES_THIS_DIR))
          && (strcmp ((char *) key, SVN_WC__ENTRIES_THIS_DIR) == 0))
        continue;

      /* If TARGETS tells us to care about this dir, we may need to
         clean up locks later.  So find out in advance. */
      if (targets)
        {
          if (! care_about_this_dir)
            {
              svn_string_t *target = svn_string_dup (path, pool);
              svn_path_add_component 
                (target,
                 svn_string_ncreate ((char *) key, keylen, pool),
                 svn_path_local_style);
              
              if (apr_hash_get (targets, target->data, target->len))
                care_about_this_dir = 1;
            }
        }
      else
        care_about_this_dir = 1;

      if (entry->kind == svn_dir_kind)
        {
          svn_string_t *subdir = svn_string_dup (path, pool);
          svn_path_add_component (subdir,
                                  svn_string_create ((char *) key, pool),
                                  svn_path_local_style);

          err = svn_wc__cleanup (subdir, targets, bail_on_lock, pool);
          if (err)
            return err;
        }
    }


  if (care_about_this_dir)
    {
      if (bail_on_lock)
        {
          svn_boolean_t locked;
          err = svn_wc__locked (&locked, path, pool);
          if (err)
            return err;
          
          if (locked)
            return svn_error_createf (SVN_ERR_WC_LOCKED,
                                      0,
                                      NULL,
                                      pool,
                                      "svn_wc__cleanup: %s locked",
                                      path->data);
        }
      
      /* Eat what's put in front of us. */
      err = svn_wc__run_log (path, pool);
      if (err)
        return err;
      
      /* Remove any lock here.  But we couldn't even be here if there were
         a lock file and bail_on_lock were set, so do the obvious check
         first. */
      if (! bail_on_lock)
        {
          err = svn_wc__unlock (path, pool);
          if (err && (err->apr_err != APR_ENOENT))
            return err;
        }
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__log_commit (svn_string_t *path,
                    apr_hash_t *targets,
                    svn_vernum_t version,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_hash_t *entries = NULL;
  apr_hash_index_t *hi;

  err = svn_wc__entries_read (&entries, path, pool);
  if (err)
    return err;

  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t keylen;
      void *val;
      svn_wc__entry_t *entry;

      apr_hash_this (hi, &key, &keylen, &val);
      entry = val;

      if ((keylen == strlen (SVN_WC__ENTRIES_THIS_DIR))
          && (strcmp ((char *) key, SVN_WC__ENTRIES_THIS_DIR) == 0))
        continue;

      if (entry->kind == svn_dir_kind)
        {
          svn_string_t *subdir = svn_string_dup (path, pool);
          svn_path_add_component (subdir,
                                  svn_string_create ((char *) key, pool),
                                  svn_path_local_style);

          err = svn_wc__log_commit (subdir, targets, version, pool);
          if (err)
            return err;
        }
      else
        {
          svn_string_t *logtag = svn_string_create ("", pool);
          char *verstr = apr_psprintf (pool, "%ld", version);
          apr_file_t *log_fp = NULL;
          
          /* entry->kind == svn_file_kind, but was the file actually
             involved in the commit? */
          
          if (targets)
            {
              svn_string_t *target = svn_string_dup (path, pool);
              svn_path_add_component
                (target,
                 svn_string_ncreate ((char *) key, keylen, pool),
                 svn_path_local_style);
              
              if (! apr_hash_get (targets, target->data, target->len))
                continue;
            }
          
          /* Yes, the file was involved in the commit. */

          err = svn_wc__open_adm_file (&log_fp, path, SVN_WC__ADM_LOG,
                                       (APR_WRITE | APR_APPEND | APR_CREATE),
                                       pool);
          if (err)
            return err;
          
          svn_xml_make_open_tag (&logtag,
                                 pool,
                                 svn_xml_self_closing,
                                 SVN_WC__LOG_COMMITTED,
                                 SVN_WC__LOG_ATTR_NAME,
                                 svn_string_create ((char *) key, pool),
                                 SVN_WC__LOG_ATTR_VERSION,
                                 svn_string_create (verstr, pool),
                                 NULL);
          
          apr_err = apr_full_write (log_fp, logtag->data, logtag->len, NULL);
          if (apr_err)
            {
              apr_close (log_fp);
              return svn_error_createf (apr_err, 0, NULL, pool,
                                        "svn_wc__log_commit: "
                                        "error writing %s's log file", 
                                        path->data);
            }
          
          err = svn_wc__close_adm_file (log_fp,
                                        path,
                                        SVN_WC__ADM_LOG,
                                        1, /* sync */
                                        pool);
          if (err)
            return err;
        }
    }

  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

