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


/*--------------------------------------------------------------- */

/*** reading and writing the entries file ***/

struct entries_accumulator
{
  /* Keys are entry names, vals are (struct svn_wc__entry_t *)'s. */
  apr_hash_t *entries; 

  /* The dir whose entries file this is. */
  svn_string_t *path;

  /* The parser that's parsing it, for signal_expat_bailout(). */
  svn_xml_parser_t *parser;

  /* Don't leave home without one. */
  apr_pool_t *pool;
};


static svn_wc__entry_t *
alloc_entry (apr_pool_t *pool)
{
  svn_wc__entry_t *entry = apr_pcalloc (pool, sizeof (*entry));
  entry->version    = SVN_INVALID_VERNUM;
  entry->kind       = svn_invalid_kind;
  entry->attributes = apr_make_hash (pool);
  return entry;
}


/* Called whenever we find an <open> tag of some kind. */
static void
handle_start_tag (void *userData, const char *tagname, const char **atts)
{
  struct entries_accumulator *accum = userData;

  /* We only care about the `entry' tag; all other tags, such as `xml'
     and `wc-entries', are ignored. */
  if ((strcmp (tagname, SVN_WC__ENTRIES_ENTRY)) == 0)
    {
      svn_wc__entry_t *entry = alloc_entry (accum->pool);
      entry->attributes = svn_xml_make_att_hash (atts, accum->pool);

      /* Find the name and set up the entry under that name. */
      {
        svn_string_t *name
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_NAME, APR_HASH_KEY_STRING);
        char *nstr = name ? name->data : SVN_WC__ENTRIES_THIS_DIR;
        apr_size_t len = name ? name->len : strlen (SVN_WC__ENTRIES_THIS_DIR);

        apr_hash_set (accum->entries, nstr, len, entry);
      }

      /* Attempt to set version (resolve_to_defaults may do it later, too) */
      {
        svn_string_t *version_str
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_VERSION, APR_HASH_KEY_STRING);

        if (version_str)
          entry->version = (svn_vernum_t) atoi (version_str->data);
        else
          entry->version = SVN_INVALID_VERNUM;
      }

      /* Attempt to set up ancestor path (again, see resolve_to_defaults). */
      {
        entry->ancestor
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_ANCESTOR, APR_HASH_KEY_STRING);
      }

      /* Set up kind. */
      {
        svn_string_t *kindstr
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_KIND, APR_HASH_KEY_STRING);

        if ((! kindstr) || (strcmp (kindstr->data, "file") == 0))
          entry->kind = svn_file_kind;
        else if (strcmp (kindstr->data, "dir") == 0)
          entry->kind = svn_dir_kind;
        else
          {
            svn_string_t *name
              = apr_hash_get (entry->attributes,
                              SVN_WC__ENTRIES_ATTR_NAME, APR_HASH_KEY_STRING);

            svn_xml_signal_bailout 
              (svn_error_createf (SVN_ERR_UNKNOWN_NODE_KIND,
                                  0,
                                  NULL,
                                  accum->pool,
                                  "entries.c:handle_start_tag(): "
                                  "entry %s in dir %s",
                                  (name ?
                                   name->data : SVN_WC__ENTRIES_THIS_DIR),
                                  accum->path->data),
               accum->parser);
          }
      }

      /* Attempt to set up timestamp. */
      {
        svn_string_t *timestr
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_TIMESTAMP, APR_HASH_KEY_STRING);

        if (timestr)
          entry->timestamp = svn_wc__string_to_time (timestr);
      }

      /* Look for any action flags. */
      {
        svn_string_t *addstr
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_ADD, APR_HASH_KEY_STRING);
        svn_string_t *delstr
          = apr_hash_get (entry->attributes,
                          SVN_WC__ENTRIES_ATTR_DELETE, APR_HASH_KEY_STRING);

        /* Technically, the value has to be "true".  But we only have
           these attributes at all when they have values of "true", so
           let's not go overboard on the paranoia here. */
        if (addstr)
          entry->flags |= SVN_WC__ENTRY_ADD;
        if (delstr)
          entry->flags |= SVN_WC__ENTRY_DELETE;
      }
    }
}


