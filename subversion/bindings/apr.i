/*
 * apr.i :  SWIG interface file for selected APR types
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
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

// only used by others; we won't build an APR module
//%module apr

// We can't include this because it uses "long long" which blows up SWIG
//%include apr.h

#define __attribute__(__x)

typedef int apr_status_t
typedef long apr_size_t
typedef struct apr_pool_t apr_pool_t
typedef struct apr_array_header_t apr_array_header_t
typedef long apr_off_t
