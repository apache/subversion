/*
 * feedback.c:  feedback handlers for cmdline client.
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

/* ==================================================================== */



/*** Includes. ***/

#include <stdio.h>

#define APR_WANT_STDIO
#define APR_WANT_STRFUNC
#include <apr_want.h>

#include "svn_pools.h"
#include "cl.h"



/* When the cmd-line client sees an unversioned item during an update,
   print a question mark (`?'), just like CVS does. */
void svn_cl__notify_unversioned_item (void *baton, const char *path)
{
  printf ("?  %s\n", path);
}


void svn_cl__notify_added_item (void *baton, const char *path)
{
  apr_pool_t *pool = baton;
  svn_stringbuf_t *spath = svn_stringbuf_create (path, pool);
  svn_wc_entry_t *entry;
  svn_error_t *err;
  int binary = 0;

  err = svn_wc_entry (&entry, spath, pool);
  if (err)
    {
      /* ### what the hell to do with this error? */
      return;
    }

  if (entry->kind == svn_node_file)
    {
      const svn_string_t *value;

      err = svn_wc_prop_get (&value, SVN_PROP_MIME_TYPE, path, pool);
      if (err)
        {
          /* ### what the hell to do with this error? */
          return;
        }

      /* If the property exists and it doesn't start with `text/', we'll
         call it binary. */
      if ((value) && (value->len > 5) && (strncmp (value->data, "text/", 5)))
        binary = 1;
    }

  printf ("A  %s  %s\n",
          binary ? "binary" : "      ",
          path);
}


void svn_cl__notify_deleted_item (void *baton, const char *path)
{
  printf ("D  %s\n", path);
}


void svn_cl__notify_restored_item (void *baton, const char *path)
{
  printf ("Restored %s\n", path);
}


void svn_cl__notify_reverted_item (void *baton, const char *path)
{
  printf ("Reverted %s\n", path);
}
 


/* 
 * local variables:
 * eval: (load-file "../../svn-dev.el")
 * end: 
 */
