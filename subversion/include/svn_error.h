/* svn_error.h:  common exception handling for Subversion
 *
 * ====================================================================
 * Copyright (c) 2000-2001 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 * ====================================================================
 */




#ifndef SVN_ERROR_H
#define SVN_ERROR_H

#include <apr.h>
#include <apr_errno.h>     /* APR's error system */
#include <apr_pools.h>

#define APR_WANT_STDIO
#include <apr_want.h>

#include <svn_types.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define SVN_NO_ERROR   0   /* the best kind of (svn_error_t *) ! */

/* ### define this symbol for the new error string stuff */
/* #define TEST_ALTERNATE_ERROR_SYSTEM */
#ifdef TEST_ALTERNATE_ERROR_SYSTEM

#include "svn_error_codes.h"
char * svn_strerror (apr_status_t statcode, char *buf, apr_size_t bufsize);

#else /* TEST_ALTERNATE_ERROR_SYSTEM */

/* 
   Define custom Subversion error numbers, in the range reserved for
   that in APR: from APR_OS_START_USEERR to APR_OS_START_SYSERR (see
   apr_errno.h).

   *****  HEY, WAKE UP! ******

   If you're about to add a new svn-specific errorcode below, be SURE
   you add an english description to the svn_strerror() hash in
   svn_error.c.

   If you forget, it's not catastrophic; svn_strerror() will print a
   generic "undefined" message for your error.

*/
typedef enum svn_errno_t {
  SVN_WARNING = (APR_OS_START_USEERR + 1),
  SVN_ERR_NOT_AUTHORIZED,
  SVN_ERR_PLUGIN_LOAD_FAILURE,
  SVN_ERR_UNKNOWN_FS_ACTION,
  SVN_ERR_UNEXPECTED_EOF,
  SVN_ERR_MALFORMED_FILE,
  SVN_ERR_INCOMPLETE_DATA,

  /* The xml delta we got was not well-formed. */
  SVN_ERR_MALFORMED_XML,

  /* A working copy "descent" crawl came up empty */
  SVN_ERR_UNFRUITFUL_DESCENT,

  /* A bogus filename was passed to a routine */
  SVN_ERR_BAD_FILENAME,

  /* Trying to use an as-yet unsupported feature. */
  SVN_ERR_UNSUPPORTED_FEATURE,

  /* There's no such xml tag attribute */
  SVN_ERR_XML_ATTRIB_NOT_FOUND,

  /* A delta-pkg is missing ancestry. */
  SVN_ERR_XML_MISSING_ANCESTRY,

  /* A binary data encoding was specified which we don't know how to decode. */
  SVN_ERR_XML_UNKNOWN_ENCODING,

  /* Not one of the valid kinds in svn_node_kind. */
  SVN_ERR_UNKNOWN_NODE_KIND,

  /* Can't do this update or checkout, because something was in the way. */
  SVN_ERR_WC_OBSTRUCTED_UPDATE,

  /* A mismatch popping the wc unwind stack. */
  SVN_ERR_WC_UNWIND_MISMATCH,

  /* Trying to pop an empty unwind stack. */
  SVN_ERR_WC_UNWIND_EMPTY,

  /* Trying to unlock when there's non-empty unwind stack. */
  SVN_ERR_WC_UNWIND_NOT_EMPTY,

  /* What happens if a non-blocking call to svn_wc__lock() encounters
     another lock. */
  SVN_ERR_WC_LOCKED,

  /* Something's wrong with the log file format. */
  SVN_ERR_WC_BAD_ADM_LOG,

  /* Unable to find a file or dir in the working copy. */
  SVN_ERR_WC_PATH_NOT_FOUND,

  /* Unable to find an entry.  Not always a fatal error, by the way. */
  SVN_ERR_WC_ENTRY_NOT_FOUND,

  /* Entry already exists when adding a file. */
  SVN_ERR_WC_ENTRY_EXISTS,

  /* Unable to get revision for an entry. */
  SVN_ERR_WC_ENTRY_MISSING_REVISION,

  /* Unable to get ancestry for an entry. */
  SVN_ERR_WC_ENTRY_MISSING_ANCESTRY,

  /* Entry has some invalid attribute value. */
  SVN_ERR_WC_ENTRY_ATTRIBUTE_INVALID,

  /* Bogus attributes are trying to be merged into an entry */
  SVN_ERR_WC_ENTRY_BOGUS_MERGE,

  /* Working copy is not up-to-date w.r.t. the repository. */
  SVN_ERR_WC_NOT_UP_TO_DATE,

  /* A recursive directory removal left locally modified files
     behind. */
  SVN_ERR_WC_LEFT_LOCAL_MOD,

  /* No unique names available for tmp files. */
  SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,

  /* If a working copy conflict is found (say, during a commit) */
  SVN_ERR_WC_FOUND_CONFLICT,

  /* The working copy is in an invalid state */
  SVN_ERR_WC_CORRUPT,

  /* A general filesystem error.  */
  SVN_ERR_FS_GENERAL,

  /* An error occurred while trying to close a Subversion filesystem.
     This status code is meant to be returned from APR pool cleanup
     functions; since that interface doesn't allow us to provide more
     detailed information, this is all you'll get.  */
  SVN_ERR_FS_CLEANUP,

  /* You called svn_fs_newfs or svn_fs_open, but the filesystem object
     you provided already refers to some filesystem.  You should allocate
     a fresh filesystem object with svn_fs_new, and use that instead.  */
  SVN_ERR_FS_ALREADY_OPEN,

  /* You tried to perform an operation on a filesystem object which
     hasn't been opened on any actual database yet.  You need to call
     `svn_fs_open_berkeley', `svn_fs_create_berkeley', or something
     like that.  */
  SVN_ERR_FS_NOT_OPEN,

  /* The filesystem has been corrupted.  The filesystem library found
     improperly formed data in the database.  */
  SVN_ERR_FS_CORRUPT,

  /* The name given is not a valid directory entry name, or filename.  */
  SVN_ERR_FS_PATH_SYNTAX,

  /* The filesystem has no revision by the given number.  */
  SVN_ERR_FS_NO_SUCH_REVISION,

  /* The filesystem has no transaction with the given name.  */
  SVN_ERR_FS_NO_SUCH_TRANSACTION,

  /* A particular entry was not found in a directory. */
  SVN_ERR_FS_NO_SUCH_ENTRY,

  /* A particular representation was not found. */
  SVN_ERR_FS_NO_SUCH_REPRESENTATION,

  /* A particular string was not found. */
  SVN_ERR_FS_NO_SUCH_STRING,

  /* There is no file by the given name.  */
  SVN_ERR_FS_NOT_FOUND,

  /* The given node revision id does not exist.  */
  SVN_ERR_FS_ID_NOT_FOUND,

  /* The given string does not represent a node or node revision id. */
  SVN_ERR_FS_NOT_ID,

  /* The name given does not refer to a directory.  */
  SVN_ERR_FS_NOT_DIRECTORY,

  /* The name given does not refer to a file.  */
  SVN_ERR_FS_NOT_FILE,

  /* The name given is not a single path component.  */
  SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT,

  /* The caller attempted to change or delete a node which is not mutable.  */
  SVN_ERR_FS_NOT_MUTABLE,

  /* You tried to create a new file in a filesystem revision, but the
     file already exists. */
  SVN_ERR_FS_ALREADY_EXISTS,

  /* Tried to remove a non-empty directory. */
  SVN_ERR_FS_DIR_NOT_EMPTY,

  /* You tried to remove the root directory of a filesystem revision,
     or create another node named /.  */
  SVN_ERR_FS_ROOT_DIR,

  /* The root object given is not a transaction root.  */
  SVN_ERR_FS_NOT_TXN_ROOT,

  /* The root object given is not a revision root.  */
  SVN_ERR_FS_NOT_REVISION_ROOT,

  /* The transaction could not be committed, because of a conflict with
     a prior change.  */
  SVN_ERR_FS_CONFLICT,

  /* This TXN's base root is not the same as that of the youngest
     revision in the repository.  */
  SVN_ERR_TXN_OUT_OF_DATE,

  /* The error is a Berkeley DB error.  `src_err' is the Berkeley DB
     error code, and `message' is an error message.  */
  SVN_ERR_BERKELEY_DB,

  /* ### need to come up with a mechanism for RA-specific errors */

  /* a bad URL was passed to the repository access layer */
  SVN_ERR_RA_ILLEGAL_URL,

  /* These RA errors are specific to ra_dav */

    /* the repository access layer could not initialize the socket layer */
    SVN_ERR_RA_SOCK_INIT,

    /* the repository access layer could not lookup the hostname */
    SVN_ERR_RA_HOSTNAME_LOOKUP,

    /* could not create an HTTP request */
    SVN_ERR_RA_CREATING_REQUEST,

    /* a request to the server failed in some way */
    SVN_ERR_RA_REQUEST_FAILED,

    /* error making an Activity for the commit to the server */
    SVN_ERR_RA_MKACTIVITY_FAILED,

    /* could not delete a resource on the server */
    SVN_ERR_RA_DELETE_FAILED,

  /* End of ra_dav errors */

  /* These RA errors are specific to ra_local */
  
    /* the given URL does not seem to point to a versioned resource */
    SVN_ERR_RA_NOT_VERSIONED_RESOURCE,

    /* the update reporter was given a bogus first path. */
    SVN_ERR_RA_BAD_REVISION_REPORT, 
 
  /* End of ra_local errors */

  /* an unsuitable container-pool was passed to svn_make_pool() */
  SVN_ERR_BAD_CONTAINING_POOL,

  /* the server was misconfigured: a pathname to an SVN FS was not supplied */
  SVN_ERR_APMOD_MISSING_PATH_TO_FS,

  /* the specified URI refers to our namespace, but was malformed */
  SVN_ERR_APMOD_MALFORMED_URI,

  /* A test in the test suite failed.  The test suite uses this
     error internally.  */
  SVN_ERR_TEST_FAILED,

  /* BEGIN Client errors */

  /* Generic arg parsing error */
  SVN_ERR_CL_ARG_PARSING_ERROR,

  /* An operation was attempted on the administrative subdirectory
     (i.e., user tried to import it, or update it, or...) */
  SVN_ERR_CL_ADM_DIR_RESERVED,

  /* END Client errors */
  

  /* simple placeholder to mark the highest SVN error. subtle benny: we don't
     have to worry about trailing commas (on errors above) as we add them */
  SVN_ERR_LAST
} svn_errno_t;



