/*
 * entries.c :  manipulating the administrative `entries' file.
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
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
  char *initial_revstr = apr_psprintf (pool, "%d", 0);

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
     revision and default ancestry are present as xml attributes. */
  svn_xml_make_open_tag 
    (&accum,
     pool,
     svn_xml_self_closing,
     SVN_WC__ENTRIES_ENTRY,
     SVN_WC_ENTRY_ATTR_KIND,
     svn_string_create (SVN_WC__ENTRIES_ATTR_DIR_STR, pool), 
     SVN_WC_ENTRY_ATTR_REVISION,
     svn_string_create (initial_revstr, pool),
     SVN_WC_ENTRY_ATTR_ANCESTOR,
     ancestor_path,
     NULL);

  /* Close the top-level form. */
  svn_xml_make_close_tag (&accum,
                          pool,
                          SVN_WC__ENTRIES_TOPLEVEL);

  apr_err = apr_file_write_full (f, accum->data, accum->len, NULL);
  if (apr_err)
    {
      apr_file_close (f);
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
  /* Keys are entry names, vals are (struct svn_wc_entry_t *)'s. */
  apr_hash_t *entries; 

  /* The dir whose entries file this is. */
  svn_string_t *path;

  /* The parser that's parsing it, for signal_expat_bailout(). */
  svn_xml_parser_t *parser;

  /* Don't leave home without one. */
  apr_pool_t *pool;
};


static svn_wc_entry_t *
alloc_entry (apr_pool_t *pool)
{
  svn_wc_entry_t *entry = apr_pcalloc (pool, sizeof (*entry));
  entry->revision   = SVN_INVALID_REVNUM;
  entry->kind       = svn_node_none;
  entry->attributes = apr_hash_make (pool);
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
      svn_wc_entry_t *entry = alloc_entry (accum->pool);
      entry->attributes = svn_xml_make_att_hash (atts, accum->pool);

      /* Find the name and set up the entry under that name. */
      {
        svn_string_t *name
          = apr_hash_get (entry->attributes,
                          SVN_WC_ENTRY_ATTR_NAME, APR_HASH_KEY_STRING);
        const char *nstr = name ? name->data : SVN_WC_ENTRY_THIS_DIR;
        apr_size_t len = name ? name->len : strlen (SVN_WC_ENTRY_THIS_DIR);

        apr_hash_set (accum->entries, nstr, len, entry);
      }

      /* Attempt to set revision (resolve_to_defaults may do it later, too) */
      {
        svn_string_t *revision_str
          = apr_hash_get (entry->attributes,
                          SVN_WC_ENTRY_ATTR_REVISION, APR_HASH_KEY_STRING);

        if (revision_str)
          entry->revision = (svn_revnum_t) atoi (revision_str->data);
        else
          entry->revision = SVN_INVALID_REVNUM;
      }

      /* Attempt to set up ancestor path (again, see resolve_to_defaults). */
      {
        entry->ancestor
          = apr_hash_get (entry->attributes,
                          SVN_WC_ENTRY_ATTR_ANCESTOR, APR_HASH_KEY_STRING);
      }

      /* Set up kind. */
      {
        svn_string_t *kindstr
          = apr_hash_get (entry->attributes,
                          SVN_WC_ENTRY_ATTR_KIND, APR_HASH_KEY_STRING);

        if ((! kindstr)
            || (strcmp (kindstr->data, SVN_WC__ENTRIES_ATTR_FILE_STR) == 0))
          entry->kind = svn_node_file;
        else if (strcmp (kindstr->data, SVN_WC__ENTRIES_ATTR_DIR_STR) == 0)
          entry->kind = svn_node_dir;
        else
          {
            svn_string_t *name
              = apr_hash_get (entry->attributes,
                              SVN_WC_ENTRY_ATTR_NAME, APR_HASH_KEY_STRING);

            svn_xml_signal_bailout 
              (svn_error_createf (SVN_ERR_UNKNOWN_NODE_KIND,
                                  0,
                                  NULL,
                                  accum->pool,
                                  "entries.c:handle_start_tag(): "
                                  "entry %s in dir %s",
                                  (name ?
                                   name->data : SVN_WC_ENTRY_THIS_DIR),
                                  accum->path->data),
               accum->parser);
          }
      }

      /* Attempt to set up timestamps. */
      {
        svn_string_t *text_timestr, *prop_timestr;

        text_timestr = apr_hash_get (entry->attributes,
                                     SVN_WC_ENTRY_ATTR_TEXT_TIME,
                                     APR_HASH_KEY_STRING);
        if (text_timestr)
          entry->text_time = svn_wc__string_to_time (text_timestr);

        prop_timestr = apr_hash_get (entry->attributes,
                                     SVN_WC_ENTRY_ATTR_PROP_TIME,
                                     APR_HASH_KEY_STRING);
        if (prop_timestr)
          entry->prop_time = svn_wc__string_to_time (prop_timestr);        
      }

      /* Look for any action flags. */
      {
        svn_string_t *addstr
          = apr_hash_get (entry->attributes,
                          SVN_WC_ENTRY_ATTR_ADD, APR_HASH_KEY_STRING);
        svn_string_t *delstr
          = apr_hash_get (entry->attributes,
                          SVN_WC_ENTRY_ATTR_DELETE, APR_HASH_KEY_STRING);
        svn_string_t *conflictstr
          = apr_hash_get (entry->attributes,
                          SVN_WC_ENTRY_ATTR_CONFLICT, APR_HASH_KEY_STRING);
        

        /* Technically, the value has to be "true".  But we only have
           these attributes at all when they have values of "true", so
           let's not go overboard on the paranoia here. */
        if (addstr)
          entry->state |= SVN_WC_ENTRY_ADDED;
        if (delstr)
          entry->state |= SVN_WC_ENTRY_DELETED;
        if (conflictstr)
          entry->state |= SVN_WC_ENTRY_CONFLICTED;
      }
    }
}


