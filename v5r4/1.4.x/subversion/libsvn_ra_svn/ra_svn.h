/*
 * ra_svn.h :  private declarations for the ra_svn module
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



#ifndef RA_SVN_H
#define RA_SVN_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <apr_network_io.h>
#include <apr_file_io.h>
#include <apr_thread_proc.h>
#include "svn_ra_svn.h"

/* Handler for blocked writes. */
typedef svn_error_t *(*ra_svn_block_handler_t)(svn_ra_svn_conn_t *conn,
                                               apr_pool_t *pool,
                                               void *baton);


/* This structure is opaque to the server.  The client pokes at the
 * first few fields during setup and cleanup. */
struct svn_ra_svn_conn_st {
  apr_socket_t *sock;     /* NULL if using in_file/out_file */
  apr_file_t *in_file;
  apr_file_t *out_file;
  apr_proc_t *proc;       /* Used by client.c when sock is NULL */
  char read_buf[4096];
  char *read_ptr;
  char *read_end;
  char write_buf[4096];
  int write_pos;
  const char *uuid;
  const char *repos_root;
  ra_svn_block_handler_t block_handler;
  void *block_baton;
  apr_hash_t *capabilities;
  apr_pool_t *pool;
};

/* Set a callback for blocked writes on conn.  This handler may
 * perform reads on the connection in order to prevent deadlock due to
 * pipelining.  If callback is NULL, the connection goes back to
 * normal blocking I/O for writes.
 */
void svn_ra_svn__set_block_handler(svn_ra_svn_conn_t *conn,
                                   ra_svn_block_handler_t callback,
                                   void *baton);

/* Return true if there is input waiting on conn. */
svn_boolean_t svn_ra_svn__input_waiting(svn_ra_svn_conn_t *conn,
                                        apr_pool_t *pool);

/* Pipelined implementation of editor; the real functions defer to
 * these if the connection has the edit-pipeline capability. */
void svn_ra_svn__get_editorp(const svn_delta_editor_t **editor,
                             void **edit_baton, svn_ra_svn_conn_t *conn,
                             apr_pool_t *pool,
                             svn_ra_svn_edit_callback callback,
                             void *callback_baton);

svn_error_t *svn_ra_svn__drive_editorp(svn_ra_svn_conn_t *conn,
                                       apr_pool_t *pool,
                                       const svn_delta_editor_t *editor,
                                       void *edit_baton,
                                       svn_boolean_t *aborted,
                                       svn_boolean_t for_replay);

/* CRAM-MD5 client implementation. */
svn_error_t *svn_ra_svn__cram_client(svn_ra_svn_conn_t *conn, apr_pool_t *pool,
                                     const char *user, const char *password,
                                     const char **message);

/* Return an error chain based on @a params (which contains a
 * command response indicating failure).  The error chain will be
 * in the same order as the errors indicated in @a params.  Use
 * @a pool for temporary allocations. */
svn_error_t *svn_ra_svn__handle_failure_status(apr_array_header_t *params,
                                               apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* RA_SVN_H */
