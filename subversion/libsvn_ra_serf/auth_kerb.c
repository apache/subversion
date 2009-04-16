/*
 * auth_kerb.c : Kerberos authn implementation
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

#include "svn_private_config.h"

#ifdef SVN_RA_SERF_HAVE_GSSAPI

#include <string.h>
#include <apr.h>
#include <apr_base64.h>
#include <gssapi/gssapi.h>
#include "svn_error.h"
#include "ra_serf.h"
#include "auth_kerb.h"

/** These functions implements Kerberos authentication, using GSS-API
    (RFC 2743).
    The message-exchange is documented in RFC 4559. **/

/******************************************************/
/** NOTE: this code is WIP and doesn't work, at all! **/
/******************************************************/
/** TODO:
 ** - free gssapi objects
 ** - add comments.
 ** - add better error reporting
 ** - fix authn status, as the COMPLETE/CONTINUE status values
 **   are never used.
 ** - test
 **/

typedef enum
{
  gss_api_auth_not_started,
  gss_api_auth_in_progress,
  gss_api_auth_completed,
} gss_api_auth_state;

/* HTTP Service name, used to get the Server Ticket.  */
#define KRB_HTTP_SERVICE "HTTP"

/* Stores the context information related to Kerberos authentication. */
typedef struct
{
  apr_pool_t *pool;

  /* GSSAPI context */
  gss_ctx_id_t gss_ctx;

  /* Current state of the authentication cycle. */
  gss_api_auth_state state;

  /* Mechanism used to authenticate, should be Kerberos. */
  gss_OID mech;

} serf_gss_api_context_t;

/* On the initial 401 response of the server, request a Server Ticket from
   the Kerberos KDC to pass to the server, proving that we are who we
   claim to be. The Server Ticket can only be used with the HTTP service
   on the target host. */
static svn_error_t *
gss_api_get_credentials(char *token, apr_size_t token_len,
                        const char *hostname,
                        const char **buf, apr_size_t *buf_len,
                        serf_gss_api_context_t *gss_api_ctx)
{
  gss_buffer_desc input_buf = GSS_C_EMPTY_BUFFER;
  gss_buffer_desc output_buf;
  OM_uint32 min_stat, maj_stat;
  gss_name_t host_gss_name;
  gss_buffer_desc bufdesc;

  /* XXXX */
  bufdesc.value = apr_psprintf(gss_api_ctx->pool, KRB_HTTP_SERVICE "@%s",
                               hostname);
  bufdesc.length = strlen(bufdesc.value);
  maj_stat = gss_import_name (&min_stat, &bufdesc, GSS_C_NT_HOSTBASED_SERVICE,
                              &host_gss_name);
  if(GSS_ERROR(maj_stat))
    {
      return svn_error_createf
        (SVN_ERR_RA_SERF_GSSAPI_INITIALISATION_FAILED, NULL,
         _("Initialization of GSS-API context failed."));
    }

  /* If the server sent us a token, pass it to gss_init_sec_token for
     validation. */
  if (token)
    {
      input_buf.value = token;
      input_buf.length = token_len;
    }

  /* Establish a security context to the server. */
  maj_stat = gss_init_sec_context
    (&min_stat,		        /* minor_status */
     GSS_C_NO_CREDENTIAL,	/* XXXXX claimant_cred_handle */
     &gss_api_ctx->gss_ctx,    	/* gssapi context handle */
     host_gss_name,    		/* HTTP@server name */
     gss_api_ctx->mech,	        /* mech_type (0 ininitially */
     GSS_C_MUTUAL_FLAG,         /* ensure the peer authenticates itself */
     0,       	                /* default validity period */
     GSS_C_NO_CHANNEL_BINDINGS, /* do not use channel bindings */
     &input_buf,                /* server token, initially empty */
     &gss_api_ctx->mech,        /* actual mech type */
     &output_buf,		/* output_token */
     NULL,         		/* ret_flags */
     NULL		        /* not interested in remaining validity */
     );

  if(GSS_ERROR(maj_stat))
    {
      switch(maj_stat)
        {
          case GSS_S_COMPLETE:
            gss_api_ctx->state = gss_api_auth_completed;
            break;
          case GSS_S_CONTINUE_NEEDED:
            gss_api_ctx->state = gss_api_auth_in_progress;
            break;
        default:
          return svn_error_createf
            (SVN_ERR_RA_SERF_GSSAPI_INITIALISATION_FAILED, NULL,
             _("Initialization of GSS-API context failed."));
        }
    }

  /* Return the Server Ticket to the caller. */
  *buf = apr_pmemdup(gss_api_ctx->pool, output_buf.value, output_buf.length);
  *buf_len = output_buf.length;

  return SVN_NO_ERROR;
}

/* Read the header sent by the server (if any), invoke the gssapi authn
   code and use the resulting Server Ticket  on the next request to the
   server. */