/* Use entry SRC to fill in blank portions of entry DST.  SRC itself
   may not have any blanks, of course, and it may not be the current
   dir entry itself (i.e., ".").
   Typically, SRC is a parent directory's own entry, and DST is some
   child in that directory. */
static void
take_from_entry (svn_wc_entry_t *src, svn_wc_entry_t *dst, apr_pool_t *pool)
{
  /* Inherits parent's revision if doesn't have a revision of one's
     own, unless this is a subdirectory. */
  if ((dst->revision == SVN_INVALID_REVNUM) && (dst->kind != svn_node_dir))
    dst->revision = src->revision;
  
  /* Inherits parent's ancestor if doesn't have an ancestor of one's
     own and is not marked for addition */
  if ((! dst->ancestor) && (! (dst->state & SVN_WC_ENTRY_ADDED)))
    {
      svn_string_t *name = apr_hash_get (dst->attributes,
                                         SVN_WC_ENTRY_ATTR_NAME,
                                         APR_HASH_KEY_STRING);
      dst->ancestor = svn_string_dup (src->ancestor, pool);
      svn_path_add_component (dst->ancestor, name,
                              svn_path_repos_style);
    }
}


/* Resolve any missing information in ENTRIES by deducing from the
   directory's own entry (which must already be present in ENTRIES). */
