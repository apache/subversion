/*
 * status.c: construct a status structure from an entry structure
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
#include <apr_file_io.h>
#include <apr_hash.h>
#include <apr_time.h>
#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_wc.h"
#include "wc.h"   



/* Fill in STATUS with ENTRY.
   ENTRY's pool must not be shorter-lived than STATUS's, since ENTRY
   will be stored directly, not copied. */
static svn_error_t *
assemble_status (svn_wc_status_t *status,
                 svn_string_t *path,
                 svn_wc_entry_t *entry,
                 apr_pool_t *pool)
{
  svn_error_t *err;

  /* Copy info from entry struct to status struct */
  status->entry = entry;
  status->repos_rev = SVN_INVALID_REVNUM;  /* caller fills in */

  if (entry->flags & SVN_WC_ENTRY_ADD)
    status->flag = svn_wc_status_added;
  else if (entry->flags & SVN_WC_ENTRY_DELETE)
    status->flag = svn_wc_status_deleted;
  else if (entry->flags & SVN_WC_ENTRY_CONFLICT)
    status->flag = svn_wc_status_conflicted;
  else 
    {
      if (entry->kind == svn_node_file)
        {
          svn_boolean_t modified_p;

          err = svn_wc__file_modified_p (&modified_p, path, pool);
          if (err) return err;

          if (modified_p)
            status->flag = svn_wc_status_modified;
        }
    }

  /* At this point, if the object is neither (M)odified nor marked
     for (D)eletion or (A)ddition, then set the flag blank. */
  if (! status->flag)
    status->flag = svn_wc_status_none;

  return SVN_NO_ERROR;
}


/* Given an ENTRY object representing PATH, build a status structure
   and store it in STATUSHASH.  */
static svn_error_t *
add_status_structure (apr_hash_t *statushash,
                      svn_string_t *path,
                      svn_wc_entry_t *entry,
                      apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_status_t *statstruct = apr_pcalloc (pool, sizeof (*statstruct));

  err = assemble_status (statstruct, path, entry, pool);
  if (err)
    return err;

  apr_hash_set (statushash, path->data, path->len, statstruct);
  
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_status (svn_wc_status_t **status,
               svn_string_t *path,
               apr_pool_t *pool)
{
  svn_error_t *err;
  svn_wc_status_t *s = apr_pcalloc (pool, sizeof (*s));
  svn_wc_entry_t *entry = NULL;

  err = svn_wc_entry (&entry, path, pool);
  if (err)
    return err;

  err = assemble_status (s, path, entry, pool);
  if (err)
    return err;
  
  *status = s;
  return SVN_NO_ERROR;
}


svn_error_t *
svn_wc_statuses (apr_hash_t *statushash,
                 svn_string_t *path,
                 apr_pool_t *pool)
{
  svn_error_t *err;
  enum svn_node_kind kind;
  apr_hash_t *entries;
  svn_wc_entry_t *entry;
  void *value;
  
  /* Is PATH a directory or file? */
  err = svn_io_check_path (path, &kind, pool);
  if (err) return err;
  
  /* Read the appropriate entries file */
  
  /* If path points to only one file, return just one status structure
     in the STATUSHASH */
  if (kind == svn_node_file)
    {
      svn_string_t *dirpath, *basename;

      /* Figure out file's parent dir */
      svn_path_split (path, &dirpath, &basename,
                      svn_path_local_style, pool);      

      /* Load entries file for file's parent */
      err = svn_wc__entries_read (&entries, dirpath, pool);
      if (err) return err;

      /* Get the entry by looking up file's basename */
      value = apr_hash_get (entries, basename->data, APR_HASH_KEY_STRING);

      if (value)  
        entry = (svn_wc_entry_t *) value;
      else
        return svn_error_createf (SVN_ERR_BAD_FILENAME, 0, NULL, pool,
                                  "svn_wc_statuses:  bogus path `%s'",
                                  path->data);

      /* Convert the entry into a status structure, store in the hash */
      err = add_status_structure (statushash, path, entry, pool);
      if (err) return err;
    }


  /* Fill the hash with a status structure for *each* entry in PATH */
  else if (kind == svn_node_dir)
    {
      apr_hash_index_t *hi;

      /* Load entries file for the directory */
      err = svn_wc__entries_read (&entries, path, pool);
      if (err) return err;

      /* Loop over entries hash */
      for (hi = apr_hash_first (entries); hi; hi = apr_hash_next (hi))
        {
          const void *key;
          void *val;
          const char *basename;
          apr_size_t keylen;
          svn_string_t *fullpath = svn_string_dup (path, pool);

          /* Get the next dirent */
          apr_hash_this (hi, &key, &keylen, &val);
          basename = (const char *) key;
          svn_path_add_component_nts (fullpath, basename,
                                      svn_path_local_style);
          entry = (svn_wc_entry_t *) val;

          /* Recurse on the dirent, provided it's not "."  */
          if (strcmp (basename, SVN_WC_ENTRY_THIS_DIR))
            {
              err = svn_wc_statuses (statushash, fullpath, pool);
              if (err) return err;
            }
          else
            {
              /* This must be the "." dir;  store it instead of
                 recursing. */
              err = add_status_structure (statushash, fullpath, entry, pool);
              if (err) return err;              
            }
        }      
    }
  
  return SVN_NO_ERROR;
}




/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
