/*
 * util.i :  SWIG interface file for various SVN and APR utilities
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

%module _util

%include "typemaps.i"

%import apr.i
%import svn_types.i

/* -----------------------------------------------------------------------
   apr_size_t * is always an IN/OUT parameter in svn_io.h
*/

%typemap(in) apr_size_t * = apr_size_t *INOUT;
%typemap(argout) apr_size_t * = apr_size_t *INOUT;

/* ----------------------------------------------------------------------- */

%include svn_io.h
%include svn_pools.h
%include svn_version.h
%{
#include "svn_io.h"
#include "svn_pools.h"
#include "svn_version.h"
%}
