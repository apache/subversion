/*
 * props.c :  routines dealing with properties in the working copy
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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



#include <stdio.h>       /* temporary, for printf() */
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_file_io.h>
#include <apr_strings.h>
#include "svn_types.h"
#include "svn_delta.h"
#include "svn_string.h"
#include "svn_path.h"
#include "svn_xml.h"
#include "svn_error.h"
#include "svn_io.h"
#include "svn_hash.h"
#include "svn_wc.h"

#include "wc.h"


/*---------------------------------------------------------------------*/

/*** Deducing local changes to properties ***/

/* Given two property hashes (working copy and `base'), deduce what
   propchanges the user has made since the last update.  Return these
   changes as a series of (svn_prop_t *) objects stored in
   LOCAL_PROPCHANGES, allocated from POOL.  */

/* For note, here's a quick little table describing the logic of this
   routine:

   basehash        localhash         event
   --------        ---------         -----
   value = foo     value = NULL      Deletion occurred.
   value = foo     value = bar       Set occurred (modification)
   value = NULL    value = baz       Set occurred (creation)
*/

svn_error_t *
svn_wc__get_local_propchanges (apr_array_header_t **local_propchanges,
                               apr_hash_t *localprops,
                               apr_hash_t *baseprops,
                               apr_pool_t *pool)
{
  apr_hash_index_t *hi;
  apr_array_header_t *ary
    =  apr_array_make (pool, 1, sizeof(svn_prop_t *));

  /* Loop over baseprops and examine each key.  This will allow us to
     detect any `deletion' events or `set-modification' events.  */
  for (hi = apr_hash_first (pool, baseprops); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_stringbuf_t *propval1, *propval2;

      /* Get next property */
      apr_hash_this (hi, &key, &klen, &val);
      propval1 = (svn_stringbuf_t *) val;

      /* Does property name exist in localprops? */
      propval2 = (svn_stringbuf_t *) apr_hash_get (localprops, key, klen);

      if (propval2 == NULL)
        {
          /* Add a delete event to the array */
          svn_prop_t *p = apr_pcalloc (pool, sizeof(*p));
          p->name = svn_stringbuf_ncreate ((char *) key, klen, pool);
          p->value = NULL;
          
          *((svn_prop_t **)apr_array_push (ary)) = p;
        }
      else if (! svn_stringbuf_compare (propval1, propval2))
        {
          /* Add a set (modification) event to the array */
          svn_prop_t *p = apr_pcalloc (pool, sizeof(*p));
          p->name = svn_stringbuf_ncreate ((char *) key, klen, pool);
          p->value = propval2;
          
          *((svn_prop_t **)apr_array_push (ary)) = p;
        }
    }

  /* Loop over localprops and examine each key.  This allows us to
     detect `set-creation' events */
  for (hi = apr_hash_first (pool, localprops); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_ssize_t klen;
      void *val;
      svn_stringbuf_t *propval1, *propval2;

      /* Get next property */
      apr_hash_this (hi, &key, &klen, &val);
      propval2 = (svn_stringbuf_t *) val;

      /* Does property name exist in baseprops? */
      propval1 = (svn_stringbuf_t *) apr_hash_get (baseprops, key, klen);

      if (propval1 == NULL)
        {
          /* Add a set (creation) event to the array */
          svn_prop_t *p = apr_pcalloc (pool, sizeof(*p));
          p->name = svn_stringbuf_ncreate ((char *) key, klen, pool);
          p->value = propval2;
          
          *((svn_prop_t **)apr_array_push (ary)) = p;
        }
    }


  /* Done building our array of user events. */
  *local_propchanges = ary;

  return SVN_NO_ERROR;
}


/*---------------------------------------------------------------------*/

/*** Detecting a property conflict ***/


/* Given two propchange objects, return TRUE iff they conflict.  If
   there's a conflict, DESCRIPTION will contain an english description
   of the problem. */