/* Use entry SRC to fill in blank portions of entry DST.  SRC itself
   may not have any blanks, of course, and it may not be the current
   dir entry itself (i.e., ".").
   Typically, SRC is a parent directory's own entry, and DST is some
   child in that directory. */
static void
take_from_entry (svn_wc__entry_t *src, svn_wc__entry_t *dst, apr_pool_t *pool)
{
  /* Inherits parent's version if don't have a version of one's own,
     unless this is a subdirectory. */
  if ((dst->version == SVN_INVALID_VERNUM) && (dst->kind != svn_dir_kind))
    dst->version = src->version;
  
  if (! dst->ancestor)
    {
      svn_string_t *name = apr_hash_get (dst->attributes,
                                         SVN_WC__ENTRIES_ATTR_NAME,
                                         APR_HASH_KEY_STRING);
      dst->ancestor = svn_string_dup (src->ancestor, pool);
      svn_path_add_component (dst->ancestor, name,
                              svn_path_repos_style);
    }
}


/* Resolve any missing information in ENTRIES by deducing from the
   directory's own entry (which must already be present in ENTRIES). */
static svn_error_t *
resolve_to_defaults (apr_hash_t *entries, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_wc__entry_t *default_entry
    = apr_hash_get (entries, SVN_WC__ENTRIES_THIS_DIR, APR_HASH_KEY_STRING);

  /* First check the dir's own entry for consistency. */
  if (! default_entry)
    return svn_error_create (SVN_ERR_WC_ENTRY_NOT_FOUND,
                             0,
                             NULL,
                             pool,
                             "missing default entry");

  if (default_entry->version == SVN_INVALID_VERNUM)
    return svn_error_create (SVN_ERR_WC_ENTRY_MISSING_VERSION,
                             0,
                             NULL,
                             pool,
                             "default entry has no version number");

  if (! default_entry->ancestor)
    return svn_error_create (SVN_ERR_WC_ENTRY_MISSING_ANCESTRY,
                             0,
                             NULL,
                             pool,
                             "default entry missing ancestry");
  
    
  /* Then use it to fill in missing information in other entries. */
  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t keylen;
      void *val;
      svn_wc__entry_t *this_entry;

      apr_hash_this (hi, &key, &keylen, &val);
      this_entry = val;

      if (strcmp (SVN_WC__ENTRIES_THIS_DIR, (char *) key) == 0)
        continue;

      take_from_entry (default_entry, this_entry, pool);
    }

  return SVN_NO_ERROR;
}


/* Update an entry's attribute hash according to its structure fields,
   which should always dominate the hash when the two differ. */
static void
sync_entry (svn_wc__entry_t *entry, apr_pool_t *pool)
{
  /* Version. */
  if (entry->version != SVN_INVALID_VERNUM)
    apr_hash_set (entry->attributes,
                  SVN_WC__ENTRIES_ATTR_VERSION, APR_HASH_KEY_STRING,
                  svn_string_createf (pool, "%ld", entry->version));
  
  /* Ancestor. */
  apr_hash_set (entry->attributes,
                SVN_WC__ENTRIES_ATTR_ANCESTOR, APR_HASH_KEY_STRING,
                entry->ancestor);
  
  /* Kind. */
  if (entry->kind == svn_dir_kind)
    apr_hash_set (entry->attributes,
                  SVN_WC__ENTRIES_ATTR_KIND, APR_HASH_KEY_STRING,
                  svn_string_create ("dir", pool));
  else if (entry->kind != svn_invalid_kind)  /* default to file kind */
    apr_hash_set (entry->attributes,
                  SVN_WC__ENTRIES_ATTR_KIND, APR_HASH_KEY_STRING,
                  NULL);
  
  /* Flags. */
  if (entry->flags & SVN_WC__ENTRY_CLEAR)
    {
      apr_hash_set (entry->attributes, SVN_WC__ENTRIES_ATTR_ADD,
                    APR_HASH_KEY_STRING, NULL);
      apr_hash_set (entry->attributes, SVN_WC__ENTRIES_ATTR_DELETE,
                    APR_HASH_KEY_STRING, NULL);
    }
  else  /* don't lose any existing flags, but maybe set some new ones */
    {
      if (entry->flags & SVN_WC__ENTRY_ADD)
        apr_hash_set (entry->attributes,
                      SVN_WC__ENTRIES_ATTR_ADD, APR_HASH_KEY_STRING,
                      svn_string_create ("true", pool));
      if (entry->flags & SVN_WC__ENTRY_DELETE)
        apr_hash_set (entry->attributes,
                      SVN_WC__ENTRIES_ATTR_DELETE, APR_HASH_KEY_STRING,
                      svn_string_create ("true", pool));
    }
  
  /* Timestamp. */
  if (entry->timestamp)
    {
      apr_hash_set (entry->attributes,
                    SVN_WC__ENTRIES_ATTR_TIMESTAMP, APR_HASH_KEY_STRING,
                    svn_wc__time_to_string (entry->timestamp, pool));
    }
}


