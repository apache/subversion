/*
 * auth.c: authentication support functions for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000-2003 CollabNet.  All rights reserved.
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


#include <apr_pools.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_error.h"
#include "svn_auth.h"


typedef struct
{
    const svn_auth_provider_t *vtable;
    void * provider_baton;
} provider_t;

typedef struct
{
    apr_array_header_t *providers; /* (ordered) array of provider_t */

} provider_set_t;


struct svn_auth_baton_t
{
    apr_hash_t *kinds;  /* map cred_kind -> provider_set */
};

struct svn_auth_iterstate_t
{
    const char *cred_kind;
    int provider_idx;
    void *provider_iter_baton;
};


svn_error_t * svn_auth_open(svn_auth_baton_t **baton,
                            apr_pool_t *pool)
{
    abort();
    return SVN_NO_ERROR;
}

svn_error_t * svn_auth_register_provider(svn_auth_baton_t *baton,
                                         int order,
                                         const svn_auth_provider_t *vtable,
                                         void *provider_baton,
                                         apr_pool_t *pool)
{
    abort();
    return SVN_NO_ERROR;
}


svn_error_t * svn_auth_first_credentials(void **credentials,
                                         svn_auth_iterstate_t **state,
                                         const char *cred_kind,
                                         svn_auth_baton_t *baton,
                                         apr_pool_t *pool)
{
    abort();
    return SVN_NO_ERROR;
}


svn_error_t * svn_auth_next_credentials(void **credentials,
                                        svn_auth_iterstate_t *state,
                                        apr_pool_t *pool)
{
    abort();
    return SVN_NO_ERROR;
}


svn_error_t * svn_auth_save_credentials(const char *cred_kind,
                                        void *credentials,
                                        svn_auth_baton_t *baton,
                                        apr_pool_t *pool)
{
    abort();
    return SVN_NO_ERROR;
}