/* For note, here's the table being implemented:

              |  update set     |    update delete   |
  ------------|-----------------|--------------------|
  user set    | conflict iff    |      conflict      |
              |  vals differ    |                    |
  ------------|-----------------|--------------------|
  user delete |   conflict      |      merge         |
              |                 |    (no problem)    |
  ----------------------------------------------------

*/
svn_boolean_t
svn_wc__conflicting_propchanges_p (svn_stringbuf_t **description,
                                   svn_prop_t *local,
                                   svn_prop_t *update,
                                   apr_pool_t *pool)
{
  /* We're assuming that whoever called this routine has already
     deduced that local and change2 affect the same property name.
     (After all, if they affect different property names, how can they
     possibly conflict?)  But still, let's make this routine
     `complete' by checking anyway. */
  if (! svn_stringbuf_compare (local->name, update->name))
    return FALSE;  /* no conflict */

  /* If one change wants to delete a property and the other wants to
     set it, this is a conflict.  This check covers two bases of our
     chi-square. */
  if ((local->value != NULL) && (update->value == NULL))
    {
      *description =
        svn_stringbuf_createf
        (pool, "prop `%s': user set value to '%s', but update deletes it.\n",
         local->name->data, local->value->data);
      return TRUE;  /* conflict */
    }
  if ((local->value == NULL) && (update->value != NULL))
    {
      *description =
        svn_stringbuf_createf
        (pool, "prop `%s': user deleted, but update sets it to '%s'.\n",
         local->name->data, update->value->data);
      return TRUE;  /* conflict */
    }

  /* If both changes delete the same property, there's no conflict.
     It's an implicit merge.  :)  */
  if ((local->value == NULL) && (update->value == NULL))
    return FALSE;  /* no conflict */

  /* If both changes set the property, it's a conflict iff the values
     are different */
  else if (! svn_stringbuf_compare (local->value, update->value))
    {
      *description =
        svn_stringbuf_createf
        (pool, "prop `%s': user set to '%s', but update set to '%s'.\n",
         local->name->data, local->value->data, update->value->data);
      return TRUE;  /* conflict */
    }
  else
    /* values are the same, so another implicit merge. */
    return FALSE;  /* no conflict */

  /* Default (will anyone ever reach this line?) */
  return FALSE;  /* no conflict found */
}



/*---------------------------------------------------------------------*/

/*** Reading/writing property hashes from disk ***/

/* The real functionality here is part of libsvn_subr, in hashdump.c.
   But these are convenience routines for use in in libsvn_wc. */



/* If PROPFILE_PATH exists (and is a file), assume it's full of
   properties and load this file into HASH.  Otherwise, leave HASH
   untouched.  */
svn_error_t *
svn_wc__load_prop_file (svn_stringbuf_t *propfile_path,
                        apr_hash_t *hash,
                        apr_pool_t *pool)
{
  svn_error_t *err;
  enum svn_node_kind kind;

  err = svn_io_check_path (propfile_path, &kind, pool);
  if (err) return err;

  if (kind == svn_node_file)
    {
      /* Ah, this file already has on-disk properties.  Load 'em. */
      apr_status_t status;
      apr_file_t *propfile = NULL;

      status = apr_file_open (&propfile, propfile_path->data,
                         APR_READ, APR_OS_DEFAULT, pool);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "load_prop_file: can't open `%s'",
                                  propfile_path->data);

      status = svn_hash_read (hash, svn_pack_bytestring,
                              propfile, pool);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "load_prop_file:  can't parse `%s'",
                                  propfile_path->data);

      status = apr_file_close (propfile);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "load_prop_file: can't close `%s'",
                                  propfile_path->data);
    }

  return SVN_NO_ERROR;
}



/* Given a HASH full of property name/values, write them to a file
   located at PROPFILE_PATH.  */
svn_error_t *
svn_wc__save_prop_file (svn_stringbuf_t *propfile_path,
                        apr_hash_t *hash,
                        apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_file_t *prop_tmp;

  apr_err = apr_file_open (&prop_tmp, propfile_path->data,
                      (APR_WRITE | APR_CREATE),
                      APR_OS_DEFAULT, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "save_prop_file: can't open `%s'",
                              propfile_path->data);

  apr_err = svn_hash_write (hash, svn_unpack_bytestring,
                            prop_tmp, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "save_prop_file: can't write prop hash to `%s'",
                              propfile_path->data);

  apr_err = apr_file_close (prop_tmp);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "save_prop_file: can't close `%s'",
                              propfile_path->data);

  return SVN_NO_ERROR;
}


