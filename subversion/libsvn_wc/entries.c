/*
 * entries.c :  manipulating the administrative `entries' file.
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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
#include <apr_strings.h>
#include <assert.h>
#include "svn_xml.h"
#include "svn_error.h"
#include "svn_types.h"
#include "svn_time.h"
#include "svn_pools.h"

#include "wc.h"
#include "adm_files.h"
#include "adm_ops.h"
#include "entries.h"


/** Overview **/

/* The administrative `entries' file tracks information about files
   and subdirs within a particular directory.
   
   See the section on the `entries' file in libsvn_wc/README, for
   concrete information about the XML format.
*/


/*--------------------------------------------------------------- */

/*** XML Attribute names and values ***/

/* Attribute values for 'schedule' */
#define SVN_WC__ENTRY_VALUE_ADD        "add"
#define SVN_WC__ENTRY_VALUE_DELETE     "delete"
#define SVN_WC__ENTRY_VALUE_REPLACE    "replace"



/*** Initialization of the entries file. ***/

svn_error_t *
svn_wc__entries_init (const char *path,
                      const char *url,
                      apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_file_t *f = NULL;
  svn_stringbuf_t *accum = NULL;
  char *initial_revstr = apr_psprintf (pool, "%d", 0);

  /* Create the entries file, which must not exist prior to this. */
  SVN_ERR (svn_wc__open_adm_file (&f, path, SVN_WC__ADM_ENTRIES,
                                  (APR_WRITE | APR_CREATE | APR_EXCL), pool));

  /* Make a the XML standard header, to satisfy bureacracy. */
  svn_xml_make_header (&accum, pool);

  /* Open the file's top-level form. */
  svn_xml_make_open_tag (&accum,
                         pool,
                         svn_xml_normal,
                         SVN_WC__ENTRIES_TOPLEVEL,
                         "xmlns",
                         SVN_XML_NAMESPACE,
                         NULL);

  /* Add an entry for the dir itself -- name is absent, only the
     revision and default ancestry are present as xml attributes. */
  svn_xml_make_open_tag 
    (&accum,
     pool,
     svn_xml_self_closing,
     SVN_WC__ENTRIES_ENTRY,
     SVN_WC__ENTRY_ATTR_KIND,
     SVN_WC__ENTRIES_ATTR_DIR_STR,
     SVN_WC__ENTRY_ATTR_REVISION,
     initial_revstr,
     SVN_WC__ENTRY_ATTR_URL,
     url,
     NULL);

  /* Close the top-level form. */
  svn_xml_make_close_tag (&accum,
                          pool,
                          SVN_WC__ENTRIES_TOPLEVEL);

  apr_err = apr_file_write_full (f, accum->data, accum->len, NULL);
  if (apr_err)
    {
      apr_file_close (f);
      return svn_error_createf (apr_err, NULL,
                                "svn_wc__entries_init: "
                                "error writing %s's entries file",
                                path);
    }

  /* Now we have a `entries' file with exactly one entry, an entry
     for this dir.  Close the file and sync it up. */
  SVN_ERR (svn_wc__close_adm_file (f, path, SVN_WC__ADM_ENTRIES, 1, pool));

  return SVN_NO_ERROR;
}


/*--------------------------------------------------------------- */

/*** reading and writing the entries file ***/

struct entries_accumulator
{
  /* Keys are entry names, vals are (struct svn_wc_entry_t *)'s. */
  apr_hash_t *entries; 

  /* The parser that's parsing it, for signal_expat_bailout(). */
  svn_xml_parser_t *parser;

  /* Should we include 'deleted' entries in the hash? */
  svn_boolean_t show_deleted;

  /* Don't leave home without one. */
  apr_pool_t *pool;
};


static svn_wc_entry_t *
alloc_entry (apr_pool_t *pool)
{
  svn_wc_entry_t *entry = apr_pcalloc (pool, sizeof (*entry));
  entry->revision = SVN_INVALID_REVNUM;
  entry->copyfrom_rev = SVN_INVALID_REVNUM;
  entry->cmt_rev = SVN_INVALID_REVNUM;
  entry->kind = svn_node_none;
  return entry;
}


