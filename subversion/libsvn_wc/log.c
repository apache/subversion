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
  svn_string_t *path;  /* the dir in which this is all happening */
  svn_error_t *error;
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
  svn_path_add_component_nts (filepath, name, SVN_PATH_LOCAL_STYLE, pool);

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


static svn_error_t *
replace_text_base (svn_string_t *path,
                   const char *name,
                   apr_pool_t *pool)
{
  svn_string_t *filepath;
  svn_string_t *tmp_text_base;
  svn_error_t *err;
  svn_boolean_t exists;

  filepath = svn_string_dup (path, pool);
  svn_path_add_component_nts (filepath, name, SVN_PATH_LOCAL_STYLE, pool);

  tmp_text_base = svn_wc__text_base_path (path, 1, pool);
  err = svn_wc__file_exists_p (&exists, tmp_text_base, pool);
  if (err)
    return err;

  if (! exists)
    return SVN_NO_ERROR;  /* tolerate mop-up calls gracefully */
  else
    return svn_wc__sync_text_base (filepath, pool);
}


static svn_error_t *
set_version (svn_string_t *path,
             const char *name,
             svn_vernum_t version,
             apr_pool_t *pool)
{
  /* This operation is idempotent, so just do it and don't worry. */
  return svn_wc__set_versions_entry (path,
                                     pool,
                                     name,
                                     version,
                                     NULL);
}


/* Set an error for later reference. */
static void
set_error (struct log_runner *loggy)
{
  loggy->error = svn_error_create (SVN_ERR_WC_BAD_ADM_LOG,
                                   0,
                                   NULL,
                                   loggy->pool,
                                   loggy->path->data);
}


static void
start_handler (void *userData, const XML_Char *name, const XML_Char **atts)
{
  struct log_runner *loggy = (struct log_runner *) userData;

  printf ("\n"); /* kff todo */

  /* Here is a sample log file:
   *
   *    <merge-text
   *       name="iota"
   *       saved-mods="kff todo"/>
   *    <replace-text-base
   *       name="iota"/>
   *    <set-version
   *       name="iota"
   *       version="1"/>
   */

  if (strcmp (name, "merge-text") == 0)
    {
      const char *name = svn_xml_get_attr_value ("name", atts);
      const char *saved_mods = svn_xml_get_attr_value ("saved-mods", atts);

      if (! name)  /* note that saved_mods is allowed to be NULL */
        set_error (loggy);
      else
        loggy->error = merge_text (loggy->path, name, saved_mods, loggy->pool);
    }
  else if (strcmp (name, "replace-text-base") == 0)
    {
      const char *name = svn_xml_get_attr_value ("name", atts);

      if (! name)
        set_error (loggy);
      else
        loggy->error = replace_text_base (loggy->path, name, loggy->pool);
    }
  else if (strcmp (name, "set-version") == 0)
    {
      const char *name = svn_xml_get_attr_value ("name", atts);
      const char *verstr = svn_xml_get_attr_value ("version", atts);

      if ((! name) || (! verstr))
        set_error (loggy);
      else
        loggy->error = set_version (loggy->path,
                                    name,
                                    atoi (verstr),
                                    loggy->pool);
    }
  else
      set_error (loggy);
}



/*** Using the parser to run the log file. ***/

svn_error_t *
svn_wc__run_log (svn_string_t *path, apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  apr_status_t apr_err;
  XML_Parser parser;
  struct log_runner *logress = apr_palloc (pool, sizeof (*logress));
  char buf[BUFSIZ];
  apr_ssize_t buf_len;
  apr_file_t *f = NULL;

  const char *log_start
    = "<svn-wc-log xmlns=\"http://subversion.tigris.org/xmlns\">\n";
  const char *log_end
    = "</svn-wc-log>\n";

  logress->path = path;
  logress->pool = pool;

  parser = svn_xml_make_parser (logress, start_handler, NULL, NULL);
  
  /* Start the log off with a pointless opening element tag. */
  if (! XML_Parse (parser, log_start, strlen (log_start), 0))
    goto expat_error;

  /* Parse the log file's contents. */
  err = svn_wc__open_adm_file (&f, path, SVN_WC__ADM_LOG, APR_READ, pool);
  if (err)
    goto any_error;
  
  do {
    buf_len = sizeof (buf);
    apr_err = apr_read (f, buf, &buf_len);

    if (! XML_Parse (parser, buf, buf_len, 0))
      {
        apr_close (f);
        goto expat_error;
      }
    
    if (apr_err == APR_EOF)
      {
        apr_close (f);
        break;
      }

    if (logress->error)
      {
        err = logress->error;
        goto any_error;
      }

  } while (apr_err == APR_SUCCESS);


  /* End the log with a pointless closing element tag. */
  if (! XML_Parse (parser, log_end, sizeof (log_end), 0))
    goto expat_error;

  /* Apparently, Expat returns 0 on fatal error *except* for the
     final call, at which time it always returns 0.  Ben, does this
     match your experience? */
  if (XML_Parse (parser, NULL, 0, 1) != 0)
    {
    expat_error:
      /* Uh oh, expat *itself* choked somehow! */
      err = svn_error_createf
        (SVN_ERR_MALFORMED_XML, 0, NULL, pool, 
         "%s at line %d",
         XML_ErrorString (XML_GetErrorCode (parser)),
         XML_GetCurrentLineNumber (parser));
      
    any_error:
      /* Kill the expat parser and return its error */
      XML_ParserFree (parser);      
      return err;
    }

  err = svn_wc__remove_adm_file (path, pool, SVN_WC__ADM_LOG, NULL);

  XML_ParserFree (parser);

  return err;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */

