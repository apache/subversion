/*
 * simple_auth.c :  Simple SASL-based authentication, used in case
 * Cyrus SASL isn't available.
 *
 * ====================================================================
 * Copyright (c) 2006 CollabNet.  All rights reserved.
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

#include "svn_private_config.h"

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_strings.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_ra.h"
#include "svn_ra_svn.h"

#include "ra_svn.h"

svn_boolean_t svn_ra_svn__find_mech(apr_array_header_t *mechlist,
                                    const char *mech)
{
  int i;
  svn_ra_svn_item_t *elt;

  for (i = 0; i < mechlist->nelts; i++)
    {
      elt = &APR_ARRAY_IDX(mechlist, i, svn_ra_svn_item_t);
      if (elt->kind == SVN_RA_SVN_WORD && strcmp(elt->u.word, mech) == 0)
        return TRUE;
    }
  return FALSE;
}

/* Read the "success" response to ANONYMOUS or EXTERNAL authentication. */
static svn_error_t *read_success(svn_ra_svn_conn_t *conn, apr_pool_t *pool)
{
  const char *status, *arg;

  SVN_ERR(svn_ra_svn_read_tuple(conn, pool, "w(?c)", &status, &arg));
  if (strcmp(status, "failure") == 0 && arg)
    return svn_error_createf(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                             _("Authentication error from server: %s"), arg);
  else if (strcmp(status, "success") != 0 || arg)
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("Unexpected server response to authentication"));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_ra_svn__do_internal_auth(svn_ra_svn__session_baton_t *sess,
                             apr_array_header_t *mechlist,
                             const char *realm, apr_pool_t *pool)
{
  svn_ra_svn_conn_t *conn = sess->conn;
  const char *realmstring, *user, *password, *msg;
  svn_auth_iterstate_t *iterstate;
  void *creds;

  realmstring = apr_psprintf(pool, "%s %s", sess->realm_prefix, realm);

  if (sess->is_tunneled && svn_ra_svn__find_mech(mechlist, "EXTERNAL"))
    {
        /* Ask the server to use the tunnel connection environment (on
        * Unix, that means uid) to determine the authentication name. */
      SVN_ERR(svn_ra_svn__auth_response(conn, pool, "EXTERNAL", ""));
      return read_success(conn, pool);
    }
  else if (svn_ra_svn__find_mech(mechlist, "ANONYMOUS"))
    {
      SVN_ERR(svn_ra_svn__auth_response(conn, pool, "ANONYMOUS", ""));
      return read_success(conn, pool);
    }
  else if (svn_ra_svn__find_mech(mechlist, "CRAM-MD5"))
    {
      SVN_ERR(svn_auth_first_credentials(&creds, &iterstate,
                                         SVN_AUTH_CRED_SIMPLE, realmstring,
                                         sess->callbacks->auth_baton, pool));
      if (!creds)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                _("Can't get password"));
      while (creds)
        {
          user = ((svn_auth_cred_simple_t *) creds)->username;
          password = ((svn_auth_cred_simple_t *) creds)->password;
          SVN_ERR(svn_ra_svn__auth_response(conn, pool, "CRAM-MD5", NULL));
          SVN_ERR(svn_ra_svn__cram_client(conn, pool, user, password, &msg));
          if (!msg)
            break;
          SVN_ERR(svn_auth_next_credentials(&creds, iterstate, pool));
        }
      if (!creds)
        return svn_error_createf(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                _("Authentication error from server: %s"),
                                msg);
      SVN_ERR(svn_auth_save_credentials(iterstate, pool));
      return SVN_NO_ERROR;
    }
  else
    return svn_error_create(SVN_ERR_RA_SVN_NO_MECHANISMS, NULL, NULL);
}
