/*
 * ====================================================================
 * Copyright (c) 2007 CollabNet.  All rights reserved.
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
 *
 * svn_diff.i: SWIG interface file for svn_diff.h
 */

#if defined(SWIGPYTHON)
%module(package="libsvn") diff
#elif defined(SWIGPERL)
%module "SVN::_Diff"
#elif defined(SWIGRUBY)
%module "svn::ext::diff"
#endif

%include svn_global.swg
%import core.i

/* -----------------------------------------------------------------------
   %apply-ing of typemaps defined elsewhere
*/

%apply const char *MAY_BE_NULL {
    const char *original_header,
    const char *modified_header,
    const char *header_encoding,
    const char *relative_to_dir
};

#ifdef SWIGPYTHON
%apply svn_stream_t *WRAPPED_STREAM { svn_stream_t * };
#endif

/* ----------------------------------------------------------------------- */

%include svn_diff_h.swg
