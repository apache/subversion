/*
 * sasl_auth.c :  functions for SASL-based authentication
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
#ifdef SVN_HAVE_SASL

#define APR_WANT_STRFUNC
#include <apr_want.h>
#include <apr_general.h>
#include <apr_strings.h>
#include <apr_atomic.h>
#include <apr_thread_mutex.h>
#include <apr_version.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_error.h"
#include "svn_pools.h"
#include "svn_ra.h"
#include "svn_ra_svn.h"
#include "svn_base64.h"

#include "private/svn_atomic.h"
#include "private/ra_svn_sasl.h"

#include "ra_svn.h"

/* Note: In addition to being used via svn_atomic__init_once to control
 *       initialization of the SASL code this will also be referenced in
 *       the various functions that work with sasl mutexes to determine
 *       if the sasl pool has been destroyed.  This should be safe, since
 *       it is only set back to zero in the sasl pool's cleanups, which
 *       only happens during apr_terminate, which we assume is occurring
 *       in atexit processing, at which point we are already running in
 *       single threaded mode.
 */
volatile svn_atomic_t svn_ra_svn__sasl_status;

static volatile svn_atomic_t sasl_ctx_count;

static apr_pool_t *sasl_pool = NULL;


/* Pool cleanup called when sasl_pool is destroyed. */
static apr_status_t sasl_done_cb(void *data)
{
  /* Reset svn_ra_svn__sasl_status, in case the client calls 
     apr_initialize()/apr_terminate() more than once. */
  svn_ra_svn__sasl_status = 0;
  if (svn_atomic_dec(&sasl_ctx_count) == 0)
    sasl_done();
  return APR_SUCCESS;
}

#ifdef APR_HAS_THREADS
/* Cyrus SASL is thread-safe only if we supply it with mutex functions
 * (with sasl_set_mutex()).  To make this work with APR, we need to use the
 * global sasl_pool for the mutex allocations.  Freeing a mutex actually
 * returns it to a global array.  We allocate mutexes from this
 * array if it is non-empty, or directly from the pool otherwise.
 * We also need a mutex to serialize accesses to the array itself.
 */

/* An array of allocated, but unused, apr_thread_mutex_t's. */
static apr_array_header_t *free_mutexes = NULL;

/* A mutex to serialize access to the array. */
static apr_thread_mutex_t *array_mutex = NULL;

/* Callbacks we pass to sasl_set_mutex(). */

static void *sasl_mutex_alloc_cb(void)
{
  apr_thread_mutex_t *mutex;
  apr_status_t apr_err;

  if (!svn_ra_svn__sasl_status)
    return NULL;

  apr_err = apr_thread_mutex_lock(array_mutex);
  if (apr_err != APR_SUCCESS)
    return NULL;

  if (apr_is_empty_array(free_mutexes))
    {
      apr_err = apr_thread_mutex_create(&mutex,
                                        APR_THREAD_MUTEX_DEFAULT,
                                        sasl_pool);
      if (apr_err != APR_SUCCESS)
        mutex = NULL;
    }
  else
    mutex = *((apr_thread_mutex_t**)apr_array_pop(free_mutexes));

  apr_err = apr_thread_mutex_unlock(array_mutex);
  if (apr_err != APR_SUCCESS)
    return NULL;

  return mutex;
}

static int sasl_mutex_lock_cb(void *mutex)
{
  if (!svn_ra_svn__sasl_status)
    return 0;
  return (apr_thread_mutex_lock(mutex) == APR_SUCCESS) ? 0 : -1;
}

static int sasl_mutex_unlock_cb(void *mutex)
{
  if (!svn_ra_svn__sasl_status)
    return 0;
  return (apr_thread_mutex_unlock(mutex) == APR_SUCCESS) ? 0 : -1;
}

static void sasl_mutex_free_cb(void *mutex)
{
  if (svn_ra_svn__sasl_status)
    {
      apr_status_t apr_err = apr_thread_mutex_lock(array_mutex);
      if (apr_err == APR_SUCCESS)
        {
          APR_ARRAY_PUSH(free_mutexes, apr_thread_mutex_t*) = mutex;
          apr_thread_mutex_unlock(array_mutex);
        }
    }
}
#endif /* APR_HAS_THREADS */

apr_status_t svn_ra_svn__sasl_common_init(void)
{
  apr_status_t apr_err = APR_SUCCESS;

  sasl_pool = svn_pool_create(NULL);
  sasl_ctx_count = 1;
  apr_pool_cleanup_register(sasl_pool, NULL, sasl_done_cb, 
                            apr_pool_cleanup_null);
#ifdef APR_HAS_THREADS
  sasl_set_mutex(sasl_mutex_alloc_cb,
                 sasl_mutex_lock_cb,
                 sasl_mutex_unlock_cb,
                 sasl_mutex_free_cb);
  free_mutexes = apr_array_make(sasl_pool, 0, sizeof(apr_thread_mutex_t *));
  apr_err = apr_thread_mutex_create(&array_mutex,
                                    APR_THREAD_MUTEX_DEFAULT, 
                                    sasl_pool);
#endif /* APR_HAS_THREADS */
  return apr_err;
}