/*---------------------------------------------------------------------*/

/*** Misc ***/

/* Assuming FP is a filehandle already open for appending, write
   CONFLICT_DESCRIPTION to file. */
static svn_error_t *
append_prop_conflict (apr_file_t *fp,
                      svn_stringbuf_t *conflict_description,
                      apr_pool_t *pool)
{
  /* TODO:  someday, perhaps prefix each conflict_description with a
     timestamp or something? */
  apr_size_t written;
  apr_status_t status;

  status = apr_file_write_full (fp, conflict_description->data,
                           conflict_description->len, &written);
  if (status)
    return svn_error_create (status, 0, NULL, pool,
                             "append_prop_conflict: apr_file_write_full failed.");
  return SVN_NO_ERROR;
}


/* Look up the entry NAME within PATH and see if it has a `current'
   reject file describing a state of conflict.  If such a file exists,
   return the name of the file in REJECT_FILE.  If no such file exists,
   return (REJECT_FILE = NULL). */
svn_error_t *
svn_wc__get_existing_prop_reject_file (svn_stringbuf_t **reject_file,
                                       svn_stringbuf_t *path,
                                       const svn_stringbuf_t *name,
                                       apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_t *entries, *atts;
  svn_wc_entry_t *the_entry;

  err = svn_wc_entries_read (&entries, path, pool);
  if (err) return err;

  the_entry = 
    (svn_wc_entry_t *) apr_hash_get (entries, name->data, name->len);

  if (! the_entry)
    return svn_error_createf
      (SVN_ERR_WC_ENTRY_NOT_FOUND, 0, NULL, pool,
       "get_existing_reject_prop_reject_file: can't find entry '%s' in '%s'",
       name->data, path->data);

  atts = the_entry->attributes;
  
  *reject_file = 
    (svn_stringbuf_t *) apr_hash_get (atts, SVN_WC_ENTRY_ATTR_PREJFILE,
                                      APR_HASH_KEY_STRING);

  return SVN_NO_ERROR;
}



/*---------------------------------------------------------------------*/

/*** Merging propchanges into the working copy ***/

/* This routine is called by the working copy update editor, by both
   close_file() and close_dir(): */