static svn_error_t *
do_auth(serf_gss_api_context_t *gss_api_ctx,
        svn_ra_serf__connection_t *conn,
        const char *auth_name,
        const char **auth_value,
        const char *auth_attr,
        const char **auth_header,
        const char *auth_header_value,
        apr_pool_t *pool)
{
  const char *tmp;
  char *token = NULL;
  apr_size_t tmp_len, token_len = 0;
  const char *space = NULL;

  /* The server will return a token  as attribute to the Negotiate key.
     Negotiate YGwGCSqGSIb3EgECAgIAb10wW6ADAgEFoQMCAQ+iTzBNoAMCARCiRgREa6mouM
               BAMFqKVdTGtfpZNXKzyw4Yo1paphJdIA3VOgncaoIlXxZLnkHiIHS2v65pVvrp
               bRIyjF8xve9HxpnNIucCY9c=

     Read this base64 value, decode it and validate it so we're sure the server
     is who we expect it to be. */
  if (auth_attr)
    space = strchr(auth_attr, ' ');

  if (space)
    {
      token = apr_palloc(pool, apr_base64_decode_len(space + 1));
      token_len = apr_base64_decode(token, space + 1);
    }

  /* We can get a whole batch of 401 responses from the server, but we should
     only start the authentication phase once, so if we started authentication
     already ignore all responses with initial Negotiate authentication header.

     Note: as we set the max. transfer rate to one message at a time until the
     authentication cycle is finished, this check shouldn't be needed. */
  if (!token && gss_api_ctx->state != gss_api_auth_not_started)
    return SVN_NO_ERROR;

  SVN_ERR(gss_api_get_credentials(token, token_len, conn->hostinfo,
                                  &tmp, &tmp_len,
                                  gss_api_ctx));

  svn_ra_serf__encode_auth_header("Negotiate", &conn->auth_value,
                                  tmp,
                                  tmp_len,
                                  pool);
  *auth_header = auth_header_value;

  /* If the handshake is finished tell serf it can send as much requests as it
     likes. */
  if (gss_api_ctx->state == gss_api_auth_completed)
    serf_connection_set_max_outstanding_requests(conn->conn, 0);

  return SVN_NO_ERROR;
}

/* A new connection is created to a server that's known to use
   Kerberos. */
svn_error_t *
svn_ra_serf__init_kerb_connection(svn_ra_serf__session_t *session,
				  svn_ra_serf__connection_t *conn,
				  apr_pool_t *pool)
{
  serf_gss_api_context_t *gss_api_ctx;

  gss_api_ctx = apr_pcalloc(pool, sizeof(*gss_api_ctx));
  gss_api_ctx->pool = session->pool;
  gss_api_ctx->state = gss_api_auth_not_started;
  gss_api_ctx->gss_ctx = GSS_C_NO_CONTEXT;
  conn->auth_context = gss_api_ctx;

  /* Make serf send the initial requests one by one */
  serf_connection_set_max_outstanding_requests(conn->conn, 1);

  return SVN_NO_ERROR;
}

/* A 401 response was received, handle the authentication. */
svn_error_t *
svn_ra_serf__handle_kerb_auth(svn_ra_serf__handler_t *ctx,
			      serf_request_t *request,
			      serf_bucket_t *response,
			      const char *auth_hdr,
			      const char *auth_attr,
			      apr_pool_t *pool)
{
  return do_auth(ctx->conn->auth_context,
                 ctx->conn,
                 ctx->session->auth_protocol->auth_name,
                 &ctx->conn->auth_value,
                 auth_attr,
                 &ctx->conn->auth_header,
                 "Authorization",
                 pool);
}

/* Setup the authn headers on this request message. */
svn_error_t *
svn_ra_serf__setup_request_kerb_auth(svn_ra_serf__connection_t *conn,
				     const char *method,
				     const char *uri,
				     serf_bucket_t *hdrs_bkt)
{
  /* Take the default authentication header for this connection, if any. */
  if (conn->auth_header && conn->auth_value)
    {
      serf_bucket_headers_setn(hdrs_bkt, conn->auth_header, conn->auth_value);
      conn->auth_header = NULL;
      conn->auth_value = NULL;
    }

  return SVN_NO_ERROR;
}

/* Function is called when 2xx responses are received. Normally we don't
   have to do anything, except for the first response after the
   authentication handshake. This specific response includes
   a server ticket (XXXXXX, use correct term) which should be validated
   by the client (mutual authentication).
   */
svn_error_t *
svn_ra_serf__validate_response_kerb_auth(svn_ra_serf__handler_t *ctx,
                                         serf_request_t *request,
                                         serf_bucket_t *response,
                                         apr_pool_t *pool)
{
  serf_gss_api_context_t *gss_api_ctx = ctx->conn->auth_context;
  serf_bucket_t *hdrs;
  const char *auth_attr;

  hdrs = serf_bucket_response_get_headers(response);
  auth_attr = serf_bucket_headers_get(hdrs, "WWW-Authenticate");

  if (gss_api_ctx->state != gss_api_auth_completed)
    {
      return do_auth(ctx->conn->auth_context,
		     ctx->conn,
		     ctx->session->auth_protocol->auth_name,
		     &ctx->conn->auth_value,
		     auth_attr,
		     &ctx->conn->auth_header,
		     "Authorization",
		     pool);
    }

  return SVN_NO_ERROR;
}

#endif /* SVN_RA_SERF_HAVE_GSSAPI */
