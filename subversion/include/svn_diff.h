/*
 * svn_diff.h :  structures related to delta-parsing
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



#ifndef SVN_DIFF_H
#define SVN_DIFF_H

#include <apr.h>
#include <apr_pools.h>

#include "svn_types.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/*** Diffs.  ***/

typedef struct svn_diff_t svn_diff_t;

typedef enum svn_diff_datasource_e
{
  svn_diff_datasource_baseline,
  svn_diff_datasource_workingcopy,
  svn_diff_datasource_repository

} svn_diff_datasource_e;


typedef struct svn_diff_fns_t
{
  apr_status_t (*datasource_open)(void *diff_baton,
                                  svn_diff_datasource_e datasoure);

  /* ### Unused at the moment.
   */
  apr_status_t (*datasource_seek)(void *diff_baton,
                                  svn_diff_datasource_e datasoure,
                                  apr_off_t token_offset);

  void *(*datasource_get_token)(void *diff_baton,
                                svn_diff_datasource_e datasoure);

  void (*datasource_close)(void *diff_baton,
                           svn_diff_datasource_e datasoure);

  int (*token_compare)(void *diff_baton,
                       void *ltoken,
                       void *rtoken);

  void (*token_discard)(void *diff_baton,
                        void *token);

  void (*token_discard_all)(void *diff_baton);
} svn_diff_fns_t;

/* Produce a diff between baseline and workingcopy.
 *
 */
apr_status_t svn_diff(svn_diff_t **diff,
                      void *diff_baton,
                      svn_diff_fns_t *diff_fns,
                      apr_pool_t *pool);

/* Merge baseline, workingcopy and repository.
 *
 */
apr_status_t svn_diff3(svn_diff_t **diff,
                       void *diff_baton,
                       svn_diff_fns_t *diff_fns,
                       apr_pool_t *pool);


/*
 *
 */
typedef struct svn_diff_output_fns_t
{
  void (*output_common)(void *output_baton,
                        apr_off_t baseline_start,
                        apr_off_t baseline_end,
                        apr_off_t workingcopy_start,
                        apr_off_t workingcopy_end,
                        apr_off_t repository_start,
                        apr_off_t repository_end);
  void (*output_conflict)(void *output_baton,
                          apr_off_t baseline_start,
                          apr_off_t baseline_end,
                          apr_off_t workingcopy_start,
                          apr_off_t workingcopy_end,
                          apr_off_t repository_start,
                          apr_off_t repository_end);
  void (*output_diff_workingcopy)(void *output_baton,
                                  apr_off_t baseline_start,
                                  apr_off_t baseline_end,
                                  apr_off_t workingcopy_start,
                                  apr_off_t workingcopy_end,
                                  apr_off_t repository_start,
                                  apr_off_t repository_end);
  void (*output_diff_repository)(void *output_baton,
                                 apr_off_t baseline_start,
                                 apr_off_t baseline_end,
                                 apr_off_t workingcopy_start,
                                 apr_off_t workingcopy_end,
                                 apr_off_t repository_start,
                                 apr_off_t repository_end);
  void (*output_diff_common)(void *output_baton,
                             apr_off_t baseline_start,
                             apr_off_t baseline_end,
                             apr_off_t workingcopy_start,
                             apr_off_t workingcopy_end,
                             apr_off_t repository_start,
                             apr_off_t repository_end);
} svn_diff_output_fns_t;

/*
 *
 */
void
svn_diff_output(svn_diff_t *diff,
                void *output_baton,
                svn_diff_output_fns_t *output_fns);


/*** Diffs on files ***/

/* Function to diff two files.
 *
 * DIFF is where the diff will be stored.
 * ORIGINAL is the path to the original file.
 * MODIFIED is the path to the file to which the
 *   original file will be compared.
 * POOL is the pool you want the diff to
 *   be allocated out of.
 */
apr_status_t
svn_diff_file(svn_diff_t **diff,
              const char *original,
              const char *modified,
              apr_pool_t *pool);

/* Function to diff three files.
 *
 * DIFF is where the diff will be stored.
 * ORIGINAL is the path to the original file; the
 *  common ancestor of MODIFIED1 and MODIFIED2.
 * MODIFIED1 and MODIFIED2 are the paths to the
 *   files to be compared with the common ancestor
 *   (and eachother).
 * POOL is the pool you want the diff to
 *   be allocated out of.
 */
apr_status_t
svn_diff3_file(svn_diff_t **diff,
               const char *original,
               const char *modified1,
               const char *modified2,
               apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_DIFF_H */

/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */

