/* svn_error_codes.h:  define error codes specific to Subversion.
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

/*
 * Process this file if we're building an error array, or if we have
 * not defined the enumerated constants yet.
 */
#if defined(SVN_ERROR_BUILD_ARRAY) || !defined(SVN_ERROR_ENUM_DEFINED)


#include <apr.h>
#include <apr_errno.h>     /* APR's error system */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#if defined(SVN_ERROR_BUILD_ARRAY)

#define SVN_ERROR_START \
	static const err_defn error_table[] = { \
	  { SVN_WARNING, "Warning" },
#define SVN_ERRDEF(n, s) { n, s },
#define SVN_ERROR_END { 0, NULL } };

#elif !defined(SVN_ERROR_ENUM_DEFINED)

#define SVN_ERROR_START \
	typedef enum svn_errno_t { \
	  SVN_WARNING = APR_OS_START_USEERR + 1,
#define SVN_ERRDEF(n, s) n,
#define SVN_ERROR_END SVN_ERR_LAST } svn_errno_t;

#define SVN_ERROR_ENUM_DEFINED

#endif

/* 
   Define custom Subversion error numbers, in the range reserved for
   that in APR: from APR_OS_START_USEERR to APR_OS_START_SYSERR (see
   apr_errno.h).

*/
SVN_ERROR_START

  SVN_ERRDEF(SVN_ERR_NOT_AUTHORIZED, "Not authorized")
  SVN_ERRDEF(SVN_ERR_PLUGIN_LOAD_FAILURE, "Failure loading plugin")
  SVN_ERRDEF(SVN_ERR_UNKNOWN_FS_ACTION, "Unknown fs action")
  SVN_ERRDEF(SVN_ERR_UNEXPECTED_EOF, "Unexpected end of file")
  SVN_ERRDEF(SVN_ERR_MALFORMED_FILE, "Malformed file")
  SVN_ERRDEF(SVN_ERR_INCOMPLETE_DATA, "Incomplete data")

  /* The xml delta we got was not well-formed. */
  SVN_ERRDEF(SVN_ERR_MALFORMED_XML, "XML data was not well-formed")

  /* A working copy "descent" crawl came up empty */
  SVN_ERRDEF(SVN_ERR_UNFRUITFUL_DESCENT, "WC descent came up empty")

  /* A bogus filename was passed to a routine */
  SVN_ERRDEF(SVN_ERR_BAD_FILENAME, "Bogus filename")

  /* Trying to use an as-yet unsupported feature. */
  SVN_ERRDEF(SVN_ERR_UNSUPPORTED_FEATURE, "Trying to use an unsupported feature")

  /* There's no such xml tag attribute */
  SVN_ERRDEF(SVN_ERR_XML_ATTRIB_NOT_FOUND, "No such XML tag attribute")

  /* A delta-pkg is missing ancestry. */
  SVN_ERRDEF(SVN_ERR_XML_MISSING_ANCESTRY, "<delta-pkg> is missing ancestry")

  /* A binary data encoding was specified which we don't know how to decode. */
  SVN_ERRDEF(SVN_ERR_XML_UNKNOWN_ENCODING, "Unrecognized binary data encoding; can't decode")

  /* Not one of the valid kinds in svn_node_kind. */
  SVN_ERRDEF(SVN_ERR_UNKNOWN_NODE_KIND, "Unknown svn_node_kind")

  /* Can't do this update or checkout, because something was in the way. */
  SVN_ERRDEF(SVN_ERR_WC_OBSTRUCTED_UPDATE, "Obstructed update; unversioned item in the way")

  /* A mismatch popping the wc unwind stack. */
  SVN_ERRDEF(SVN_ERR_WC_UNWIND_MISMATCH, "Mismatch popping the WC unwind stack")

  /* Trying to pop an empty unwind stack. */
  SVN_ERRDEF(SVN_ERR_WC_UNWIND_EMPTY, "Attempt to pop empty WC unwind stack")

  /* Trying to unlock when there's non-empty unwind stack. */
  SVN_ERRDEF(SVN_ERR_WC_UNWIND_NOT_EMPTY, "Attempt to unlock with non-empty unwind stack")

  /* What happens if a non-blocking call to svn_wc__lock() encounters
     another lock. */
  SVN_ERRDEF(SVN_ERR_WC_LOCKED, "Attempted to lock an already-locked dir")

  /* Something's wrong with the log file format. */
  SVN_ERRDEF(SVN_ERR_WC_BAD_ADM_LOG, "Logfile is corrupted")

  /* Unable to find a file or dir in the working copy. */
  SVN_ERRDEF(SVN_ERR_WC_PATH_NOT_FOUND, "Can't find a working copy path")

  /* Unable to find an entry.  Not always a fatal error, by the way. */
  SVN_ERRDEF(SVN_ERR_WC_ENTRY_NOT_FOUND, "Can't find an entry")

  /* Entry already exists when adding a file. */
  SVN_ERRDEF(SVN_ERR_WC_ENTRY_EXISTS, "Entry already exists")

  /* Unable to get revision for an entry. */
  SVN_ERRDEF(SVN_ERR_WC_ENTRY_MISSING_REVISION, "Entry has no revision")

  /* Unable to get ancestry for an entry. */
  SVN_ERRDEF(SVN_ERR_WC_ENTRY_MISSING_ANCESTRY, "Entry has no ancestor")

  /* Entry has some invalid attribute value. */
  SVN_ERRDEF(SVN_ERR_WC_ENTRY_ATTRIBUTE_INVALID, "Entry has an invalid attribute")

  /* Bogus attributes are trying to be merged into an entry */
  SVN_ERRDEF(SVN_ERR_WC_ENTRY_BOGUS_MERGE, "Bogus entry attributes during entry merge")

  /* Working copy is not up-to-date w.r.t. the repository. */
  SVN_ERRDEF(SVN_ERR_WC_NOT_UP_TO_DATE, "Working copy os not up-to-date")

  /* A recursive directory removal left locally modified files
     behind. */
  SVN_ERRDEF(SVN_ERR_WC_LEFT_LOCAL_MOD, "Left locally modified or unversioned files")

  /* No unique names available for tmp files. */
  SVN_ERRDEF(SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED, "Ran out of unique names")

  /* If a working copy conflict is found (say, during a commit) */
  SVN_ERRDEF(SVN_ERR_WC_FOUND_CONFLICT, "Found a conflict in working copy")

  /* The working copy is in an invalid state */
  SVN_ERRDEF(SVN_ERR_WC_CORRUPT, "Working copy is corrupt")

  /* A general filesystem error.  */
  SVN_ERRDEF(SVN_ERR_FS_GENERAL, "General filesystem error")

  /* An error occurred while trying to close a Subversion filesystem.
     This status code is meant to be returned from APR pool cleanup
     functions; since that interface doesn't allow us to provide more
     detailed information, this is all you'll get.  */
  SVN_ERRDEF(SVN_ERR_FS_CLEANUP, "Error closing filesystem")

  /* You called svn_fs_newfs or svn_fs_open, but the filesystem object
     you provided already refers to some filesystem.  You should allocate
     a fresh filesystem object with svn_fs_new, and use that instead.  */
  SVN_ERRDEF(SVN_ERR_FS_ALREADY_OPEN, "Filesystem is already open")

  /* You tried to perform an operation on a filesystem object which
     hasn't been opened on any actual database yet.  You need to call
     `svn_fs_open_berkeley', `svn_fs_create_berkeley', or something
     like that.  */
  SVN_ERRDEF(SVN_ERR_FS_NOT_OPEN, "Filesystem is not open")

  /* The filesystem has been corrupted.  The filesystem library found
     improperly formed data in the database.  */
  SVN_ERRDEF(SVN_ERR_FS_CORRUPT, "Filesystem is corrupt")

  /* The name given is not a valid directory entry name, or filename.  */
  SVN_ERRDEF(SVN_ERR_FS_PATH_SYNTAX, "Invalid filesystem path syntax")

  /* The filesystem has no revision by the given number.  */
  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_REVISION, "Invalid filesystem revision number")

  /* The filesystem has no transaction with the given name.  */
  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_TRANSACTION, "Invalid filesystem transaction name")

  /* A particular entry was not found in a directory. */
  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_ENTRY, "Filesystem directory has no such entry")

  /* A particular representation was not found. */
  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_REPRESENTATION, "Filesystem has no such representation")

  /* A particular string was not found. */
  SVN_ERRDEF(SVN_ERR_FS_NO_SUCH_STRING, "Filesystem has no such string")

  /* There is no file by the given name.  */
  SVN_ERRDEF(SVN_ERR_FS_NOT_FOUND, "Filesystem has no such file")

  /* The given node revision id does not exist.  */
  SVN_ERRDEF(SVN_ERR_FS_ID_NOT_FOUND, "Filesystem has no such node-rev-id")

  /* The given string does not represent a node or node revision id. */
  SVN_ERRDEF(SVN_ERR_FS_NOT_ID, "String does not represent a node or node-rev-id")

  /* The name given does not refer to a directory.  */
  SVN_ERRDEF(SVN_ERR_FS_NOT_DIRECTORY, "Name does not refer to a filesystem directory")

  /* The name given does not refer to a file.  */
  SVN_ERRDEF(SVN_ERR_FS_NOT_FILE, "Name does not refer to a filesystem file")

  /* The name given is not a single path component.  */
  SVN_ERRDEF(SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, "Name is not a single path component")

  /* The caller attempted to change or delete a node which is not mutable.  */
  SVN_ERRDEF(SVN_ERR_FS_NOT_MUTABLE, "Attempt to change immutable filesystem node")

  /* You tried to create a new file in a filesystem revision, but the
     file already exists. */
  SVN_ERRDEF(SVN_ERR_FS_ALREADY_EXISTS, "File already exists in revision")

  /* Tried to remove a non-empty directory. */
  SVN_ERRDEF(SVN_ERR_FS_DIR_NOT_EMPTY, "Attempt to remove non-empty filesystem directory")

  /* You tried to remove the root directory of a filesystem revision,
     or create another node named /.  */
  SVN_ERRDEF(SVN_ERR_FS_ROOT_DIR, "Attempt to remove or recreate fs root dir")

  /* The root object given is not a transaction root.  */
  SVN_ERRDEF(SVN_ERR_FS_NOT_TXN_ROOT, "Object is not a transaction root")

  /* The root object given is not a revision root.  */
  SVN_ERRDEF(SVN_ERR_FS_NOT_REVISION_ROOT, "Object is not a revision root")

  /* The transaction could not be committed, because of a conflict with
     a prior change.  */
  SVN_ERRDEF(SVN_ERR_FS_CONFLICT, "Merge conflict during commit")

  /* This TXN's base root is not the same as that of the youngest
     revision in the repository.  */
  SVN_ERRDEF(SVN_ERR_TXN_OUT_OF_DATE, "Transaction is out of date")

  /* The error is a Berkeley DB error.  `src_err' is the Berkeley DB
     error code, and `message' is an error message.  */
  SVN_ERRDEF(SVN_ERR_BERKELEY_DB, "Berkeley DB error")

  /* ### need to come up with a mechanism for RA-specific errors */

  /* a bad URL was passed to the repository access layer */
  SVN_ERRDEF(SVN_ERR_RA_ILLEGAL_URL, "Bad URL passed to RA layer")

  /* These RA errors are specific to ra_dav */

    /* the repository access layer could not initialize the socket layer */
    SVN_ERRDEF(SVN_ERR_RA_SOCK_INIT, "RA layer failed to init socket layer")

    /* the repository access layer could not lookup the hostname */
    SVN_ERRDEF(SVN_ERR_RA_HOSTNAME_LOOKUP, "RA layer failed hostname lookup")

    /* could not create an HTTP request */
    SVN_ERRDEF(SVN_ERR_RA_CREATING_REQUEST, "RA layer failed to create HTTP request")

    /* a request to the server failed in some way */
    SVN_ERRDEF(SVN_ERR_RA_REQUEST_FAILED, "RA layer's server request failed")

    /* error making an Activity for the commit to the server */
    SVN_ERRDEF(SVN_ERR_RA_MKACTIVITY_FAILED, "RA layer failed to make an activity for commit")

    /* could not delete a resource on the server */
    SVN_ERRDEF(SVN_ERR_RA_DELETE_FAILED, "RA layer failed to delete server resource")

  /* End of ra_dav errors */

  /* These RA errors are specific to ra_local */
  
    /* the given URL does not seem to point to a versioned resource */
    SVN_ERRDEF(SVN_ERR_RA_NOT_VERSIONED_RESOURCE, "URL is not a versioned resource")

    /* the update reporter was given a bogus first path. */
    SVN_ERRDEF(SVN_ERR_RA_BAD_REVISION_REPORT, "Bogus revision report")
 
  /* End of ra_local errors */

  /* an unsuitable container-pool was passed to svn_make_pool() */
  SVN_ERRDEF(SVN_ERR_BAD_CONTAINING_POOL, "Bad parent pool passed to svn_make_pool()")

  /* the server was misconfigured: a pathname to an SVN FS was not supplied */
  SVN_ERRDEF(SVN_ERR_APMOD_MISSING_PATH_TO_FS, "Apache has no path to an SVN filesystem")

  /* the specified URI refers to our namespace, but was malformed */
  SVN_ERRDEF(SVN_ERR_APMOD_MALFORMED_URI, "Apache got a malformed URI")

  /* A test in the test suite failed.  The test suite uses this
     error internally.  */
  SVN_ERRDEF(SVN_ERR_TEST_FAILED, "Test failed")

  /* BEGIN Client errors */

  /* Generic arg parsing error */
  SVN_ERRDEF(SVN_ERR_CL_ARG_PARSING_ERROR, "Client error in parsing arguments")

  /* An operation was attempted on the administrative subdirectory
     (i.e., user tried to import it, or update it, or...) */
  SVN_ERRDEF(SVN_ERR_CL_ADM_DIR_RESERVED, "Attempted command in administrative dir")

  /* END Client errors */
  

SVN_ERROR_END


#undef SVN_ERROR_START
#undef SVN_ERRDEF
#undef SVN_ERROR_END

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* defined(SVN_ERROR_BUILD_ARRAY) || !defined(SVN_ERROR_ENUM_DEFINED) */


/* ----------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
