/*
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
#ifndef WINSVN_COM_MISC_H_
#define WINSVN_COM_MISC_H_
#pragma once

const UINT k_uicbINTAsDecimalString = sizeof("-2147483648") - 1;

// Root pool every other pool is a subpool of this pool.
extern apr_pool_t *g_pool;
// Pool to use for operations on the UI thread, use a separate pool
// for the libsvn_client thread.
extern apr_pool_t *g_global_pool;

// Converts an svn_error_t to an HRESULT, and
// an IErrorInfo record.
HRESULT 
convert_err_to_hresult(svn_error_t *error);

#endif
