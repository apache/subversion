/*
 * svn_props.h :  Subversion properties
 *
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

/* ==================================================================== */

#ifndef SVN_PROPS_H
#define SVN_PROPS_H

#include <apr_pools.h>
#include <apr_tables.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */




/* Subversion distinguishes among several kinds of properties,
   particularly on the client-side.  There is no "unknown" kind; if
   there's nothing special about a property name, the default category
   is `svn_prop_regular_kind'. */ 
typedef enum svn_prop_kind
{
  svn_prop_entry_kind,   /* In .svn/entries, i.e., author, date, etc. */
  svn_prop_wc_kind,      /* Client-side only, stored by specific RA layer. */
  svn_prop_regular_kind  /* Seen if user does "svn proplist"; note
                            that this includes some "svn:" props and
                            all user props, i.e. ones stored in the
                            repository fs. */
} svn_prop_kind_t;

/* Return the prop kind of a property named NAME, and (if PREFIX_LEN
   is non-NULL) set *PREFIX_LEN to the length of the prefix of NAME
   that was sufficient to distinguish its kind. */
svn_prop_kind_t svn_property_kind (int *prefix_len,
                                   const char *prop_name);


/* Return TRUE iff PROP_NAME represents the name of a Subversion
   property. */
svn_boolean_t svn_prop_is_svn_prop (const char *prop_name);


/* Given an PROPLIST array of svn_prop_t structures, allocate three
   new arrays in POOL.  Categorize each property and then create new
   svn_prop_t structures in the proper lists.  Each new svn_prop_t
   structure's fields will point to the same data within PROPLIST's
   structures.

   If no props exist in a certain category, then the array will come
   back with ->nelts == 0.

   ### Hmmm, maybe a better future interface is to return an array of
       arrays, where the index into the array represents the index
       into enum svn_prop_kind.  That way we can add more prop kinds
       in the future without changing this interface...
 */
svn_error_t *svn_categorize_props (const apr_array_header_t *proplist,
                                   apr_array_header_t **entry_props,
                                   apr_array_header_t **wc_props,
                                   apr_array_header_t **regular_props,
                                   apr_pool_t *pool);



/* Defines for reserved ("svn:") property names.  */

/* All Subversion property names start with this. */
#define SVN_PROP_PREFIX "svn:"


/* --------------------------------------------------------------------- */
/** VISIBLE PROPERTIES **/

/* These are regular properties that are attached to ordinary files
   and dirs, and are visible (and tweakable) by svn client programs
   and users.  Adding these properties causes specific effects.  */

/* The mime-type of a given file. */
#define SVN_PROP_MIME_TYPE  SVN_PROP_PREFIX "mime-type"

/* The ignore patterns for a given directory. */
#define SVN_PROP_IGNORE  SVN_PROP_PREFIX "ignore"

/* The line ending style for a given file. */
#define SVN_PROP_EOL_STYLE  SVN_PROP_PREFIX "eol-style"

/* The "activated" keywords (for keyword substitution) for a given file. */
#define SVN_PROP_KEYWORDS  SVN_PROP_PREFIX "keywords"

/* Set to either TRUE or FALSE if we want a file to be executable or not. */
#define SVN_PROP_EXECUTABLE  SVN_PROP_PREFIX "executable"

/* Describes external items to check out into this directory. 
 *
 * The format is a series of lines, such as:
 *
 *   localdir1           http://url.for.external.source/etc/
 *   localdir1/foo       http://url.for.external.source/foo
 *   localdir1/bar       http://blah.blah.blah/repositories/theirproj
 *   localdir1/bar/baz   http://blorg.blorg.blorg/basement/code
 *   localdir2           http://another.url/blah/blah/blah
 *   localdir3           http://and.so.on/and/so/forth
 *
 * The subdir names on the left side are relative to the directory on
 * which this property is set.
 */
#define SVN_PROP_EXTERNALS  SVN_PROP_PREFIX "externals"

/* --------------------------------------------------------------------- */
/** INVISIBLE PROPERTIES  **/

/* WC props are props that are invisible to users:  they're generated
   by an RA layer, and stored in secret parts of .svn/.  */

/* The propname *prefix* that makes a propname a "WC property". 
   For example, ra_dav might store a versioned-resource url as a WC
   prop like this:

      name = svn:wc:dav_url
      val  = http://www.lyra.org/repos/452348/e.289

   The client will try to protect WC props by warning users against
   changing them.  The client will also send them back to the RA layer
   when committing.  */
#define SVN_PROP_WC_PREFIX     SVN_PROP_PREFIX "wc:"

/* Another type of non-user-visible property.  "Entry properties" are
   stored as fields with the adminstrative 'entries' file.  
*/
#define SVN_PROP_ENTRY_PREFIX  SVN_PROP_PREFIX "entry:"

/* Define specific entry-property names.  */
#define SVN_PROP_ENTRY_COMMITTED_REV     SVN_PROP_ENTRY_PREFIX "committed-rev"
#define SVN_PROP_ENTRY_COMMITTED_DATE    SVN_PROP_ENTRY_PREFIX "committed-date"
#define SVN_PROP_ENTRY_LAST_AUTHOR       SVN_PROP_ENTRY_PREFIX "last-author"

/* When custom, user-defined properties are passed over the wire, they will
   have this prefix added to their name */
#define SVN_PROP_CUSTOM_PREFIX SVN_PROP_PREFIX "custom:"

/** These are reserved properties attached to a "revision" object in
    the repository filesystem.  They can be queried by using
    svn_fs_revision_prop().  They are invisible to svn clients. **/

/* The fs revision property that stores a commit's author. */
#define SVN_PROP_REVISION_AUTHOR  SVN_PROP_PREFIX "author"

/* The fs revision property that stores a commit's log message. */
#define SVN_PROP_REVISION_LOG  SVN_PROP_PREFIX "log"

/* The fs revision property that stores a commit's date. */
#define SVN_PROP_REVISION_DATE  SVN_PROP_PREFIX "date"




#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_PROPS_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