static svn_error_t *
resolve_to_defaults (svn_string_t *path,
                     apr_hash_t *entries,
                     apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_wc_entry_t *default_entry
    = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);

  /* First check the dir's own entry for consistency. */
  if (! default_entry)
    return svn_error_create (SVN_ERR_WC_ENTRY_NOT_FOUND,
                             0,
                             NULL,
                             pool,
                             "missing default entry");

  if (default_entry->revision == SVN_INVALID_REVNUM)
    return svn_error_create (SVN_ERR_WC_ENTRY_MISSING_REVISION,
                             0,
                             NULL,
                             pool,
                             "default entry has no revision number");

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
      svn_wc_entry_t *this_entry;
      svn_string_t *entryname;

      apr_hash_this (hi, &key, &keylen, &val);
      this_entry = val;
      entryname = svn_string_ncreate (key, keylen, pool);

      if (this_entry == default_entry) 
        /* THIS_DIR already has all the information it can possibly
           have.  */
        continue;

      if (this_entry->kind == svn_node_dir)
        /* Entries that are directories have everything but their
           name, kind, and state stored in the THIS_DIR entry of the
           directory itself.  However, we are disallowing the perusing
           of any entries outside of the current entries file.  If a
           caller wants more info about a directory, it should look in
           the entries file in the directory.  */
        continue;

      if (this_entry->kind == svn_node_file)
        /* For file nodes that do not explicitly have their ancestry
           stated, this can be derived from the default entry of the
           directory in which those files reside.  */
        take_from_entry (default_entry, this_entry, pool);
    }

  return SVN_NO_ERROR;
}


/* Update an entry's attribute hash according to its structure fields,
   which should always dominate the hash when the two differ. */
static void
normalize_entry (svn_wc_entry_t *entry, apr_pool_t *pool)
{
  /* Revision. */
  if (entry->revision != SVN_INVALID_REVNUM)
    apr_hash_set (entry->attributes,
                  SVN_WC_ENTRY_ATTR_REVISION, APR_HASH_KEY_STRING,
                  svn_string_createf (pool, "%ld", entry->revision));
  
  /* Ancestor. */
  if ((entry->ancestor) && (entry->ancestor->len))
  apr_hash_set (entry->attributes,
                SVN_WC_ENTRY_ATTR_ANCESTOR, APR_HASH_KEY_STRING,
                entry->ancestor);
  
  /* Kind. */
  if (entry->kind == svn_node_dir)
    apr_hash_set (entry->attributes,
                  SVN_WC_ENTRY_ATTR_KIND, APR_HASH_KEY_STRING,
                  svn_string_create (SVN_WC__ENTRIES_ATTR_DIR_STR, pool));
  else if (entry->kind != svn_node_none)  /* default to file kind */
    apr_hash_set (entry->attributes,
                  SVN_WC_ENTRY_ATTR_KIND, APR_HASH_KEY_STRING,
                  NULL);
  
  /* State. */
  {
    /* Just make the att hash *exactly* reflect the `state' flags.  

       By the time we get here, the CLEAR_NAMED and CLEAR_ALL flags
       should *not* be set in the entry.  This would meaningless;
       entry->state is a data-state, not a command.  The only routine
       to interpret the "command" flag-style is fold_sync(). */

    apr_hash_set (entry->attributes,
                  SVN_WC_ENTRY_ATTR_ADD, APR_HASH_KEY_STRING,
                  (entry->state & SVN_WC_ENTRY_ADDED) ?
                  svn_string_create ("true", pool) : NULL);

    apr_hash_set (entry->attributes,
                  SVN_WC_ENTRY_ATTR_DELETE, APR_HASH_KEY_STRING,
                  (entry->state & SVN_WC_ENTRY_DELETED) ?
                  svn_string_create ("true", pool) : NULL);

    apr_hash_set (entry->attributes,
                  SVN_WC_ENTRY_ATTR_MERGED, APR_HASH_KEY_STRING,
                  (entry->state & SVN_WC_ENTRY_MERGED) ?
                  svn_string_create ("true", pool) : NULL);

    apr_hash_set (entry->attributes,
                  SVN_WC_ENTRY_ATTR_CONFLICT, APR_HASH_KEY_STRING,
                  (entry->state & SVN_WC_ENTRY_CONFLICTED) ?
                  svn_string_create ("true", pool) : NULL);
  }
  
  /* Timestamps. */
  if (entry->text_time)
    {
      apr_hash_set (entry->attributes,
                    SVN_WC_ENTRY_ATTR_TEXT_TIME, APR_HASH_KEY_STRING,
                    svn_wc__time_to_string (entry->text_time, pool));
    }
  if (entry->prop_time)
    {
      apr_hash_set (entry->attributes,
                    SVN_WC_ENTRY_ATTR_PROP_TIME, APR_HASH_KEY_STRING,
                    svn_wc__time_to_string (entry->prop_time, pool));
    }
}