svn_error_t *
svn_wc__do_property_merge (svn_stringbuf_t *path,
                           const svn_stringbuf_t *name,
                           apr_array_header_t *propchanges,
                           apr_pool_t *pool,
                           svn_stringbuf_t **entry_accum)
{
  int i;
  svn_error_t *err;
  svn_boolean_t is_dir;
  const char * str;
  apr_off_t len;
  
  /* Zillions of pathnames to compute!  yeargh!  */
  svn_stringbuf_t *base_propfile_path, *local_propfile_path;
  svn_stringbuf_t *base_prop_tmp_path, *local_prop_tmp_path;
  svn_stringbuf_t *tmp_prop_base, *real_prop_base;
  svn_stringbuf_t *tmp_props, *real_props;

  svn_stringbuf_t *entryname;
  svn_stringbuf_t *full_path;
  
  apr_array_header_t *local_propchanges; /* propchanges that the user
                                            has made since last update */
  apr_hash_t *localhash;   /* all `working' properties */
  apr_hash_t *basehash;    /* all `pristine' properties */

  /* For writing conflicts to a .prej file */
  apr_file_t *reject_fp = NULL;           /* the real conflicts file */
  svn_stringbuf_t *reject_path = NULL;

  apr_file_t *reject_tmp_fp = NULL;       /* the temporary conflicts file */
  svn_stringbuf_t *reject_tmp_path = NULL;

  if (name == NULL)
    {
      /* We must be merging props on the directory PATH  */
      entryname = svn_stringbuf_create (SVN_WC_ENTRY_THIS_DIR, pool);
      full_path = path;
      is_dir = TRUE;
    }
  else
    {
      /* We must be merging props on the file PATH/NAME */
      entryname = svn_stringbuf_dup (name, pool);
      full_path = svn_stringbuf_dup (path, pool);
      svn_path_add_component (full_path, name, svn_path_local_style);
      is_dir = FALSE;
    }

  /* Get paths to the local and pristine property files. */
  err = svn_wc__prop_path (&local_propfile_path, full_path, 0, pool);
  if (err) return err;
  
  err = svn_wc__prop_base_path (&base_propfile_path, full_path, 0, pool);
  if (err) return err;

  /* Load the base & working property files into hashes */
  localhash = apr_hash_make (pool);
  basehash = apr_hash_make (pool);
  
  err = svn_wc__load_prop_file (base_propfile_path,
                                basehash, pool);
  if (err) return err;
  
  err = svn_wc__load_prop_file (local_propfile_path,
                                localhash, pool);
  if (err) return err;
  
  /* Deduce any local propchanges the user has made since the last
     update.  */
  err = svn_wc__get_local_propchanges (&local_propchanges,
                                       localhash, basehash, pool);
  if (err) return err;
  
  /* Looping over the array of `update' propchanges we want to apply: */
  for (i = 0; i < propchanges->nelts; i++)
    {
      int j;
      int found_match = 0;          
      svn_stringbuf_t *conflict_description;
      svn_prop_t *update_change, *local_change = NULL;
      
      update_change = (((svn_prop_t **)(propchanges)->elts)[i]);
      
      /* Apply the update_change to the pristine hash, no
         questions asked. */
      apr_hash_set (basehash,
                    update_change->name->data,
                    update_change->name->len,
                    update_change->value);
      
      /* Now, does the update_change conflict with some local change?  */
      
      /* First check if the property name even exists in our list
         of local changes... */
      for (j = 0; j < local_propchanges->nelts; j++)
        {
          local_change =
            (((svn_prop_t **)(local_propchanges)->elts)[j]);
          
          if (svn_stringbuf_compare (local_change->name, update_change->name))
            {
              found_match = 1;
              break;
            }
        }
      
      if (found_match)
        /* Now see if the two changes actually conflict */
        if (svn_wc__conflicting_propchanges_p (&conflict_description,
                                               local_change,
                                               update_change,
                                               pool))
          {
            /* Found a conflict! */
            
            if (! reject_tmp_fp)
              {
                /* This is the very first prop conflict found on this
                   node. */
                svn_stringbuf_t *tmppath;
                svn_stringbuf_t *tmpname;

                /* Get path to /temporary/ local prop file */
                err = svn_wc__prop_path (&tmppath, full_path, 1, pool);
                if (err) return err;

                /* Reserve a .prej file based on it.  */
                err = svn_io_open_unique_file (&reject_tmp_fp,
                                               &reject_tmp_path,
                                               tmppath,
                                               SVN_WC__PROP_REJ_EXT,
                                               FALSE,
                                               pool);
                if (err) return err;

                /* reject_tmp_path is an absolute path at this point,
                   but that's no good for us.  We need to convert this
                   path to a *relative* path to use in the logfile. */
                tmpname = svn_path_last_component (reject_tmp_path,
                                                   svn_path_local_style,
                                                   pool);

                if (is_dir)
                  {
                    /* Dealing with directory "path" */
                    reject_tmp_path = 
                      svn_wc__adm_path (svn_stringbuf_create ("", pool),
                                        TRUE, /* use tmp */
                                        pool,
                                        tmpname->data,
                                        NULL);
                  }
                else
                  {
                    /* Dealing with file "path/name" */
                    reject_tmp_path = 
                      svn_wc__adm_path (svn_stringbuf_create ("", pool),
                                        TRUE, 
                                        pool,
                                        SVN_WC__ADM_PROPS,
                                        tmpname->data,
                                        NULL);
                  }               
              }

            /* Append the conflict to the open tmp/PROPS/---.prej file */
            err = append_prop_conflict (reject_tmp_fp,
                                        conflict_description,
                                        pool);
            if (err) return err;

            continue;  /* skip to the next update_change */
          }
      
      /* If we reach this point, there's no conflict, so we can safely
         apply the update_change to our working property hash. */
      apr_hash_set (localhash,
                    update_change->name->data,
                    update_change->name->len,
                    update_change->value);
    }
  
  
  /* Done merging property changes into both pristine and working
  hashes.  Now we write them to temporary files.  Notice that the
  paths computed are ABSOLUTE pathnames, which is what our disk
  routines require.*/

  err = svn_wc__prop_base_path (&base_prop_tmp_path, full_path, 1, pool);
  if (err) return err;

  err = svn_wc__prop_path (&local_prop_tmp_path, full_path, 1, pool);
  if (err) return err;
  
  /* Write the merged pristine prop hash to either
     path/.svn/tmp/prop-base/name or path/.svn/tmp/dir-prop-base */
  err = svn_wc__save_prop_file (base_prop_tmp_path, basehash, pool);
  if (err) return err;
  
  /* Write the merged local prop hash to path/.svn/tmp/props/name or
     path/.svn/tmp/dir-props */
  err = svn_wc__save_prop_file (local_prop_tmp_path, localhash, pool);
  if (err) return err;
  
  /* Compute pathnames for the "mv" log entries.  Notice that these
     paths are RELATIVE pathnames (each beginning with ".svn/"), so
     that each .svn subdir remains separable when executing run_log().  */
  str = strstr (base_prop_tmp_path->data, SVN_WC_ADM_DIR_NAME);
  len = base_prop_tmp_path->data + base_prop_tmp_path->len - str;
  tmp_prop_base = svn_stringbuf_ncreate (str, len, pool);

  str = strstr (base_propfile_path->data, SVN_WC_ADM_DIR_NAME);
  len = base_propfile_path->data + base_propfile_path->len - str;
  real_prop_base = svn_stringbuf_ncreate (str, len, pool);

  str = strstr (local_prop_tmp_path->data, SVN_WC_ADM_DIR_NAME);
  len = local_prop_tmp_path->data + local_prop_tmp_path->len - str;
  tmp_props = svn_stringbuf_ncreate (str, len, pool);

  str = strstr (local_propfile_path->data, SVN_WC_ADM_DIR_NAME);
  len = local_propfile_path->data + local_propfile_path->len - str;
  real_props = svn_stringbuf_ncreate (str, len, pool);
  
  /* Write log entry to move pristine tmp copy to real pristine area. */
  svn_xml_make_open_tag (entry_accum,
                         pool,
                         svn_xml_self_closing,
                         SVN_WC__LOG_MV,
                         SVN_WC__LOG_ATTR_NAME,
                         tmp_prop_base,
                         SVN_WC__LOG_ATTR_DEST,
                         real_prop_base,
                         NULL);

  /* Write log entry to move working tmp copy to real working area. */
  svn_xml_make_open_tag (entry_accum,
                         pool,
                         svn_xml_self_closing,
                         SVN_WC__LOG_MV,
                         SVN_WC__LOG_ATTR_NAME,
                         tmp_props,
                         SVN_WC__LOG_ATTR_DEST,
                         real_props,
                         NULL);

  if (reject_tmp_fp)
    {
      /* There's a .prej file sitting in .svn/tmp/ somewhere.  Deal
         with the conflicts.  */

      /* First, _close_ this temporary conflicts file.  We've been
         appending to it all along. */
      apr_status_t status;
      status = apr_file_close (reject_tmp_fp);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "do_property_merge: can't close '%s'",
                                  reject_tmp_path->data);
                                  
      /* Now try to get the name of a pre-existing .prej file from the
         entries file */
      err = svn_wc__get_existing_prop_reject_file (&reject_path,
                                                   path,
                                                   entryname,
                                                   pool);
      if (err) return err;

      if (! reject_path)
        {
          /* Reserve a new .prej file *above* the .svn/ directory by
             opening and closing it. */
          svn_stringbuf_t *reserved_path;
          svn_stringbuf_t *full_reject_path = svn_stringbuf_dup (path, pool);

          if (is_dir)
            svn_path_add_component (full_reject_path,
                                    svn_stringbuf_create
                                    (SVN_WC__THIS_DIR_PREJ,
                                     pool),
                                    svn_path_local_style);
          else
            svn_path_add_component (full_reject_path, name,
                                    svn_path_local_style);

          err = svn_io_open_unique_file (&reject_fp,
                                         &reserved_path,
                                         full_reject_path,
                                         SVN_WC__PROP_REJ_EXT,
                                         FALSE,
                                         pool);
          if (err) return err;

          status = apr_file_close (reject_fp);
          if (status)
            return svn_error_createf (status, 0, NULL, pool,
                                      "do_property_merge: can't close '%s'",
                                      full_reject_path->data);
          
          /* This file will be overwritten when the log is run; that's
             ok, because at least now we have a reservation on
             disk. */

          /* Now just get the name of the reserved file.  This is the
             "relative" path we will use in the log entry. */
          reject_path = svn_path_last_component (reserved_path,
                                                 svn_path_local_style,
                                                 pool);
        }

      /* We've now guaranteed that some kind of .prej file exists
         above the .svn/ dir.  We write log entries to append our
         conflicts to it. */      
      svn_xml_make_open_tag (entry_accum,
                             pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_APPEND,
                             SVN_WC__LOG_ATTR_NAME,
                             reject_tmp_path,
                             SVN_WC__LOG_ATTR_DEST,
                             reject_path,
                             NULL);

      /* And of course, delete the temporary reject file. */
      svn_xml_make_open_tag (entry_accum,
                             pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_RM,
                             SVN_WC__LOG_ATTR_NAME,
                             reject_tmp_path,
                             NULL);
      

      /* Mark entry as "conflicted" with a particular .prej file. */
      svn_xml_make_open_tag (entry_accum,
                             pool,
                             svn_xml_self_closing,
                             SVN_WC__LOG_MODIFY_ENTRY,
                             SVN_WC__LOG_ATTR_NAME,
                             entryname,
                             SVN_WC_ENTRY_ATTR_CONFLICTED,
                             svn_stringbuf_create ("true", pool),
                             SVN_WC_ENTRY_ATTR_PREJFILE,
                             reject_path,
                             NULL);      

    } /* if (reject_tmp_fp) */
  
  /* At this point, we need to write log entries that bump revision
     number and set new entry timestamps.  The caller of this function
     should (hopefully) add those commands to the log accumulator. */

  return SVN_NO_ERROR;
}

