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

/* This is never a valid revision number.  (Actually, anything less
   than 0 is never a valid revision number.) */
#define SVN_INVALID_REVNUM (-1)


/* YABT:  Yet Another Boolean Type */
typedef int svn_boolean_t;

#ifndef TRUE
#define TRUE 1
#endif /* TRUE */

#ifndef FALSE
#define FALSE 0
#endif /* FALSE */



#endif  /* SVN_TYPES_H */

#ifdef __cplusplus
}
#endif /* __cplusplus */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
