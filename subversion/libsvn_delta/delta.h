/*
 * delta.h:  private delta library things
 *
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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


#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_delta.h"

#ifndef SVN_LIBSVN_DELTA_H
#define SVN_LIBSVN_DELTA_H


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Private interface for text deltas. */

/* The standard size of one svndiff window. */

#define SVN_DELTA_WINDOW_SIZE 102400


/* Context/baton for building an operation sequence. */

typedef struct svn_txdelta__ops_baton_t {
  int num_ops;                  /* current number of ops */
  int src_ops;                  /* current number of source copy ops */
  int ops_size;                 /* number of ops allocated */
  svn_txdelta_op_t *ops;        /* the operations */

  svn_stringbuf_t *new_data;    /* any new data used by the operations */
} svn_txdelta__ops_baton_t;


/* Insert a delta op into the delta window being built via BUILD_BATON. If
   OPCODE is svn_delta_new, bytes from NEW_DATA are copied into the window
   data and OFFSET is ignored.  Otherwise NEW_DATA is ignored. All
   allocations are performed in POOL. */
void svn_txdelta__insert_op(svn_txdelta__ops_baton_t *build_baton,
                            enum svn_delta_action opcode,
                            apr_size_t offset,
                            apr_size_t length,
                            const char *new_data,
                            apr_pool_t *pool);


/* Allocate a delta window from POOL. */
svn_txdelta_window_t *
svn_txdelta__make_window(const svn_txdelta__ops_baton_t *build_baton,
                         apr_pool_t *pool);

/* Create vdelta window data. Allocate temporary data from POOL. */
void svn_txdelta__vdelta(svn_txdelta__ops_baton_t *build_baton,
                         const char *start,
                         apr_size_t source_len,
                         apr_size_t target_len,
                         apr_pool_t *pool);


/* Create xdelta window data. Allocate temporary data from POOL. */
void svn_txdelta__xdelta(svn_txdelta__ops_baton_t *build_baton,
                         const char *start,
                         apr_size_t source_len,
                         apr_size_t target_len,
                         apr_pool_t *pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_LIBSVN_DELTA_H */
