/*
 * props.c :  routines dealing with properties in the working copy
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
   changes as a series of (svn_propchange_t *) objects stored in
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
    =  apr_make_array (pool, 1, sizeof(svn_propdelta_t *));

  /* Loop over baseprops and examine each key.  This will allow us to
     detect any `deletion' events or `set-modification' events.  */
  for (hi = apr_hash_first (baseprops); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t klen;
      void *val;
      svn_string_t *propval1, *propval2;

      /* Get next property */
      apr_hash_this (hi, &key, &klen, &val);
      propval1 = (svn_string_t *) val;

      /* Does property name exist in localprops? */
      propval2 = (svn_string_t *) apr_hash_get (localprops, key, klen);

      if (propval2 == NULL)
        {
          /* Add a delete event to the array */
          svn_propdelta_t *p = apr_pcalloc (pool, sizeof(*p));
          p->name = svn_string_ncreate ((char *) key, klen, pool);
          p->value = NULL;
          
          *((svn_propdelta_t **)apr_push_array (ary)) = p;
        }
      else if (! svn_string_compare (propval1, propval2))
        {
          /* Add a set (modification) event to the array */
          svn_propdelta_t *p = apr_pcalloc (pool, sizeof(*p));
          p->name = svn_string_ncreate ((char *) key, klen, pool);
          p->value = propval2;
          
          *((svn_propdelta_t **)apr_push_array (ary)) = p;
        }
    }

  /* Loop over localprops and examine each key.  This allows us to
     detect `set-creation' events */
  for (hi = apr_hash_first (localprops); hi; hi = apr_hash_next (hi))
    {
      const void *key;
      apr_size_t klen;
      void *val;
      svn_string_t *propval1, *propval2;

      /* Get next property */
      apr_hash_this (hi, &key, &klen, &val);
      propval2 = (svn_string_t *) val;

      /* Does property name exist in baseprops? */
      propval1 = (svn_string_t *) apr_hash_get (baseprops, key, klen);

      if (propval1 == NULL)
        {
          /* Add a set (creation) event to the array */
          svn_propdelta_t *p = apr_pcalloc (pool, sizeof(*p));
          p->name = svn_string_ncreate ((char *) key, klen, pool);
          p->value = propval2;
          
          *((svn_propdelta_t **)apr_push_array (ary)) = p;
        }
    }


  /* Done building our array of user events. */
  *local_propchanges = ary;

  return SVN_NO_ERROR;
}


/*---------------------------------------------------------------------*/

/*** Detecting a property conflict ***/


/* Given two propchange objects, return TRUE iff they conflict. */

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
svn_wc__conflicting_propchanges_p (svn_propdelta_t *change1,
                                   svn_propdelta_t *change2)
{
  /* We're assuming that whoever called this routine has already
     deduced that change1 and change2 affect the same property name.
     (After all, if they affect different property names, how can they
     possibly conflict?)  But still, let's make this routine
     `complete' by checking anyway. */
  if (! svn_string_compare (change1->name, change2->name))
    return FALSE;  /* no conflict */

  /* If one change wants to delete a property and the other wants to
     set it, this is a conflict.  This check covers two bases of our
     chi-square. */
  if ( ((change1->value != NULL) && (change2->value == NULL))
       || ((change1->value == NULL) && (change2->value != NULL)) )
    return TRUE;  /* conflict */

  /* If both changes delete the same property, there's no conflict.
     It's an implicit merge.  :)  */
  if ((change1->value == NULL) && (change2->value == NULL))
    return FALSE;  /* no conflict */

  /* If both changes set the property, it's a conflict iff the values
     are different */
  if (! svn_string_compare (change1->value, change2->value))
    return TRUE;  /* conflict */
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
svn_wc__load_prop_file (svn_string_t *propfile_path,
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

      status = apr_open (&propfile, propfile_path->data,
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

      status = apr_close (propfile);
      if (status)
        return svn_error_createf (status, 0, NULL, pool,
                                  "load_prop_file: can't close `%s'",
                                  propfile_path->data);
    }

  return SVN_NO_ERROR;
}



/* Given a HASH full of property name/values, write them to a file
   located at PROPFILE_PATH */
svn_error_t *
svn_wc__save_prop_file (svn_string_t *propfile_path,
                        apr_hash_t *hash,
                        apr_pool_t *pool)
{
  apr_status_t apr_err;
  apr_file_t *prop_tmp;

  apr_err = apr_open (&prop_tmp, propfile_path->data,
                      (APR_WRITE | APR_CREATE),
                      APR_OS_DEFAULT, pool);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "save_prop_file: can't open `%s'",
                              propfile_path->data);

  apr_err = svn_hash_write (hash, svn_unpack_bytestring,
                            prop_tmp);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "save_prop_file: can't write prop hash to `%s'",
                              propfile_path->data);

  apr_err = apr_close (prop_tmp);
  if (apr_err)
    return svn_error_createf (apr_err, 0, NULL, pool,
                              "save_prop_file: can't close `%s'",
                              propfile_path->data);

  return SVN_NO_ERROR;
}



/*---------------------------------------------------------------------*/

/*** Merging propchanges into the working copy ***/

/* This routine is called by the working copy update editor, by both
   close_file() and close_dir(): */