/* Lookup APR_ERR and return an english description.

   POOL is used on the very first call to allocate a hash of
   descriptions. */
const char *svn_strerror (apr_status_t apr_err, apr_pool_t *pool);

#endif /* TEST_ALTERNATE_ERROR_SYSTEM */



/*** SVN error creation and destruction. ***/

typedef struct svn_error
{
  apr_status_t apr_err;      /* APR error value, possibly SVN_ custom err */
  int src_err;               /* native error code (e.g. errno, h_errno...) */
  const char *message;       /* details from producer of error */
  struct svn_error *child;   /* ptr to the error we "wrap" */
  apr_pool_t *pool;          /* The pool holding this error and any
                                child errors it wraps */
} svn_error_t;



/*
  svn_error_create() : for creating nested exception structures.

  Input:  an APR or SVN custom error code,
          the original errno,
          a "child" error to wrap,
          a pool
          a descriptive message,

  Returns:  a new error structure (containing the old one).

  Notes: Errors are always allocated in a special top-level error
         pool, obtained from POOL's attributes.  If POOL is null, then
         the error pool is obtained from CHILD's pool's attributes.
         A pool has this attribute if it was allocated using
         svn_pool_create().

         If creating the "bottommost" error in a chain, pass NULL for
         the child argument.
 */
