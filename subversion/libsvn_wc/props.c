/*
 * props.c :  routines dealing with properties
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







/*
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end: */