/*------------------------------------------------------------------*/

/*** Private 'wc prop' functions ***/

/* A clone of svn_wc_prop_list, for the most part, except that it
   returns 'wc' props instead of normal props.  */
static svn_error_t *
wcprop_list (apr_hash_t **props,
             svn_stringbuf_t *path,
             apr_pool_t *pool)
{
  svn_error_t *err;
  enum svn_node_kind kind, pkind;
  svn_stringbuf_t *prop_path;
  
  *props = apr_hash_make (pool);

  /* Check validity of PATH */
  err = svn_io_check_path (path, &kind, pool);
  if (err) return err;
  
  if (kind == svn_node_none)
    return svn_error_createf (SVN_ERR_BAD_FILENAME, 0, NULL, pool,
                              "wcprop_list: non-existent path '%s'.",
                              path->data);
  
  if (kind == svn_node_unknown)
    return svn_error_createf (SVN_ERR_UNKNOWN_NODE_KIND, 0, NULL, pool,
                              "wcprop_list: unknown node kind: '%s'.",
                              path->data);

  /* Construct a path to the relevant property file */
  err = svn_wc__wcprop_path (&prop_path, path, 0, pool);
  if (err) return err;

  /* Does the property file exist? */
  err = svn_io_check_path (prop_path, &pkind, pool);
  if (err) return err;
  
  if (pkind == svn_node_none)
    /* No property file exists.  Just go home, with an empty hash. */
    return SVN_NO_ERROR;
  
  /* else... */

  err = svn_wc__load_prop_file (prop_path, *props, pool);
  if (err) return err;

  return SVN_NO_ERROR;
}


