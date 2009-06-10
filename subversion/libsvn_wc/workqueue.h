/*
 * workqueue.h :  manipulating work queue items
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

#ifndef SVN_WC_WORKQUEUE_H
#define SVN_WC_WORKQUEUE_H

#include <apr_pools.h>

#include "svn_types.h"
#include "svn_wc.h"

#include "wc_db.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* For the WCROOT identified by the DB and LOCAL_ABSPATH pair, run any
   work items that may be present in its workqueue.  */
svn_error_t *
svn_wc__wq_run(svn_wc__db_t *db,
               const char *local_abspath,
               apr_pool_t *scratch_pool);


/* Record a work item to revert LOCAL_ABSPATH.  */
svn_error_t *
svn_wc__wq_add_revert(svn_wc__db_t *db,
                      const char *local_abspath,
                      svn_wc_schedule_t orig_schedule,
                      apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_WC_WORKQUEUE_H */