/* Fill ENTRIES according to PATH's entries file. */
static svn_error_t *
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

  /* Parse. */
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
  err = resolve_to_defaults (entries, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entries_read (apr_hash_t **entries,
                      svn_string_t *path,
                      apr_pool_t *pool)
{
  svn_error_t *err;
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
      const void *key;
      apr_size_t keylen;
      void *val;
      svn_wc__entry_t *this_entry;

      /* Get the entry and make sure its attributes are up-to-date. */
      apr_hash_this (hi, &key, &keylen, &val);
      this_entry = val;
      sync_entry (this_entry, pool);

      /* Append the entry onto the accumulating string. */
      svn_xml_make_open_tag_hash (&bigstr,
                                  pool,
                                  svn_xml_self_closing,
                                  SVN_WC__ENTRIES_ENTRY,
                                  this_entry->attributes);
    }

  svn_xml_make_close_tag (&bigstr, pool, SVN_WC__ENTRIES_TOPLEVEL);

  apr_err = apr_full_write (outfile, bigstr->data, bigstr->len, NULL);
  if (apr_err)
    err = svn_error_createf (apr_err, 0, NULL, pool,
                             "svn_wc__entries_write: %s",
                             path->data);
      
  /* Close & sync. */
  err2 = svn_wc__close_adm_file (outfile, path, SVN_WC__ADM_ENTRIES, 1, pool);
  if (err)
    return err;
  else if (err2)
    return err2;

  return SVN_NO_ERROR;
}


/* Create or modify an entry NAME in ENTRIES, using the arguments given. */
static void
stuff_entry_v (apr_hash_t *entries,
               svn_string_t *name,
               svn_vernum_t version,
               enum svn_node_kind kind,
               int flags,
               apr_time_t timestamp,
               apr_pool_t *pool,
               va_list ap)
{
  char *key;
  struct svn_wc__entry_t *entry
    = apr_hash_get (entries, name->data, name->len);

  assert (name != NULL);

  if (! entry)
    entry = alloc_entry (pool);

  /* Set up the explicit attributes. */
  if (version != SVN_INVALID_VERNUM)
    entry->version = version;
  if (kind != svn_invalid_kind)
    entry->kind = kind;
  if (timestamp)
    entry->timestamp = timestamp;
  entry->flags |= flags;

  /* Do any other attributes. */
  while ((key = va_arg (ap, char *)) != NULL)
    {
      svn_string_t *val = va_arg (ap, svn_string_t *);
      assert (val != NULL);
      apr_hash_set (entry->attributes, key, APR_HASH_KEY_STRING, val);
    }

  /* The entry's name is an attribute, too. */
  apr_hash_set (entry->attributes,
                SVN_WC__ENTRIES_ATTR_NAME,
                APR_HASH_KEY_STRING,
                name);

  /* Absorb defaults from the parent dir, if any. */
  {
    svn_wc__entry_t *default_entry
      = apr_hash_get (entries, SVN_WC__ENTRIES_THIS_DIR, APR_HASH_KEY_STRING);
    if (default_entry)
      take_from_entry (default_entry, entry, pool);
  }

  /* Make attribute hash reflect the explicit attributes. */
  sync_entry (entry, pool);

  /* Make sure the entry exists in the entries hash.  Possibly it
     already did, in which case this could have been skipped, but what
     the heck. */
  apr_hash_set (entries, name->data, name->len, entry);
}