svn_error_t *
svn_wc__atts_to_entry (svn_wc_entry_t **new_entry,
                       apr_uint32_t *modify_flags,
                       apr_hash_t *atts,
                       apr_pool_t *pool)
{
  svn_wc_entry_t *entry = alloc_entry (pool);
  const char *name;
  
  *modify_flags = 0;

  /* Find the name and set up the entry under that name. */
  name = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_NAME, APR_HASH_KEY_STRING);
  entry->name = name ? name : SVN_WC_ENTRY_THIS_DIR;

  /* Attempt to set revision (resolve_to_defaults may do it later, too) */
  {
    const char *revision_str
      = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_REVISION, APR_HASH_KEY_STRING);

    if (revision_str)
      {
        entry->revision = SVN_STR_TO_REV (revision_str);
        *modify_flags |= SVN_WC__ENTRY_MODIFY_REVISION;
      }
    else
      entry->revision = SVN_INVALID_REVNUM;
  }

  /* Attempt to set up url path (again, see resolve_to_defaults). */
  {
    entry->url
      = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_URL, APR_HASH_KEY_STRING);

    if (entry->url)
      *modify_flags |= SVN_WC__ENTRY_MODIFY_URL;
  }

  /* Set up kind. */
  {
    const char *kindstr
      = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_KIND, APR_HASH_KEY_STRING);

    entry->kind = svn_node_none;
    if (kindstr)
      {
        if (! strcmp (kindstr, SVN_WC__ENTRIES_ATTR_FILE_STR))
          entry->kind = svn_node_file;
        else if (! strcmp (kindstr, SVN_WC__ENTRIES_ATTR_DIR_STR))
          entry->kind = svn_node_dir;
        else
          return svn_error_createf 
            (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
             "Entry '%s' has invalid node kind",
             (name ? name : SVN_WC_ENTRY_THIS_DIR));
        *modify_flags |= SVN_WC__ENTRY_MODIFY_KIND;
      }
  }

  /* Look for a schedule attribute on this entry. */
  {
    const char *schedulestr
      = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_SCHEDULE, APR_HASH_KEY_STRING);
    
    entry->schedule = svn_wc_schedule_normal;
    if (schedulestr)
      {
        if (! strcmp (schedulestr, SVN_WC__ENTRY_VALUE_ADD))
          entry->schedule = svn_wc_schedule_add;
        else if (! strcmp (schedulestr, SVN_WC__ENTRY_VALUE_DELETE))
              entry->schedule = svn_wc_schedule_delete;
        else if (! strcmp (schedulestr, SVN_WC__ENTRY_VALUE_REPLACE))
          entry->schedule = svn_wc_schedule_replace;
        else if (! strcmp (schedulestr, ""))
          entry->schedule = svn_wc_schedule_normal;
        else
          return svn_error_createf 
            (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
             "Entry '%s' has invalid '%s' value",
             (name ? name : SVN_WC_ENTRY_THIS_DIR),
             SVN_WC__ENTRY_ATTR_SCHEDULE);

        *modify_flags |= SVN_WC__ENTRY_MODIFY_SCHEDULE;
      }
  }   
  
  /* Is this entry in a state of mental torment (conflict)? */
  {
    if ((entry->prejfile 
         = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_PREJFILE, 
                         APR_HASH_KEY_STRING)))
      *modify_flags |= SVN_WC__ENTRY_MODIFY_PREJFILE;

    if ((entry->conflict_old 
         = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_CONFLICT_OLD, 
                         APR_HASH_KEY_STRING)))
      *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_OLD;

    if ((entry->conflict_new 
         = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_CONFLICT_NEW, 
                         APR_HASH_KEY_STRING)))
      *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_NEW;

    if ((entry->conflict_wrk 
         = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_CONFLICT_WRK, 
                         APR_HASH_KEY_STRING)))
      *modify_flags |= SVN_WC__ENTRY_MODIFY_CONFLICT_WRK;
  }

  /* Is this entry copied? */
  {
    const char *copiedstr, *revstr;
      
    copiedstr = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_COPIED, 
                              APR_HASH_KEY_STRING);
        
    entry->copied = FALSE;
    if (copiedstr)
      {
        if (! strcmp (copiedstr, "true"))
          entry->copied = TRUE;
        else if (! strcmp (copiedstr, "false"))
          entry->copied = FALSE;
        else if (! strcmp (copiedstr, ""))
          entry->copied = FALSE;
        else
          return svn_error_createf 
            (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
             "Entry '%s' has invalid '%s' value",
             (name ? name : SVN_WC_ENTRY_THIS_DIR),
             SVN_WC__ENTRY_ATTR_COPIED);

        *modify_flags |= SVN_WC__ENTRY_MODIFY_COPIED;
      }

    entry->copyfrom_url = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_COPYFROM_URL,
                                        APR_HASH_KEY_STRING);

    revstr = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_COPYFROM_REV, 
                           APR_HASH_KEY_STRING);
    if (revstr)
      entry->copyfrom_rev = SVN_STR_TO_REV (revstr);
    
  }

  /* Is this entry deleted? */
  {
    const char *deletedstr;
      
    deletedstr = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_DELETED, 
                               APR_HASH_KEY_STRING);
        
    entry->deleted = FALSE;
    if (deletedstr)
      {
        if (! strcmp (deletedstr, "true"))
          entry->deleted = TRUE;
        else if (! strcmp (deletedstr, "false"))
          entry->deleted = FALSE;
        else if (! strcmp (deletedstr, ""))
          entry->deleted = FALSE;
        else
          return svn_error_createf 
            (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, NULL,
             "Entry '%s' has invalid '%s' value",
             (name ? name : SVN_WC_ENTRY_THIS_DIR),
             SVN_WC__ENTRY_ATTR_DELETED);

        *modify_flags |= SVN_WC__ENTRY_MODIFY_DELETED;
      }
  }

  /* Attempt to set up timestamps. */
  {
    const char *text_timestr, *prop_timestr;
    
    text_timestr = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_TEXT_TIME,
                                 APR_HASH_KEY_STRING);
    if (text_timestr)
      {
        if (! strcmp (text_timestr, SVN_WC_TIMESTAMP_WC))
          {
            /* Special case:  a magic string that means 'get this value
               from the working copy' -- we ignore it here, trusting
               that the caller of this function know what to do about
               it.  */
          }
        else
          SVN_ERR (svn_time_from_cstring (&entry->text_time, text_timestr,
                                          pool));
        
        *modify_flags |= SVN_WC__ENTRY_MODIFY_TEXT_TIME;
      }
    
    prop_timestr = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_PROP_TIME,
                                 APR_HASH_KEY_STRING);
    if (prop_timestr)
      {
        if (! strcmp (prop_timestr, SVN_WC_TIMESTAMP_WC))
          {
            /* Special case:  a magic string that means 'get this value
               from the working copy' -- we ignore it here, trusting
               that the caller of this function know what to do about
               it.  */
          }
        else
          SVN_ERR (svn_time_from_cstring (&entry->prop_time, prop_timestr,
                                          pool));
        
        *modify_flags |= SVN_WC__ENTRY_MODIFY_PROP_TIME;
      }
  }

  /* Checksum. */
  {
    entry->checksum = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_CHECKSUM,
                                    APR_HASH_KEY_STRING);
    if (entry->checksum)
      *modify_flags |= SVN_WC__ENTRY_MODIFY_CHECKSUM;
  }

  /* Setup last-committed values. */
  {
    const char *cmt_datestr, *cmt_revstr;
    
    cmt_datestr = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_CMT_DATE,
                                APR_HASH_KEY_STRING);
    if (cmt_datestr)
      {
        SVN_ERR (svn_time_from_cstring (&entry->cmt_date, cmt_datestr, pool));
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CMT_DATE;
      }
    else
      entry->cmt_date = 0;

    cmt_revstr = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_CMT_REV,
                               APR_HASH_KEY_STRING);
    if (cmt_revstr)
      {
        entry->cmt_rev = SVN_STR_TO_REV (cmt_revstr);
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CMT_REV;
      }
    else
      entry->cmt_rev = SVN_INVALID_REVNUM;

    entry->cmt_author = apr_hash_get (atts, SVN_WC__ENTRY_ATTR_CMT_AUTHOR,
                                      APR_HASH_KEY_STRING);
    if (entry->cmt_author)
        *modify_flags |= SVN_WC__ENTRY_MODIFY_CMT_AUTHOR;
  }
  
  
  *new_entry = entry;
  return SVN_NO_ERROR;
}

                       

/* Called whenever we find an <open> tag of some kind. */
static void
handle_start_tag (void *userData, const char *tagname, const char **atts)
{
  struct entries_accumulator *accum = userData;
  apr_hash_t *attributes;
  svn_wc_entry_t *entry;
  svn_error_t *err;
  apr_uint32_t modify_flags = 0;

  /* We only care about the `entry' tag; all other tags, such as `xml'
     and `wc-entries', are ignored. */
  if (strcmp (tagname, SVN_WC__ENTRIES_ENTRY))
    return;

  /* Make an entry from the attributes. */
  attributes = svn_xml_make_att_hash (atts, accum->pool);
  err = svn_wc__atts_to_entry (&entry, &modify_flags, attributes, accum->pool);
  if (err)
    svn_xml_signal_bailout (err, accum->parser);
        
  /* Find the name and set up the entry under that name.  This
     should *NOT* be NULL, since svn_wc__atts_to_entry() should
     have made it into SVN_WC_ENTRY_THIS_DIR.  */
  if (entry->deleted
      && (entry->schedule != svn_wc_schedule_add)
      && (! accum->show_deleted))
    ;
  else
    apr_hash_set (accum->entries, entry->name, APR_HASH_KEY_STRING, entry);
}


