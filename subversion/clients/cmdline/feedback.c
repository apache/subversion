/*
 * feedback.c:  feedback handlers for cmdline client.
 *
      * ====================================================================
 * Copyright (c) 2000-2002 CollabNet.  All rights reserved.
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


static void 
notify_added (void *baton, const char *path)
{
  apr_pool_t *pool = baton;
  svn_stringbuf_t *spath = svn_stringbuf_create (path, pool);
  svn_wc_entry_t *entry;
  svn_error_t *err;
  const char *type = "      ";  /* fill with "binary" if binary, etc */

  err = svn_wc_entry (&entry, spath, pool);
  if (err)
    {
      printf ("WARNING: error fetching entry for %s\n", path);
      return;
    }
  else if (! entry)
    {
      printf ("WARNING: apparently failed to add %s\n", path);
      return;
    }
           
  if (entry->kind == svn_node_file)
    {
      const svn_string_t *value;

      err = svn_wc_prop_get (&value, SVN_PROP_MIME_TYPE, path, pool);
      if (err)
        {
          printf ("WARNING: error fetching %s property for %s\n",
                  SVN_PROP_MIME_TYPE, path);
          return;
        }

      /* If the property exists and it doesn't start with `text/', we'll
         call it binary. */
      if ((value) && (value->len > 5) && (strncmp (value->data, "text/", 5)))
        type = "binary";
    }

  printf ("A  %s  %s\n", type, path);
}


static void 
notify_deleted (void *baton, const char *path)
{
  printf ("D  %s\n", path);
}


static void 
notify_restored (void *baton, const char *path)
{
  printf ("Restored %s\n", path);
}


static void 
notify_reverted (void *baton, const char *path)
{
  printf ("Reverted %s\n", path);
}
 

void svn_cl__notify_func (void *baton, 
                          svn_wc_notify_action_t action, 
                          const char *path)
{
  switch (action)
    {
    case svn_wc_notify_add:
      return notify_added (baton, path);

    case svn_wc_notify_delete:
      return notify_deleted (baton, path);

    case svn_wc_notify_restore:
      return notify_restored (baton, path);

    case svn_wc_notify_revert:
      return notify_reverted (baton, path);

    default:
      break;
    }
  return;
}


void *svn_cl__make_notify_baton (apr_pool_t *pool)
{
  return (void *)pool;
}


/* 
 * local variables:
 * eval: (load-file "../../../tools/dev/svn-dev.el")
 * end: 
 */
