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
   ENTRY may be null, for non-versioned entities.
   Else, ENTRY's pool must not be shorter-lived than STATUS's, since
   ENTRY will be stored directly, not copied. */
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
  status->text_status = svn_wc_status_none;       /* default to no
                                                     status. */
  status->prop_status = svn_wc_status_none;       /* default to no status. */

  if (status->entry)
    {
      if (entry->flags & SVN_WC_ENTRY_ADD)
        {
          status->text_status = svn_wc_status_added;
        }
      else if (entry->flags & SVN_WC_ENTRY_DELETE)
        status->text_status = svn_wc_status_deleted;
      else if (entry->flags & SVN_WC_ENTRY_CONFLICT)
        {
          /* We must decide to mark 0, 1, or 2 status flags as
             "conflicted", based on whether reject files are mentioned
             and/or continue to exist.  Luckily, we have a function to do
             this.  :)  */          
          svn_boolean_t text_conflict_p, prop_conflict_p;
          svn_string_t *parent_dir;
          
          if (entry->kind == svn_node_file)
            {
              parent_dir = svn_string_dup (path, pool);
              svn_path_remove_component (parent_dir, svn_path_local_style);
            }
          else if (entry->kind == svn_node_dir)
            parent_dir = path;

          /*          svn_boolean_t conflicted_p; */
          /* See if the user has resolved the conflict before we
             report the entry's status. */
          /*          err = svn_wc__conflicted_p (&conflicted_p, path,
                                      entry, pool);
          if (err) return err;
          
          if (conflicted_p)*/
            status->text_status = svn_wc_status_conflicted;
            status->prop_status = svn_wc_status_conflicted;
        }
      else 
        {
          if (entry->kind == svn_node_file)
            {
              svn_boolean_t modified_p;
              
              err = svn_wc_text_modified_p (&modified_p, path, pool);
              if (err) return err;
              
              if (modified_p)
                status->text_status = svn_wc_status_modified;
            }
        }
    }

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
  
  /* kff todo: this has to deal with the case of a type-changing edit,
     i.e., someone removed a file under vc and replaced it with a dir,
     or vice versa.  In such a case, when you ask for the status, you
     should get mostly information about the now-vanished entity, plus
     some information about what happened to it.  The same situation
     is handled in entries.c:svn_wc_entry. */

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
      value = apr_hash_get (entries, basename->data, basename->len);

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

          err = svn_io_check_path (fullpath, &kind, pool);
          if (err) return err;

          if ((kind == svn_node_dir)
              && (strcmp (basename, SVN_WC_ENTRY_THIS_DIR) != 0))
            svn_wc_statuses (statushash, fullpath, pool);
          else
            {
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
