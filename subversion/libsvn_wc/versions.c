/*
 * entries.c :  manipulating the administrative `entries' file.
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

/* The administrative `entries' file tracks information about files
   and subdirs within a particular directory.
   
   See the section on the `entries' file in libsvn_wc/README, for
   concrete information about the XML format.
*/


/*--------------------------------------------------------------- */

/*** Initialization of the entries file. ***/

svn_error_t *
svn_wc__entries_init (svn_string_t *path, apr_pool_t *pool)
{
  svn_error_t *err;
  apr_file_t *f = NULL;

  /* Create the entries file, which must not exist prior to this. */
  err = svn_wc__open_adm_file (&f, path, SVN_WC__ADM_ENTRIES,
                               (APR_WRITE | APR_CREATE | APR_EXCL), pool);
  if (err)
    return err;

  /* Write out the XML standard header to satisfy bureacracy. */
  err = svn_xml_write_header (f, pool);
  if (err)
    {
      apr_close (f);
      return err;
    }

  /* Open the file's top-level form. */
  err = svn_xml_write_tag (f,
                           pool,
                           svn_xml__open_tag,
                           SVN_WC__ENTRIES_START,
                           "xmlns",
                           svn_string_create (SVN_XML_NAMESPACE, pool),
                           NULL);
  if (err)
    {
      apr_close (f);
      return err;
    }

  /* Add an entry for the dir itself -- name is absent, only the
     version is present in the dir entry. */
  {
    char *verstr = apr_psprintf (pool, "%ld", 0);
    err = svn_xml_write_tag (f, 
                             pool,
                             svn_xml__self_close_tag,
                             SVN_WC__ENTRIES_ENTRY,
                             SVN_WC__ENTRIES_ATTR_VERSION,
                             svn_string_create (verstr, pool),
                             NULL);
    if (err)
      {
        apr_close (f);
        return err;
      }
  }

  /* Close the top-level form. */
  err = svn_xml_write_tag (f,
                           pool,
                           svn_xml__close_tag,
                           SVN_WC__ENTRIES_END,
                           NULL);
  if (err)
    {
      apr_close (f);
      return err;
    }

  /* Now we have a `entries' file with exactly one entry, an entry
     for this dir.  Close the file and sync it up. */
  err = svn_wc__close_adm_file (f, path, SVN_WC__ADM_ENTRIES, 1, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


/*--------------------------------------------------------------- */

/*** xml callbacks ***/

/* For a given ENTRYNAME in PATH's entries file, set the entry's
 * version to VERSION.  Also set other XML attributes via varargs:
 * key, value, key, value, etc, terminated by a single NULL.  (The
 * keys are char *'s and values are svn_string_t *'s.)
 * 
 * If no such ENTRYNAME exists, create it.
 */


/* The userdata that will go to our expat callbacks */
typedef struct svn_wc__entry_baton_t
{
  apr_pool_t *pool;
  svn_xml_parser_t *parser;

  svn_boolean_t found_it;  /* Gets set to true iff we see a matching entry. */

  apr_file_t *infile;      /* The entries file we're reading from. */
  apr_file_t *outfile;     /* If this is NULL, then we're GETTING
                              attributes; if this is non-NULL, then
                              we're SETTING attributes by writing a
                              new file.  */

  const char *entryname;   /* The name of the entry we're looking for. */
  svn_vernum_t version;    /* The version we will get or set. */

  apr_hash_t *attributes;  /* The attribute list we want to set or
                              get.  If the former, the values are
                              svn_string_t *'s; if the latter, then
                              the values are svn_string_t **'s. */

} svn_wc__entry_baton_t;



/* Search through ATTS and fill in BATON->attributes appropriately.
   BATON->attributes is a hash whose keys are char *'s and values are
   svn_string_t **'s, which will be set to point to new strings
   representing the values discovered in ATTS.

   Also, BATON->version will be set appropriately.
 */
static void
get_entry_attributes (const char **atts,
                      svn_vernum_t *version,
                      apr_hash_t *desired_attrs,
                      apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* Handle version specially. */
  *version = (svn_vernum_t) 
    atoi (svn_xml_get_attr_value (SVN_WC__ENTRIES_ATTR_VERSION, atts));

  /* Now loop through the requested attributes, setting by reference. */
  for (hi = apr_hash_first (desired_attrs); hi; hi = apr_hash_next (hi))
    {
      const char *key;
      size_t keylen;
      const char *val; 
      svn_string_t **receiver;
      
      apr_hash_this (hi, (const void **) &key, &keylen, (void **) &receiver);
      assert (receiver != NULL);

      val = svn_xml_get_attr_value (key, atts);
      *receiver = (val ? svn_string_create (val, pool) : NULL);
    }
}


/* Called whenever we find an <open> tag of some kind. */
static void
handle_start_tag (void *userData, const char *tagname, const char **atts)
{
  svn_wc__entry_baton_t *baton = (svn_wc__entry_baton_t *) userData;
  svn_error_t *err;

  /* We only care about the `entry' tag; all other tags, such as `xml'
     and `wc-entries', are simply written back out verbatim. */

  if ((strcmp (tagname, SVN_WC__ENTRIES_ENTRY)) == 0)
    {
      const char *entry
        = svn_xml_get_attr_value (SVN_WC__ENTRIES_ATTR_NAME, atts);
      
      /* Nulls count as a match, because null represents the dir itself. */
      if (((entry == NULL) && (baton->entryname == NULL))
          || ((entry != NULL)
              && (baton->entryname != NULL)
              && ((strcmp (entry, baton->entryname)) == 0)))
        {
          baton->found_it = 1;

          if (baton->outfile) /* we're writing out a changed tag */
            {
              char *verstr = apr_psprintf (baton->pool,
                                           "%ld",
                                           (long int) baton->version);

              svn_xml_hash_atts_preserving (atts,
                                            baton->attributes,
                                            baton->pool);

              /* Version has to be stored specially. */
              apr_hash_set (baton->attributes,
                            SVN_WC__ENTRIES_ATTR_VERSION,
                            strlen (SVN_WC__ENTRIES_ATTR_VERSION),
                            svn_string_create (verstr, baton->pool));

              err = svn_xml_write_tag_hash (baton->outfile,
                                            baton->pool,
                                            svn_xml__self_close_tag,
                                            SVN_WC__ENTRIES_ENTRY,
                                            baton->attributes);
              if (err)
                {
                  svn_xml_signal_bailout (err, baton->parser);
                  return;
                }
            }
          else  /* just reading attribute values, not writing a new tag */
            get_entry_attributes (atts,
                                  &(baton->version),
                                  baton->attributes,
                                  baton->pool);
        }
      else  /* An entry tag, but not the one we're looking for. */
        {
          if (baton->outfile)  /* just write it back out unchanged. */
            {
              err = svn_xml_write_tag_hash 
                (baton->outfile,
                 baton->pool,
                 svn_xml__self_close_tag,
                 SVN_WC__ENTRIES_ENTRY,
                 svn_xml_make_att_hash (atts, baton->pool));
                                            
              if (err)
                {
                  svn_xml_signal_bailout (err, baton->parser);
                  return;
                }
            }
        } 
    }
  else  /* This is some tag other than `entry', preserve it unchanged.  */
    {
      if (baton->outfile)
        {
          err = svn_xml_write_tag_hash
            (baton->outfile, 
             baton->pool,
             svn_xml__open_tag,
             tagname,
             svn_xml_make_att_hash (atts, baton->pool));
          if (err)
            svn_xml_signal_bailout (err, baton->parser);
        }
    }
}


/* Called whenever we find a </close> tag of some kind. */
static void
handle_end_tag (void *userData, const char *tagname)
{
  svn_wc__entry_baton_t *baton = (svn_wc__entry_baton_t *) userData;
  svn_error_t *err;

  if ((strcmp (tagname, SVN_WC__ENTRIES_END)) == 0)
    {
      if (baton->outfile)
        {
          /* If this entry didn't exist before, then add it now. */
          if (! baton->found_it)
            {
              char *verstr
                = apr_psprintf (baton->pool, "%ld", (long int) baton->version);
              
              err = svn_xml_write_tag (baton->outfile,
                                       baton->pool,
                                       svn_xml__self_close_tag,
                                       SVN_WC__ENTRIES_ENTRY,
                                       SVN_WC__ENTRIES_ATTR_NAME,
                                       svn_string_create (baton->entryname,
                                                          baton->pool),
                                       SVN_WC__ENTRIES_ATTR_VERSION,
                                       svn_string_create (verstr, baton->pool),
                                       NULL);
              if (err)
                {
                  svn_xml_signal_bailout (err, baton->parser);
                  return;
                }
            }

          /* Now close off the file. */
          err = svn_xml_write_tag (baton->outfile,
                                   baton->pool,
                                   svn_xml__close_tag,
                                   tagname,
                                   NULL);
          if (err)
            svn_xml_signal_bailout (err, baton->parser);
        }
    }
}


/* Code chunk shared by svn_wc__[gs]et_entry()
   
   Parses xml in BATON->infile using BATON as userdata. */
static svn_error_t *
do_parse (svn_wc__entry_baton_t *baton)
{
  svn_error_t *err;
  svn_xml_parser_t *svn_parser;
  char buf[BUFSIZ];
  apr_status_t apr_err;
  apr_size_t bytes_read;

  /* Create a custom XML parser */
  svn_parser = svn_xml_make_parser (baton,
                                    handle_start_tag,
                                    handle_end_tag,
                                    NULL,
                                    baton->pool);

  baton->parser = svn_parser;  /* Don't forget to store the parser in
                                  our userdata, so that callbacks can
                                  call svn_xml_signal_bailout() */

  /* Parse the xml in infile, and write modified stream back out to
     outfile. */
  do {
    apr_err = apr_full_read (baton->infile, buf, BUFSIZ, &bytes_read);
    if (apr_err && (apr_err != APR_EOF))
      return svn_error_create 
        (apr_err, 0, NULL, baton->pool,
         "svn_wc__entry_set: apr_full_read choked");
    
    err = svn_xml_parse (svn_parser, buf, bytes_read, (apr_err == APR_EOF));
    if (err)
      return svn_error_quick_wrap 
        (err,
         "svn_wc__entry_set:  xml parser failed.");
  } while (apr_err != APR_EOF);


  /* Clean up xml parser */
  svn_xml_free_parser (svn_parser);

  return SVN_NO_ERROR;
}



/*----------------------------------------------------------------------*/

/*** Getting and setting entries. ***/

/* Common code for entry_set and entry_get. */
static svn_error_t *
do_entry (svn_string_t *path,
          apr_pool_t *pool,
          const char *entryname,
          svn_vernum_t version,
          svn_vernum_t *version_receiver,
          svn_boolean_t setting,
          va_list ap)
{
  svn_error_t *err;
  apr_file_t *infile = NULL;
  apr_file_t *outfile = NULL;

  svn_wc__entry_baton_t *baton 
    = apr_pcalloc (pool, sizeof (svn_wc__entry_baton_t));

  /* Open current entries file for reading */
  err = svn_wc__open_adm_file (&infile, path,
                               SVN_WC__ADM_ENTRIES,
                               APR_READ, pool);
  if (err)
    return err;

  if (setting)
    {
      /* Open a new `tmp/entries' file for writing */
      err = svn_wc__open_adm_file (&outfile, path,
                                   SVN_WC__ADM_ENTRIES,
                                   (APR_WRITE | APR_CREATE | APR_EXCL), pool);
      if (err)
        return err;
    }

  /* Fill in the userdata structure */
  baton->pool       = pool;
  baton->infile     = infile;
  baton->outfile    = outfile;
  baton->entryname  = entryname;
  baton->version    = version;
  baton->attributes = svn_xml_ap_to_hash (ap, pool);

  /* Set the att. */
  err = do_parse (baton);
  if (err)
    return err;

  /* Close infile */
  err = svn_wc__close_adm_file (infile, path,
                                SVN_WC__ADM_ENTRIES, 0, pool);
  if (err)
    return err;
  
  if (setting)
    {
      /* Close the outfile and *sync* it, so it replaces the original
         infile. */
      err = svn_wc__close_adm_file (outfile, path,
                                    SVN_WC__ADM_ENTRIES, 1, pool);
      if (err)
        return err;
    }
  else
    *version_receiver = baton->version;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entry_set (svn_string_t *path,
                   apr_pool_t *pool,
                   const char *entryname,
                   svn_vernum_t version,
                   ...)
{
  svn_error_t *err;
  va_list ap;

  va_start (ap, version);
  err = do_entry (path, pool, entryname, version, NULL, 1, ap);
  va_end (ap);

  return err;
}



svn_error_t *
svn_wc__entry_get (svn_string_t *path,
                   apr_pool_t *pool,
                   const char *entryname,
                   svn_vernum_t *version,
                   ...)
{
  svn_error_t *err;
  va_list ap;

  va_start (ap, version);
  err = do_entry (path, pool, entryname, 0, version, 0, ap);
  va_end (ap);

  return err;
}


/* Remove ENTRYNAME from PATH's `entries' file. */
svn_error_t *svn_wc__entry_remove (svn_string_t *path,
                                   apr_pool_t *pool,
                                   const char *entryname)
{
  /* kff todo: finish this. */
  return SVN_NO_ERROR;
}



/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