svn_error_t *
svn_wc__entry_add (apr_hash_t *entries,
                   svn_string_t *name,
                   svn_vernum_t version,
                   enum svn_node_kind kind,
                   int flags,
                   apr_time_t timestamp,
                   apr_pool_t *pool,
                   ...)
{
  struct svn_wc__entry_t *entry
    = apr_hash_get (entries, name->data, name->len);

  if (entry)
    {
      return svn_error_createf (SVN_ERR_WC_ENTRY_EXISTS,
                                0,
                                NULL,
                                pool,
                                "entries.c:svn_wc__entry_add(): %s",
                                name->data);
    }
  else
    {
      va_list ap;
      va_start (ap, pool);
      stuff_entry_v (entries, name, version, kind, flags, timestamp, pool, ap);
      va_end (ap);
      return SVN_NO_ERROR;
    }
}


/* kff todo: we shouldn't have this function in the interface, probably. */
void
svn_wc__entry_remove (apr_hash_t *entries,
                      svn_string_t *name)
{
  apr_hash_set (entries, name->data, name->len, NULL);
}


svn_error_t *
svn_wc__entry_merge_sync (svn_string_t *path,
                          svn_string_t *name,
                          svn_vernum_t version,
                          enum svn_node_kind kind,
                          int flags,
                          apr_time_t timestamp,
                          apr_pool_t *pool,
                          ...)
{
  svn_error_t *err;
  apr_hash_t *entries = NULL;
  va_list ap;

  err = svn_wc__entries_read (&entries, path, pool);
  if (err)
    return err;
  
  if (name == NULL)
    name = svn_string_create (SVN_WC__ENTRIES_THIS_DIR, pool);

  va_start (ap, pool);
  stuff_entry_v (entries, name, version, kind, flags, timestamp, pool, ap);
  va_end (ap);
  
  err = svn_wc__entries_write (entries, path, pool);
  if (err)
    return err;

  return SVN_NO_ERROR;
}



/* Utility: return a duplicate of ENTRY object allocated in POOL. */
svn_wc__entry_t *
svn_wc__entry_dup (svn_wc__entry_t *entry, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_wc__entry_t *dupentry = apr_pcalloc (pool, sizeof(*dupentry));

  dupentry->version    = entry->version;
  dupentry->ancestor   = svn_string_dup (entry->ancestor, pool);
  dupentry->kind       = entry->kind;
  dupentry->flags      = entry->flags;
  dupentry->timestamp  = entry->timestamp;

  dupentry->attributes = apr_make_hash (pool);

  /* Now the hard part:  copying one hash to another! */

  for (hi = apr_hash_first (entry->attributes); hi; hi = apr_hash_next (hi))
    {
      const void *k;
      apr_size_t klen;
      void *v;

      const char *key;
      svn_string_t *val;

      svn_string_t *new_keystring, *new_valstring;

      /* Get a hash key and value */
      apr_hash_this (hi, &k, &klen, &v);
      key = (const char *) k;
      val = (svn_string_t *) v;

      /* Allocate two *new* svn_string_t's from them, out of POOL. */
      new_keystring = svn_string_ncreate (key, klen, pool);
      new_valstring = svn_string_dup (val, pool);

      /* Store in *new* hashtable */
      apr_hash_set (dupentry->attributes,
                    new_keystring->data, new_keystring->len,
                    new_valstring);
    }

  return dupentry;
}







#if 0
/*** Recursion on entries. ***/