/* Use entry SRC to fill in blank portions of entry DST.  SRC itself
   may not have any blanks, of course.
   Typically, SRC is a parent directory's own entry, and DST is some
   child in that directory. */
static void
take_from_entry (svn_wc_entry_t *src, svn_wc_entry_t *dst, apr_pool_t *pool)
{
  /* Inherits parent's revision if doesn't have a revision of one's
     own, unless this is a subdirectory. */
  if ((dst->revision == SVN_INVALID_REVNUM) && (dst->kind != svn_node_dir))
    dst->revision = src->revision;
  
  /* Inherits parent's url if doesn't have a url of one's own and is not
     marked for addition.  An entry being added doesn't really have
     url yet.  */
  if ((! dst->url) 
      && (! ((dst->schedule == svn_wc_schedule_add)
             || (dst->schedule == svn_wc_schedule_replace))))
    {
      dst->url = svn_path_url_add_component (src->url, dst->name, pool);
    }
}


/* Resolve any missing information in ENTRIES by deducing from the
   directory's own entry (which must already be present in ENTRIES). */
static svn_error_t *
resolve_to_defaults (apr_hash_t *entries,
                     apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  svn_wc_entry_t *default_entry
    = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);

  /* First check the dir's own entry for consistency. */
  if (! default_entry)
    return svn_error_create (SVN_ERR_ENTRY_NOT_FOUND,
                             NULL,
                             "missing default entry");

  if (default_entry->revision == SVN_INVALID_REVNUM)
    return svn_error_create (SVN_ERR_ENTRY_MISSING_REVISION,
                             NULL,
                             "default entry has no revision number");

  if (! default_entry->url)
    return svn_error_create (SVN_ERR_ENTRY_MISSING_URL,
                             NULL,
                             "default entry missing url");
  
    
  /* Then use it to fill in missing information in other entries. */
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      void *val;
      svn_wc_entry_t *this_entry;

      apr_hash_this (hi, NULL, NULL, &val);
      this_entry = val;

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



/* Fill the entries cache in ADM_ACCESS. Either the full hash cache will
   populated, if SHOW_DELETED is TRUE, or the truncated hash cache will be
   populated if SHOW_DELETED is FALSE.  POOL is used for local memory
   allocation, the access baton pool is used for the cache. */
static svn_error_t *
read_entries (svn_wc_adm_access_t *adm_access,
              svn_boolean_t show_deleted,
              apr_pool_t *pool)
{
  svn_error_t *err;
  apr_file_t *infile = NULL;
  svn_xml_parser_t *svn_parser;
  apr_status_t apr_err;
  char buf[BUFSIZ];
  apr_size_t bytes_read;
  struct entries_accumulator accum;
  apr_hash_t *entries = apr_hash_make (svn_wc_adm_access_pool (adm_access));

  /* Open the entries file. */
  SVN_ERR (svn_wc__open_adm_file (&infile,
                                  svn_wc_adm_access_path (adm_access),
                                  SVN_WC__ADM_ENTRIES, APR_READ, pool));

  /* Set up userData for the XML parser. */
  accum.entries = entries;
  accum.show_deleted = show_deleted;
  accum.pool = svn_wc_adm_access_pool (adm_access);

  /* Create the XML parser */
  svn_parser = svn_xml_make_parser (&accum,
                                    handle_start_tag,
                                    NULL,
                                    NULL,
                                    pool);

  /* Store parser in its own userdata, so callbacks can call
     svn_xml_signal_bailout() */
  accum.parser = svn_parser;

  /* Parse. */
  do {
    apr_err = apr_file_read_full (infile, buf, sizeof(buf), &bytes_read);
    if (apr_err && !APR_STATUS_IS_EOF(apr_err))
      return svn_error_create 
        (apr_err, NULL, "read_entries: apr_file_read_full choked");
    
    err = svn_xml_parse (svn_parser, buf, bytes_read,
                         APR_STATUS_IS_EOF(apr_err));
    if (err)
      return svn_error_createf (err->apr_err, err, 
                                "read_entries: xml parser failed (%s).", 
                                svn_wc_adm_access_path (adm_access));
  } while (!APR_STATUS_IS_EOF(apr_err));

  /* Close the entries file. */
  SVN_ERR (svn_wc__close_adm_file (infile, svn_wc_adm_access_path (adm_access),
                                   SVN_WC__ADM_ENTRIES, 0, pool));

  /* Clean up the xml parser */
  svn_xml_free_parser (svn_parser);

  /* Fill in any implied fields. */
  SVN_ERR (resolve_to_defaults (entries, svn_wc_adm_access_pool (adm_access)));

  svn_wc__adm_access_set_entries (adm_access, show_deleted, entries);

  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_entry (const svn_wc_entry_t **entry,
              const char *path,
              svn_wc_adm_access_t *adm_access,
              svn_boolean_t show_deleted,
              apr_pool_t *pool)
{
  svn_node_kind_t kind;
  apr_hash_t *entries;
  int is_wc;

  *entry = NULL;

  SVN_ERR (svn_io_check_path (path, &kind, pool));

  /* ### todo:
     Make an innocent way to discover that a dir/path is or is not
     under version control, so that this function can be robust.  I
     think svn_wc_entries_read() will return an error right now if,
     for example, PATH represents a new dir that svn still thinks is a
     regular file under version control. */

  if (kind == svn_node_dir)
    {
      SVN_ERR (svn_wc_check_wc (path, &is_wc, pool));
      if (is_wc)
        {
          svn_wc_adm_access_t *dir_access;
          svn_error_t *err;
          err = svn_wc_adm_retrieve (&dir_access, adm_access, path, pool);
          if (err)
            {
              if (err->apr_err != SVN_ERR_WC_NOT_LOCKED)
                return err;
              svn_error_clear (err);

              /* It could be that this is a versioned file that has
                 been replaced by a unversioned directory. */
              is_wc = FALSE;
            }
          else
            {
              SVN_ERR (svn_wc_entries_read (&entries, dir_access, show_deleted,
                                            pool));
              *entry = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR,
                                     APR_HASH_KEY_STRING);
            }
        }
    }

  if (kind != svn_node_dir || ! is_wc)
    {
      /* Maybe we're here because PATH is an unversioned directory, in
         which case PATH may also be a file that is deleted or scheduled
         for deletion.
         
         Or PATH could be a versioned directory that has been removed from
         the filesystem, and possibly replaced by an unversioned file. In
         this case there is partial entry information in the parent.

         Or maybe we're here because PATH is a regular file.

         Whatever, if PATH is a versioned entity, it has an entry in the
         parent.  So split and look in parent for entry info. */

      const char *dir, *base_name;
      svn_path_split (path, &dir, &base_name, pool);

      SVN_ERR (svn_wc_check_wc (dir, &is_wc, pool));
      if (! is_wc)
        return svn_error_createf
          (SVN_ERR_WC_NOT_DIRECTORY, NULL,
           "svn_wc_entry: %s is not a working copy directory",
           kind == svn_node_dir ? path : dir);

      SVN_ERR (svn_wc_entries_read (&entries, adm_access, show_deleted, pool));
      
      *entry = apr_hash_get (entries, base_name, APR_HASH_KEY_STRING);
    }

  return SVN_NO_ERROR;
}


