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

/* This structure is opaque to the server; the client needs to get at
 * the pool element because the ra interface doesn't pass pools. */
struct svn_ra_svn_conn_st {
  int sock;
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