static sasl_callback_t interactions[] =
{
  /* Use SASL interactions for username & password */
  {SASL_CB_AUTHNAME, NULL, NULL}, 
  {SASL_CB_PASS, NULL, NULL},
  {SASL_CB_LIST_END, NULL, NULL}
};

static svn_error_t *sasl_init_cb(void)
{
  if (svn_ra_svn__sasl_common_init() != APR_SUCCESS
      || sasl_client_init(interactions) != SASL_OK)
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            _("Could not initialize the SASL library"));
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn__sasl_init(void)
{
  SVN_ERR(svn_atomic__init_once(&svn_ra_svn__sasl_status, sasl_init_cb));
  return SVN_NO_ERROR;
}

static apr_status_t sasl_dispose_cb(void *data)
{
  sasl_conn_t *sasl_ctx = data;
  sasl_dispose(&sasl_ctx);
  if (svn_atomic_dec(&sasl_ctx_count) == 0)
    sasl_done();
  return APR_SUCCESS;
}

void svn_ra_svn__default_secprops(sasl_security_properties_t *secprops)
{
  /* The minimum and maximum security strength factors that the chosen 
     SASL mechanism should provide.  0 means 'no encryption', 256 means 
     '256-bit encryption', which is about the best that any SASL
     mechanism can provide.  Using these values effectively means 'use 
     whatever encryption the other side wants'.  Note that SASL will try 
     to use better encryption whenever possible, so if both the server and
     the client use these values the highest possible encryption strength 
     will be used. */
  secprops->min_ssf = 0;
  secprops->max_ssf = 256;

  /* Set maxbufsize to the maximum amount of data we can read at any one time. 
     This value needs to be commmunicated to the peer if a security layer 
     is negotiated. */
  secprops->maxbufsize = SVN_RA_SVN__READBUF_SIZE;

  secprops->security_flags = 0;
  secprops->property_names = secprops->property_values = NULL;
}

/* Create a new SASL context. */
static svn_error_t *new_sasl_ctx(sasl_conn_t **sasl_ctx,
                                 svn_boolean_t is_tunneled,
                                 const char *hostname, 
                                 const char *local_addrport,
                                 const char *remote_addrport, 
                                 apr_pool_t *pool)
{
  sasl_security_properties_t secprops;
  int result;

  result = sasl_client_new("svn", hostname, local_addrport, remote_addrport,
                           interactions, SASL_SUCCESS_DATA, 
                           sasl_ctx);
  if (result != SASL_OK)
    return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                            sasl_errstring(result, NULL, NULL));

  svn_atomic_inc(&sasl_ctx_count);
  apr_pool_cleanup_register(pool, *sasl_ctx, sasl_dispose_cb,
                            apr_pool_cleanup_null);

  if (is_tunneled)
    {
      /* We need to tell SASL that this connection is tunneled,
         otherwise it will ignore EXTERNAL. The third paramater
         should be the username, but since SASL doesn't seem
         to use it on the client side, any non-empty string will do. */
      result = sasl_setprop(*sasl_ctx, 
                            SASL_AUTH_EXTERNAL, " ");
      if (result != SASL_OK)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                sasl_errdetail(*sasl_ctx));
    }

  /* Set security properties. Don't allow PLAIN or LOGIN, since we 
     don't support TLS yet. */
  svn_ra_svn__default_secprops(&secprops);
  secprops.security_flags = SASL_SEC_NOPLAINTEXT;
  sasl_setprop(*sasl_ctx, SASL_SEC_PROPS, &secprops);

  return SVN_NO_ERROR;
}

/* Fill in the information requested by client_interact */
static svn_error_t *handle_interact(svn_auth_cred_simple_t *creds,
                                    sasl_interact_t *client_interact,
                                    const char *last_err,
                                    apr_pool_t *pool)
{
  sasl_interact_t *prompt;

  for (prompt = client_interact; prompt->id != SASL_CB_LIST_END; prompt++)
    {
      switch (prompt->id)
        {
        case SASL_CB_AUTHNAME:
          prompt->result = creds->username;
          prompt->len = strlen(creds->username);
          break;
        case SASL_CB_PASS:
          prompt->result = creds->password;
          prompt->len = strlen(creds->password);
          break;
        default:
          /* This should never be reached. */
          return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                  _("Unhandled SASL interaction"));
          break;
        }
    }
  return SVN_NO_ERROR;
}