/* This is what RA_DAV will use to fetch 'wc' properties.  It will be
   passed to ra_session_baton->do_commit(). */
svn_error_t *
svn_wc__wcprop_get (svn_stringbuf_t **value,
                    svn_stringbuf_t *name,
                    svn_stringbuf_t *path,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_t *prophash;

  err = wcprop_list (&prophash, path, pool);
  if (err)
    return
      svn_error_quick_wrap
      (err, "svn_wc__wcprop_get: failed to load props from disk.");

  *value = apr_hash_get (prophash, name->data, name->len);

  return SVN_NO_ERROR;
}



/* This is what RA_DAV will use to store 'wc' properties.  It will be
   passed to ra_session_baton->do_commit(). */
svn_error_t *
svn_wc__wcprop_set (svn_stringbuf_t *name,
                    svn_stringbuf_t *value,
                    svn_stringbuf_t *path,
                    apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_hash_t *prophash;
  apr_file_t *fp = NULL;

  err = wcprop_list (&prophash, path, pool);
  if (err)
    return
      svn_error_quick_wrap
      (err, "svn_wc__wcprop_set: failed to load props from disk.");

  /* Now we have all the properties in our hash.  Simply merge the new
     property into it. */
  apr_hash_set (prophash, name->data, name->len, value);

  /* Open the propfile for writing. */
  SVN_ERR (svn_wc__open_props (&fp, 
                               path, /* open in PATH */
                               (APR_WRITE | APR_CREATE),
                               0, /* not base props */
                               1, /* we DO want wcprops */
                               pool));
  /* Write. */
  apr_err = svn_hash_write (prophash, svn_unpack_bytestring, fp, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "can't write prop hash for %s", path->data);
  
  /* Close file, and doing an atomic "move". */
  SVN_ERR (svn_wc__close_props (fp, path, 0, 1,
                                1, /* sync! */
                                pool));

  return SVN_NO_ERROR;
}