/* Fill ENTRIES according to PATH's entries file. */
static svn_error_t *
read_entries (apr_hash_t *entries,
              svn_string_t *path,
              svn_boolean_t get_all_missing_info,
              apr_pool_t *pool)
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
    apr_err = apr_file_read_full (infile, buf, sizeof(buf), &bytes_read);
    if (apr_err && !APR_STATUS_IS_EOF(apr_err))
      return svn_error_create 
        (apr_err, 0, NULL, pool, "read_entries: apr_file_read_full choked");
    
    err = svn_xml_parse (svn_parser, buf, bytes_read,
                         APR_STATUS_IS_EOF(apr_err));
    if (err)
      return svn_error_quick_wrap 
        (err,
         "read_entries: xml parser failed.");
  } while (!APR_STATUS_IS_EOF(apr_err));

  /* Close the entries file. */
  err = svn_wc__close_adm_file (infile, path, SVN_WC__ADM_ENTRIES, 0, pool);
  if (err)
    return err;

  /* Clean up the xml parser */
  svn_xml_free_parser (svn_parser);

  /* Fill in any implied fields. */
  if (get_all_missing_info)
    SVN_ERR (resolve_to_defaults (path, entries, pool));

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_entry (svn_wc_entry_t **entry,
              svn_string_t *path,
              apr_pool_t *pool)
{
  svn_error_t *err;
  enum svn_node_kind kind;
  apr_hash_t *entries = apr_hash_make (pool);
  svn_boolean_t is_wc;

  *entry = NULL;

  err = svn_io_check_path (path, &kind, pool);
  if (err)
    return err;

  /* kff todo: fooo working here:
     Make an innocent way to discover that a dir/path is or is not
     under version control, so that this function can be robust.  I
     think svn_wc_entries_read() will return an error right now if,
     for example, PATH represents a new dir that svn still thinks is a
     regular file under version control. */

  if (kind == svn_node_dir)
    {
      err = svn_wc_check_wc (path, &is_wc, pool);
      if (err)
        return err;
      else if (! is_wc)
        return svn_error_createf
          (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
           "svn_wc_entry: %s is not a working copy directory", path->data);


      err = svn_wc_entries_read (&entries, path, pool);
      if (err)
        return err;

      *entry
        = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
    }

  if (! *entry)
    {
      /* Maybe we're here because PATH is a directory, and we've
         already tried and failed to retrieve its revision information
         (we could have failed because PATH is under rev control as a
         file, not a directory, i.e., the user rm'd the file and
         created a dir there).
         
         Or maybe we're here because PATH is a regular file.
         
         Either way, if PATH is a versioned entity, it is versioned as
         a file.  So look split and look in parent for entry info. */

      svn_string_t *dir, *basename;
      svn_path_split (path, &dir, &basename, svn_path_local_style, pool);
      
      err = svn_wc_check_wc (dir, &is_wc, pool);
      if (err)
        return err;
      else if (! is_wc)
        return svn_error_createf
          (SVN_ERR_WC_OBSTRUCTED_UPDATE, 0, NULL, pool,
           "svn_wc_entry: %s is not a working copy directory", path->data);
      
      err = svn_wc_entries_read (&entries, dir, pool);
      if (err)
        return err;
      
      *entry = apr_hash_get (entries, basename->data, basename->len);
    }

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_entries_read (apr_hash_t **entries,
                     svn_string_t *path,
                     apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_t *new_entries;

  new_entries = apr_hash_make (pool);

  err = read_entries (new_entries, path, TRUE, pool);
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
  svn_wc_entry_t *this_dir;
  svn_string_t *this_dir_name = 
    svn_string_create (SVN_WC_ENTRY_THIS_DIR, pool);

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

  /* Get a copy of the "this dir" entry for comparison purposes. */
  this_dir = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, 
                           APR_HASH_KEY_STRING);

  for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t keylen;
      void *val;
      svn_wc_entry_t *this_entry;
      svn_string_t *name;

      /* Get the entry and make sure its attributes are up-to-date. */
      apr_hash_this (hi, &key, &keylen, &val);
      this_entry = val;

      /* Normalize this entry */
      normalize_entry (this_entry, pool);

      /* We only want to write out 'revision' and 'ancestor' for the
         following things:
         1. the current directory's "this dir" entry.
         2. non-directory entries:
            a. which are marked for addition (and consequently should
               have an invalid revnum) 
            b. whose revision or ancestor is valid and different than 
               that of the "this dir" entry.
      */
      name = svn_string_ncreate ((const char *)key, keylen, pool);
      if (! svn_string_compare (this_dir_name, name))
        {
          if (this_entry->kind == svn_node_dir)
            {
              /* We don't write ancestor or revision for subdir
                 entries. */
              apr_hash_set (this_entry->attributes, 
                            SVN_WC_ENTRY_ATTR_REVISION,
                            APR_HASH_KEY_STRING,
                            NULL);
              apr_hash_set (this_entry->attributes, 
                            SVN_WC_ENTRY_ATTR_ANCESTOR,
                            APR_HASH_KEY_STRING,
                            NULL);
            }
          else
            {
              svn_string_t *this_path;

              /* If this is not the "this dir" entry, and the revision is
                 the same as that of the "this dir" entry, don't write it
                 out at all. */
              if (this_entry->revision == this_dir->revision)
                apr_hash_set (this_entry->attributes, 
                              SVN_WC_ENTRY_ATTR_REVISION,
                              APR_HASH_KEY_STRING,
                              NULL);

              /* If this is not the "this dir" entry, and the revision is
                 the same as that of the "this dir" entry, don't write it
                 out at all. */
              if (this_entry->ancestor)
                {
                  this_path = svn_string_dup (this_dir->ancestor, pool);
                  svn_path_add_component (this_path, name,
                                          svn_path_repos_style);
                  if (svn_string_compare (this_path, this_entry->ancestor))
                    apr_hash_set (this_entry->attributes, 
                                  SVN_WC_ENTRY_ATTR_ANCESTOR,
                                  APR_HASH_KEY_STRING,
                                  NULL);
                }
            }
        }

      /* Append the entry onto the accumulating string. */
      svn_xml_make_open_tag_hash (&bigstr,
                                  pool,
                                  svn_xml_self_closing,
                                  SVN_WC__ENTRIES_ENTRY,
                                  this_entry->attributes);
    }

  svn_xml_make_close_tag (&bigstr, pool, SVN_WC__ENTRIES_TOPLEVEL);

  apr_err = apr_file_write_full (outfile, bigstr->data, bigstr->len, NULL);
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


