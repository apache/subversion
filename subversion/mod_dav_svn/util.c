/*
 * util.c: some handy utilities functions
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

#include <mod_dav.h>

#include "svn_error.h"
#include "dav_svn.h"


dav_error * dav_svn_convert_err(const svn_error_t *serr, int status,
                                const char *message)
{
    dav_error *derr;

    derr = dav_new_error(serr->pool, status, serr->apr_err, serr->message);
    if (message != NULL)
        derr = dav_push_error(serr->pool, status, serr->apr_err,
                              message, derr);
    return derr;
}


/* 
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