/*------------------------------------------------------------------*/

/*** Public Functions ***/


svn_error_t *
svn_wc_prop_list (apr_hash_t **props,
                  svn_stringbuf_t *path,
                  apr_pool_t *pool)
{
  svn_error_t *err;
  enum svn_node_kind pkind;
  svn_stringbuf_t *prop_path;
  
  *props = apr_hash_make (pool);

  /* Construct a path to the relevant property file */
  err = svn_wc__prop_path (&prop_path, path, 0, pool);
  if (err) return err;

  /* Does the property file exist? */
  err = svn_io_check_path (prop_path, &pkind, pool);
  if (err) return err;
  
  if (pkind == svn_node_none)
    /* No property file exists.  Just go home, with an empty hash. */
    return SVN_NO_ERROR;
  
  /* else... */

  err = svn_wc__load_prop_file (prop_path, *props, pool);
  if (err) return err;

  return SVN_NO_ERROR;
}





svn_error_t *
svn_wc_prop_get (svn_stringbuf_t **value,
                 svn_stringbuf_t *name,
                 svn_stringbuf_t *path,
                 apr_pool_t *pool)
{
  svn_error_t *err;
  apr_hash_t *prophash;

  /* Boy, this is an easy routine! */
  err = svn_wc_prop_list (&prophash, path, pool);
  if (err)
    return
      svn_error_quick_wrap
      (err, "svn_wc_prop_get: failed to load props from disk.");

  *value = apr_hash_get (prophash, name->data, name->len);

  return SVN_NO_ERROR;
}




svn_error_t *
svn_wc_prop_set (svn_stringbuf_t *name,
                 svn_stringbuf_t *value,
                 svn_stringbuf_t *path,
                 apr_pool_t *pool)
{
  svn_error_t *err;
  apr_status_t apr_err;
  apr_hash_t *prophash;
  apr_file_t *fp = NULL;

  err = svn_wc_prop_list (&prophash, path, pool);
  if (err)
    return
      svn_error_quick_wrap
      (err, "svn_wc_prop_set: failed to load props from disk.");

  /* Now we have all the properties in our hash.  Simply merge the new
     property into it. */
  apr_hash_set (prophash, name->data, name->len, value);
  
  /* Open the propfile for writing. */
  SVN_ERR (svn_wc__open_props (&fp, 
                               path, /* open in PATH */
                               (APR_WRITE | APR_CREATE),
                               0, /* not base props */
                               0, /* not wcprops */
                               pool));
  /* Write. */
  apr_err = svn_hash_write (prophash, svn_unpack_bytestring, fp, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "can't write prop hash for %s", path->data);
  
  /* Close file, and doing an atomic "move". */
  SVN_ERR (svn_wc__close_props (fp, path, 0, 0,
                                1, /* sync! */
                                pool));


  return SVN_NO_ERROR;
}


