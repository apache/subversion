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
#include "svn_types.h"


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
svn_wc__entries_init (svn_string_t *path,
                      svn_string_t *ancestor_path,
                      apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_file_t *f = NULL;
  svn_string_t *accum = NULL;
  char *initial_verstr = apr_psprintf (pool, "%ld", 0);

  /* Create the entries file, which must not exist prior to this. */
  err = svn_wc__open_adm_file (&f, path, SVN_WC__ADM_ENTRIES,
                               (APR_WRITE | APR_CREATE | APR_EXCL), pool);
  if (err)
    return err;

  /* Make a the XML standard header, to satisfy bureacracy. */
  svn_xml_make_header (&accum, pool);

  /* Open the file's top-level form. */
  svn_xml_make_open_tag (&accum,
                         pool,
                         svn_xml_normal,
                         SVN_WC__ENTRIES_TOPLEVEL,
                         "xmlns",
                         svn_string_create (SVN_XML_NAMESPACE, pool),
                         NULL);

  /* Add an entry for the dir itself -- name is absent, only the
     version and default ancestry are present as xml attributes. */
  svn_xml_make_open_tag (&accum,
                         pool,
                         svn_xml_self_closing,
                         SVN_WC__ENTRIES_ENTRY,
                         SVN_WC__ENTRIES_ATTR_VERSION,
                         svn_string_create (initial_verstr, pool),
                         SVN_WC__ENTRIES_ATTR_ANCESTOR,
                         ancestor_path,
                         NULL);

  /* Close the top-level form. */
  svn_xml_make_close_tag (&accum,
                          pool,
                          SVN_WC__ENTRIES_TOPLEVEL);

  apr_err = apr_full_write (f, accum->data, accum->len, NULL);
  if (apr_err)
    {
      apr_close (f);
      return svn_error_createf (apr_err, 0, NULL, pool,
                                "svn_wc__entries_init: "
                                "error writing %s's entries file",
                                path->data);
    }

  /* Now we have a `entries' file with exactly one entry, an entry
     for this dir.  Close the file and sync it up. */
  err = svn_wc__close_adm_file (f, path, SVN_WC__ADM_ENTRIES, 1, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


#if BRAVE_NEW_INTERFACE

/*--------------------------------------------------------------- */

/*** reading and writing the entries file ***/

/* 
   typedef struct svn_wc__entry_t
   #define SVN_WC__ENTRY_ADD     1
   #define SVN_WC__ENTRY_DELETE  2
*/


struct entries_accumulator
{
  /* Keys are entry names, vals are (struct svn_wc__entry_t *)'s. */
  apr_hash_t *entries; 

  /* The dir whose entries file this is. */
  svn_string_t *path;

  /* The parser that's parsing it, for signal_expat_bailout(). */
  svn_xml_parser_t *parser;

  /* Don't leave home without it. */
  apr_pool_t *pool;
};


/* Called whenever we find an <open> tag of some kind. */
static void
handle_start_tag (void *userData, const char *tagname, const char **atts)
{
  struct entries_accumulator *accum = (struct entries_accumulator *) userData;
  svn_error_t *err;

  /* We only care about the `entry' tag; all other tags, such as `xml'
     and `wc-entries', are ignored. */
  if ((strcmp (tagname, SVN_WC__ENTRIES_ENTRY)) == 0)
    {
      svn_wc__entry_t *entry
        = apr_pcalloc (accum->pool, sizeof (*entry));

      entry->attributes = svn_xml_make_att_hash (atts, accum->pool);

      /* Find the name and set up the entry under that name. */
      {
        svn_string_t *name
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_NAME, 
                          strlen (SVN_WC__ENTRIES_ATTR_NAME));
        char *nstr = name ? name->data : SVN_WC__ENTRIES_THIS_DIR;
        apr_size_t len = name ? name->len : strlen (SVN_WC__ENTRIES_THIS_DIR);

        apr_hash_set (accum->entries, nstr, len, entry);

        if (! name)
          apr_hash_set (entry->attributes,
                        SVN_WC__ENTRIES_ATTR_NAME,
                        strlen (SVN_WC__ENTRIES_ATTR_NAME),
                        NULL);
      }

      /* Attempt to set up the version (but see function check_defaults). */
      {
        svn_string_t *version_str
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_VERSION, 
                          strlen (SVN_WC__ENTRIES_ATTR_VERSION));

        if (version_str)
          entry->version = (svn_vernum_t) atoi (version_str);
        else
          entry->version = SVN_INVALID_VERNUM;
      }

      /* Attempt to set up ancestor path (again, see check_defaults). */
      {
        entry->ancestor
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_ANCESTOR, 
                          strlen (SVN_WC__ENTRIES_ATTR_ANCESTOR));
      }

      /* Set up kind. */
      {
        svn_string_t *kindstr
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_KIND, 
                          strlen (SVN_WC__ENTRIES_ATTR_KIND));

        if ((! kind) || (strcmp (kind->data, "file") == 0))
          entry->kind = svn_file_kind;
        else if (strcmp (kind->data, "dir") == 0)
          entry->kind = svn_dir_kind;
        else
          {
            svn_string_t *name
              = apr_hash_get (entry->attributes,
                              SVN_WC__ENTRY_ATTR_NAME,
                              strlen (SVN_WC__ENTRY_ATTR_NAME));

            svn_xml_signal_bailout 
              (svn_error_createf (SVN_ERR_UNKNOWN_NODE_KIND,
                                  0,
                                  NULL,
                                  accum->pool,
                                  "entries.c:handle_start_tag(): "
                                  "entry %s in dir %s",
                                  (name ?
                                   name->data : SVN_WC__ENTRIES_THIS_DIR),
                                  accum->path),
               accum->parser);
          }
      }

      /* Attempt to set up timestamp. */
      {
        svn_string_t *timestr
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_TIMESTAMP, 
                          strlen (SVN_WC__ENTRIES_ATTR_TIMESTAMP));

        if (timestr)
          entry->timestamp = svn_wc__string_to_time (timestr);
      }

      /* Look for any action flags. */
      {
        svn_string_t *addstr
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_ADD, 
                          strlen (SVN_WC__ENTRIES_ATTR_ADD));
        svn_string_t *delstr
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_DELETE, 
                          strlen (SVN_WC__ENTRIES_ATTR_DELETE));

        /* Technically, the value has to actually be "true".  But
           we never have these attributes at all, unless it's with
           those values. */
        if (addstr)
          entry->flags |= SVN_WC__ENTRY_ADD;
        if (delstr)
          entry->flags |= SVN_WC__ENTRY_DELETE;
      }
    }
}