/* Update an entry NAME in ENTRIES, according to a set of changes
   {REVISION, KIND, STATE, TEXT_TIME, PROP_TIME, ATTS}.  ATTS may be
   null.

   If the entry already exists, the requested changes will be folded
   (merged) into the entry's existing state.

   If the entry doesn't exist, the entry will be created with exactly
   those properties described by the set of changes. */
static void
fold_entry (apr_hash_t *entries,
            svn_string_t *name,
            svn_revnum_t revision,
            enum svn_node_kind kind,
            int state,
            apr_time_t text_time,
            apr_time_t prop_time,
            apr_pool_t *pool,
            apr_hash_t *atts,
            va_list ap)
{
  apr_hash_index_t *hi;
  struct svn_wc_entry_t *entry
    = apr_hash_get (entries, name->data, name->len);
  int incoming_flags = state;
  
  assert (name != NULL);

  if (! entry)
    entry = alloc_entry (pool);

  /* Set up the explicit attributes. */
  if (revision != SVN_INVALID_REVNUM)
    entry->revision = revision;
  if (kind != svn_node_none)
    entry->kind = kind;
  if (text_time)
    entry->text_time = text_time;
  if (prop_time)
    entry->prop_time = prop_time;

  /* Merge the incoming_flags into the entry's flags, correctly
     interpreting "clear" bits. */
  if (incoming_flags)
    {
      if (incoming_flags & SVN_WC_ENTRY_CLEAR_ALL)
        entry->state = 0;

      else if (incoming_flags & SVN_WC_ENTRY_CLEAR_NAMED)
        {
          entry->state &= ~incoming_flags;
        }

      else
        entry->state |= incoming_flags;
    }

  /* Do any other attributes. */
  if (atts)
    {
      for (hi = apr_hash_first (atts); hi; hi = apr_hash_next (hi))
        {
          const void *k;
          apr_size_t klen;
          void *v;
          const char *key;
          svn_string_t *val;
          
          /* Get a hash key and value */
          apr_hash_this (hi, &k, &klen, &v);
          key = (const char *) k;
          val = (svn_string_t *) v;
          
          apr_hash_set (entry->attributes, key, APR_HASH_KEY_STRING, val);
        }
    }

  /* The entry's name is an attribute, too. */
  apr_hash_set (entry->attributes,
                SVN_WC_ENTRY_ATTR_NAME,
                APR_HASH_KEY_STRING,
                name);

  /* Absorb defaults from the parent dir, if any, unless this is a
     subdir entry. */
  if (kind != svn_node_dir)
  {
    svn_wc_entry_t *default_entry
      = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
    if (default_entry)
      take_from_entry (default_entry, entry, pool);
  }

  /* Make attribute hash reflect the explicit attributes. */
  normalize_entry (entry, pool);

  /* Remove any attributes named for removal. */
  {
    const char *remove_me;
    while ((remove_me = va_arg (ap, const char *)) != NULL)
      apr_hash_set (entry->attributes, remove_me, APR_HASH_KEY_STRING, NULL);
  }

  /* Make sure the entry exists in the entries hash.  Possibly it
     already did, in which case this could have been skipped, but what
     the heck. */
  apr_hash_set (entries, name->data, name->len, entry);
}


