/*
 * ra_session.h :  private declarations for RA/DAV session state
 *
 * ====================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */



#ifndef RA_SESSION_H
#define RA_SESSION_H

#include <apr_pools.h>

#include <http_request.h>
#include <uri.h>


struct svn_ra_session_t {
  apr_pool_t *pool;

  struct uri root;              /* repository root */

  http_session *sess;           /* HTTP session to server */

};


#endif  /* RA_SESSION_H */


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
