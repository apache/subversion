/*
 * delta.h:  private delta library things
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


#include <apr_pools.h>
#include <apr_hash.h>

#include "svn_xml.h"

#ifndef SVN_LIBSVN_DELTA_H
#define SVN_LIBSVN_DELTA_H


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Private interface for text deltas. */

/* Context/baton for building an operation sequence. */

typedef struct svn_txdelta__ops_baton_t {
  int num_ops;                  /* current number of ops */
  int src_ops;                  /* current number of source copy ope */
  int ops_size;                 /* number of ops allocated */
  svn_txdelta_op_t *ops;        /* the operations */

  svn_stringbuf_t *new_data;    /* any new data used by the operations */
} svn_txdelta__ops_baton_t;


/* Context for composing windows. */
typedef struct svn_txdelta__compose_ctx_t
{
  apr_off_t sview_offset;       /* Source view offset in the combined window */
  apr_size_t sview_len;         /* Source view length in the combined window */
  svn_boolean_t use_second;     /* TRUE if window_B is the composite. */
} svn_txdelta__compose_ctx_t;


/* Insert a delta op into the delta window being built via BUILD_BATON. If
   OPCODE is svn_delta_new, bytes from NEW_DATA are copied into the window
   data and OFFSET is ignored.  Otherwise NEW_DATA is ignored. All
   allocations are performed in POOL. */
void svn_txdelta__insert_op (svn_txdelta__ops_baton_t *build_baton,
                             int opcode,
                             apr_off_t offset,
                             apr_off_t length,
                             const char *new_data,
                             apr_pool_t *pool);


/* Allocate a delta window from POOL. */
svn_txdelta_window_t *
svn_txdelta__make_window (const svn_txdelta__ops_baton_t *build_baton,
                          apr_pool_t *pool);

/* Return a copy of WINDOW, allocated from POOL. */
svn_txdelta_window_t *
svn_txdelta__copy_window (const svn_txdelta_window_t *window,
                          apr_pool_t *pool);


/* Create vdelta window data. Allocate temporary data from POOL. */
void svn_txdelta__vdelta (svn_txdelta__ops_baton_t *build_baton,
                          const char *start,
                          apr_size_t source_len,
                          apr_size_t target_len,
                          apr_pool_t *pool);


/* Compose two delta windows, yielding a third, allocated from POOL.
   Return NULL If WINDOW_B doesn't depend on WINDOW_A (i.e., it's
   already a valid composed window. */
svn_txdelta_window_t *
svn_txdelta__compose_windows (const svn_txdelta_window_t *window_A,
                              const svn_txdelta_window_t *window_B,
                              svn_txdelta__compose_ctx_t *context,
                              apr_pool_t *pool);


/* Apply the instructions from WINDOW to a source view SBUF to produce
   a target view TBUF.  SBUF is assumed to have WINDOW->sview_len
   bytes of data and TBUF is assumed to have room for TLEN bytes of
   output.  TLEN may be more than WINDOW->tview_len, so return the
   actual number of bytes written.  This is purely a memory operation;
   nothing can go wrong as long as we have a valid window.  */
void
svn_txdelta__apply_instructions (svn_txdelta_window_t *window,
                                 const char *sbuf, char *tbuf,
                                 apr_size_t *tlen);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_LIBSVN_DELTA_H */
