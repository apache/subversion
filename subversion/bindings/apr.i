/*
 * apr.i :  SWIG interface file for selected APR types
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

// only used by others; we won't build an APR module
//%module apr

// We can't include this because it uses "long long" which blows up SWIG
//%include apr.h

#define __attribute__(__x)

typedef int apr_status_t
