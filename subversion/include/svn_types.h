/*
 * svn_types.h :  Subversion's data types
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

#ifndef SVN_TYPES_H
#define SVN_TYPES_H

#include <apr.h>        /* for apr_size_t */

/* ### these should go away, but I don't feel like working on it yet */
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/*** Errors. ***/

/* Subversion error object.  Defined here, rather than in svn_error.h,
   to avoid a recursive #include situation. */
typedef struct svn_error
{
  apr_status_t apr_err;      /* APR error value, possibly SVN_ custom err */
  int src_err;               /* native error code (e.g. errno, h_errno...) */
  const char *message;       /* details from producer of error */
  struct svn_error *child;   /* ptr to the error we "wrap" */
  apr_pool_t *pool;          /* The pool holding this error and any
                                child errors it wraps */

  /* Source file and line where the error originated.
     Only used iff SVN_DEBUG. */
  const char *file;
  long line;
} svn_error_t;



/* index into an apr_array_header_t */
#define APR_ARRAY_IDX(ary,i,type) (((type *)(ary)->elts)[i])

typedef enum svn_node_kind
{
  svn_node_none,        /* absent */
  svn_node_file,        /* regular file */
  svn_node_dir,         /* directory */
  svn_node_unknown      /* something's here, but we don't know what */
} svn_node_kind_t;


typedef long int svn_revnum_t;

/* Valid revision numbers begin at 0 */
#define SVN_IS_VALID_REVNUM(n) (n >= 0)
#define SVN_INVALID_REVNUM (-1) /* The 'official' invalid revision num */
#define SVN_IGNORED_REVNUM (-1) /* Not really invalid...just
                                   unimportant -- one day, this can be
                                   its own unique value, for now, just
                                   make it the same as
                                   SVN_INVALID_REVNUM. */

/* Convert null-terminated C string STR to a revision number. */
#define SVN_STR_TO_REV(str) ((svn_revnum_t) atol(str))

/* In printf()-style functions, format revision numbers using this. */
#define SVN_REVNUM_T_FMT "ld"

/* YABT:  Yet Another Boolean Type */
typedef int svn_boolean_t;

#ifndef TRUE
#define TRUE 1
#endif /* TRUE */

#ifndef FALSE
#define FALSE 0
#endif /* FALSE */


/* An enum to indicate whether recursion is needed. */
enum svn_recurse_kind
{
  svn_nonrecursive = 1,
  svn_recursive
};




/* Subversion distinguishes among several kinds of properties,
   particularly on the client-side.  There is no "unknown" kind; if
   there's nothing special about a property name, the default category
   is `svn_prop_regular_kind'. */ 
enum svn_prop_kind
  {
    svn_prop_entry_kind,   /* In .svn/entries, i.e., author, date, etc. */
    svn_prop_wc_kind,      /* Client-side only, stored by specific RA layer. */
    svn_prop_regular_kind  /* Seen if user does "svn proplist"; note
                              that this includes some "svn:" props and
                              all user props, i.e. ones stored in the
                              repository fs. */
  };

/* Return the prop kind of a property named NAME, and set *PREFIX_LEN
   to the length of the prefix of NAME that was sufficient to
   distinguish its kind. */
enum svn_prop_kind svn_property_kind (int *prefix_len,
                                      const char *prop_name);


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

/* The character set of a given file. */
#define SVN_PROP_CHARSET  SVN_PROP_PREFIX "charset"

/* --------------------------------------------------------------------- */
/** INVISBILE PROPERTIES  **/

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




/*** Keyword substitution. ***/

/* The maximum size of an expanded or un-expanded keyword. */
#define SVN_KEYWORD_MAX_LEN    255