/* Perform an authentication exchange */
static svn_error_t *try_auth(svn_ra_svn__session_baton_t *sess,
                             sasl_conn_t *sasl_ctx,
                             svn_auth_cred_simple_t *creds,
                             svn_boolean_t *success,
                             const char **last_err,
                             const char *mechstring,
                             svn_boolean_t compat,
                             apr_pool_t *pool)
{
  sasl_interact_t *client_interact = NULL;
  const char *out, *mech, *status = NULL;
  const svn_string_t *arg = NULL, *in;
  int result;
  unsigned int outlen;
  svn_boolean_t again;

  do
    {
      again = FALSE;
      do
        {
          result = sasl_client_start(sasl_ctx,
                                     mechstring,
                                     &client_interact,
                                     &out,
                                     &outlen,
                                     &mech);

          /* Fill in username and password, if required. */
          if (result == SASL_INTERACT)
            SVN_ERR(handle_interact(creds, client_interact, *last_err, pool));
        }
      while (result == SASL_INTERACT);

      switch (result)
        {
          case SASL_OK:
          case SASL_CONTINUE:
            /* Success. */
            break;
          case SASL_NOMECH:
          case SASL_BADPARAM:
          case SASL_NOMEM:
            /* Fatal error.  Fail the authentication. */
            return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                    sasl_errdetail(sasl_ctx));
          default:
            /* For anything else, delete the mech from the list
               and try again. */
            {
              char *dst = strstr(mechstring, mech);
              char *src = dst + strlen(mech);
              while ((*dst++ = *src++) != '\0') 
                ;
              again = TRUE;
            }
        }
    }
  while (again);

  /* Prepare the initial authentication token. */
  if (outlen > 0 || strcmp(mech, "EXTERNAL") == 0) 
    arg = svn_base64_encode_string(svn_string_ncreate(out, outlen, pool), 
                                   pool);

  /* Send the initial client response */
  SVN_ERR(svn_ra_svn__auth_response(sess->conn, pool, mech, 
                                    arg ? arg->data : NULL, compat));

  while (result == SASL_CONTINUE) 
    {
      /* Read the server response */
      SVN_ERR(svn_ra_svn_read_tuple(sess->conn, pool, "w(?s)", 
                                    &status, &in));

      if (strcmp(status, "failure") == 0)
        {
          /* Authentication failed.  Use the next set of credentials */
          *success = FALSE;
          /* Remember the message sent by the server because we'll want to
             return a meaningful error if we run out of auth providers. */
          *last_err = in ? in->data : "";
          return SVN_NO_ERROR;
        }

      if ((strcmp(status, "success") != 0 && strcmp(status, "step") != 0) 
          || in == NULL)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                _("Unexpected server response"
                                " to authentication"));

      /* If the mech is CRAM-MD5 we don't base64-decode the server response. */
      if (strcmp(mech, "CRAM-MD5") != 0) 
        in = svn_base64_decode_string(in, pool);
 
      do
        {
          result = sasl_client_step(sasl_ctx, 
                                    in->data,
                                    in->len,
                                    &client_interact, 
                                    &out, /* Filled in by SASL. */
                                    &outlen);

          /* Fill in username and password, if required. */
          if (result == SASL_INTERACT)
            SVN_ERR(handle_interact(creds, client_interact, *last_err, pool));
        }
      while (result == SASL_INTERACT);

      if (result != SASL_OK && result != SASL_CONTINUE)
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                sasl_errdetail(sasl_ctx));

      if (outlen > 0)
        {
          arg = svn_string_ncreate(out, outlen, pool); 
          /* Write our response. */
          /* For CRAM-MD5, we don't use base64-encoding. */
          if (strcmp(mech, "CRAM-MD5") != 0)
            arg = svn_base64_encode_string(arg, pool);
          SVN_ERR(svn_ra_svn_write_cstring(sess->conn, pool, arg->data));
        }
    }

  if (!status || strcmp(status, "step") == 0)
    {
      /* This is a client-send-last mech.  Read the last server response. */
      SVN_ERR(svn_ra_svn_read_tuple(sess->conn, pool, "w(?s)", 
              &status, &in));

      if (strcmp(status, "failure") == 0)
        {
          *success = FALSE;
          *last_err = in ? in->data : "";
        }
      else if (strcmp(status, "success") == 0)
        {
          /* We're done */
          *success = TRUE;
        }
      else
        return svn_error_create(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                _("Unexpected server response"
                                " to authentication"));
    }
  else
    *success = TRUE;
  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn__get_addresses(const char **local_addrport, 
                                       const char **remote_addrport,
                                       svn_ra_svn_conn_t *conn,
                                       apr_pool_t *pool)
{
  if (conn->sock)
    {
      apr_status_t apr_err;
      apr_sockaddr_t *local_sa, *remote_sa;
      char *local_addr, *remote_addr;

      apr_err = apr_socket_addr_get(&local_sa, APR_LOCAL, conn->sock);
      if (apr_err)
        return svn_error_wrap_apr(apr_err, NULL);

      apr_err = apr_socket_addr_get(&remote_sa, APR_REMOTE, conn->sock);
      if (apr_err)
        return svn_error_wrap_apr(apr_err, NULL);

      apr_err = apr_sockaddr_ip_get(&local_addr, local_sa);
      if (apr_err)
        return svn_error_wrap_apr(apr_err, NULL);

      apr_err = apr_sockaddr_ip_get(&remote_addr, remote_sa);
      if (apr_err)
        return svn_error_wrap_apr(apr_err, NULL);

      /* Format the IP address and port number like this: a.b.c.d;port */
      *local_addrport = apr_pstrcat(pool, local_addr, ";",
                                    apr_itoa(pool, (int)local_sa->port), NULL);
      *remote_addrport = apr_pstrcat(pool, remote_addr, ";", 
                                     apr_itoa(pool, (int)remote_sa->port), NULL);
    }
  return SVN_NO_ERROR;
}

