/**
 * @copyright
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
 * @endcopyright
 *
 * @file svn_types.h
 * @brief Subversion's data types
 */

#ifndef SVN_TYPES_H
#define SVN_TYPES_H

#include <apr.h>        /* for apr_size_t */

/* ### these should go away, but I don't feel like working on it yet */
#include <stdlib.h>
#include <apr_pools.h>
#include <apr_hash.h>
#include <apr_tables.h>
#include <apr_time.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */



/** Subversion error object.
 *
 * Defined here, rather than in svn_error.h, to avoid a recursive #include 
 * situation.
 */
typedef struct svn_error
{
  /** APR error value, possibly SVN_ custom err */
  apr_status_t apr_err;

  /** details from producer of error */
  const char *message;

  /** ptr to the error we "wrap" */
  struct svn_error *child;

  /** The pool holding this error and any child errors it wraps */
  apr_pool_t *pool;

  /** Source file where the error originated.  Only used iff @c SVN_DEBUG. */
  const char *file;

  /** Source line where the error originated.  Only used iff @c SVN_DEBUG. */
  long line;

} svn_error_t;



/** index into an apr_array_header_t */
#define APR_ARRAY_IDX(ary,i,type) (((type *)(ary)->elts)[i])

/** The various types of nodes in the Subversion filesystem. */
typedef enum
{
  /* absent */
  svn_node_none,

  /* regular file */
  svn_node_file,

  /* directory */
  svn_node_dir,

  /* something's here, but we don't know what */
  svn_node_unknown

} svn_node_kind_t;


/** A revision number. */
typedef long int svn_revnum_t;

/** Valid revision numbers begin at 0 */
#define SVN_IS_VALID_REVNUM(n) ((n) >= 0)

/** The 'official' invalid revision num */
#define SVN_INVALID_REVNUM ((svn_revnum_t) -1)

/** Not really invalid...just unimportant -- one day, this can be its
 * own unique value, for now, just make it the same as
 * @c SVN_INVALID_REVNUM.
 */
#define SVN_IGNORED_REVNUM ((svn_revnum_t) -1) 

/** Convert null-terminated C string @a str to a revision number. */
#define SVN_STR_TO_REV(str) ((svn_revnum_t) atol(str))

/** In @c printf()-style functions, format revision numbers using this. */
#define SVN_REVNUM_T_FMT "ld"

/** YABT:  Yet Another Boolean Type */
typedef int svn_boolean_t;

#ifndef TRUE
/** uhh... true */
#define TRUE 1
#endif /* TRUE */

#ifndef FALSE
/** uhh... false */
#define FALSE 0
#endif /* FALSE */


/** An enum to indicate whether recursion is needed. */
enum svn_recurse_kind
{
  svn_nonrecursive = 1,
  svn_recursive
};


/** A general subversion directory entry. */
typedef struct svn_dirent
{
  /** node kind */
  svn_node_kind_t kind;

  /** length of file text, or 0 for directories */
  apr_off_t size;

  /** does the node have props? */
  svn_boolean_t has_props;

  /** last rev in which this node changed */
  svn_revnum_t created_rev;

  /** time of created_rev (mod-time) */
  apr_time_t time;

  /** author of created_rev */
  const char *last_author;

} svn_dirent_t;




/** Keyword substitution.
 *
 * All the keywords Subversion recognizes.
 * 
 * Note that there is a better, more general proposal out there, which
 * would take care of both internationalization issues and custom
 * keywords (e.g., $NetBSD$).  See
 * 
 *<pre>    http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=8921
 *    =====
 *    From: "Jonathan M. Manning" <jmanning@alisa-jon.net>
 *    To: dev@subversion.tigris.org
 *    Date: Fri, 14 Dec 2001 11:56:54 -0500
 *    Message-ID: <87970000.1008349014@bdldevel.bl.bdx.com>
 *    Subject: Re: keywords</pre>
 *
 * and Eric Gillespie's support of same:
 *
 *<pre>    http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=8757
 *    =====
 *    From: "Eric Gillespie, Jr." <epg@pretzelnet.org>
 *    To: dev@subversion.tigris.org
 *    Date: Wed, 12 Dec 2001 09:48:42 -0500
 *    Message-ID: <87k7vsebp1.fsf@vger.pretzelnet.org>
 *    Subject: Re: Customizable Keywords</pre>
 *
 * However, it is considerably more complex than the scheme below.
 * For now we're going with simplicity, hopefully the more general
 * solution can be done post-1.0.
 *
 * @defgroup svn_types_keywords keywords
 * @{
 */