svn_error_t *svn_error_create (apr_status_t apr_err,
                               int src_err,
                               svn_error_t *child,
                               apr_pool_t *pool,
                               const char *message);

/* Create an error structure with the given APR_ERR, SRC_ERR, CHILD,
   and POOL, with a printf-style error message produced by passing
   FMT, ... through apr_psprintf.  */
svn_error_t *svn_error_createf (apr_status_t apr_err,
                                int src_err,
                                svn_error_t *child,
                                apr_pool_t *pool,
                                const char *fmt, 
                                ...)
       __attribute__ ((format (printf, 5, 6)));


/* A quick n' easy way to create a wrappered exception with your own
   message, before throwing it up the stack.  (It uses all of the
   child's fields.)  */
svn_error_t *svn_error_quick_wrap (svn_error_t *child, const char *new_msg);


/* Free ERROR by destroying its pool; note that the pool may be shared
   with wrapped child errors inside this error. */
void svn_error_free (svn_error_t *error);


/* Very basic default error handler: print out error stack, and quit
   iff the FATAL flag is set. */
void svn_handle_error (svn_error_t *error,
                       FILE *stream,
                       svn_boolean_t fatal);

/* Basic, default warning handler, just prints to stderr. */
void svn_handle_warning (void *data, const char *fmt, ...);


/* A statement macro for checking error return values.
   Evaluate EXPR.  If it yields an error, return that error from the
   current function.  Otherwise, continue.

   The `do { ... } while (0)' wrapper has no semantic effect, but it
   makes this macro syntactically equivalent to the expression
   statement it resembles.  Without it, statements like

     if (a)
       SVN_ERR (some operation);
     else
       foo;

   would not mean what they appear to.  */

#define SVN_ERR(expr)                           \
  do {                                          \
    svn_error_t *svn_err__temp = (expr);        \
    if (svn_err__temp)                          \
      return svn_err__temp;                     \
  } while (0)


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_ERROR_H */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