#if 0
/* This is #if 0'd out until I decide where to use it. --cmpilato */

/* Run a simple validity check on the ENTRIES (the list of entries
   associated with the directory PATH). */
static svn_error_t *
check_entries (apr_hash_t *entries,
               const char *path,
               apr_pool_t *pool)
{
  svn_wc_entry_t *default_entry;
  apr_hash_index_t *hi;

  default_entry = apr_hash_get (entries, 
                                SVN_WC_ENTRY_THIS_DIR, 
                                APR_HASH_KEY_STRING);
  if (! default_entry)
    return svn_error_createf
      (SVN_ERR_WC_CORRUPT, NULL,
       "'%s' has no default entry",
       path);

  /* Validate DEFAULT_ENTRY's current schedule. */
  switch (default_entry->schedule)
    {
    case svn_wc_schedule_normal:
    case svn_wc_schedule_add:
    case svn_wc_schedule_delete:
    case svn_wc_schedule_replace:
      /* These are all valid states */
      break;

    default:
      /* This is an invalid state */
      return svn_error_createf
        (SVN_ERR_WC_CORRUPT, NULL,
         "Directory '%s' has an invalid schedule",
         path);
    }
  
  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      const char *name;
      void *val;
      svn_wc_entry_t *this_entry;

      /* Get the entry */
      apr_hash_this (hi, &key, NULL, &val);
      this_entry = val;
      name = key;

      /* We've already checked the "this dir" entry */
      if (! strcmp (name, SVN_WC_ENTRY_THIS_DIR ))
        continue;

      /* Validate THIS_ENTRY's current schedule. */
      switch (this_entry->schedule)
        {
        case svn_wc_schedule_normal:
        case svn_wc_schedule_add:
        case svn_wc_schedule_delete:
        case svn_wc_schedule_replace:
          /* These are all valid states */
          break;

        default:
          /* This is an invalid state */
          return svn_error_createf
            (SVN_ERR_WC_CORRUPT, NULL,
             "'%s' in directory '%s' has an invalid schedule",
             name, path);
        }

      if ((default_entry->schedule == svn_wc_schedule_add)
          && (this_entry->schedule != svn_wc_schedule_add))
        return svn_error_createf
          (SVN_ERR_WC_CORRUPT, NULL,
           "'%s' in directory '%s' (which is scheduled for addition) "
           "is not itself scheduled for addition",
           name, path);
  
      if ((default_entry->schedule == svn_wc_schedule_delete)
          && (this_entry->schedule != svn_wc_schedule_delete))
        return svn_error_createf
          (SVN_ERR_WC_CORRUPT, NULL,
           "'%s' in directory '%s' (which is scheduled for deletion) "
           "is not itself scheduled for deletion",
           name, path);

      if ((default_entry->schedule == svn_wc_schedule_replace)
          && (this_entry->schedule == svn_wc_schedule_normal))
        return svn_error_createf
          (SVN_ERR_WC_CORRUPT, NULL,
           "'%s' in directory '%s' (which is scheduled for replacement) "
           "has in invalid schedule",
           name, path);
    }
  
  return SVN_NO_ERROR;
}
#endif /* 0 */


svn_error_t *
svn_wc_entries_read (apr_hash_t **entries,
                     svn_wc_adm_access_t *adm_access,
                     svn_boolean_t show_deleted,
                     apr_pool_t *pool)
{
  apr_hash_t *new_entries;

  new_entries = svn_wc__adm_access_entries (adm_access, show_deleted, pool);
  if (! new_entries)
    {
      SVN_ERR (read_entries (adm_access, show_deleted, pool));
      new_entries = svn_wc__adm_access_entries (adm_access, show_deleted, pool);
    }

  *entries = new_entries;
  return SVN_NO_ERROR;
}


/* Append a single entry THIS_ENTRY to the string OUTPUT, using the
   entry for "this dir" THIS_DIR for comparison/optimization.
   Allocations are done in POOL.  */
