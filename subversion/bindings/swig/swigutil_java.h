/*
 * swigutil_java.h :  utility functions and stuff for the SWIG Java bindings
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


#ifndef SVN_SWIG_SWIGUTIL_JAVA_H
#define SVN_SWIG_SWIGUTIL_JAVA_H

#include <jni.h>

#include <apr.h>
#include <apr_pools.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_tables.h>

#include "svn_types.h"
#include "svn_string.h"
#include "svn_delta.h"
#include "svn_wc.h"

#include "swigutil_java_cache.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* convert an svn_error_t into a SubversionException and clear error */
jthrowable svn_swig_java_convert_error(JNIEnv *jenv, svn_error_t *error);

/* helper function to convert an apr_hash_t* (char* -> svnstring_t*) to
   a Java Map */
jobject svn_swig_java_prophash_to_dict(JNIEnv *jenv, apr_hash_t *hash);

/* convert a hash of 'const char *' -> TYPE into a Java Map */
jobject svn_swig_java_convert_hash(JNIEnv *jenv, apr_hash_t *hash);

/* add all the elements from an array to an existing java.util.List */
void svn_swig_java_add_to_list(JNIEnv* jenv, apr_array_header_t *array,
                               jobject list);

/* add all the elements from a hash to an existing java.util.Map */
void svn_swig_java_add_to_map(JNIEnv* jenv, apr_hash_t *hash, jobject map);

/* helper function to convert a 'char **' into a Java List of String
   objects */
jobject svn_swig_java_c_strings_to_list(JNIEnv *jenv, char **strings);

/* helper function to convert an array of 'const char *' to a Java List of
   String objects */
jobject svn_swig_java_array_to_list(JNIEnv *jenv,
                                    const apr_array_header_t *strings);

/* helper function to convert a Java List of String objects into an
   'apr_array_header_t *' of 'const char *' objects.  Note that the
   objects must remain alive -- the values are not copied. This is
   appropriate for incoming arguments which are defined to last the
   duration of the function's execution.  */
const apr_array_header_t *svn_swig_java_strings_to_array(JNIEnv *jenv,
                                                         jobject source,
                                                         apr_pool_t *pool);

/* make a editor that "thunks" from C callbacks up to Java */
void svn_swig_java_make_editor(JNIEnv *jenv,
                               const svn_delta_editor_t **editor,
                               void **edit_baton,
                               jobject java_editor,
                               apr_pool_t *pool);

/* a notify function that executes a Java method on an object which is
   passed in via the baton argument */
void svn_swig_java_notify_func(void *baton,
                               const char *path,
                               svn_wc_notify_action_t action,
                               svn_node_kind_t kind,
                               const char *mime_type,
                               svn_wc_notify_state_t content_state,
                               svn_wc_notify_state_t prop_state,
                               svn_revnum_t revision);

/* thunked commit log fetcher */
svn_error_t *svn_swig_java_get_commit_log_func(const char **log_msg,
                                              const char **tmp_file,
                                              apr_array_header_t *commit_items,
                                              void *baton,
                                              apr_pool_t *pool);

/* log messages are returned in this */
svn_error_t *svn_swig_java_log_message_receiver(void *baton,
      apr_hash_t *changed_paths,
      svn_revnum_t revision,
      const char *author,
      const char *date,  /* use svn_time_from_string() if need apr_time_t */
      const char *message,
      apr_pool_t *pool);

/* Create write-only svn_stream_t from java.io.OutputStream */
svn_stream_t *svn_swig_java_outputstream_to_stream(JNIEnv *jenv, 
                                                   jobject outputstream, 
                                                   apr_pool_t *pool);

/* Create read-only svn_stream_t from java.io.InputStream */
svn_stream_t *svn_swig_java_inputstream_to_stream(JNIEnv *jenv,
                                                  jobject inputstream,
                                                  apr_pool_t *pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_SWIG_SWIGUTIL_JAVA_H */