/* All the keywords Subversion recognizes.
 * 
 * Note that there is a better, more general proposal out there, which
 * would take care of both internationalization issues and custom
 * keywords (e.g., $NetBSD$).  See
 * 
 *    http://subversion.tigris.org/servlets/ReadMsg?msgId=49180&listName=dev
 *    =====
 *    From: "Jonathan M. Manning" <jmanning@alisa-jon.net>
 *    To: dev@subversion.tigris.org
 *    Date: Fri, 14 Dec 2001 11:56:54 -0500
 *    Message-ID: <87970000.1008349014@bdldevel.bl.bdx.com>
 *    Subject: Re: keywords
 *
 * and Eric Gillespie's support of same:
 * 
 *    http://subversion.tigris.org/servlets/ReadMsg?msgId=48658&listName=dev
 *    =====
 *    From: "Eric Gillespie, Jr." <epg@pretzelnet.org>
 *    To: dev@subversion.tigris.org
 *    Date: Wed, 12 Dec 2001 09:48:42 -0500
 *    Message-ID: <87k7vsebp1.fsf@vger.pretzelnet.org>
 *    Subject: Re: Customizable Keywords
 *
 * However, it is considerably more complex than the scheme below.
 * For now we're going with simplicity, hopefully the more general
 * solution can be done post-1.0.
 */

/* The most recent revision in which this file was changed. */
#define SVN_KEYWORD_REVISION_LONG    "LastChangedRevision"
#define SVN_KEYWORD_REVISION_SHORT   "Rev"

/* The most recent date (repository time) when this file was changed. */
#define SVN_KEYWORD_DATE_LONG        "LastChangedDate"
#define SVN_KEYWORD_DATE_SHORT       "Date"

/* Who most recently committed to this file. */
#define SVN_KEYWORD_AUTHOR_LONG      "LastChangedBy"
#define SVN_KEYWORD_AUTHOR_SHORT     "Author"

/* The URL for the head revision of this file. */
#define SVN_KEYWORD_URL_LONG         "HeadURL"
#define SVN_KEYWORD_URL_SHORT        "URL"



/*** Shared function types ***/

/* The callback invoked by log message loopers, such as
 * svn_ra_plugin_t.get_log() and svn_repos_get_logs().
 *
 * This function is invoked once on each log message, in the order
 * determined by the caller (see above-mentioned functions).
 *
 * BATON, REVISION, AUTHOR, DATE, and MESSAGE are what you think they
 * are.  DATE was generated by svn_time_to_string() and can be
 * converted to apr_time_t with svn_time_from_string().  
 *
 * If CHANGED_PATHS is non-null, then it contains as keys every path
 * committed in REVISION; the values are (void *) 'A' or 'D' or 'R',
 * for added, deleted, or replaced (text or property mod),
 * respectively.  Note to developers: there is no compelling reason
 * for these particular values -- they were chosen to match
 * `svn_repos_node_t.action', but if more information were desired, we
 * could switch to a different convention.
 *
 * ### The only reason CHANGED_PATHS is not qualified with `const' is
 * that we usually want to loop over it, and apr_hash_first() doesn't
 * take a const hash, for various reasons.  I'm not sure that those
 * "various reasons" are actually ever relevant anymore, and if
 * they're not, it might be nice to change apr_hash_first() so
 * read-only uses of hashes can be protected via the type system.
 *
 * Use POOL for all allocation.  (If the caller is iterating over log
 * messages, invoking this receiver on each, we recommend the standard
 * pool loop recipe: create a subpool, pass it as POOL to each call,
 * clear it after each iteration, destroy it after the loop is done.)
 */
typedef svn_error_t *(*svn_log_message_receiver_t)
     (void *baton,
      apr_hash_t *changed_paths,
      svn_revnum_t revision,
      const char *author,
      const char *date,  /* use svn_time_from_string() if need apr_time_t */
      const char *message,
      apr_pool_t *pool);


/* The maximum amount we (ideally) hold in memory at a time when
 * processing a stream of data.  For example, when copying data from
 * one stream to another, do it in blocks of this size; also, the
 * standard size of one svndiff window; etc.
 */
#define SVN_STREAM_CHUNK_SIZE 102400




#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_TYPES_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
