/*
 * versions.c :  manipulating the administrative `versions' file.
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



#include <apr_strings.h>
#include <assert.h>
#include "wc.h"
#include "svn_xml.h"
#include "svn_error.h"


/*------------------------------------------------------------------*/

/** Overview **/

/* The administrative `versions' file tracks the version numbers of
   files within a particular subdirectory.  Subdirectories are *not*
   tracked, because subdirs record their own version information.
   
   See the section on the `versions' file in libsvn_wc/README, for
   concrete information about the XML format.
   
   Note that if there exists a file in text-base that is not mentioned
   in the `versions' file, it is assumed to have the same version as
   the parent directory.  The `versions' file always mentions files
   whose version is different from the dir's, and may (but is not
   required to) mention files that are at the same version as the dir.
   
   In practice, this parser tries to filter out non-exceptions as it
   goes, so the `versions' file is always left without redundancies.
*/


/*--------------------------------------------------------------- */

/*** Initialization of the versions file. ***/

svn_error_t *
svn_wc__versions_init (svn_string_t *path, apr_pool_t *pool)
{
  svn_error_t *err;
  apr_file_t *f = NULL;

  err = svn_wc__open_adm_file (&f, path, SVN_WC__ADM_VERSIONS,
                               (APR_WRITE | APR_CREATE), pool);
  if (err)
    return err;

  /* Satisfy bureacracy. */
  err = svn_xml_write_header (f, pool);
  if (err)
    {
      apr_close (f);
      return err;
    }

  /* Open the file's top-level form. */
  err = svn_xml_write_tag (f, pool, svn_xml__open_tag,
                           SVN_WC__VERSIONS_START,
                           "xmlns", SVN_XML_NAMESPACE,
                           NULL);
  if (err)
    {
      apr_close (f);
      return err;
    }

  /* Write the entry for this dir itself.  The dir's own entry has no
     name attribute, only a version. */
  err = svn_xml_write_tag (f, pool, svn_xml__self_close_tag,
                           SVN_WC__VERSIONS_ENTRY,
                           "version", apr_psprintf (pool, "%ld", 0),
                           NULL);
  if (err)
    {
      apr_close (f);
      return err;
    }

  /* Close the top-level form. */
  err = svn_xml_write_tag (f, pool, svn_xml__open_tag,
                           SVN_WC__VERSIONS_END,
                           NULL);
  if (err)
    {
      apr_close (f);
      return err;
    }

  err = svn_wc__close_adm_file (f, path, SVN_WC__ADM_VERSIONS, 1, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


/*--------------------------------------------------------------- */

/** xml callbacks **/

/* For a given ENTRYNAME in PATH's versions file, set the entry's
 * version to VERSION.  Also set other XML attributes via varargs:
 * key, value, key, value, etc, terminated by a single NULL.  (The
 * keys are char *'s and values are svn_string_t *'s.)
 * 
 * If no such ENTRYNAME exists, create it.
 */


/* The userdata that will go to our expat callbacks */
typedef struct svn_wc__version_baton_t
{
  apr_pool_t *pool;
  svn_xml_parser_t *parser;

  svn_vernum_t default_version;  /* The version of `.' */

  apr_file_t *infile;      /* The versions file we're reading from */
  apr_file_t *outfile;     /* If this is NULL, then we're GETTING
                              attributes; if this is non-NULL, then
                              we're SETTING attributes by writing to
                              this file.  */

  const char *entryname;   /* The name of the entry we're looking for */
  svn_vernum_t *version;   /* The version we will get or set */

  va_list valist;          /* The attribute list we want to set or get */

} svn_wc__version_baton_t;



/* Write out a new <entry ...> tag in BATON->outfile, an entry whose
   attributes are the _union_ of those in ATTS and those specified in
   BATON->valist */
static svn_error_t *
set_entry_attributes (svn_wc__version_baton_t *baton,
                      const char **atts)
{
  svn_error_t *err;
  const char **newatts;    /* What will become our final union of attributes */
  const char **tempatts;   /* Temp variable for looping over att lists */
  char *attribute;
  svn_string_t *orig_value;
  svn_string_t *dup_value;
  int len = 0;             /* The length of newatts */
  int count = 0;
  apr_pool_t *subpool = svn_pool_create (baton->pool, NULL);

  /* Figure out the length of atts */
  tempatts = atts;
  while (tempatts)
    {
      len++;
      tempatts++;
    }
  assert (len % 2 == 0);  /* The length of atts should be even! */
  
  /* Allocate newatts with the same length */
  newatts = apr_pcalloc (subpool, (sizeof(char *) * len));
  
  /* Now copy atts into newatts */
  tempatts = atts;
  while (tempatts && (*tempatts))
    {
      newatts[count] = apr_psprintf (subpool, "%s", tempatts[0]);
      newatts[(count + 1)] = apr_psprintf (subpool, "%s", tempatts[1]);
      atts += 2;
      count += 2;
    }

  /* Loop through our va_list, modifiying newatts appropriately.  */
  while ((attribute = va_arg (baton->valist, char *)))
    {
      int found_match = 0;

      /* Get the value of our current vararg and duplicate it */
      orig_value = va_arg (baton->valist, svn_string_t *);
      dup_value = svn_string_dup (orig_value, subpool);

      /* Does this attribute already exist in newatts? */
      tempatts = newatts;
      while (tempatts && (*tempatts))
        {
          if (! strcmp (tempatts[0], attribute)) 
            {
              /* Found a match... let's overwrite the value. */
              tempatts[1] = dup_value->data;
              
              /* Don't forget, we also need to set the version number
                 specified in our baton!  */
              if (! strcmp (attribute, "version"))
                tempatts[1] = apr_psprintf (subpool, "%ld",
                                            (* (baton->version)) );

              found_match = 1;
              break;
            }
          tempatts += 2;
        }

      if (! found_match)
        {
          /* If no match, then we need to manually add attribute/value
             pair to newatts, by re-allocing newatts. */
          tempatts = newatts;
          len += 2;
          newatts = apr_pcalloc (subpool, (sizeof(char *) * len));
          memcpy (newatts, tempatts, (sizeof(char *) * (len - 2)));

          newatts[(len - 2)] = apr_psprintf (subpool, "%s", attribute);
          newatts[(len - 1)] = dup_value->data;
        }

    } /* while (attribute = va_arg) */


  /* Presumably, by this point, newatts is now a union of atts and the
     valist, containing no redundancies.  Print it out! */
  err = svn_xml_write_tag_list (baton->outfile, baton->pool,
                                svn_xml__self_close_tag,
                                SVN_WC__VERSIONS_ENTRY,
                                newatts);
  
  if (err)
    return err;
  
  /* Clean up all the memory we used for copying/constructing
     attribute lists.  */
  apr_destroy_pool (subpool);

  return SVN_NO_ERROR;
}



/* Search through ATTS and fill in each attribute in BATON->valist.
   It's assumed that the valist contains pairs of arguments in the
   form:

        {char *attribute_name, svn_string_t **attribute_value}

   This function will _set_ the latter value by dereferencing the
   double pointer.

   Also, BATON->version will be set appropriately as well.

 */
static svn_error_t *
get_entry_attributes (svn_wc__version_baton_t *baton,
                      const char **atts)
{
  const char *val;
  char *variable_attribute_name;
  svn_string_t **variable_attribute_value;

  /* Before we do anything, return the `version' attribute by setting
     baton->version correctly. */
  val = svn_xml_get_attr_value ("version", atts);
  (* (baton->version)) = (svn_vernum_t) atoi (val);

  /* Now loop through our va_list and return values in every other one */
  while ((variable_attribute_name = va_arg (baton->valist, char *)))
    {
      /* The caller wants us to fetch the value of
         variable_attribute_name.  Let's do so.  */
      val = svn_xml_get_attr_value (variable_attribute_name, atts);
      
      /* Grab the next varargs variable and SET it to the answer. */
      variable_attribute_value = va_arg (baton->valist, svn_string_t **);

      *variable_attribute_value = svn_string_create (val, baton->pool);
    }

  return SVN_NO_ERROR;
}



/* Called whenever we find an <open> tag of some kind. */
static void
xml_handle_start (void *userData, const char *tagname, const char **atts)
{
  svn_error_t *err;

  /* Salvage our baton */
  svn_wc__version_baton_t *baton = (svn_wc__version_baton_t *) userData;

  /* We only care about the `entry' tag; all other tags such as `xml'
     and `wc-versions' will simply be written write back out,
     verbatim.  */

  if (! strcmp (tagname, SVN_WC__VERSIONS_ENTRY))
    {
      /* Get the `name' attribute */
      const char *nameval = svn_xml_get_attr_value ("name", atts);
      
      /* Is this the droid we're looking for? */
      if (! strcmp (nameval, baton->entryname))
        {
          if (baton->outfile) 
            {
              err = set_entry_attributes (baton, atts);
              if (err)
                {
                  svn_xml_signal_bailout (err, baton->parser);
                  return;
                }
            }
          else 
            {
              err = get_entry_attributes (baton, atts);
              if (err)
                {
                  svn_xml_signal_bailout (err, baton->parser);
                  return;
                }
            }          
        }
        
      else  /* This isn't the droid we're looking for. */
        {
          /* However, if we're writing to an outfile, we need to write
             it back out anyway.  */
          if (baton->outfile)
            {
              /* Write it back out again. */
              err = svn_xml_write_tag_list (baton->outfile, baton->pool,
                                            svn_xml__self_close_tag,
                                            SVN_WC__VERSIONS_ENTRY,
                                            atts);
              if (err)
                {
                  svn_xml_signal_bailout (err, baton->parser);
                  return;
                }
            }
        } 
    } 

  else  /* This is some other non-`entry' tag.  Preserve it.  */
    {
      /* We only care about this tag if we're writing to an outfile. */
      if (baton->outfile)
        {
          /* Write it back out again. */
          err = svn_xml_write_tag_list (baton->outfile, baton->pool,
                                        svn_xml__self_close_tag,
                                        tagname,
                                        atts);
          if (err)
            {
              svn_xml_signal_bailout (err, baton->parser);
              return;
            }
        }
    }

  /* This is an expat callback;  return nothing. */
}




/* Called whenever we find an </close> tag of some kind. */
static void
xml_handle_end (void *userData, const char *tagname)
{
  svn_error_t *err;

  /* Salvage our baton */
  svn_wc__version_baton_t *baton = (svn_wc__version_baton_t *) userData;

  /* We don't care about closures of SVN_WC__VERSIONS_ENTRY, because
     they're all self-closing anyway, and well, xml_handle_start is
     writing them back out to disk already.  We only care about
     </wc-versions> here, because it's the *only* non-self-closing tag
     we're gonig to run across in the versions file.  */

  if (! strcmp (tagname, SVN_WC__VERSIONS_END))
    {
      /* Copy this tag back out to the outfile, if we have one. */
      if (baton->outfile)
        {
          /* Write it back out again. */
          err = svn_xml_write_tag (baton->outfile, baton->pool,
                                   svn_xml__close_tag,
                                   SVN_WC__VERSIONS_END,
                                   NULL);
          if (err)
            {
              svn_xml_signal_bailout (err, baton->parser);
              return;
            }
        }
    }

  /* This is an expat callback;  return nothing. */
}
  


/* Code chunk shared by svn_wc__[gs]et_versions_entry()
   
   Parses xml in BATON->infile using BATON as userdata. */
static svn_error_t *
do_parse (svn_wc__version_baton_t *baton)
{
  svn_error_t *err;
  svn_xml_parser_t *svn_parser;
  char buf[BUFSIZ];
  apr_status_t status;
  apr_size_t bytes_read;

  /* Create a custom XML parser */
  svn_parser = svn_xml_make_parser (baton,
                                    xml_handle_start,
                                    xml_handle_end,
                                    NULL,
                                    baton->pool);

  baton->parser = svn_parser;  /* Don't forget to store the parser in
                                  our userdata, so that callbacks can
                                  call svn_xml_signal_bailout() */

  /* Parse the xml in infile, and write new versions of it back out to
     outfile. */
  while (status != APR_EOF)
    {
      status = apr_full_read (baton->infile, buf, BUFSIZ, &bytes_read);
      if (status && (status != APR_EOF))
        return svn_error_create 
          (status, 0, NULL, baton->pool,
           "svn_wc__set_versions_entry: apr_full_read choked");

      err = svn_xml_parse (svn_parser, buf, bytes_read, (status == APR_EOF));
      if (err)
        return svn_error_quick_wrap 
          (err,
           "svn_wc__set_versions_entry:  xml parser failed.");
    }


  /* Clean up xml parser */
  svn_xml_free_parser (svn_parser);

  return SVN_NO_ERROR;
}




/*----------------------------------------------------------------------*/

/** Public Interfaces **/



/* For a given ENTRYNAME in PATH, set its version to VERSION in the
   `versions' file.  Also set other XML attributes via varargs: name,
   value, name, value, etc. -- where names are char *'s and values are
   svn_string_t *'s.   Terminate list with NULL. 

   If no such ENTRYNAME exists, create it.
 */
svn_error_t *svn_wc__set_versions_entry (svn_string_t *path,
                                         apr_pool_t *pool,
                                         const char *entryname,
                                         svn_vernum_t version,
                                         ...)
{
  va_list argptr;
  svn_error_t *err;
  apr_file_t *infile = NULL;
  apr_file_t *outfile = NULL;

  svn_wc__version_baton_t *version_baton 
    = apr_pcalloc (pool, sizeof (svn_wc__version_baton_t));

  /* Open current versions file for reading */
  err = svn_wc__open_adm_file (&infile, path,
                               SVN_WC__ADM_VERSIONS,
                               APR_READ, pool);
  if (err)
    return err;

  /* Open a new `tmp/versions' file for writing */
  /* TODO: Karl... what makes these two file-open calls any different?
     Won't they both result in opening the *same* `path/tmp/versions'
     file?!? */
  err = svn_wc__open_adm_file (&outfile, path,
                               SVN_WC__ADM_VERSIONS,
                               (APR_WRITE | APR_CREATE), pool);
  if (err)
    return err;

  /* Fill in our userdata structure */
  version_baton->infile    = infile;
  version_baton->outfile   = outfile;
  version_baton->entryname = entryname;
  version_baton->version   = &version;
  version_baton->valist    = argptr;

  va_start (argptr, version);

  err = do_parse (version_baton);
  if (err)
    return err;

  va_end (argptr);

  /* Close infile */
  err = svn_wc__close_adm_file (infile, path,
                                SVN_WC__ADM_VERSIONS, 0, pool);
  if (err)
    return err;
  
  /* Close the outfile and *sync* it, so it replaces the original
     infile. */
  err = svn_wc__close_adm_file (outfile, path,
                                SVN_WC__ADM_VERSIONS, 1, pool);
  if (err)
    return err;


  return SVN_NO_ERROR;
}



/* For a given ENTRYNAME in PATH, read the `version's file and get its
   version into VERSION.  Also get other XML attributes via varargs:
   name, value, name, value, etc. -- where names are char *'s and
   values are svn_string_t **'s.  Terminate list with NULL.  */
svn_error_t *svn_wc__get_versions_entry (svn_string_t *path,
                                         apr_pool_t *pool,
                                         const char *entryname,
                                         svn_vernum_t *version,
                                         ...)
{
  va_list argptr;
  svn_error_t *err;
  apr_file_t *infile = NULL;

  svn_wc__version_baton_t *version_baton 
    = apr_pcalloc (pool, sizeof (svn_wc__version_baton_t));

  /* Open current versions file for reading */
  err = svn_wc__open_adm_file (&infile, path,
                               SVN_WC__ADM_VERSIONS,
                               APR_READ, pool);
  if (err)
    return err;

  /* Fill in our userdata structure */
  version_baton->infile    = infile;
  version_baton->entryname = entryname;
  version_baton->version   = version;
  version_baton->valist    = argptr;

  va_start (argptr, version);

  err = do_parse (version_baton);
  if (err)
    return err;

  va_end (argptr);

  /* Close infile */
  err = svn_wc__close_adm_file (infile, path,
                                SVN_WC__ADM_VERSIONS, 0, pool);
  if (err)
    return err;
  
  return SVN_NO_ERROR;
}




/* Remove ENTRYNAME from PATH's `versions' file. */
svn_error_t *svn_wc__remove_versions_entry (svn_string_t *path,
                                            apr_pool_t *pool,
                                            const char *entryname)
{
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