/* Given PATH/NAME (represting a node of type KIND) and an array of
   PROPCHANGES, merge the changes into the working copy.  Necessary
   log entries will be appended to ENTRY_ACCUM.  */
svn_error_t *
svn_wc__do_property_merge (svn_string_t *path,
                           const svn_string_t *name,
                           apr_array_header_t *propchanges,
                           apr_pool_t *pool,
                           enum svn_node_kind kind,
                           svn_string_t **entry_accum)
{
  int i;
  svn_error_t *err;

  /* Zillions of pathnames to compute!  yeargh!  */
  svn_string_t *base_propfile_path, *local_propfile_path;
  svn_string_t *base_prop_tmp_path, *local_prop_tmp_path;
  svn_string_t *tmp_prop_base, *real_prop_base;
  svn_string_t *tmp_props, *real_props;
  
  const char *PROPS, *PROP_BASE;  /* constants pointing to parts of SVN/ */
  apr_array_header_t *local_propchanges; /* propchanges that the user
                                            has made since last update */
  apr_hash_t *localhash;   /* all `working' properties */
  apr_hash_t *basehash;    /* all `pristine' properties */

  /* Decide which areas of SVN/ are relevant */
  if (kind == svn_node_file)
    {
      PROPS = SVN_WC__ADM_PROPS;
      PROP_BASE = SVN_WC__ADM_PROP_BASE;
    }
  else if (kind == svn_node_dir)
    {
      PROPS = SVN_WC__ADM_DIR_PROPS;
      PROP_BASE = SVN_WC__ADM_DIR_PROP_BASE;
    }
  
  /* Load the base & working property files into hashes */
  localhash = apr_make_hash (pool);
  basehash = apr_make_hash (pool);
  
  base_propfile_path = svn_wc__adm_path (path,
                                         0, /* not tmp */
                                         pool,
                                         PROP_BASE,
                                         name->data,
                                         NULL);
  
  local_propfile_path = svn_wc__adm_path (path,
                                          0, /* not tmp */
                                          pool,
                                          PROPS,
                                          name->data,
                                          NULL);
  
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
  
  /* Looping over the array of propchanges we want to apply: */
  for (i = 0; i < propchanges->nelts; i++)
    {
      int j;
      int found_match = 0;          
      svn_propdelta_t *update_change, *local_change;
      
      update_change = (((svn_propdelta_t **)(propchanges)->elts)[i]);
      
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
            (((svn_propdelta_t **)(local_propchanges)->elts)[j]);
          
          if (svn_string_compare (local_change->name, update_change->name))
            {
              found_match = 1;
              break;
            }
        }
      
      if (found_match)
        /* Now see if the two changes actually conflict */
        if (svn_wc__conflicting_propchanges_p (update_change,
                                               local_change))
          {
            /* TODO:  write log:  mark entry as conflicted
               TODO:  write log:  append english to some .prej
               file */
            continue;  /* skip to the next update_change */
          }
      
      /* If we get here, there's no conflict and we safely apply
         the update_change to our working property hash */
      apr_hash_set (localhash,
                    update_change->name->data,
                    update_change->name->len,
                    update_change->value);
    }
  
  
  /* Done merging property changes into both pristine and working
     hashes.  Now we write them to temporary files.  Notice that
     the paths computed are ABSOLUTE pathnames.  */
  
  /* Write the merged pristine prop hash to SVN/tmp/prop-base/ */
  base_prop_tmp_path = svn_wc__adm_path (path,
                                         TRUE, /* tmp area */
                                         pool,
                                         PROP_BASE,
                                         name->data,
                                         NULL);
  
  err = svn_wc__save_prop_file (base_prop_tmp_path, basehash, pool);
  if (err) return err;
  
  /* Write the merged local prop hash to SVN/tmp/props/ */
  local_prop_tmp_path = svn_wc__adm_path (path,
                                          TRUE, /* tmp area */
                                          pool,
                                          PROPS,
                                          name->data,
                                          NULL);
  
  err = svn_wc__save_prop_file (local_prop_tmp_path, localhash, pool);
  if (err) return err;
  
  /* Compute pathnames for the "mv" log entries.  Notice that
     these paths are RELATIVE pathnames, so that each SVN subdir
     remains separable when executing run_log().  */
  tmp_prop_base = svn_wc__adm_path (svn_string_create ("", pool),
                                    1, /* tmp */
                                    pool,
                                    PROP_BASE,
                                    name->data,
                                    NULL);
  real_prop_base = svn_wc__adm_path (svn_string_create ("", pool),
                                     0, /* no tmp */
                                     pool,
                                     PROP_BASE,
                                     name->data,
                                     NULL);
  
  tmp_props = svn_wc__adm_path (svn_string_create ("", pool),
                                1, /* tmp */
                                pool,
                                PROPS,
                                name->data,
                                NULL);
  real_props = svn_wc__adm_path (svn_string_create ("", pool),
                                 0, /* no tmp */
                                 pool,
                                 PROPS,
                                 name->data,
                                 NULL);
  
  
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
  
  /* At this point, we need to write log entries that bump revision
     number and set new entry timestamps.  The caller of this function
     should (hopefully) follow up with this. */

  return SVN_NO_ERROR;
}




/*
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
