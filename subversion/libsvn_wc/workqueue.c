/*
 * workqueue.c :  manipulating work queue items
 *
 * ====================================================================
 * Copyright (c) 2009 CollabNet.  All rights reserved.
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

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_pools.h"
#include "svn_dirent_uri.h"
#include "svn_path.h"

#include "wc_db.h"
#include "workqueue.h"

#include "svn_private_config.h"
#include "private/svn_skel.h"


#define NOT_IMPLEMENTED() \
  return svn_error__malfunction(TRUE, __FILE__, __LINE__, "Not implemented.")


struct work_item_dispatch {
  const char *name;
  svn_error_t *(*func)(svn_wc__db_t *db,
                       const char *local_abspath,
                       const svn_skel_t *work_item,
                       apr_pool_t *scratch_pool);
};


static svn_error_t *
run_revert(svn_wc__db_t *db,
           const char *local_abspath,
           const svn_skel_t *work_item,
           apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}


static const struct work_item_dispatch dispatch_table[] = {
  {"revert", run_revert},
  {NULL, NULL}
};


svn_error_t *
svn_wc__wq_run(svn_wc__db_t *db,
               const char *local_abspath,
               apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);

  while (TRUE)
    {
      apr_uint64_t id;
      svn_skel_t *work_item;
      const struct work_item_dispatch *scan;

      svn_pool_clear(iterpool);

      SVN_ERR(svn_wc__db_wq_fetch(&id, &work_item, db, local_abspath,
                                  iterpool, iterpool));
      if (work_item == NULL)
        {
          svn_pool_destroy(iterpool);
          return SVN_NO_ERROR;
        }

      /* Scan the dispatch table for a function to handle this work item.  */
      for (scan = &dispatch_table[0]; scan->name != NULL; ++scan)
        {
          if (svn_skel__matches_atom(work_item, scan->name))
            {
              SVN_ERR((*scan->func)(db, local_abspath, work_item, iterpool));
              break;
            }
        }

      if (scan->name == NULL)
        {
          /* We should know about ALL possible work items here. If we do not,
             then something is wrong. Most likely, some kind of format/code
             skew. There is nothing more we can do. Erasing or ignoring this
             work item could leave the WC in an even more broken state.

             Contrary to issue #1581, we cannot simply remove work items and
             continue, so bail out with an error.  */
          return svn_error_createf(SVN_ERR_WC_BAD_ADM_LOG, NULL,
                                   _("Unrecognized work item in the queue "
                                     "associated with '%s'"),
                                   svn_dirent_local_style(local_abspath,
                                                          iterpool));
        }

      SVN_ERR(svn_wc__db_wq_completed(db, local_abspath, id, iterpool));
    }

  /* NOTREACHED */
}


/* Record a work item to revert LOCAL_ABSPATH.  */
svn_error_t *
svn_wc__wq_add_revert(svn_wc__db_t *db,
                      const char *local_abspath,
                      svn_wc_schedule_t orig_schedule,
                      apr_pool_t *scratch_pool)
{
  NOT_IMPLEMENTED();
}
