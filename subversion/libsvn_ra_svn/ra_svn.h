/*
 * ra_svn.h :  private declarations for the ra_svn module
 *
 * ====================================================================
 * Copyright (c) 2002 CollabNet.  All rights reserved.
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

/* This structure is opaque to the server.  The client pokes at the
 * first few fields during setup and cleanup, and also uses the pool. */
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
  apr_pool_t *pool;
};

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* RA_SVN_H */