static svn_error_t *
write_entry (svn_stringbuf_t **output,
             svn_wc_entry_t *entry,
             const char *name,
             svn_wc_entry_t *this_dir,
             apr_pool_t *pool)
{
  apr_hash_t *atts = apr_hash_make (pool);
  const char *valuestr;

  /*** Create a hash that represents an entry. ***/

  assert (name);

  /* Name */
  apr_hash_set (atts, SVN_WC__ENTRY_ATTR_NAME, APR_HASH_KEY_STRING, 
                entry->name);

  /* Revision */
  if (SVN_IS_VALID_REVNUM (entry->revision))
    apr_hash_set (atts, SVN_WC__ENTRY_ATTR_REVISION, APR_HASH_KEY_STRING,
                  apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, entry->revision));
  
  /* URL */
  if (entry->url)
    apr_hash_set (atts, SVN_WC__ENTRY_ATTR_URL, APR_HASH_KEY_STRING,
                  entry->url);
  
  /* Kind */
  switch (entry->kind)
    {
    case svn_node_dir:
      valuestr = SVN_WC__ENTRIES_ATTR_DIR_STR;
      break;

    case svn_node_none:
      valuestr = NULL;
      break;

    case svn_node_file:
    case svn_node_unknown:
    default:
      valuestr = SVN_WC__ENTRIES_ATTR_FILE_STR;
      break;
    }
  apr_hash_set (atts, SVN_WC__ENTRY_ATTR_KIND, APR_HASH_KEY_STRING, valuestr);

  /* Schedule */
  switch (entry->schedule)
    {
    case svn_wc_schedule_add:
      valuestr = SVN_WC__ENTRY_VALUE_ADD;
      break;

    case svn_wc_schedule_delete:
      valuestr = SVN_WC__ENTRY_VALUE_DELETE;
      break;

    case svn_wc_schedule_replace:
      valuestr = SVN_WC__ENTRY_VALUE_REPLACE;
      break;

    case svn_wc_schedule_normal:
    default:
      valuestr = NULL;
      break;
    }
  apr_hash_set (atts, SVN_WC__ENTRY_ATTR_SCHEDULE, APR_HASH_KEY_STRING, 
                valuestr);

  /* Conflicts */
  if (entry->conflict_old)
    apr_hash_set (atts, SVN_WC__ENTRY_ATTR_CONFLICT_OLD, APR_HASH_KEY_STRING,
                  entry->conflict_old);

  if (entry->conflict_new)
    apr_hash_set (atts, SVN_WC__ENTRY_ATTR_CONFLICT_NEW, APR_HASH_KEY_STRING,
                  entry->conflict_new);

  if (entry->conflict_wrk)
    apr_hash_set (atts, SVN_WC__ENTRY_ATTR_CONFLICT_WRK, APR_HASH_KEY_STRING,
                  entry->conflict_wrk);

  if (entry->prejfile)
    apr_hash_set (atts, SVN_WC__ENTRY_ATTR_PREJFILE, APR_HASH_KEY_STRING,
                  entry->prejfile);

  /* Copy-related Stuff */
  apr_hash_set (atts, SVN_WC__ENTRY_ATTR_COPIED, APR_HASH_KEY_STRING,
                (entry->copied ? "true" : NULL));

  if (SVN_IS_VALID_REVNUM (entry->copyfrom_rev))
    apr_hash_set (atts, SVN_WC__ENTRY_ATTR_COPYFROM_REV, APR_HASH_KEY_STRING,
                  apr_psprintf (pool, "%" SVN_REVNUM_T_FMT,
                                entry->copyfrom_rev));

  if (entry->copyfrom_url)
    apr_hash_set (atts, SVN_WC__ENTRY_ATTR_COPYFROM_URL, APR_HASH_KEY_STRING,
                  entry->copyfrom_url);

  /* Deleted state */
  apr_hash_set (atts, SVN_WC__ENTRY_ATTR_DELETED, APR_HASH_KEY_STRING,
                (entry->deleted ? "true" : NULL));

  /* Timestamps */
  if (entry->text_time)
    {
      apr_hash_set (atts, SVN_WC__ENTRY_ATTR_TEXT_TIME, APR_HASH_KEY_STRING,
                    svn_time_to_cstring (entry->text_time, pool));
    }
  if (entry->prop_time)
    {
      apr_hash_set (atts, SVN_WC__ENTRY_ATTR_PROP_TIME, APR_HASH_KEY_STRING,
                    svn_time_to_cstring (entry->prop_time, pool));
    }

  /* Checksum */
  if (entry->checksum)
    apr_hash_set (atts, SVN_WC__ENTRY_ATTR_CHECKSUM, APR_HASH_KEY_STRING,
                  entry->checksum);

  /* Last-commit Stuff */
  if (SVN_IS_VALID_REVNUM (entry->cmt_rev))
    apr_hash_set (atts, SVN_WC__ENTRY_ATTR_CMT_REV, APR_HASH_KEY_STRING,
                  apr_psprintf (pool, "%" SVN_REVNUM_T_FMT, entry->cmt_rev));

  if (entry->cmt_author)
    apr_hash_set (atts, SVN_WC__ENTRY_ATTR_CMT_AUTHOR, APR_HASH_KEY_STRING,
                  entry->cmt_author);

  if (entry->cmt_date)
    {
      apr_hash_set (atts, SVN_WC__ENTRY_ATTR_CMT_DATE, APR_HASH_KEY_STRING,
                    svn_time_to_cstring (entry->cmt_date, pool));
    }
    

  /*** Now, remove stuff that can be derived through inheritance rules. ***/

  /* We only want to write out 'revision' and 'url' for the
     following things:
     1. the current directory's "this dir" entry.
     2. non-directory entries:
        a. which are marked for addition (and consequently should
           have an invalid revnum) 
        b. whose revision or url is valid and different than 
           that of the "this dir" entry.
  */
  if (strcmp (name, SVN_WC_ENTRY_THIS_DIR))
    {
      /* This is NOT the "this dir" entry */
      if (! strcmp (name, "."))
        {
          /* By golly, if this isn't recognized as the "this dir"
             entry, and it looks like '.', we're just asking for an
             infinite recursion to happen.  Abort! */
          abort();
        }

      if (entry->kind == svn_node_dir)
        {
          /* We don't write url or revision for subdir
             entries. */
          apr_hash_set (atts, SVN_WC__ENTRY_ATTR_REVISION, APR_HASH_KEY_STRING,
                        NULL);
          apr_hash_set (atts, SVN_WC__ENTRY_ATTR_URL, APR_HASH_KEY_STRING,
                        NULL);
        }
      else
        {
          /* If this is not the "this dir" entry, and the revision is
             the same as that of the "this dir" entry, don't write out
             the revision. */
          if (entry->revision == this_dir->revision)
            apr_hash_set (atts, SVN_WC__ENTRY_ATTR_REVISION, 
                          APR_HASH_KEY_STRING, NULL);
          
          /* If this is not the "this dir" entry, and the url is
             trivially calculable from that of the "this dir" entry,
             don't write out the url */
          if (entry->url)
            {
              if (strcmp (entry->url,
                          svn_path_url_add_component (this_dir->url, 
                                                      name, pool)) == 0)
                apr_hash_set (atts, SVN_WC__ENTRY_ATTR_URL,
                              APR_HASH_KEY_STRING, NULL);
            }
        }
    }

  /* Append the entry onto the accumulating string. */
  svn_xml_make_open_tag_hash (output,
                              pool,
                              svn_xml_self_closing,
                              SVN_WC__ENTRIES_ENTRY,
                              atts);
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc__entries_write (apr_hash_t *entries,
                       svn_wc_adm_access_t *adm_access,
                       apr_pool_t *pool)
{
  svn_error_t *err = NULL;
  svn_stringbuf_t *bigstr = NULL;
  apr_file_t *outfile = NULL;
  apr_status_t apr_err;
  apr_hash_index_t *hi;
  svn_wc_entry_t *this_dir;

  SVN_ERR (svn_wc_adm_write_check (adm_access));

  /* Get a copy of the "this dir" entry for comparison purposes. */
  this_dir = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, 
                           APR_HASH_KEY_STRING);

  /* If there is no "this dir" entry, something is wrong. */
  if (! this_dir)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, NULL,
                              "No default entry in directory `%s'", 
                              svn_wc_adm_access_path (adm_access));

  /* Open entries file for writing.  It's important we don't use APR_EXCL
   * here.  Consider what happens if a log file is interrupted, it may
   * leave a .svn/tmp/entries file behind.  Then when cleanup reruns the
   * log file, and it attempts to modify the entries file, APR_EXCL would
   * cause an error that prevents cleanup running.  We don't use log file
   * tags such as SVN_WC__LOG_MV to move entries files so any existing file
   * is not "valuable".
   */
  SVN_ERR (svn_wc__open_adm_file (&outfile, svn_wc_adm_access_path (adm_access),
                                  SVN_WC__ADM_ENTRIES,
                                  (APR_WRITE | APR_CREATE),
                                  pool));

  svn_xml_make_header (&bigstr, pool);
  svn_xml_make_open_tag (&bigstr, pool, svn_xml_normal,
                         SVN_WC__ENTRIES_TOPLEVEL,
                         "xmlns",
                         SVN_XML_NAMESPACE,
                         NULL);

  /* Write out "this dir" */
  SVN_ERR (write_entry (&bigstr, this_dir, SVN_WC_ENTRY_THIS_DIR, 
                        this_dir, pool));

  for (hi = apr_hash_first (pool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      void *val;
      svn_wc_entry_t *this_entry;

      /* Get the entry and make sure its attributes are up-to-date. */
      apr_hash_this (hi, &key, NULL, &val);
      this_entry = val;

      /* Don't rewrite the "this dir" entry! */
      if (! strcmp (key, SVN_WC_ENTRY_THIS_DIR ))
        continue;

      /* Append the entry to BIGSTR */
      write_entry (&bigstr, this_entry, key, this_dir, pool);
    }

  svn_xml_make_close_tag (&bigstr, pool, SVN_WC__ENTRIES_TOPLEVEL);

  apr_err = apr_file_write_full (outfile, bigstr->data, bigstr->len, NULL);
  if (apr_err)
    err = svn_error_createf (apr_err, NULL,
                             "svn_wc__entries_write: %s",
                             svn_wc_adm_access_path (adm_access));
  else
    err = svn_wc__close_adm_file (outfile,
                                   svn_wc_adm_access_path (adm_access),
                                   SVN_WC__ADM_ENTRIES, 1, pool);

  svn_wc__adm_access_set_entries (adm_access, TRUE, entries);
  svn_wc__adm_access_set_entries (adm_access, FALSE, NULL);

  return err;
}


