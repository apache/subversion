/* svn_error_codes.h:  define error codes specific to Subversion.
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

/* What's going on here?
 
   In order to define error codes and their associated description
   strings in the same place, we overload the SVN_ERRDEF() macro with
   two definitions below.  Both take two arguments, an error code name
   and a description string.  One definition of the macro just throws
   away the string and defines enumeration constants using the error
   code names -- that definition is used by the header file that
   exports error codes to the rest of Subversion.  The other
   definition creates a static table mapping the enum codes to their
   corresponding strings -- that definition is used by the C file that
   implements svn_strerror().
 
   The header and C files both include this file, using #defines to
   control which version of the macro they get.  
*/


/* Process this file if we're building an error array, or if we have
   not defined the enumerated constants yet.  */
#if defined(SVN_ERROR_BUILD_ARRAY) || !defined(SVN_ERROR_ENUM_DEFINED)


#include <apr.h>
#include <apr_errno.h>     /* APR's error system */

#include "svn_props.h"     /* For SVN_PROP_EXTERNALS. */

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

/* Define custom Subversion error numbers, in the range reserved for
   that in APR: from APR_OS_START_USEERR to APR_OS_START_SYSERR (see
   apr_errno.h).  */
SVN_ERROR_START

  SVN_ERRDEF (SVN_ERR_GENERAL, 
              "A general problem occured") /* This should ONLY be used
                                              to add information to existing
                                              error chains!! */

  SVN_ERRDEF (SVN_ERR_PLUGIN_LOAD_FAILURE,
              "Failure loading plugin")

  SVN_ERRDEF (SVN_ERR_UNKNOWN_FS_ACTION,
              "Unknown fs action")

  SVN_ERRDEF (SVN_ERR_UNEXPECTED_EOF,
              "Unexpected end of file")

  SVN_ERRDEF (SVN_ERR_MALFORMED_FILE,
              "Malformed file")

  SVN_ERRDEF (SVN_ERR_MALFORMED_STREAM_DATA,
              "Malformed stream data")

  SVN_ERRDEF (SVN_ERR_UNRECOGNIZED_STREAM_DATA,
              "Unrecognized stream data")

  SVN_ERRDEF (SVN_ERR_INCOMPLETE_DATA,
              "Incomplete data")

  SVN_ERRDEF (SVN_ERR_INCORRECT_PARAMS,
              "Incorrect parameters given")

  SVN_ERRDEF (SVN_ERR_MALFORMED_XML,
              "XML data was not well-formed")

  SVN_ERRDEF (SVN_ERR_UNVERSIONED_RESOURCE,
              "Tried a versioning operation on an unversioned resource")

  SVN_ERRDEF (SVN_ERR_UNFRUITFUL_DESCENT,
              "WC descent came up empty")

  SVN_ERRDEF (SVN_ERR_BAD_CONTAINING_POOL,
              "Bad parent pool passed to svn_make_pool()")

  SVN_ERRDEF (SVN_ERR_TEST_FAILED,
              "Test failed")
       
  SVN_ERRDEF (SVN_ERR_BAD_FILENAME,
              "Bogus filename")

  SVN_ERRDEF (SVN_ERR_BAD_URL,
              "Bogus URL")

  SVN_ERRDEF (SVN_ERR_BAD_DATE,
              "Bogus date")

  SVN_ERRDEF (SVN_ERR_BAD_MIME_TYPE,
              "Bogus mime-type")

  SVN_ERRDEF (SVN_ERR_BAD_REVISION,
              "Bogus revision number")

  SVN_ERRDEF (SVN_ERR_UNSUPPORTED_FEATURE,
              "Trying to use an unsupported feature")

  SVN_ERRDEF (SVN_ERR_MISSING_ENV_VARIABLE,
              "Missing environment variable")

  SVN_ERRDEF (SVN_ERR_UNKNOWN_NODE_KIND,
              "Unknown svn_node_kind")

  SVN_ERRDEF (SVN_ERR_UNKNOWN_PROP_KIND,
              "Unknown svn_prop_kind")

  SVN_ERRDEF (SVN_ERR_ILLEGAL_TARGET,
              "Illegal target for the requested operation")

  SVN_ERRDEF (SVN_ERR_DELTA_MD5_CHECKSUM_ABSENT,
              "MD5 checksum is missing")

  SVN_ERRDEF (SVN_ERR_DIR_NOT_EMPTY,
              "Directory needs to be empty but is not")

  SVN_ERRDEF (SVN_ERR_XML_ATTRIB_NOT_FOUND,
              "No such XML tag attribute")

  SVN_ERRDEF (SVN_ERR_XML_MISSING_ANCESTRY,
              "<delta-pkg> is missing ancestry")

  SVN_ERRDEF (SVN_ERR_XML_UNKNOWN_ENCODING,
              "Unrecognized binary data encoding; can't decode")

  SVN_ERRDEF (SVN_ERR_IO_INCONSISTENT_EOL,
              "Inconsistent line ending style")

  SVN_ERRDEF (SVN_ERR_IO_UNKNOWN_EOL,
              "Unrecognized line ending style")

  SVN_ERRDEF (SVN_ERR_IO_CORRUPT_EOL,
              "Line endings other than expected")

  SVN_ERRDEF (SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,
              "Ran out of unique names")

  SVN_ERRDEF (SVN_ERR_ENTRY_NOT_FOUND,
              "Can't find an entry")

  SVN_ERRDEF (SVN_ERR_ENTRY_EXISTS,
              "Entry already exists")

  SVN_ERRDEF (SVN_ERR_ENTRY_MISSING_REVISION,
              "Entry has no revision")

  SVN_ERRDEF (SVN_ERR_ENTRY_MISSING_URL,
              "Entry has no url")

  SVN_ERRDEF (SVN_ERR_ENTRY_ATTRIBUTE_INVALID,
              "Entry has an invalid attribute")

  SVN_ERRDEF (SVN_ERR_WC_OBSTRUCTED_UPDATE,
              "Obstructed update")

  SVN_ERRDEF (SVN_ERR_WC_UNWIND_MISMATCH,
              "Mismatch popping the WC unwind stack")

  SVN_ERRDEF (SVN_ERR_WC_UNWIND_EMPTY,
              "Attempt to pop empty WC unwind stack")

  SVN_ERRDEF (SVN_ERR_WC_UNWIND_NOT_EMPTY,
              "Attempt to unlock with non-empty unwind stack")

  SVN_ERRDEF (SVN_ERR_WC_LOCKED,
              "Attempted to lock an already-locked dir")

  SVN_ERRDEF (SVN_ERR_WC_NOT_LOCKED,
              "Working copy not locked")

  SVN_ERRDEF (SVN_ERR_WC_INVALID_LOCK,
              "Invalid lock")

  SVN_ERRDEF (SVN_ERR_WC_NOT_DIRECTORY,
              "Path is not a working copy directory")

  SVN_ERRDEF (SVN_ERR_WC_NOT_FILE,
              "Path is not a working copy file")

  SVN_ERRDEF (SVN_ERR_WC_BAD_ADM_LOG,
              "Problem running log")

  SVN_ERRDEF (SVN_ERR_WC_PATH_NOT_FOUND,
              "Can't find a working copy path")

  SVN_ERRDEF (SVN_ERR_WC_UNEXPECTED_KIND,
              "Unexpected node kind found")

  SVN_ERRDEF (SVN_ERR_WC_NOT_UP_TO_DATE,
              "Working copy is not up-to-date")

  SVN_ERRDEF (SVN_ERR_WC_LEFT_LOCAL_MOD,
              "Left locally modified or unversioned files")

  SVN_ERRDEF (SVN_ERR_WC_SCHEDULE_CONFLICT,
              "Unmergeable scheduling requested on an entry")

  SVN_ERRDEF (SVN_ERR_WC_CONFLICT,
              "A conflict occurred in the working copy")

  SVN_ERRDEF (SVN_ERR_WC_FOUND_CONFLICT,
              "A conflict in working copy obstructs the current operation")

  SVN_ERRDEF (SVN_ERR_WC_CORRUPT,
              "Working copy is corrupt")

  SVN_ERRDEF (SVN_ERR_WC_CORRUPT_TEXT_BASE,
              "Working copy text base is corrupt")

  SVN_ERRDEF (SVN_ERR_FS_GENERAL,
              "General filesystem error")

  SVN_ERRDEF (SVN_ERR_FS_CLEANUP,
              "Error closing filesystem")

  SVN_ERRDEF (SVN_ERR_FS_ALREADY_OPEN,
              "Filesystem is already open")

  SVN_ERRDEF (SVN_ERR_FS_NOT_OPEN,
              "Filesystem is not open")

  SVN_ERRDEF (SVN_ERR_FS_CORRUPT,
              "Filesystem is corrupt")

  SVN_ERRDEF (SVN_ERR_FS_PATH_SYNTAX,
              "Invalid filesystem path syntax")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_REVISION,
              "Invalid filesystem revision number")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_TRANSACTION,
              "Invalid filesystem transaction name")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_ENTRY,
              "Filesystem directory has no such entry")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_REPRESENTATION,
              "Filesystem has no such representation")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_STRING,
              "Filesystem has no such string")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_COPY,
              "Filesystem has no such copy")

  SVN_ERRDEF (SVN_ERR_FS_TRANSACTION_NOT_MUTABLE,
              "The specified transaction is not mutable")

  SVN_ERRDEF (SVN_ERR_FS_NOT_FOUND,
              "Filesystem has no item")

  SVN_ERRDEF (SVN_ERR_FS_ID_NOT_FOUND,
              "Filesystem has no such node-rev-id")

  SVN_ERRDEF (SVN_ERR_FS_NOT_ID,
              "String does not represent a node or node-rev-id")

  SVN_ERRDEF (SVN_ERR_FS_NOT_DIRECTORY,
              "Name does not refer to a filesystem directory")

  SVN_ERRDEF (SVN_ERR_FS_NOT_FILE,
              "Name does not refer to a filesystem file")

  SVN_ERRDEF (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT,
              "Name is not a single path component")

  SVN_ERRDEF (SVN_ERR_FS_NOT_MUTABLE,
              "Attempt to change immutable filesystem node")

  SVN_ERRDEF (SVN_ERR_FS_ALREADY_EXISTS,
              "Item already exists in filesystem")

  SVN_ERRDEF (SVN_ERR_FS_ROOT_DIR,
              "Attempt to remove or recreate fs root dir")

  SVN_ERRDEF (SVN_ERR_FS_NOT_TXN_ROOT,
              "Object is not a transaction root")

  SVN_ERRDEF (SVN_ERR_FS_NOT_REVISION_ROOT,
              "Object is not a revision root")

  SVN_ERRDEF (SVN_ERR_FS_CONFLICT,
              "Merge conflict during commit")

  SVN_ERRDEF (SVN_ERR_FS_REP_CHANGED,
              "A representation vanished or changed between reads.")

  SVN_ERRDEF (SVN_ERR_FS_REP_NOT_MUTABLE,
              "Tried to change an immutable representation.")

  SVN_ERRDEF (SVN_ERR_FS_MALFORMED_SKEL,
              "Malformed skeleton data")

  SVN_ERRDEF (SVN_ERR_TXN_OUT_OF_DATE,
              "Transaction is out of date")

  SVN_ERRDEF (SVN_ERR_REPOS_LOCKED,
              "The repository is locked, perhaps for db recovery.")

  SVN_ERRDEF (SVN_ERR_REPOS_HOOK_FAILURE,
              "A repository hook failed.")

  SVN_ERRDEF (SVN_ERR_REPOS_BAD_ARGS,
              "Incorrect arguments supplied.")

  SVN_ERRDEF (SVN_ERR_REPOS_NO_DATA_FOR_REPORT,
              "A report cannot be generated because no data was supplied.")

  SVN_ERRDEF (SVN_ERR_EXTERNAL_PROGRAM,
              "Error calling external program")

  SVN_ERRDEF (SVN_ERR_BERKELEY_DB,
              "Berkeley DB error")

  /* ### need to come up with a mechanism for RA-specific errors */

  SVN_ERRDEF (SVN_ERR_RA_ILLEGAL_URL,
              "Bad URL passed to RA layer")

  SVN_ERRDEF (SVN_ERR_RA_NOT_AUTHORIZED,
              "Authorization failed")

  SVN_ERRDEF (SVN_ERR_RA_UNKNOWN_AUTH,
              "Unknown authorization method")

  SVN_ERRDEF (SVN_ERR_RA_NOT_IMPLEMENTED,
              "Repository access method not implemented")
       
  /* These RA errors are specific to ra_dav */

    SVN_ERRDEF (SVN_ERR_RA_SOCK_INIT,
                "RA layer failed to init socket layer")

    SVN_ERRDEF (SVN_ERR_RA_HOSTNAME_LOOKUP,
                "RA layer failed hostname lookup")

    SVN_ERRDEF (SVN_ERR_RA_CREATING_REQUEST,
                "RA layer failed to create HTTP request")

    SVN_ERRDEF (SVN_ERR_RA_REQUEST_FAILED,
                "RA layer request failed")

    SVN_ERRDEF (SVN_ERR_RA_OPTIONS_REQUEST_FAILED,
                "RA layer didn't receive requested OPTIONS info")
    
    SVN_ERRDEF (SVN_ERR_RA_PROPS_NOT_FOUND,
                "RA layer failed to fetch properties")

   SVN_ERRDEF (SVN_ERR_RA_ALREADY_EXISTS,
               "RA layer file already exists")

  /* End of ra_dav errors */

  /* These RA errors are specific to ra_local */
  
    SVN_ERRDEF (SVN_ERR_RA_REPOSITORY_NOT_FOUND,
                "Couldn't find a repository.")

    SVN_ERRDEF (SVN_ERR_RA_BAD_REVISION_REPORT,
                "Bogus revision report")
 
  /* End of ra_local errors */

  /* BEGIN svndiff errors */

  SVN_ERRDEF (SVN_ERR_SVNDIFF_INVALID_HEADER,
              "Svndiff data has invalid header")

  SVN_ERRDEF (SVN_ERR_SVNDIFF_CORRUPT_WINDOW,
              "Svndiff data contains corrupt window")

  SVN_ERRDEF (SVN_ERR_SVNDIFF_BACKWARD_VIEW,
              "Svndiff data contains backward-sliding source view")

  SVN_ERRDEF (SVN_ERR_SVNDIFF_INVALID_OPS,
              "Svndiff data contains invalid instruction")

  SVN_ERRDEF (SVN_ERR_SVNDIFF_UNEXPECTED_END,
              "Svndiff data ends unexpectedly")

  /* END svndiff errors */

  /* BEGIN mod_dav_svn errors */

  SVN_ERRDEF (SVN_ERR_APMOD_MISSING_PATH_TO_FS,
              "Apache has no path to an SVN filesystem")

  SVN_ERRDEF (SVN_ERR_APMOD_MALFORMED_URI,
              "Apache got a malformed URI")

  SVN_ERRDEF (SVN_ERR_APMOD_ACTIVITY_NOT_FOUND,
              "Activity not found")

  SVN_ERRDEF (SVN_ERR_APMOD_BAD_BASELINE,
              "Baseline incorrect")

 /* END mod_dav_svn errors*/

  /* BEGIN libsvn_client errors */

  SVN_ERRDEF (SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED,
              "A path under revision control is needed for this operation")

  SVN_ERRDEF (SVN_ERR_CLIENT_RA_ACCESS_REQUIRED,
              "Repository access is needed for this operation")

  SVN_ERRDEF (SVN_ERR_CLIENT_BAD_REVISION,
              "Bogus revision information given")

  SVN_ERRDEF (SVN_ERR_CLIENT_DUPLICATE_COMMIT_URL,
              "Attempting to commit to a URL more than once")

  SVN_ERRDEF (SVN_ERR_CLIENT_UNVERSIONED,
              "Attempting restricted operation for unversioned resource")

  SVN_ERRDEF (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION,
              "Format of an " SVN_PROP_EXTERNALS " property was invalid.")

  SVN_ERRDEF (SVN_ERR_CLIENT_MODIFIED,
              "Attempting restricted operation for modified resource")

  /* END libsvn_client errors */


  /* BEGIN Client errors */

  SVN_ERRDEF (SVN_ERR_CL_ARG_PARSING_ERROR,
              "Client error in parsing arguments")

  SVN_ERRDEF (SVN_ERR_CL_INSUFFICIENT_ARGS,
              "Not enough args provided")

  SVN_ERRDEF (SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS,
              "Mutually exclusive arguments specified.")                   

  SVN_ERRDEF (SVN_ERR_CL_ADM_DIR_RESERVED,
              "Attempted command in administrative dir")

  SVN_ERRDEF (SVN_ERR_CL_LOG_MESSAGE_IS_VERSIONED_FILE,
              "The log message file is under version control")

  SVN_ERRDEF (SVN_ERR_CL_LOG_MESSAGE_IS_PATHNAME,
              "The log message is a pathname")

  SVN_ERRDEF (SVN_ERR_CL_COMMIT_IN_ADDED_DIR,
              "Commiting in directory scheduled for addition")

  SVN_ERRDEF (SVN_ERR_CL_NO_EXTERNAL_EDITOR,
              "No external editor available")

  /* END Client errors */
        
  /* BEGIN Generic UI related errors */
  SVN_ERRDEF (SVN_ERR_CANCELED,
              "Something, probably the user, canceled the operation")
  /* END Generic UI related errors */
  

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
 * eval: (load-file "../../tools/dev/svn-dev.el")
 * end:
 */
