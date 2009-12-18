/*
 * ====================================================================
 * Copyright (c) 2000-2007 CollabNet.  All rights reserved.
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
 * svn_ra.i: SWIG interface file for svn_ra.h
 */

#if defined(SWIGPYTHON)
%module(package="libsvn") ra
#elif defined(SWIGPERL)
%module "SVN::_Ra"
#elif defined(SWIGRUBY)
%module "svn::ext::ra"
#endif

%include svn_global.swg
%import core.i
%import svn_delta.i

/* Bad pool convention, also these are not public interfaces, they were
   simply placed in the public header by mistake. */
%ignore svn_ra_svn_init;
%ignore svn_ra_local_init;
%ignore svn_ra_dav_init;
%ignore svn_ra_serf_init;

%apply Pointer NONNULL { svn_ra_callbacks2_t *callbacks };

/* -----------------------------------------------------------------------
   %apply-ing of typemaps defined elsewhere
*/
%apply const char *MAY_BE_NULL {
    const char *comment,
    const char *lock_token
};

#ifdef SWIGPYTHON
%apply svn_stream_t *WRAPPED_STREAM { svn_stream_t * };
#endif

/* ----------------------------------------------------------------------- */

#ifdef SWIGPYTHON
%typemap(in) (const svn_ra_callbacks2_t *callbacks, void *callback_baton) {
  svn_swig_py_setup_ra_callbacks(&$1, &$2, $input, _global_pool);
}
/* FIXME: svn_ra_callbacks_t ? */
#endif
#ifdef SWIGPERL
/* FIXME: svn_ra_callbacks2_t ? */
%typemap(in) (const svn_ra_callbacks_t *callbacks, void *callback_baton) {
  svn_ra_make_callbacks(&$1, &$2, $input, _global_pool);
}
#endif
#ifdef SWIGRUBY
%typemap(in) (const svn_ra_callbacks2_t *callbacks, void *callback_baton) {
  svn_swig_rb_setup_ra_callbacks(&$1, &$2, $input, _global_pool);
}
/* FIXME: svn_ra_callbacks_t ? */
#endif

#ifdef SWIGPYTHON
%callback_typemap(const svn_ra_reporter2_t *reporter, void *report_baton,
                  (svn_ra_reporter2_t *)&swig_py_ra_reporter2,
                  ,
                  )
%callback_typemap(svn_location_segment_receiver_t receiver, void *receiver_baton,
                  svn_swig_py_location_segment_receiver_func,
                  ,
                  )
#endif

#ifdef SWIGRUBY
%callback_typemap(const svn_ra_reporter3_t *reporter, void *report_baton,
                  ,
                  ,
                  svn_swig_rb_ra_reporter3)
#endif

#ifndef SWIGPERL
%callback_typemap(svn_ra_file_rev_handler_t handler, void *handler_baton,
                  svn_swig_py_ra_file_rev_handler_func,
                  ,
                  svn_swig_rb_ra_file_rev_handler)
#endif

%callback_typemap(svn_ra_lock_callback_t lock_func, void *lock_baton,
                  svn_swig_py_ra_lock_callback,
                  svn_swig_pl_ra_lock_callback,
                  svn_swig_rb_ra_lock_callback)

/* ----------------------------------------------------------------------- */

%include svn_ra_h.swg