/* Update an entry NAME in ENTRIES, according to the combination of
   entry data found in ENTRY and masked by MODIFY_FLAGS. If the entry
   already exists, the requested changes will be folded (merged) into
   the entry's existing state.  If the entry doesn't exist, the entry
   will be created with exactly those properties described by the set
   of changes.

   POOL may be used to allocate memory referenced by ENTRIES.
 */
static void
fold_entry (apr_hash_t *entries,
            const char *name,
            apr_uint32_t modify_flags,
            svn_wc_entry_t *entry,
            apr_pool_t *pool)
{
  svn_wc_entry_t *cur_entry
    = apr_hash_get (entries, name, APR_HASH_KEY_STRING);
  
  assert (name != NULL);

  if (! cur_entry)
    cur_entry = alloc_entry (pool);

  /* Name (just a safeguard here, really) */
  if (! cur_entry->name)
    cur_entry->name = apr_pstrdup (pool, name);

  /* Revision */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_REVISION)
    cur_entry->revision = entry->revision;

  /* Ancestral URL in repository */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_URL)
    cur_entry->url = entry->url ? apr_pstrdup (pool, entry->url) : NULL;

  /* Kind */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_KIND)
    cur_entry->kind = entry->kind;

  /* Schedule */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE)
    cur_entry->schedule = entry->schedule;

  /* Checksum */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CHECKSUM)
    cur_entry->checksum = entry->checksum
                          ? apr_pstrdup (pool, entry->checksum)
                          : NULL;
  
  /* Copy-related stuff */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_COPIED)
    cur_entry->copied = entry->copied;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_COPYFROM_URL)
    cur_entry->copyfrom_url = entry->copyfrom_url 
                              ? apr_pstrdup (pool, entry->copyfrom_url)
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_COPYFROM_REV)
    cur_entry->copyfrom_rev = entry->copyfrom_rev;

  /* Deleted state */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_DELETED)
    cur_entry->deleted = entry->deleted;

  /* Text/prop modification times */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_TEXT_TIME)
    cur_entry->text_time = entry->text_time;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_PROP_TIME)
    cur_entry->prop_time = entry->prop_time;

  /* Conflict stuff */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CONFLICT_OLD)
    cur_entry->conflict_old = entry->conflict_old 
                              ? apr_pstrdup (pool, entry->conflict_old) 
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CONFLICT_NEW)
    cur_entry->conflict_new = entry->conflict_new
                              ? apr_pstrdup (pool, entry->conflict_new) 
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CONFLICT_WRK)
    cur_entry->conflict_wrk = entry->conflict_wrk
                              ? apr_pstrdup (pool, entry->conflict_wrk)
                              : NULL;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_PREJFILE)
    cur_entry->prejfile = entry->prejfile 
                          ? apr_pstrdup (pool, entry->prejfile)
                          : NULL;

  /* Last-commit stuff */
  if (modify_flags & SVN_WC__ENTRY_MODIFY_CMT_REV)
    cur_entry->cmt_rev = entry->cmt_rev;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CMT_DATE)
    cur_entry->cmt_date = entry->cmt_date;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_CMT_AUTHOR)
    cur_entry->cmt_author = entry->cmt_author
                            ? apr_pstrdup (pool, entry->cmt_author) 
                            : NULL;

  /* Absorb defaults from the parent dir, if any, unless this is a
     subdir entry. */
  if (cur_entry->kind != svn_node_dir)
    {
      svn_wc_entry_t *default_entry
        = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, APR_HASH_KEY_STRING);
      if (default_entry)
        take_from_entry (default_entry, cur_entry, pool);
    }

  /* Make sure the entry exists in the entries hash.  Possibly it
     already did, in which case this could have been skipped, but what
     the heck. */
  apr_hash_set (entries, cur_entry->name, APR_HASH_KEY_STRING, cur_entry);
}


void
svn_wc__entry_remove (apr_hash_t *entries, const char *name)
{
  apr_hash_set (entries, name, APR_HASH_KEY_STRING, NULL);
}


/* Our general purpose intelligence module for handling scheduling
   changes to a single entry.

   Given an entryname NAME in ENTRIES, examine the caller's requested
   change in *SCHEDULE and the current state of the entry.  Possibly
   modify *SCHEDULE and *MODIFY_FLAGS so that when merged, it will
   reflect the caller's original intent.

   POOL is used for local allocations only, calling this function does not
   use POOL to allocate any memory referenced by ENTRIES.
 */