/* kff todo: we shouldn't have this function in the interface, probably. */
void
svn_wc__entry_remove (apr_hash_t *entries,
                      svn_string_t *name)
{
  apr_hash_set (entries, name->data, name->len, NULL);
}



/*
  Our general purpose intelligence module for "interpreting" changes
  to a single entry.

  Given an entryname NAME in ENTRIES, examine the caller's requested
  change in *STATE.  Compare against existing state, and possibly
  modify *STATE (or ENTRIES) so that when merged, it will reflect the
  caller's original intent.

  Right now, the interface is simple (only examines "add" and "delete"
  flag bits), but we can expand later to include other arguments.  */
static svn_error_t *
interpret_changes (apr_hash_t *entries,
                   svn_string_t *name,
                   int *state,
                   apr_pool_t *pool)
{
  int current_state, new_state;
  struct svn_wc_entry_t *entry;

  char current_addonly, current_delonly, current_both, current_neither;
  char new_addonly, new_delonly;

  /* If no flags are being changed, GET OUT! */
  if ( (! (*state & SVN_WC_ENTRY_DELETED))
       && (! (*state & SVN_WC_ENTRY_ADDED)) )
    return SVN_NO_ERROR;

  /* Get the entry */
  entry = apr_hash_get (entries, name->data, name->len);

  /* What if the entry doesn't yet exist?  That's ok.  Presumably the
     fold_entry() routines are being asked to create it. */
  if (! entry)
    {
      if (*state == SVN_WC_ENTRY_ADDED)
        /* The -only- permissible flag to set, if the entry doesn't
           yet exist, is the ADD flag. */
        return SVN_NO_ERROR;

      else
        /* Any other flag state is verboten, or at least nonsensical. */
        return 
          svn_error_createf 
          (SVN_ERR_WC_ENTRY_BOGUS_MERGE, 0, NULL, pool,
           "error: bogus flags (%d) used in creation of entry `%s'",
           *state, name->data);
    }

  /* For convenience. */
  current_state = entry->state;
  new_state = *state;

  /* If the caller is trying to simultaneously set add and delete,
     this is an egregious error.  (It's possible to have both flags
     set at the same time, but *only* because some caller first set
     the delete flag, then another caller set the add flag later.) */
  if ((new_state & SVN_WC_ENTRY_DELETED)
      && (new_state & SVN_WC_ENTRY_ADDED))
    return 
      svn_error_createf 
      (SVN_ERR_WC_ENTRY_BOGUS_MERGE, 0, NULL, pool, 
       "error: simultaneous set of add & del flags on `%s'", name->data);

  /* All the (remaining) possible current states. */
  current_addonly = ((current_state & SVN_WC_ENTRY_ADDED)
                     && (! (current_state & SVN_WC_ENTRY_DELETED)));
  current_delonly = ((current_state & SVN_WC_ENTRY_DELETED)
                     && (! (current_state & SVN_WC_ENTRY_ADDED)));
  current_both = ((current_state & SVN_WC_ENTRY_DELETED)
                  && (current_state & SVN_WC_ENTRY_ADDED));
  current_neither = ((! (current_state & SVN_WC_ENTRY_DELETED))
                     && (! (current_state & SVN_WC_ENTRY_ADDED)));

  /* All the (remaining) possible proposed states. */
  new_addonly = ((new_state & SVN_WC_ENTRY_ADDED)
                 && (! (new_state & SVN_WC_ENTRY_DELETED)));
  new_delonly = ((new_state & SVN_WC_ENTRY_DELETED)
                 && (! (new_state & SVN_WC_ENTRY_ADDED)));
  

  /* Remaining logic, yum. */

  if (new_addonly)
    {
      if (current_addonly || current_neither)
        {
          return svn_error_createf(SVN_ERR_WC_ENTRY_EXISTS, 0, NULL, pool, 
                                   "%s is already under version control",
                                   name->data);
        }
      else if (current_both)
        {
          /* TODO: generate a friendly warning here someday */
        }
    }
  
  else if (new_delonly)
    {
      if (current_delonly)
        {
          /* TODO: generate a friendly warning here someday */
        }
      else if (current_addonly)
        {
          /* The caller wants to set the delete flag, but entry has
             nothing but the add flag set.  Obviously, this entry was
             added and is now being removed before a commit ever
             happens.  So the logical thing to do is remove the entry
             completely. */
          apr_hash_set (entries, name->data, name->len, NULL);
        }
      else if (current_both)
        {
          /* The caller wants to set the delete flag, but entry
             already has both add and del flags set -- which means:

             1. the user deleted an old entry
             2. the user added a new entry with the same name
             3. the user reversed decision #2, and now wants to
                deleted the added file.
                
             So the logical thing to do is just make sure that the add
             flag gets *un*set during the flag merge. */

          /* Unset the delete flag, it's irrelevant. */
          *state &= ~SVN_WC_ENTRY_DELETED;

          /* Set the add and "clear" flag */
          *state |= SVN_WC_ENTRY_ADDED;
          *state |= SVN_WC_ENTRY_CLEAR_NAMED;

          /* When *state is merged, fold_entry should only unset the
             add flag now. */
        }
    }
  
  return SVN_NO_ERROR;
}