/* todo: this is the right idea, but it doesn't handle two situations
 * well right now.  Superdirectories are problematic:
 *
 *   svn commit ../../foo.c ../baz/bar/blah.c
 *
 * and sibling files can result in redundant descents:
 *
 *   svn commit bar/baz/blim.c bar/baz/bloo.c
 *
 * The fix, especially for the latter, involves returning something
 * other than just a hash of paths.  Instead, we'll have to turn the
 * hash into a hash of directory paths, where a null value means
 * recurse on everyone in the directory, and a non-null value is a
 * list/hash of filenames *in that directory* to care about.
 *
 * Fairly easy to turn the below into that, luckily.
 *
 * -------------------------------------------------------------------
 * Recurse on the versioned parts of a working copy tree, starting at
 * PATH.
 *
 * Each time a directory is entered, ENTER_DIR is called with the
 * directory's path and the BATON as arguments.
 *
 * Each time a directory is left, LEAVE_DIR is called with the
 * directory's path and the BATON as arguments.
 *
 * Each time a file is seen, HANDLE_FILE is called with the parent
 * directory, the file's basename, and the BATON as arguments.
 *
 * If NAMED_TARGETS is non-null, then those functions are only invoked
 * on directories and files whose names are included (perhaps
 * implicitly) in NAMED_TARGETS:
 *
 * Each key in NAMED_TARGETS is a path to a file or directory, and the
 * value is the (svn_string_t *) corresponding to that path (this is
 * done for convenience).  The goal of NAMED_TARGETS is to reflect the
 * behavior of svn on the command line.  For example, if you invoke
 *
 *    svn commit foo bar/baz/blim.c blah.c
 *
 * the commit should 
 *
 *    1. descend into foo (which is a directory), calling ENTER_DIR
 *       and LEAVE_DIR on foo itself, and calling those two and
 *       HANDLE_FILE appropriately depending on what it finds
 *       underneath foo,
 *
 *    2. call ENTER_DIR and LEAVE_DIR on every intermediate dir
 *       leading up to blim.c, and call HANDLE_FILE on blim.c itself,
 *
 *    3. call handle_file on blah.c
 *
 * In order for that to happen with depth-firstness observed and no
 * redundant entering or leaving of directories, the NAMED_TARGETS
 * hash undergoes the following treatment:
 *
 * Every path P in NAMED_TARGETS is checked to make sure that a parent
 * path of P is not also in NAMED_TARGETS.  If P does have a parent, P
 * is removed from NAMED_TARGETS, because recursion on the parent will
 * be sufficient to reach P anyway.
 *
 * After this, there will be no two paths with a parent/descendant
 * relationship in P -- all relationships will be sibling or cousin.
 *
 * Once NAMED_TARGETS is free of redundancies, recursion happens on
 * each path P in NAMED_TARGETS like so:
 *
 *    ENTER_DIR is called on the first component of P
 *      [ENTER_DIR is called on the first/second component of P]
 *        [ENTER_DIR is called on the first/second/third component of P]
 *          [...]
 *            [If P's last component is a file, then HANDLE_FILE is
 *            invoked on that file only.  Else if P's last component
 *            is a directory, then we recurse on every entry in that
 *            directory, calling HANDLE_FILE and/or {ENTER,LEAVE}_DIR
 *            as appropriate.]
 *          [...]
 *        [LEAVE_DIR is called on the first/second/third component of P]
 *      [LEAVE_DIR is called on the first/second component of P]
 *    LEAVE_DIR is called on the first component of P
 */
static void
svn_wc__compose_paths (apr_hash_t *paths, apr_pool_t *pool)
{
  apr_hash_index_t *hi;

  /* First, iterate over the hash canonicalizing paths. */
  for (hi = apr_hash_first (paths); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t keylen;
      void *val;
      svn_string_t *path;

      apr_hash_this (hi, &key, &keylen, &val);
      path = val;

      apr_hash_set (paths, key, keylen, NULL);
      svn_path_canonicalize (path, svn_path_local_style);
      apr_hash_set (paths, path->data, path->len, path);
    }

  /* Now, iterate over the hash removing redundancies. */
  for (hi = apr_hash_first (paths); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t keylen;
      void *val;
      svn_string_t *path;

      apr_hash_this (hi, &key, &keylen, &val);
      path = val;

      /* Untelescope path, checking at each stage to see if the new,
         shorter parent path is already in the hash.  If it is, remove
         the original path from the hash. */
      {
        svn_string_t *shrinking = svn_string_dup (path, pool);
        for (svn_path_remove_component (shrinking, svn_path_local_style);
             (! svn_string_isempty (shrinking));
             svn_path_remove_component (shrinking, svn_path_local_style))
          {
            if (apr_hash_get (paths, shrinking->data, shrinking->len))
              apr_hash_set (paths, path->data, path->len, NULL);
          }
      }
    }
}
#endif /* 0 */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