static svn_error_t *
fold_scheduling (apr_hash_t *entries,
                 const char *name,
                 apr_uint32_t *modify_flags,
                 svn_wc_schedule_t *schedule,
                 apr_pool_t *pool)
{
  svn_wc_entry_t *entry, *this_dir_entry;

  /* If we're not supposed to be bothering with this anyway...return. */
  if (! (*modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE))
    return SVN_NO_ERROR;

  /* Get the current entry */
  entry = apr_hash_get (entries, name, APR_HASH_KEY_STRING);

  /* If we're not merging in changes, only the _add, _delete, _replace
     and _normal schedules are allowed. */
  if (*modify_flags & SVN_WC__ENTRY_MODIFY_FORCE)
    {
      switch (*schedule)
        {
        case svn_wc_schedule_add:
        case svn_wc_schedule_delete:
        case svn_wc_schedule_replace:
        case svn_wc_schedule_normal:
          /* Since we aren't merging in a change, not only are these
             schedules legal, but they are final.  */
          return SVN_NO_ERROR;

        default:
          return 
            svn_error_createf 
            (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
             "fold_state_changes: Illegal schedule in state set operation");
        }
    }

  /* The only operation valid on an item not already in revision
     control is addition. */
  if (! entry)
    {
      if (*schedule == svn_wc_schedule_add)
        return SVN_NO_ERROR;
      else
        return 
          svn_error_createf 
          (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
           "fold_state_changes: '%s' is not a versioned resource",
           name);
    }

  /* Get the default entry */
  this_dir_entry = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, 
                                 APR_HASH_KEY_STRING);

  /* At this point, we know the following things:

     1. There is already an entry for this item in the entries file
        whose existence is either _normal or _added (or about to
        become such), which for our purposes mean the same thing.

     2. We have been asked to merge in a state change, not to
        explicitly set the state.  */

  /* Here are some cases that are parent-directory sensitive.
     Basically, we make sure that we are not allowing versioned
     resources to just sorta dangle below directories marked for
     deletion. */
  if ((entry != this_dir_entry)
      && (this_dir_entry->schedule == svn_wc_schedule_delete))
    {
      if (*schedule == svn_wc_schedule_add)
        return 
          svn_error_createf 
          (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
           "fold_state_changes: Can't add '%s' to deleted directory"
           "--try undeleting its parent directory first",
           name);
      if (*schedule == svn_wc_schedule_replace)
        return 
          svn_error_createf 
          (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
           "fold_state_changes: Can't replace '%s' in deleted directory"
           "--try undeleting its parent directory first",
           name);
    }

  switch (entry->schedule)
    {
    case svn_wc_schedule_normal:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
          /* Normal is a trivial no-op case. Reset the
             schedule modification bit and move along. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;


        case svn_wc_schedule_delete:
        case svn_wc_schedule_replace:
          /* These are all good. */
          return SVN_NO_ERROR;
            

        case svn_wc_schedule_add:
          /* You can't add something that's already been added to
             revision control... unless it's got a 'deleted' state */
          if (! entry->deleted)
            return 
              svn_error_createf 
              (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
               "fold_state_changes: Entry '%s' already under revision control",
               name);
        }
      break;

    case svn_wc_schedule_add:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
        case svn_wc_schedule_add:
        case svn_wc_schedule_replace:
          /* These are all no-op cases.  Normal is obvious, as is add.
             Replace on an entry marked for addition breaks down to
             (add + (delete + add)), which resolves to just (add), and
             since this entry is already marked with (add), this too
             is a no-op. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;


        case svn_wc_schedule_delete:
          /* Not-yet-versioned item being deleted.  If the original
             entry was not marked as "deleted", then remove the entry.
             Else, return the entry to a 'normal' state, preserving
             the "deleted" flag.  Check that we are not trying to
             remove the SVN_WC_ENTRY_THIS_DIR entry as that would
             leave the entries file in an invalid state. */
          assert (entry != this_dir_entry);
          if (! entry->deleted)
            apr_hash_set (entries, name, APR_HASH_KEY_STRING, NULL);
          else
            *schedule = svn_wc_schedule_normal;
          return SVN_NO_ERROR;
        }
      break;

    case svn_wc_schedule_delete:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
        case svn_wc_schedule_delete:
          /* These are no-op cases. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;


        case svn_wc_schedule_add:
          /* Re-adding an entry marked for deletion?  This is really a
             replace operation. */
          *schedule = svn_wc_schedule_replace;
          return SVN_NO_ERROR;


        case svn_wc_schedule_replace:
          /* Replacing an item marked for deletion breaks down to
             (delete + (delete + add)), which might deserve a warning,
             but whatever. */
          return SVN_NO_ERROR;

        }
      break;

    case svn_wc_schedule_replace:
      switch (*schedule)
        {
        case svn_wc_schedule_normal:
          /* These are all no-op cases. */
        case svn_wc_schedule_add:
          /* Adding a to-be-replaced entry breaks down to ((delete +
             add) + add) which might deserve a warning, but we'll just
             no-op it. */
        case svn_wc_schedule_replace:
          /* Replacing a to-be-replaced entry breaks down to ((delete
             + add) + (delete + add)), which is insane!  Make up your
             friggin' mind, dude! :-)  Well, we'll no-op this one,
             too. */
          *modify_flags &= ~SVN_WC__ENTRY_MODIFY_SCHEDULE;
          return SVN_NO_ERROR;
          

        case svn_wc_schedule_delete:
          /* Deleting a to-be-replaced entry breaks down to ((delete +
             add) + delete) which resolves to a flat deletion. */
          *schedule = svn_wc_schedule_delete;
          return SVN_NO_ERROR;

        }
      break;

    default:
      return 
        svn_error_createf 
        (SVN_ERR_WC_SCHEDULE_CONFLICT, NULL,
         "fold_state_changes: Entry '%s' has illegal schedule",
         name);
    }
  return SVN_NO_ERROR;
}



