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
  /* the pool (BATON) is typically the global pool; don't keep filling it */
  apr_pool_t *subpool = svn_pool_create (baton);

  svn_stringbuf_t *spath = svn_stringbuf_create (path, subpool);
  svn_wc_entry_t *entry;
  svn_error_t *err;
  const char *type = "      ";  /* fill with "binary" if binary, etc */

  /* ### this sucks. we have to open/parse the entries file to get this
     ### information. when adding thousands of files, this blows... */
  err = svn_wc_entry (&entry, spath, subpool);
  if (err)
    {
      printf ("WARNING: error fetching entry for %s\n", path);
      goto done;
    }
  else if (! entry)
    {
      printf ("WARNING: apparently failed to add %s\n", path);
      goto done;
    }
           
  if (entry->kind == svn_node_file)
    {
      const svn_string_t *value;

      /* ### and again: open/parse a properties file. urk... */
      err = svn_wc_prop_get (&value, SVN_PROP_MIME_TYPE, path, subpool);
      if (err)
        {
          printf ("WARNING: error fetching %s property for %s\n",
                  SVN_PROP_MIME_TYPE, path);
          goto done;
        }

      /* If the property exists and it doesn't start with `text/', we'll
         call it binary. */
      if ((value) && (value->len > 5) && (strncmp (value->data, "text/", 5)))
        type = "binary";
    }

  printf ("A  %s  %s\n", type, path);

 done:
  svn_pool_destroy (subpool);
}


void svn_cl__notify_func (void *baton, 
                          svn_wc_notify_action_t action, 
                          const char *path)
{
  switch (action)
    {
    case svn_wc_notify_add:
      notify_added (baton, path);
      return;

    case svn_wc_notify_delete:
      printf ("D  %s\n", path);
      return;

    case svn_wc_notify_restore:
      printf ("Restored %s\n", path);
      return;

    case svn_wc_notify_revert:
      printf ("Reverted %s\n", path);
      return;

    case svn_wc_notify_resolve:
      printf ("Resolved conflicted state of %s\n", path);
      return;

    case svn_wc_notify_update:
      printf ("U   %s\n", path);
      return;

    default:
      break;
    }
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