/* Fill in unfilled portions of DST entry from SRC. */
static void
take_from_entry (svn_wc__entry_t *src, svn_wc__entry_t *dst, apr_pool_t *pool)
{
  if (! dst->version)
    dst->version = src->version;
  
  if (! dst->ancestor)
    {
      svn_string_t *name = svn_string_ncreate (key, keylen, pool);
      dst->ancestor
        = svn_string_dup (src->ancestor, pool);
      svn_path_add_component (dst->ancestor, name,
                              svn_path_repos_style, pool);
    }
}


static svn_error_t *
check_defaults (apr_hash_t *entries, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_wc__entry_t *default_entry
    = apr_hash_get (entries, SVN_WC__ENTRIES_THIS_DIR);

  /* First check the dir's own entry for consistency. */
  if (! default_entry)
    return svn_error_create (SVN_ERR_WC_ENTRY_NOT_FOUND,
                             0,
                             NULL,
                             "missing default entry",
                             pool);

  if (default_entry->version == SVN_INVALID_VERNUM)
    return svn_error_create (SVN_ERR_WC_ENTRY_MISSING_VERSION,
                             0,
                             NULL,
                             "default entry has no version number",
                             pool);

  if (! default_entry->ancestor)
    return svn_error_create (SVN_ERR_WC_ENTRY_MISSING_ANCESTRY,
                             0,
                             NULL,
                             "default entry missing ancestry",
                             pool);
  
    
  /* Then use it to fill in missing information in other entries. */
  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const char *key;
      apr_size_t keylen;
      svn_wc__entry_t *this_entry;

      apr_hash_this (hi, &key, &keylen, &this_entry);

      if (strcmp (SVN_WC__ENTRIES_THIS_DIR, key) == 0)
        continue;

      take_from_entry (default_entry, this_entry, pool);
    }

  return SVN_NO_ERROR;
}