svn_error_t *
svn_wc__entry_modify (svn_wc_adm_access_t *adm_access,
                      const char *name,
                      svn_wc_entry_t *entry,
                      apr_uint32_t modify_flags,
                      svn_boolean_t do_sync,
                      apr_pool_t *pool)
{
  apr_hash_t *entries;
  svn_boolean_t entry_was_deleted_p = FALSE;

  /* ENTRY is rather necessary, and ENTRY->kind is required to be valid! */
  assert (entry);

  /* Load PATH's whole entries file. */
  SVN_ERR (svn_wc_entries_read (&entries, adm_access, TRUE, pool));

  /* Ensure that NAME is valid. */
  if (name == NULL)
    name = SVN_WC_ENTRY_THIS_DIR;

  if (modify_flags & SVN_WC__ENTRY_MODIFY_SCHEDULE)
    {
      svn_wc_entry_t *entry_before, *entry_after;

      /* Keep a copy of the unmodified entry on hand. */
      entry_before = apr_hash_get (entries, name, APR_HASH_KEY_STRING);
      
      /* If scheduling changes were made, we have a special routine to
         manage those modifications. */
      SVN_ERR (fold_scheduling (entries, name, &modify_flags, 
                                &entry->schedule, pool));

      /* Special case:  fold_state_changes() may have actually REMOVED
         the entry in question!  If so, don't try to fold_entry, as
         this will just recreate the entry again. */
      entry_after = apr_hash_get (entries, name, APR_HASH_KEY_STRING);

      /* Note if this entry was deleted above so we don't accidentally
         re-add it in the following steps. */
      if (entry_before && (! entry_after))
        entry_was_deleted_p = TRUE;
    }

  /* If the entry wasn't just removed from the entries hash, fold the
     changes into the entry. */
  if (! entry_was_deleted_p)
    fold_entry (entries, name, modify_flags, entry,
                svn_wc_adm_access_pool (adm_access));

  /* Sync changes to disk. */
  if (do_sync)
    SVN_ERR (svn_wc__entries_write (entries, adm_access, pool));

  return SVN_NO_ERROR;
}


svn_wc_entry_t *
svn_wc_entry_dup (const svn_wc_entry_t *entry, apr_pool_t *pool)
{
  svn_wc_entry_t *dupentry = apr_pcalloc (pool, sizeof(*dupentry));

  /* Perform a trivial copy ... */
  *dupentry = *entry;

  /* ...and then re-copy stuff that needs to be duped into our pool. */
  if (entry->name)
    dupentry->name = apr_pstrdup (pool, entry->name);
  if (entry->url)
    dupentry->url = apr_pstrdup (pool, entry->url);
  if (entry->copyfrom_url)
    dupentry->copyfrom_url = apr_pstrdup (pool, entry->copyfrom_url);
  if (entry->conflict_old)
    dupentry->conflict_old = apr_pstrdup (pool, entry->conflict_old);
  if (entry->conflict_new)
    dupentry->conflict_new = apr_pstrdup (pool, entry->conflict_new);
  if (entry->conflict_wrk)
    dupentry->conflict_wrk = apr_pstrdup (pool, entry->conflict_wrk);
  if (entry->prejfile)
    dupentry->prejfile = apr_pstrdup (pool, entry->prejfile);
  if (entry->cmt_author)
    dupentry->cmt_author = apr_pstrdup (pool, entry->cmt_author);

  return dupentry;
}


svn_error_t *
svn_wc__tweak_entry (apr_hash_t *entries,
                     const char *name,
                     const char *new_url,
                     const svn_revnum_t new_rev,
                     apr_pool_t *pool)
{
  svn_wc_entry_t *entry;

  entry = apr_hash_get (entries, name, APR_HASH_KEY_STRING);
  if (! entry)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, NULL,
                              "No such entry: '%s'", name);

  if (new_url != NULL)
    entry->url = apr_pstrdup (pool, new_url);

  if ((SVN_IS_VALID_REVNUM (new_rev))
      && (entry->schedule != svn_wc_schedule_add)
      && (entry->schedule != svn_wc_schedule_replace))
    entry->revision = new_rev;

  /* As long as this function is only called as a helper to
     svn_wc__do_update_cleanup, then it's okay to totally remove any
     'deleted' entry.  The rationale is: if the server didn't
     overwrite the 'deleted' entry with something new during the
     update, then it *must* have meant for the entry to be permanently
     gone in the parent dir's revision. */
  if (entry->deleted)
    apr_hash_set (entries, name, APR_HASH_KEY_STRING, NULL);

  return SVN_NO_ERROR;
}




/*** Generic Entry Walker */


/* A recursive entry-walker, helper for svn_wc_walk_entries */
static svn_error_t *
walker_helper (const char *dirpath,
               svn_wc_adm_access_t *adm_access,
               const svn_wc_entry_callbacks_t *walk_callbacks,
               void *walk_baton,
               svn_boolean_t show_deleted,
               apr_pool_t *pool)
{
  apr_pool_t *subpool = svn_pool_create (pool);
  apr_hash_t *entries;
  apr_hash_index_t *hi;
  svn_wc_entry_t *dot_entry;

  SVN_ERR (svn_wc_entries_read (&entries, adm_access, show_deleted, subpool));
  
  /* As promised, always return the '.' entry first. */
  dot_entry = apr_hash_get (entries, SVN_WC_ENTRY_THIS_DIR, 
                            APR_HASH_KEY_STRING);
  if (! dot_entry)
    return svn_error_createf (SVN_ERR_ENTRY_NOT_FOUND, NULL,
                              "Directory '%s' has no THIS_DIR entry!",
                              dirpath);

  SVN_ERR (walk_callbacks->found_entry (dirpath, dot_entry, walk_baton));

  /* Loop over each of the other entries. */
  for (hi = apr_hash_first (subpool, entries); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      const svn_wc_entry_t *current_entry; 
      const char *entrypath;

      apr_hash_this (hi, &key, &klen, &val);
      current_entry = val;

      if (strcmp (current_entry->name, SVN_WC_ENTRY_THIS_DIR) == 0)
        continue;

      entrypath = svn_path_join (dirpath, key, subpool);
      SVN_ERR (walk_callbacks->found_entry (entrypath, current_entry,
                                            walk_baton));

      if (current_entry->kind == svn_node_dir)
        {
          svn_wc_adm_access_t *entry_access;
          SVN_ERR (svn_wc_adm_retrieve (&entry_access, adm_access, entrypath,
                                        subpool));
          SVN_ERR (walker_helper (entrypath, entry_access,
                                  walk_callbacks, walk_baton,
                                  show_deleted, subpool));
        }
    }

  svn_pool_destroy (subpool);
  return SVN_NO_ERROR;
}


/* The public function */
svn_error_t *
svn_wc_walk_entries (const char *path,
                     svn_wc_adm_access_t *adm_access,
                     const svn_wc_entry_callbacks_t *walk_callbacks,
                     void *walk_baton,
                     svn_boolean_t show_deleted,
                     apr_pool_t *pool)
{
  const svn_wc_entry_t *entry;
  
  SVN_ERR (svn_wc_entry (&entry, path, adm_access, show_deleted, pool));

  if (! entry)
    return svn_error_createf (SVN_ERR_UNVERSIONED_RESOURCE, NULL,
                              "%s is not under revision control.", path);

  if (entry->kind == svn_node_file)
    return walk_callbacks->found_entry (path, entry, walk_baton);

  else if (entry->kind == svn_node_dir)
    return walker_helper (path, adm_access, walk_callbacks, walk_baton,
                          show_deleted, pool);

  else
    return svn_error_createf (SVN_ERR_NODE_UNKNOWN_KIND, NULL,
                              "%s: unrecognized node kind.", path);
}
