/*
 * svn_types.h :  Subversion's data types
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

/* ==================================================================== */

/* This is more or less an implementation of the filesystem "schema"
   defined tin the design doc. */


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef SVN_TYPES_H
#define SVN_TYPES_H


#include <stdlib.h>          /* defines size_t */
#include <apr_pools.h>       /* APR memory pools for everyone. */
#include <apr_hash.h>        /* proplists are hashtables */



/* useful macro, suggested by Greg Stein */
#define APR_ARRAY_GET_ITEM(ary,i,type) (((type *)(ary)->elts)[i])

enum svn_node_kind
{
  svn_node_none,        /* absent */
  svn_node_file,        /* regular file */
  svn_node_dir,         /* directory */
  svn_node_unknown      /* something's here, but we don't know what */
};

/* This type exists because apr_item_t was removed from apr, and
   libsvn_subr/keysort.c needs structures like this to sort hashes. */

typedef struct svn_item_t {
    /** The key for the current table entry */
    char *key; 
    /** Size of the opaque block comprising the item's content. */
    size_t size;
    /** A pointer to the content itself. */
    void *data;
} svn_item_t;


typedef long int svn_revnum_t;

/* Valid revision numbers begin at 0 */
#define SVN_IS_VALID_REVNUM(n) (n >= 0)
#define SVN_INVALID_REVNUM (-1) /* The 'official' invalid revision num */
#define SVN_IGNORED_REVNUM (-1) /* Not really invalid...just
                                   unimportant -- one day, this can be
                                   its own unique value, for now, just
                                   make it the same as
                                   SVN_INVALID_REVNUM. */

/* YABT:  Yet Another Boolean Type */
typedef int svn_boolean_t;

#ifndef TRUE
#define TRUE 1
#endif /* TRUE */

#ifndef FALSE
#define FALSE 0
#endif /* FALSE */



/* Defines for reserved ("svn:") property names.  */

/* The fs revision property that stores the commit-log. */
#define SVN_PROP_REVISION_LOG "svn:log"

/* The propname *prefix* that makes a propname a "WC property". 
   
   For example, ra_dav might store a versioned-resource url as a WC
   prop like this:

      name = svn:wc:dav_url
      val  = http://www.lyra.org/repos/452348/e.289

   The client will try to protect WC props by warning users against
   changing them.  The client will also send them back to the RA layer
   when committing.  (gstein:  does the client need to send them when
   "reporting" wc state before an update, too?)
*/
#define SVN_PROP_WC_PREFIX "svn:wc:"



#endif  /* SVN_TYPES_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