/* Update an entry's attribute hash with potentially changed values. */
static svn_error_t   /* kff todo: return void instead? */
sync_entry (svn_wc__entry_t *entry, apr_pool_t *pool)
{
  svn_error_t *err = SVN_NO_ERROR;
  
  /* Version. */
  apr_hash_set (entry->attributes,
                SVN_WC__ENTRIES_ATTR_VERSION,
                strlen (SVN_WC__ENTRIES_ATTR_VERSION),
                svn_string_createf (pool, "%ld", entry->version));
  
  /* Ancestor. */
  apr_hash_set (entry->attributes,
                SVN_WC__ENTRIES_ATTR_ANCESTOR,
                strlen (SVN_WC__ENTRIES_ATTR_ANCESTOR),
                entry->ancestor);
  
  /* Kind. */
  if (entry->kind == svn_file_kind)
    apr_hash_set (entry->attributes,
                  SVN_WC__ENTRIES_ATTR_KIND,
                  strlen (SVN_WC__ENTRIES_ATTR_KIND),
                  NULL);
  else if (entry->kind == svn_dir_kind)
    apr_hash_set (entry->attributes,
                  SVN_WC__ENTRIES_ATTR_KIND,
                  strlen (SVN_WC__ENTRIES_ATTR_KIND),
                  svn_string_create ("dir", pool));
  else
    {
      svn_string_t *name
        = apr_hash_get (entry->attributes,
                        SVN_WC__ENTRY_ATTR_NAME,
                        strlen (SVN_WC__ENTRY_ATTR_NAME));
      
      err = svn_error_createf
        (SVN_ERR_UNKNOWN_NODE_KIND,
         0,
         NULL,
         pool,
         "entries.c:svn_wc__entries_write(): "
         "entry %s in dir %s",
         (name ? name->data : SVN_WC__ENTRIES_THIS_DIR),
         path);
    }
  
  /* Flags. */
  apr_hash_set (entry->attributes,
                SVN_WC__ENTRIES_ATTR_ADD,
                strlen (SVN_WC__ENTRIES_ATTR_ADD),
                ((entry->flags && SVN_WC__ENTRY_ADD) ?
                 svn_string_create ("true", pool) : NULL));
  apr_hash_set (entry->attributes,
                SVN_WC__ENTRIES_ATTR_DELETE,
                strlen (SVN_WC__ENTRIES_ATTR_DELETE),
                ((entry->flags && SVN_WC__ENTRY_DELETE) ?
                 svn_string_create ("true", pool) : NULL));
  
  /* Timestamp. */
  apr_hash_set (entry->attributes,
                SVN_WC__ENTRIES_ATTR_TIMESTAMP,
                strlen (SVN_WC__ENTRIES_ATTR_TIMESTAMP),
                svn_wc__time_to_string (entry->timestamp, pool));

  return err;
}