svn_boolean_t
svn_wc_is_normal_prop (svn_stringbuf_t *name)
{
  size_t wc_prefix_len = sizeof (SVN_PROP_WC_PREFIX) - 1;
  size_t entry_prefix_len = sizeof (SVN_PROP_ENTRY_PREFIX) - 1;

  /* quick answer */
  if ((name->len < wc_prefix_len) && (name->len < entry_prefix_len))
    return TRUE;

  if ((strncmp (name->data, SVN_PROP_WC_PREFIX, wc_prefix_len) == 0)
      || (strncmp (name->data, SVN_PROP_ENTRY_PREFIX, entry_prefix_len) == 0))
    return FALSE;
  else
    return TRUE;
}



svn_boolean_t
svn_wc_is_wc_prop (svn_stringbuf_t *name)
{
  size_t prefix_len = sizeof (SVN_PROP_WC_PREFIX) - 1;

  if ((name->len < prefix_len)
      || (strncmp (name->data, SVN_PROP_WC_PREFIX, prefix_len) != 0))
    return FALSE;
  else
    return TRUE;
}


svn_boolean_t
svn_wc_is_entry_prop (svn_stringbuf_t *name)
{
  size_t prefix_len = sizeof (SVN_PROP_ENTRY_PREFIX) - 1;

  if ((name->len < prefix_len)
      || (strncmp (name->data, SVN_PROP_ENTRY_PREFIX, prefix_len) != 0))
    return FALSE;
  else
    return TRUE;
}


void
svn_wc__strip_entry_prefix (svn_stringbuf_t *name)
{
  size_t prefix_len = sizeof (SVN_PROP_ENTRY_PREFIX) - 1;

  if (! svn_wc_is_entry_prop (name))
    return;

  name->data += prefix_len;
  name->len -= prefix_len;
}



svn_error_t *
svn_wc__get_eol_style (enum svn_wc__eol_style *style,
                       const char **eol,
                       const char *path,
                       apr_pool_t *pool)
{
  /* I hate stringbufs. */
  svn_stringbuf_t *propval;
  svn_stringbuf_t *propname = svn_stringbuf_create (SVN_PROP_EOL_STYLE, pool);

  /* Get the property value. */
  SVN_ERR (svn_wc_prop_get (&propval, propname,
                            svn_stringbuf_create (path, pool), pool));

  /* Convert it. */
  svn_wc__eol_style_from_value (style, eol,
                                propval ? propval->data : NULL);

  return SVN_NO_ERROR;
}


void 
svn_wc__eol_style_from_value (enum svn_wc__eol_style *style,
                              const char **eol,
                              const char *value)
{
  if (value == NULL)
    {
      /* property dosen't exist. */
      *style = svn_wc__eol_style_none;
      *eol = NULL;
    }
  else if (! strcmp ("native", value))
    {
      *style = svn_wc__eol_style_native;
      *eol = APR_EOL_STR;       /* whee, a portability library! */
    }
  else if (! strcmp ("LF", value))
    {
      *style = svn_wc__eol_style_fixed;
      *eol = "\n";
    }
  else if (! strcmp ("CR", value))
    {
      *style = svn_wc__eol_style_fixed;
      *eol = "\r";
    }
  else if (! strcmp ("CRLF", value))
    {
      *style = svn_wc__eol_style_fixed;
      *eol = "\r\n";
    }
  else
    {
      /* unrecognized value of property;  equivalent to non-existence. */
      *style = svn_wc__eol_style_none;
      *eol = NULL;
    }
}



/*
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