/** The maximum size of an expanded or un-expanded keyword. */
#define SVN_KEYWORD_MAX_LEN    255

/** The most recent revision in which this file was changed. */
#define SVN_KEYWORD_REVISION_LONG    "LastChangedRevision"

/** Short version of LastChangedRevision */
#define SVN_KEYWORD_REVISION_SHORT   "Rev"

/** The most recent date (repository time) when this file was changed. */
#define SVN_KEYWORD_DATE_LONG        "LastChangedDate"

/** Short version of LastChangedDate */
#define SVN_KEYWORD_DATE_SHORT       "Date"

/** Who most recently committed to this file. */
#define SVN_KEYWORD_AUTHOR_LONG      "LastChangedBy"

/** Short version of LastChangedBy */
#define SVN_KEYWORD_AUTHOR_SHORT     "Author"

/** The URL for the head revision of this file. */
#define SVN_KEYWORD_URL_LONG         "HeadURL"

/** Short version of HeadURL */
#define SVN_KEYWORD_URL_SHORT        "URL"

/** A compressed combination of the other four keywords.
 *
 * (But see comments above about a more general solution to keyword
 * combinations.)
 */
#define SVN_KEYWORD_ID               "Id"

/** @} */


/** A structure to represent a path that changed for a log entry. */
typedef struct svn_log_changed_path_t
{
  /** 'A'dd, 'D'elete, 'R'eplace, 'M'odify */
  char action;

  /** Source path of copy (if any). */
  const char *copyfrom_path;

  /** Source revision of copy (if any). */
  svn_revnum_t copyfrom_rev;

} svn_log_changed_path_t;


/** The callback invoked by log message loopers, such as
 * @c svn_ra_plugin_t.get_log() and @c svn_repos_get_logs().
 *
 * This function is invoked once on each log message, in the order
 * determined by the caller (see above-mentioned functions).
 *
 * @a baton, @a revision, @a author, @a date, and @a message are what you 
 * think they are.  Any of @a author, @a date, or @a message may be @c NULL.
 *
 * If @a date is neither null nor the empty string, it was generated by
 * @c svn_time_to_string() and can be converted to @c apr_time_t with
 * @c svn_time_from_string().
 *
 * If @a changed_paths is non-@c NULL, then it contains as keys every path
 * committed in @a revision; the values are (@c svn_log_changed_path_t *) 
 * structures (see above).
 *
 * ### The only reason @a changed_paths is not qualified with `const' is
 * that we usually want to loop over it, and @c apr_hash_first() doesn't
 * take a const hash, for various reasons.  I'm not sure that those
 * "various reasons" are actually even relevant anymore, and if
 * they're not, it might be nice to change @c apr_hash_first() so
 * read-only uses of hashes can be protected via the type system.
 *
 * Use @a pool for all allocation.  (If the caller is iterating over log
 * messages, invoking this receiver on each, we recommend the standard
 * pool loop recipe: create a subpool, pass it as @a pool to each call,
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


/** The maximum amount we (ideally) hold in memory at a time when
 * processing a stream of data.
 *
 * The maximum amount we (ideally) hold in memory at a time when
 * processing a stream of data.  For example, when copying data from 
 * one stream to another, do it in blocks of this size; also, the 
 * standard size of one svndiff window; etc.
 */
#define SVN_STREAM_CHUNK_SIZE 102400



/* ### Note: despite being about mime-TYPES, thes probably don't
 * ### belong in svn_types.h.  However, no other header is more
 * ### appropriate, and didn't feel like creating svn_validate.h for
 * ### so little.
 */

/** Validate @a mime_type.
 *
 * If @a mime_type does not contain a "/", or ends with non-alphanumeric
 * data, return @c SVN_ERR_BAD_MIME_TYPE, else return success.
 * 
 * Use @a pool only to find error allocation.
 *
 * Goal: to match both "foo/bar" and "foo/bar; charset=blah", without
 * being too strict about it, but to disallow mime types that have
 * quotes, newlines, or other garbage on the end, such as might be
 * unsafe in an HTTP header.
 */
svn_error_t *svn_mime_type_validate (const char *mime_type,
                                     apr_pool_t *pool);


/** Determine if @a mime_type is binary.
 *
 * Return false iff @a mime_type is a textual type.
 *
 * All mime types that start with "text/" are textual, plus some special 
 * cases (for example, "image/x-xbitmap").
 */
svn_boolean_t svn_mime_type_is_binary (const char *mime_type);




#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_TYPES_H */