/* Fill ENTRIES according to PATH's entries file. */
svn_error_t *
read_entries (apr_hash_t *entries, svn_string_t *path, apr_pool_t *pool)
{
  svn_error_t *err;
  apr_file_t *infile = NULL;
  svn_xml_parser_t *svn_parser;
  apr_status_t apr_err;
  char buf[BUFSIZ];
  apr_size_t bytes_read;
  struct entries_accumulator *accum;

  /* Open the entries file. */
  err = svn_wc__open_adm_file (&infile, path, SVN_WC__ADM_ENTRIES,
                               APR_READ, pool);
  if (err)
    return err;

  /* Set up userData for the XML parser. */
  accum = apr_palloc (pool, sizeof (*accum));
  accum->entries = entries;
  accum->path = path;
  accum->pool = pool;

  /* Create the XML parser */
  svn_parser = svn_xml_make_parser (accum,
                                    handle_start_tag,
                                    NULL,
                                    NULL,
                                    pool);

  /* Store parser in its own userdata, so callbacks can call
     svn_xml_signal_bailout() */
  accum->parser = svn_parser;

  /* Parse the xml in infile, and write modified stream back out to
     outfile. */
  do {
    apr_err = apr_full_read (infile, buf, BUFSIZ, &bytes_read);
    if (apr_err && (apr_err != APR_EOF))
      return svn_error_create 
        (apr_err, 0, NULL, pool, "read_entries: apr_full_read choked");
    
    err = svn_xml_parse (svn_parser, buf, bytes_read, (apr_err == APR_EOF));
    if (err)
      return svn_error_quick_wrap 
        (err,
         "read_entries: xml parser failed.");
  } while (apr_err != APR_EOF);

  /* Close the entries file. */
  err = svn_wc__close_adm_file (infile, path, SVN_WC__ADM_ENTRIES, 0, pool);
  if (err)
    return err;

  /* Clean up the xml parser */
  svn_xml_free_parser (svn_parser);

  /* Fill in any implied fields. */
  err = check_defaults (entries, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entries_read (apr_hash_t **entries,
                      svn_string_t *path,
                      apr_pool_t *pool)
{
  apr_error_t *err;
  apr_hash_t *new_entries;

  new_entries = apr_make_hash (pool);

  err = read_entries (new_entries, path, pool);
  if (err)
    return err;

  *entries = new_entries;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entries_write (apr_hash_t *entries,
                       svn_string_t *path,
                       apr_pool_t *pool)
{
  svn_error_t *err = NULL, *err2 = NULL;
  svn_string_t *bigstr = NULL;
  apr_file_t *outfile = NULL;
  apr_status_t apr_err;
  svn_wc__entry_t *this_entry;
  apr_hash_index_t *hi;

  /* Open entries file for writing. */
  err = svn_wc__open_adm_file (&outfile, path, SVN_WC__ADM_ENTRIES,
                               (APR_WRITE | APR_CREATE | APR_EXCL),
                               pool);
  if (err)
    return err;

  svn_xml_make_header (&bigstr, pool);
  svn_xml_make_open_tag (&bigstr, pool, svn_xml_normal,
                         SVN_WC__ENTRIES_TOPLEVEL,
                         "xmlns",
                         svn_string_create (SVN_XML_NAMESPACE, pool),
                         NULL);

  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const char *key;
      apr_size_t keylen;
      svn_wc__entry_t *this_entry;

      apr_hash_this (hi, &key, &keylen, &this_entry);

      err = sync_entry (entry, pool);
      if (err)
        goto handle_error;

      /* Now write out the tag. */
      svn_xml_make_open_tag_hash (&bigstr,
                                  pool,
                                  svn_xml_self_closing,
                                  SVN_WC__ENTRIES_ENTRY,
                                  this_entry->attributes);
    }

  apr_err = apr_full_write (outfile, bigstr->data, bigstr->len, NULL);
  if (apr_err)
    {
      err = svn_error_createf (apr_err, 0, NULL, pool,
                               "svn_wc__entries_write: %s",
                               path->data);
      goto handle_error;
    }
      
 handle_error:
  /* Close & sync. */
  err2 = svn_wc__close_adm_file (&outfile, path, SVN_WC__ADM_ENTRIES, 1, pool);
  if (err)
    return err;
  else if (err2)
    return err2;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entry_add (apr_hash_t *entries,
                   apr_pool_t *pool,
                   svn_string_t *name,
                   svn_vernum_t version,
                   enum svn_node_kind kind,
                   int flags,
                   apr_time_t timestamp,
                   ...)
{
  svn_error_t *err;
  svn_wc__entry_t *entry = apr_pcalloc (pool, sizeof (*entry));
  svn_wc__entry_t *default_entry
    = apr_hash_get (entries, SVN_WC__ENTRIES_THIS_DIR,
                    strlen (SVN_WC__ENTRIES_THIS_DIR));

  assert (name != NULL);

  entry->version = version;
  entry->kind = kind;
  entry->flags = flags;
  entry->timestamp = timestamp;
  entry->attributes = apr_make_hash (pool);

  take_from_entry (default_entry, entry, pool);
  err = sync_entry (entries, pool);
  if (err)
    return err;

  if (apr_hash_get (entries, name->data, name->len))
    return svn_error_createf (SVN_ERR_WC_ENTRY_EXISTS,
                              0,
                              NULL,
                              pool,
                              "entries.c:svn_wc__entry_add(): %s"
                              name->data);

  apr_hash_set (entries, name->data, name->len, entry);

  return SVN_NO_ERROR;
}


/* kff todo: we shouldn't have this function in the interface, probably. */
/* Remove entry NAME from ENTRIES. */
svn_error_t *
svn_wc__entry_remove (apr_hash_t *entries,
                      svn_string_t *name)
{
  return SVN_NO_ERROR;
}



#else /* ! BRAVE_NEW_INTERFACE */

/*--------------------------------------------------------------- */

/*** ancestry ***/
svn_error_t *
svn_wc__entry_get_ancestry (svn_string_t *path,
                            svn_string_t *entry,
                            svn_string_t **ancestor_path,
                            svn_vernum_t *ancestor_ver,
                            apr_pool_t *pool)
{
  svn_string_t *ancestor;
  svn_vernum_t version;
  svn_error_t *err;
  apr_hash_t *h;

  err = svn_wc__entry_get (path, entry, &version, NULL, pool, &h);
  if (err)
    return err;

  ancestor = apr_hash_get (h,
                           SVN_WC__ENTRIES_ATTR_ANCESTOR,
                           strlen (SVN_WC__ENTRIES_ATTR_ANCESTOR));

  /* Default back to directory itself, in case anything's missing. */
  if (entry && ((! ancestor) || (version == SVN_INVALID_VERNUM)))
    {
      svn_string_t *default_ancestor;
      svn_vernum_t default_version;
      
      err = svn_wc__entry_get (path,
                               NULL,
                               &default_version,
                               NULL,
                               pool,
                               &h);
      if (err)
        return err;

      default_ancestor = apr_hash_get (h,
                                       SVN_WC__ENTRIES_ATTR_ANCESTOR,
                                       strlen (SVN_WC__ENTRIES_ATTR_ANCESTOR));
      if (! default_ancestor)
        return svn_error_create (SVN_ERR_WC_ENTRY_MISSING_ANCESTRY, 0,
                                 NULL, pool,
                                 "entry_get_ancestry: `.' has no ancestor");
      
      if (! ancestor)
        {
          ancestor = default_ancestor;
          svn_path_add_component (ancestor,
                                  entry,
                                  svn_path_repos_style,
                                  pool);
        }
      
      if (version == SVN_INVALID_VERNUM)
        version = default_version;
    }

  /* If we still don't have the information, that's an error. */
  if ((! ancestor) || (version == SVN_INVALID_VERNUM))
    return svn_error_createf (SVN_ERR_WC_ENTRY_MISSING_ANCESTRY,
                              0,
                              NULL,
                              pool,
                              "missing ancestor path or version for %s/%s",
                              path->data,
                              entry ? entry->data : "");


  *ancestor_path = ancestor;
  *ancestor_ver  = version;

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



/* Search through ATTS and fill in ATTR_HASH appropriately.
   ATTR_HASH is a hash whose keys are char *'s and values will be
   svn_string_t *'s, which will be set to point to new strings
   representing the values discovered in ATTS.

   Certain arguments will also be converted and set directly: VERSION
   and KIND.
 */
static void
get_entry_attributes (const char **atts,
                      svn_vernum_t *version,
                      enum svn_node_kind *kind,
                      apr_hash_t *attr_hash,
                      apr_pool_t *pool)
{
  const char *found_version, *found_kind;

  /* Handle version specially. */
  found_version = svn_xml_get_attr_value (SVN_WC__ENTRIES_ATTR_VERSION, atts);
  if (found_version)
    *version = (svn_vernum_t) atoi (found_version);
  else
    *version = SVN_INVALID_VERNUM;
      
  /* Handle kind specially. */
  found_kind = svn_xml_get_attr_value (SVN_WC__ENTRIES_ATTR_KIND, atts);
  *kind = 0;  /* set to a known invalid default */
  if (found_kind)
    {
      if (strcmp (found_kind, "file") == 0)
        *kind = svn_file_kind;
      else if (strcmp (found_kind, "dir") == 0)
        *kind = svn_dir_kind;
      /* someday there will be symlink kind, etc, here */
    }

  svn_xml_hash_atts_overlaying (atts, attr_hash, pool);
}


static svn_error_t *
write_entry (apr_file_t *outfile,
             svn_string_t *entryname,
             svn_vernum_t version,
             enum svn_node_kind kind,
             apr_hash_t *attributes,
             apr_pool_t *pool)
{
  apr_status_t apr_err;
  svn_string_t *entry = NULL;
  svn_string_t *verstr
    = svn_string_create (apr_psprintf (pool, "%ld", (long int) version), pool);
  svn_string_t *kindstr;

  switch (kind)
    {
    case svn_file_kind:
      kindstr = svn_string_create ("file", pool);
      break;
    case svn_dir_kind:
      kindstr = svn_string_create ("dir", pool);
      break;
    default:
      kindstr = NULL;  /* tolerate unknown kind, for forward compatibility */
    }
  
  if (entryname)
    apr_hash_set (attributes,
                  SVN_WC__ENTRIES_ATTR_NAME,
                  strlen (SVN_WC__ENTRIES_ATTR_NAME),
                  entryname);

  if (version != SVN_INVALID_VERNUM)
    apr_hash_set (attributes,
                  SVN_WC__ENTRIES_ATTR_VERSION,
                  strlen (SVN_WC__ENTRIES_ATTR_VERSION),
                  verstr);

  if (kind)
    apr_hash_set (attributes,
                  SVN_WC__ENTRIES_ATTR_KIND,
                  strlen (SVN_WC__ENTRIES_ATTR_KIND),
                  kindstr);
  
  svn_xml_make_open_tag_hash (&entry,
                              pool,
                              svn_xml_self_closing,
                              SVN_WC__ENTRIES_ENTRY,
                              attributes);

  apr_err = apr_full_write (outfile, entry->data, entry->len, NULL);
  if (apr_err)
    return svn_error_create (apr_err, 0, NULL, pool, "write_entry");

  return SVN_NO_ERROR;
}


/* Called whenever we find an <open> tag of some kind. */
static void
handle_start_tag (void *userData, const char *tagname, const char **atts)
{
  svn_wc__entry_baton_t *baton = (svn_wc__entry_baton_t *) userData;
  svn_error_t *err;

  /* We only care about the `entry' tag; all other tags, such as `xml'
     and `wc-entries', are simply written back out verbatim. */

  /* Found an entry: */
  if ((strcmp (tagname, SVN_WC__ENTRIES_ENTRY)) == 0)
    {
      /* Get the "name" of this entry */
      const char *entry
        = svn_xml_get_attr_value (SVN_WC__ENTRIES_ATTR_NAME, atts);

      /* Are we simply looping and returning each entry? */
      if (baton->looping)
        {
          /* Store the entry's name in the entrybaton */
          if (entry)
            baton->entryname = svn_string_create (entry, baton->pool);
          else
            baton->entryname = NULL;

          /* Fill in all the other fields in the entrybaton */
          get_entry_attributes (atts,
                                &(baton->version),
                                &(baton->kind),
                                baton->attributes,
                                baton->pool);

          /* And call the looper callback! */
          err = baton->looper_callback (baton->callback_baton,
                                        baton);
          if (err)
            svn_xml_signal_bailout (err, baton->parser);
          
          return;
        }

      /* We're not looping, but looking for a *particular* entry to
         set or get: */
      else
        {
          /* Nulls count as a match, because null represents the dir itself. */
          if (((entry == NULL) && (baton->entryname == NULL))
              || ((entry != NULL)
                  && (baton->entryname != NULL)
                  && ((strcmp (entry, baton->entryname->data)) == 0)))
            {
              baton->found_it = 1;
              
              if (baton->outfile) /* we're writing out a change */
                {
                  /* Maybe rewrite the tag. */
                  if (! baton->removing)
                    {

                      /* Maybe preserve the old tag too. */
                      if (baton->allow_duplicate)
                        {
                          apr_hash_t *pristine_att_hash
                            = apr_make_hash (baton->pool);
                          svn_xml_hash_atts_overlaying (atts,
                                                        pristine_att_hash,
                                                        baton->pool);
                          err = write_entry (baton->outfile,
                                             baton->entryname,
                                             SVN_INVALID_VERNUM,
                                             0, /* kff todo: invalid_kind? */
                                             pristine_att_hash,
                                             baton->pool);
                          if (err)
                            {
                              svn_xml_signal_bailout (err, baton->parser);
                              return;
                            }
                        }

                      err = write_entry (baton->outfile,
                                         baton->entryname,
                                         baton->version,
                                         baton->kind,
                                         baton->attributes,
                                         baton->pool);
                      if (err)
                        {
                          svn_xml_signal_bailout (err, baton->parser);
                          return;
                        }
                    }
                }

              else  /* just reading attribute values, not writing a new tag */
                get_entry_attributes (atts,
                                      &(baton->version),
                                      &(baton->kind),
                                      baton->attributes,
                                      baton->pool);
            }

          else  /* An entry tag, but not the one we're looking for. */
            goto write_it_back_out;
        }
    }
    
  else  /* This is some tag other than `entry', preserve it unchanged.  */
    {
    write_it_back_out:
      if (baton->outfile)
        {
          enum svn_xml_open_tag_style tag_style = svn_xml_self_closing;
          apr_status_t apr_err;
          svn_string_t *dup = NULL;

          if (strcmp (tagname, SVN_WC__ENTRIES_TOPLEVEL) == 0)
            tag_style = svn_xml_normal;

          svn_xml_make_open_tag_hash (&dup,
                                      baton->pool,
                                      tag_style,
                                      tagname,
                                      svn_xml_make_att_hash
                                      (atts, baton->pool));
          
          apr_err = apr_full_write (baton->outfile, dup->data, dup->len, NULL);
          if (apr_err)
            {
              err = svn_error_create (apr_err, 0, NULL, baton->pool,
                                      "entries.c: handle_start_tag");
              svn_xml_signal_bailout (err, baton->parser);
              return;
            }
        }
    }
}


/* Called whenever we find a </close> tag of some kind. */
static void
handle_end_tag (void *userData, const char *tagname)
{
  svn_wc__entry_baton_t *baton = (svn_wc__entry_baton_t *) userData;
  svn_error_t *err;

  if ((strcmp (tagname, SVN_WC__ENTRIES_TOPLEVEL)) == 0)
    {
      if (baton->outfile)
        {
          apr_status_t apr_err;
          svn_string_t *close_tag = NULL;

          /* If this entry didn't exist before, then add it now. */
          if (! baton->found_it)
            {
              err = write_entry (baton->outfile,
                                 baton->entryname,
                                 baton->version,
                                 baton->kind,
                                 baton->attributes,
                                 baton->pool);
              if (err)
                {
                  svn_xml_signal_bailout (err, baton->parser);
                  return;
                }
            }

          /* Now close off the file. */
          svn_xml_make_close_tag (&close_tag,
                                  baton->pool,
                                  tagname);

          apr_err = apr_full_write (baton->outfile,
                                    close_tag->data,
                                    close_tag->len,
                                    NULL);
          if (apr_err)
            {
              err = svn_error_create (apr_err, 0, NULL, baton->pool,
                                      "entries.c: handle_end_tag");
              svn_xml_signal_bailout (err, baton->parser);
            }
        }
    }
}


/* Code chunk shared by svn_wc__{get,set}_entry()
   
   Parses xml in BATON->infile using BATON as userdata. */
svn_error_t *
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
         "do_parse: apr_full_read choked");
    
    err = svn_xml_parse (svn_parser, buf, bytes_read, (apr_err == APR_EOF));
    if (err)
      return svn_error_quick_wrap 
        (err,
         "do_parse:  xml parser failed.");
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
          svn_string_t *entryname,
          svn_boolean_t allow_duplicate,
          svn_vernum_t version,
          svn_vernum_t *version_receiver,
          enum svn_node_kind kind,
          enum svn_node_kind *kind_receiver,
          svn_boolean_t removing,
          svn_boolean_t setting,
          apr_hash_t *attributes)
{
  svn_error_t *err;
  apr_file_t *infile = NULL;
  apr_file_t *outfile = NULL;

  svn_wc__entry_baton_t *baton 
    = apr_pcalloc (pool, sizeof (svn_wc__entry_baton_t));

  assert (! (setting && removing));
  assert (! ((entryname == NULL) && removing));

  /* Open current entries file for reading */
  err = svn_wc__open_adm_file (&infile, path,
                               SVN_WC__ADM_ENTRIES,
                               APR_READ, pool);
  if (err)
    return err;

  if (setting || removing)
    {
      apr_status_t apr_err;
      svn_string_t *front = NULL;

      /* Open a new `tmp/entries' file for writing */
      err = svn_wc__open_adm_file (&outfile, path,
                                   SVN_WC__ADM_ENTRIES,
                                   (APR_WRITE | APR_CREATE | APR_EXCL), pool);
      if (err)
        return err;

      svn_xml_make_header (&front, pool);
      apr_err = apr_full_write (outfile, front->data, front->len, NULL);
      if (apr_err)
        {
          apr_close (outfile);
          return svn_error_create (apr_err, 0, NULL, baton->pool,
                                   "entries.c: do_entry");
        }
    }

  /* Fill in the userdata structure */
  baton->pool              = pool;
  baton->infile            = infile;
  baton->outfile           = outfile;
  baton->removing          = removing;
  baton->entryname         = entryname;
  baton->version           = version;
  baton->kind              = kind;
  baton->allow_duplicate   = allow_duplicate;
  baton->attributes        = attributes;

  /* Set the att. */
  err = do_parse (baton);
  if (err)
    return err;

  /* Close infile */
  err = svn_wc__close_adm_file (infile, path,
                                SVN_WC__ADM_ENTRIES, 0, pool);
  if (err)
    return err;
  
  if (setting || removing)
    {
      /* Close the outfile and *sync* it, so it replaces the original
         infile. */
      err = svn_wc__close_adm_file (outfile, path,
                                    SVN_WC__ADM_ENTRIES, 1, pool);
      if (err)
        return err;
    }
  else if ((! baton->looping) && (! setting) && (! baton->found_it))
    {
      return svn_error_createf (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
                                "entries.c do_entry(): entry %s not found",
                                entryname->data);
    }
  else
    {
      if (version_receiver)
        *version_receiver = baton->version;
      if (kind_receiver)
        *kind_receiver = baton->kind;
    }

  return SVN_NO_ERROR;
}




svn_error_t *
svn_wc__entry_set (svn_string_t *path,
                   svn_string_t *entryname,
                   svn_vernum_t version,
                   enum svn_node_kind kind,
                   apr_pool_t *pool,
                   ...)
{
  svn_error_t *err;
  apr_hash_t *att_hash;
  va_list ap;

  /* Convert the va_list into a hash of attributes */
  va_start (ap, pool);
  att_hash = svn_xml_ap_to_hash (ap, pool);
  va_end (ap);
  
  err = do_entry (path, pool, entryname,
                  0, /* kff todo: if this func goes away, this won't matter */
                  version, NULL,
                  kind, NULL,
                  0, /* not removing */
                  1, /* setting */
                  att_hash);

  return err;
}



/* kff todo: I'm not sure we need both entry_merge and entry_set.
   The merging behavior is always desired, right?  Think about
   this... */
svn_error_t *
svn_wc__entry_merge (svn_string_t *path,
                     svn_string_t *entryname,
                     svn_vernum_t version,
                     enum svn_node_kind kind,
                     apr_pool_t *pool,
                     ...)
{
  svn_error_t *err;
  va_list ap;
  const char *key;
  svn_boolean_t allow_duplicate = 0;
  svn_boolean_t deleting_an_added_file = 0;

  svn_vernum_t existing_version;
  enum svn_node_kind existing_kind;
  apr_hash_t *existing_hash;

  /* First be sure to GET all information about the entry (assuming it
     already exists) */
  err = svn_wc__entry_get (path, entryname,
                           &existing_version, &existing_kind,
                           pool,
                           &existing_hash);
  if (err && (err->apr_err != SVN_ERR_WC_ENTRY_NOT_FOUND))
    return err;
  else if (err && (err->apr_err == SVN_ERR_WC_ENTRY_NOT_FOUND))
    /* No previous entry to merge with, so we must make a fresh hash. */
    existing_hash = apr_make_hash (pool);
  else /* There is an entry, so merge with it, overriding. */
    {
      if (version == SVN_INVALID_VERNUM)
        version = existing_version;
      if (! kind)
        kind = existing_kind;
      
      /* Walk the va_list, storing attributes into the existing hash */
      va_start (ap, pool);
      while ((key = va_arg (ap, char *)) != NULL)
        {
          svn_string_t *val;
          
          /* Treat add and delete specially. */
          if ((strcmp (key, SVN_WC__ENTRIES_ATTR_ADD) == 0)
              && (apr_hash_get (existing_hash, SVN_WC__ENTRIES_ATTR_DELETE,
                                strlen (SVN_WC__ENTRIES_ATTR_DELETE))))
            {
              apr_hash_set (existing_hash, SVN_WC__ENTRIES_ATTR_DELETE,
                            strlen (SVN_WC__ENTRIES_ATTR_DELETE), NULL);
              allow_duplicate = 1;
            }
          else if ((strcmp (key, SVN_WC__ENTRIES_ATTR_DELETE) == 0)
                   && (apr_hash_get (existing_hash, SVN_WC__ENTRIES_ATTR_ADD,
                                     strlen (SVN_WC__ENTRIES_ATTR_ADD))))
            {
              deleting_an_added_file = 1;
            }
          
          val = va_arg (ap, svn_string_t *);
          apr_hash_set (existing_hash, key, strlen (key), val);
        }
      va_end (ap);
    }

  if (deleting_an_added_file)
    err = do_entry (path, pool, entryname,
                    0,
                    version, NULL,
                    kind, NULL,
                    1, /* removing */
                    0, /* not setting */
                    existing_hash);
  else
    err = do_entry (path, pool, entryname,
                    allow_duplicate,
                    version, NULL,
                    kind, NULL,
                    0, /* not removing */
                    1, /* setting */
                    existing_hash);

  return err;
}


svn_error_t *
svn_wc__entry_get (svn_string_t *path,
                   svn_string_t *entryname,
                   svn_vernum_t *version,
                   enum svn_node_kind *kind,
                   apr_pool_t *pool,
                   apr_hash_t **hash)
{
  svn_error_t *err;
  apr_hash_t *ht = apr_make_hash (pool);

  err = do_entry (path, pool, entryname,
                  0,
                  SVN_INVALID_VERNUM, version,
                  0, kind,
                  0, /* not removing */
                  0, /* not setting */
                  ht);
  if (err)
    {
      if (hash)
        *hash = NULL;
      return err;
    }

  if (hash) 
    *hash = ht;

  return SVN_NO_ERROR;
}


/* Remove ENTRYNAME from PATH's `entries' file. */
svn_error_t *svn_wc__entry_remove (svn_string_t *path,
                                   svn_string_t *entryname,
                                   apr_pool_t *pool)
{
  svn_error_t *err;

  /* kff todo: this whole do_entry() interface has become a burden.
     Rethink this, it should be a natural fix. */
  err = do_entry (path,
                  pool,
                  entryname,
                  0,
                  SVN_INVALID_VERNUM,  /* irrelevant */
                  NULL,                /* irrelevant */
                  0,                   /* irrelevant */
                  NULL,                /* irrelevant */
                  1,                   /* removing */
                  0,                   /* not setting */
                  NULL);               /* irrelevant */
  if (err)
    return err;

  return SVN_NO_ERROR;
}


#endif /* BRAVE_NEW_INTERFACE */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