static svn_error_t *get_remote_hostname(char **hostname, apr_socket_t *sock)
{
  apr_status_t apr_err;
  apr_sockaddr_t *sa;

  apr_err = apr_socket_addr_get(&sa, APR_REMOTE, sock);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, NULL);

  apr_err = apr_getnameinfo(hostname, sa, 0);
  if (apr_err)
    return svn_error_wrap_apr(apr_err, NULL);

  return SVN_NO_ERROR;
}

svn_error_t *svn_ra_svn__do_auth(svn_ra_svn__session_baton_t *sess,
                                 apr_array_header_t *mechlist,
                                 const char *realm, apr_pool_t *pool)
{
  apr_pool_t *subpool;
  sasl_conn_t *sasl_ctx;
  const char *mechstring = "", *last_err = "";
  const char *local_addrport = NULL, *remote_addrport = NULL;
  char *hostname = NULL;
  svn_auth_iterstate_t *iterstate = NULL;
  void *creds;
  svn_boolean_t success, compat = (realm == NULL), need_creds = TRUE;
  int i;

  if (!sess->is_tunneled)
    {
      SVN_ERR(svn_ra_svn__get_addresses(&local_addrport, &remote_addrport,
                                        sess->conn, pool));
      SVN_ERR(get_remote_hostname(&hostname, sess->conn->sock));
    }

  /* Create a string containing the list of mechanisms, separated by spaces. */
  for (i = 0; i < mechlist->nelts; i++)
    {
      svn_ra_svn_item_t *elt = &APR_ARRAY_IDX(mechlist, i, svn_ra_svn_item_t);

      /* Force the client to use ANONYMOUS or EXTERNAL if they are available.*/
      if (strcmp(elt->u.word, "ANONYMOUS") == 0
          || strcmp(elt->u.word, "EXTERNAL") == 0)
        {
          mechstring = elt->u.word;
          need_creds = FALSE;
          break;
        }

      mechstring = apr_pstrcat(pool, 
                               mechstring, 
                               i == 0 ? "" : " ", 
                               elt->u.word, NULL);
    }

  if (need_creds)
    {
      const char *realmstring;

      realmstring = realm ? 
                    apr_psprintf(pool, "%s %s", sess->realm_prefix, realm)
                    : sess->realm_prefix;
      SVN_ERR(svn_auth_first_credentials(&creds, &iterstate,
                                         SVN_AUTH_CRED_SIMPLE, 
                                         realmstring,
                                         sess->auth_baton, pool));
    }

  subpool = svn_pool_create(pool);
  do
    {
      svn_pool_clear(subpool);

      SVN_ERR(new_sasl_ctx(&sasl_ctx, sess->is_tunneled,
                           hostname, local_addrport, remote_addrport,
                           pool));
      SVN_ERR(try_auth(sess,
                       sasl_ctx,
                       creds,
                       &success,
                       &last_err,
                       mechstring,
                       compat,
                       subpool));

      if (!success && need_creds)
        SVN_ERR(svn_auth_next_credentials(&creds, iterstate, pool));
      /* If we ran out of authentication providers, return the last 
         error sent by the server. */
      if (!creds)
        return svn_error_createf(SVN_ERR_RA_NOT_AUTHORIZED, NULL,
                                _("Authentication error from server: %s"),
                                last_err);
    }
  while (!success);
  svn_pool_destroy(subpool);

  SVN_ERR(svn_auth_save_credentials(iterstate, pool));

  return SVN_NO_ERROR;
}

#endif /* SVN_HAVE_SASL */