/* Shared by __entry_fold_sync() and   __entry_fold_sync_intelligently().

   Loads up an entries file, calls the "logic" module if necessary to
   transform the requested changes, folds the changes, then syncs
   entries to disk.  */
static svn_error_t *
internal_fold_sync (svn_boolean_t be_intelligent,
                    svn_string_t *path,
                    svn_string_t *name,
                    svn_revnum_t revision,
                    enum svn_node_kind kind,
                    int state,
                    apr_time_t text_time,
                    apr_time_t prop_time,
                    apr_pool_t *pool,
                    apr_hash_t *atts,
                    va_list ap)
{
  svn_error_t *err;
  svn_wc_entry_t *entry_before, *entry_after;
  svn_boolean_t entry_was_deleted_p = FALSE;
  apr_hash_t *entries = NULL;

  /* Load whole entries file */
  err = svn_wc_entries_read (&entries, path, pool);
  if (err) return err;
  
  if (name == NULL)
    name = svn_string_create (SVN_WC_ENTRY_THIS_DIR, pool);

  /* Optional:  -interpret- the changes */
  if (be_intelligent)
    {
      entry_before = apr_hash_get (entries, name->data, name->len);

      /* Right now, the intelligence module only (possibly) changes
         the state flags, and (possibly) removes the whole entry.  */
      err = interpret_changes (entries, name, &state, pool);
      if (err) return err;

      /* Special case:  interpret_changes() may have actually REMOVED
         the entry in question!  If so, don't try to fold_entry, as
         this will just recreate the entry again. */
      entry_after = apr_hash_get (entries, name->data, name->len);
      if (entry_before && (! entry_after))
        entry_was_deleted_p = TRUE;
    }

  /* Fold changes into (or create) the entry. */
  if (! entry_was_deleted_p)
    fold_entry (entries, name, revision, kind, state, text_time,
                prop_time, pool, atts, ap);
  
  /* Write whole entries file */
  err = svn_wc__entries_write (entries, path, pool);
  if (err) return err;

  return SVN_NO_ERROR;
}



