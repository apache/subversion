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
#include <apr_time.h>

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
#define SVN_IS_VALID_REVNUM(n) ((n) >= 0)

/* The 'official' invalid revision num */
#define SVN_INVALID_REVNUM ((svn_revnum_t) -1)

/* Not really invalid...just unimportant -- one day, this can be its
   own unique value, for now, just make it the same as
   SVN_INVALID_REVNUM. */
#define SVN_IGNORED_REVNUM ((svn_revnum_t) -1) 

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


/* A general subversion directory entry. */
typedef struct svn_dirent
{
  enum svn_node_kind kind;  /* node kind */
  apr_off_t size;           /* length of file text, or 0 for directories */
  svn_boolean_t has_props;  /* does the node have props? */

  svn_revnum_t created_rev; /* last rev in which this node changed */
  apr_time_t time;          /* time of created_rev (mod-time) */
  const char *last_author;  /* author of created_rev */

} svn_dirent_t;



/*** Keyword substitution. ***/

/* The maximum size of an expanded or un-expanded keyword. */
#define SVN_KEYWORD_MAX_LEN    255

/* All the keywords Subversion recognizes.
 * 
 * Note that there is a better, more general proposal out there, which
 * would take care of both internationalization issues and custom
 * keywords (e.g., $NetBSD$).  See
 * 
 *    http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=8921
 *    =====
 *    From: "Jonathan M. Manning" <jmanning@alisa-jon.net>
 *    To: dev@subversion.tigris.org
 *    Date: Fri, 14 Dec 2001 11:56:54 -0500
 *    Message-ID: <87970000.1008349014@bdldevel.bl.bdx.com>
 *    Subject: Re: keywords
 *
 * and Eric Gillespie's support of same:
 * 
 *    http://subversion.tigris.org/servlets/ReadMsg?list=dev&msgNo=8757
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

typedef struct svn_log_changed_path_t
{
  char action; /* 'A'dd, 'D'elete, 'R'eplace, 'M'odify */
  const char *copyfrom_path; /* Source path of copy (if any). */
  svn_revnum_t copyfrom_rev; /* Source revision of copy (if any). */

} svn_log_changed_path_t;


/* The callback invoked by log message loopers, such as
 * svn_ra_plugin_t.get_log() and svn_repos_get_logs().
 *
 * This function is invoked once on each log message, in the order
 * determined by the caller (see above-mentioned functions).
 *
 * BATON, REVISION, AUTHOR, DATE, and MESSAGE are what you think they
 * are.  Any of AUTHOR, DATE, or MESSAGE may be null.
 *
 * If DATE is neither null nor the empty string, it was generated by
 * svn_time_to_string() and can be converted to apr_time_t with
 * svn_time_from_string().
 *
 * If CHANGED_PATHS is non-null, then it contains as keys every path
 * committed in REVISION; the values are (svn_log_changed_path_t *) 
 * structures (see above).
 *
 * ### The only reason CHANGED_PATHS is not qualified with `const' is
 * that we usually want to loop over it, and apr_hash_first() doesn't
 * take a const hash, for various reasons.  I'm not sure that those
 * "various reasons" are actually even relevant anymore, and if
 * they're not, it might be nice to change apr_hash_first() so
 * read-only uses of hashes can be protected via the type system.
 *
 * Use POOL for all allocation.  (If the caller is iterating over log
 * messages, invoking this receiver on each, we recommend the standard
 * pool loop recipe: create a subpool, pass it as POOL to each call,
 * clear it after each iteration, destroy it after the loop is done.)  */
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



/* If MIME_TYPE does not contain a "/", or ends with non-alphanumeric
   data, return SVN_ERR_BAD_MIME_TYPE, else return success.  Use POOL
   only to find error allocation.

   Goal: to match both "foo/bar" and "foo/bar; charset=blah", without
   being too strict about it, but to disallow mime types that have
   quotes, newlines, or other garbage on the end, such as might be
   unsafe in an HTTP header.

   ### Note: despite being about mime-TYPES, this probably doesn't
   ### belong in svn_types.h.  However, no other header is more
   ### appropriate, and didn't feel like creating svn_validate.h for
   ### this one function.
 */
svn_error_t *svn_validate_mime_type (const char *mime_type,
                                     apr_pool_t *pool);




#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_TYPES_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