/*
   NOTES on svn_wc__entry_fold_sync functions
   ==============================================

   There are three ways to change an entry on disk:

     1.  Use entry_fold_sync() to directly merge changes into a single
         entry

     2.  Use entry_fold_sync_intelligently() to *logically* merge
         changes into a single entry

     3.  read all entries into a hash with svn_wc_entries_read, modify
         the entry structures manually, and write them all out again
         with svn_wc__entries_write.

 */


/* The "stupid" version of fold_sync, which simply merges the changes
   directly into an entry, no questions asked.  */
svn_error_t *
svn_wc__entry_fold_sync (svn_string_t *path,
                         svn_string_t *name,
                         svn_revnum_t revision,
                         enum svn_node_kind kind,
                         int state,
                         apr_time_t text_time,
                         apr_time_t prop_time,
                         apr_pool_t *pool,
                         apr_hash_t *atts,
                         ...)
{
  svn_error_t *err;
  va_list ap;

  va_start (ap, atts);
  err = internal_fold_sync (FALSE,  /* be "stupid" */
                            path, name, revision, kind, state,
                            text_time, prop_time, pool, atts, ap);
  va_end (ap);
  
  if (err) return err;

  return SVN_NO_ERROR;
}



/* The "smart" version of fold_sync, which tries to deduce the
   caller's intent; may end up folding a different set of changes than
   what was literally requested.  */
svn_error_t *
svn_wc__entry_fold_sync_intelligently (svn_string_t *path,
                                       svn_string_t *name,
                                       svn_revnum_t revision,
                                       enum svn_node_kind kind,
                                       int state,
                                       apr_time_t text_time,
                                       apr_time_t prop_time,
                                       apr_pool_t *pool,
                                       apr_hash_t *atts,
                                       ...)
{
  svn_error_t *err;
  va_list ap;

  va_start (ap, atts);
  err = internal_fold_sync (TRUE,  /* be "smart" */
                            path, name, revision, kind, state,
                            text_time, prop_time, pool, atts, ap);
  va_end (ap);
  
  if (err) return err;

  return SVN_NO_ERROR;
}



svn_wc_entry_t *
svn_wc__entry_dup (svn_wc_entry_t *entry, apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_wc_entry_t *dupentry = apr_pcalloc (pool, sizeof(*dupentry));

  dupentry->revision   = entry->revision;
  if (entry->ancestor)
    dupentry->ancestor = svn_string_dup (entry->ancestor, pool);
  dupentry->kind       = entry->kind;
  dupentry->state      = entry->state;
  dupentry->text_time  = entry->text_time;
  dupentry->prop_time  = entry->prop_time;

  dupentry->attributes = apr_hash_make (pool);

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
 * Recurse on the revisioned parts of a working copy tree, starting at
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
