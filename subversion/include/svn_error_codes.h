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
#define SVN_ERRDEF(num, offset, str) { num, str },
#define SVN_ERROR_END { 0, NULL } };

#elif !defined(SVN_ERROR_ENUM_DEFINED)

#define SVN_ERROR_START \
	typedef enum svn_errno_t { \
	  SVN_WARNING = APR_OS_START_USERERR + 1,
#define SVN_ERRDEF(num, offset, str) num = offset,
#define SVN_ERROR_END SVN_ERR_LAST } svn_errno_t;

#define SVN_ERROR_ENUM_DEFINED

#endif

/* Define custom Subversion error numbers, in the range reserved for
   that in APR: from APR_OS_START_USERERR to APR_OS_START_SYSERR (see
   apr_errno.h).

   Error numbers are divided into categories of up to 5000 errors
   each.  Since we're dividing up the APR user error space, which has
   room for 500,000 errors, we can have up to 100 categories.
   Categories are fixed-size; if a category has fewer than 5000
   errors, then it just ends with a range of unused numbers.

   To maintain binary compatibility, please observe these guidelines:

      - When adding a new error, always add on the end of the
        appropriate category, so that the real values of existing
        errors are not changed.

      - When deleting an error, leave a placeholder comment indicating
        the offset, again so that the values of other errors are not
        perturbed.
*/

#define SVN_ERR_CATEGORY_SIZE 5000

/* Leave one category of room at the beginning, for SVN_WARNING and
   any other such beasts we might create in the future. */
#define SVN_ERR_BAD_CATEGORY_START      (APR_OS_START_USERERR \
                                          + ( 1 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_XML_CATEGORY_START      (APR_OS_START_USERERR \
                                          + ( 2 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_IO_CATEGORY_START       (APR_OS_START_USERERR \
                                          + ( 3 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_STREAM_CATEGORY_START   (APR_OS_START_USERERR \
                                          + ( 4 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_NODE_CATEGORY_START     (APR_OS_START_USERERR \
                                          + ( 5 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_ENTRY_CATEGORY_START    (APR_OS_START_USERERR \
                                          + ( 6 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_WC_CATEGORY_START       (APR_OS_START_USERERR \
                                          + ( 7 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_FS_CATEGORY_START       (APR_OS_START_USERERR \
                                          + ( 8 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_REPOS_CATEGORY_START    (APR_OS_START_USERERR \
                                          + ( 9 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_RA_CATEGORY_START       (APR_OS_START_USERERR \
                                          + (10 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_RA_DAV_CATEGORY_START   (APR_OS_START_USERERR \
                                          + (11 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_RA_LOCAL_CATEGORY_START (APR_OS_START_USERERR \
                                          + (12 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_SVNDIFF_CATEGORY_START  (APR_OS_START_USERERR \
                                          + (13 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_APMOD_CATEGORY_START    (APR_OS_START_USERERR \
                                          + (14 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_CLIENT_CATEGORY_START   (APR_OS_START_USERERR \
                                          + (15 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_MISC_CATEGORY_START     (APR_OS_START_USERERR \
                                           + (16 * SVN_ERR_CATEGORY_SIZE))
#define SVN_ERR_CL_CATEGORY_START       (APR_OS_START_USERERR \
                                           + (17 * SVN_ERR_CATEGORY_SIZE))

SVN_ERROR_START

  /* validation ("BAD_FOO") errors */

  SVN_ERRDEF (SVN_ERR_BAD_CONTAINING_POOL,
              SVN_ERR_BAD_CATEGORY_START + 0,
              "Bad parent pool passed to svn_make_pool()")

  SVN_ERRDEF (SVN_ERR_BAD_FILENAME,
              SVN_ERR_BAD_CATEGORY_START + 1,
              "Bogus filename")

  SVN_ERRDEF (SVN_ERR_BAD_URL,
              SVN_ERR_BAD_CATEGORY_START + 2,
              "Bogus URL")

  SVN_ERRDEF (SVN_ERR_BAD_DATE,
              SVN_ERR_BAD_CATEGORY_START + 3,
              "Bogus date")

  SVN_ERRDEF (SVN_ERR_BAD_MIME_TYPE,
              SVN_ERR_BAD_CATEGORY_START + 4,
              "Bogus mime-type")

  /* UNUSED error slot:                  + 5 */

  SVN_ERRDEF (SVN_ERR_BAD_VERSION_FILE_FORMAT,
              SVN_ERR_BAD_CATEGORY_START + 6,
              "Version file format not correct")

  /* xml errors */

  SVN_ERRDEF (SVN_ERR_XML_ATTRIB_NOT_FOUND,
              SVN_ERR_XML_CATEGORY_START + 0,
              "No such XML tag attribute")

  SVN_ERRDEF (SVN_ERR_XML_MISSING_ANCESTRY,
              SVN_ERR_XML_CATEGORY_START + 1,
              "<delta-pkg> is missing ancestry")

  SVN_ERRDEF (SVN_ERR_XML_UNKNOWN_ENCODING,
              SVN_ERR_XML_CATEGORY_START + 2,
              "Unrecognized binary data encoding; can't decode")

  SVN_ERRDEF (SVN_ERR_XML_MALFORMED,
              SVN_ERR_XML_CATEGORY_START + 3,
              "XML data was not well-formed")

  /* io errors */

  SVN_ERRDEF (SVN_ERR_IO_INCONSISTENT_EOL,
              SVN_ERR_IO_CATEGORY_START + 0,
              "Inconsistent line ending style")

  SVN_ERRDEF (SVN_ERR_IO_UNKNOWN_EOL,
              SVN_ERR_IO_CATEGORY_START + 1,
              "Unrecognized line ending style")

  SVN_ERRDEF (SVN_ERR_IO_CORRUPT_EOL,
              SVN_ERR_IO_CATEGORY_START + 2,
              "Line endings other than expected")

  SVN_ERRDEF (SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED,
              SVN_ERR_IO_CATEGORY_START + 3,
              "Ran out of unique names")

  SVN_ERRDEF (SVN_ERR_IO_PIPE_FRAME_ERROR,
              SVN_ERR_IO_CATEGORY_START + 4,
              "Framing error in pipe protocol")

  SVN_ERRDEF (SVN_ERR_IO_PIPE_READ_ERROR,
              SVN_ERR_IO_CATEGORY_START + 5,
              "Read error in pipe")

  /* stream errors */

  SVN_ERRDEF (SVN_ERR_STREAM_UNEXPECTED_EOF,
              SVN_ERR_STREAM_CATEGORY_START + 0,
              "Unexpected EOF on stream")

  SVN_ERRDEF (SVN_ERR_STREAM_MALFORMED_DATA,
              SVN_ERR_STREAM_CATEGORY_START + 1,
              "Malformed stream data")

  SVN_ERRDEF (SVN_ERR_STREAM_UNRECOGNIZED_DATA,
              SVN_ERR_STREAM_CATEGORY_START + 2,
              "Unrecognized stream data")

  /* node errors */

  SVN_ERRDEF (SVN_ERR_NODE_UNKNOWN_KIND,
              SVN_ERR_NODE_CATEGORY_START + 0,
              "Unknown svn_node_kind")

  SVN_ERRDEF (SVN_ERR_NODE_UNEXPECTED_KIND,
              SVN_ERR_NODE_CATEGORY_START + 1,
              "Unexpected node kind found")

  /* entry errors */

  SVN_ERRDEF (SVN_ERR_ENTRY_NOT_FOUND,
              SVN_ERR_ENTRY_CATEGORY_START + 0,
              "Can't find an entry")

  /* UNUSED error slot:                    + 1 */

  SVN_ERRDEF (SVN_ERR_ENTRY_EXISTS,
              SVN_ERR_ENTRY_CATEGORY_START + 2,
              "Entry already exists")

  SVN_ERRDEF (SVN_ERR_ENTRY_MISSING_REVISION,
              SVN_ERR_ENTRY_CATEGORY_START + 3,
              "Entry has no revision")

  SVN_ERRDEF (SVN_ERR_ENTRY_MISSING_URL,
              SVN_ERR_ENTRY_CATEGORY_START + 4,
              "Entry has no url")

  SVN_ERRDEF (SVN_ERR_ENTRY_ATTRIBUTE_INVALID,
              SVN_ERR_ENTRY_CATEGORY_START + 5,
              "Entry has an invalid attribute")

  /* wc errors */

  SVN_ERRDEF (SVN_ERR_WC_OBSTRUCTED_UPDATE,
              SVN_ERR_WC_CATEGORY_START + 0,
              "Obstructed update")

  SVN_ERRDEF (SVN_ERR_WC_UNWIND_MISMATCH,
              SVN_ERR_WC_CATEGORY_START + 1,
              "Mismatch popping the WC unwind stack")

  SVN_ERRDEF (SVN_ERR_WC_UNWIND_EMPTY,
              SVN_ERR_WC_CATEGORY_START + 2,
              "Attempt to pop empty WC unwind stack")

  SVN_ERRDEF (SVN_ERR_WC_UNWIND_NOT_EMPTY,
              SVN_ERR_WC_CATEGORY_START + 3,
              "Attempt to unlock with non-empty unwind stack")

  SVN_ERRDEF (SVN_ERR_WC_LOCKED,
              SVN_ERR_WC_CATEGORY_START + 4,
              "Attempted to lock an already-locked dir")

  SVN_ERRDEF (SVN_ERR_WC_NOT_LOCKED,
              SVN_ERR_WC_CATEGORY_START + 5,
              "Working copy not locked")

  SVN_ERRDEF (SVN_ERR_WC_INVALID_LOCK,
              SVN_ERR_WC_CATEGORY_START + 6,
              "Invalid lock")

  SVN_ERRDEF (SVN_ERR_WC_NOT_DIRECTORY,
              SVN_ERR_WC_CATEGORY_START + 7,
              "Path is not a working copy directory")

  SVN_ERRDEF (SVN_ERR_WC_NOT_FILE,
              SVN_ERR_WC_CATEGORY_START + 8,
              "Path is not a working copy file")

  SVN_ERRDEF (SVN_ERR_WC_BAD_ADM_LOG,
              SVN_ERR_WC_CATEGORY_START + 9,
              "Problem running log")

  SVN_ERRDEF (SVN_ERR_WC_PATH_NOT_FOUND,
              SVN_ERR_WC_CATEGORY_START + 10,
              "Can't find a working copy path")

  SVN_ERRDEF (SVN_ERR_WC_NOT_UP_TO_DATE,
              SVN_ERR_WC_CATEGORY_START + 11,
              "Working copy is not up-to-date")

  SVN_ERRDEF (SVN_ERR_WC_LEFT_LOCAL_MOD,
              SVN_ERR_WC_CATEGORY_START + 12,
              "Left locally modified or unversioned files")

  SVN_ERRDEF (SVN_ERR_WC_SCHEDULE_CONFLICT,
              SVN_ERR_WC_CATEGORY_START + 13,
              "Unmergeable scheduling requested on an entry")

  /* UNUSED error slot:                 + 14 */

  SVN_ERRDEF (SVN_ERR_WC_FOUND_CONFLICT,
              SVN_ERR_WC_CATEGORY_START + 15,
              "A conflict in working copy obstructs the current operation")

  SVN_ERRDEF (SVN_ERR_WC_CORRUPT,
              SVN_ERR_WC_CATEGORY_START + 16,
              "Working copy is corrupt")

  SVN_ERRDEF (SVN_ERR_WC_CORRUPT_TEXT_BASE,
              SVN_ERR_WC_CATEGORY_START + 17,
              "Working copy text base is corrupt")

  SVN_ERRDEF (SVN_ERR_WC_NODE_KIND_CHANGE,
              SVN_ERR_WC_CATEGORY_START + 18,
              "Cannot change node kind")

  /* fs errors */

  SVN_ERRDEF (SVN_ERR_FS_GENERAL,
              SVN_ERR_FS_CATEGORY_START + 0,
              "General filesystem error")

  SVN_ERRDEF (SVN_ERR_FS_CLEANUP,
              SVN_ERR_FS_CATEGORY_START + 1,
              "Error closing filesystem")

  SVN_ERRDEF (SVN_ERR_FS_ALREADY_OPEN,
              SVN_ERR_FS_CATEGORY_START + 2,
              "Filesystem is already open")

  SVN_ERRDEF (SVN_ERR_FS_NOT_OPEN,
              SVN_ERR_FS_CATEGORY_START + 3,
              "Filesystem is not open")

  SVN_ERRDEF (SVN_ERR_FS_CORRUPT,
              SVN_ERR_FS_CATEGORY_START + 4,
              "Filesystem is corrupt")

  SVN_ERRDEF (SVN_ERR_FS_PATH_SYNTAX,
              SVN_ERR_FS_CATEGORY_START + 5,
              "Invalid filesystem path syntax")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_REVISION,
              SVN_ERR_FS_CATEGORY_START + 6,
              "Invalid filesystem revision number")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_TRANSACTION,
              SVN_ERR_FS_CATEGORY_START + 7,
              "Invalid filesystem transaction name")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_ENTRY,
              SVN_ERR_FS_CATEGORY_START + 8,
              "Filesystem directory has no such entry")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_REPRESENTATION,
              SVN_ERR_FS_CATEGORY_START + 9,
              "Filesystem has no such representation")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_STRING,
              SVN_ERR_FS_CATEGORY_START + 10,
              "Filesystem has no such string")

  SVN_ERRDEF (SVN_ERR_FS_NO_SUCH_COPY,
              SVN_ERR_FS_CATEGORY_START + 11,
              "Filesystem has no such copy")

  SVN_ERRDEF (SVN_ERR_FS_TRANSACTION_NOT_MUTABLE,
              SVN_ERR_FS_CATEGORY_START + 12,
              "The specified transaction is not mutable")

  SVN_ERRDEF (SVN_ERR_FS_NOT_FOUND,
              SVN_ERR_FS_CATEGORY_START + 13,
              "Filesystem has no item")

  SVN_ERRDEF (SVN_ERR_FS_ID_NOT_FOUND,
              SVN_ERR_FS_CATEGORY_START + 14,
              "Filesystem has no such node-rev-id")

  SVN_ERRDEF (SVN_ERR_FS_NOT_ID,
              SVN_ERR_FS_CATEGORY_START + 15,
              "String does not represent a node or node-rev-id")

  SVN_ERRDEF (SVN_ERR_FS_NOT_DIRECTORY,
              SVN_ERR_FS_CATEGORY_START + 16,
              "Name does not refer to a filesystem directory")

  SVN_ERRDEF (SVN_ERR_FS_NOT_FILE,
              SVN_ERR_FS_CATEGORY_START + 17,
              "Name does not refer to a filesystem file")

  SVN_ERRDEF (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT,
              SVN_ERR_FS_CATEGORY_START + 18,
              "Name is not a single path component")

  SVN_ERRDEF (SVN_ERR_FS_NOT_MUTABLE,
              SVN_ERR_FS_CATEGORY_START + 19,
              "Attempt to change immutable filesystem node")

  SVN_ERRDEF (SVN_ERR_FS_ALREADY_EXISTS,
              SVN_ERR_FS_CATEGORY_START + 20,
              "Item already exists in filesystem")

  SVN_ERRDEF (SVN_ERR_FS_ROOT_DIR,
              SVN_ERR_FS_CATEGORY_START + 21,
              "Attempt to remove or recreate fs root dir")

  SVN_ERRDEF (SVN_ERR_FS_NOT_TXN_ROOT,
              SVN_ERR_FS_CATEGORY_START + 22,
              "Object is not a transaction root")

  SVN_ERRDEF (SVN_ERR_FS_NOT_REVISION_ROOT,
              SVN_ERR_FS_CATEGORY_START + 23,
              "Object is not a revision root")

  SVN_ERRDEF (SVN_ERR_FS_CONFLICT,
              SVN_ERR_FS_CATEGORY_START + 24,
              "Merge conflict during commit")

  SVN_ERRDEF (SVN_ERR_FS_REP_CHANGED,
              SVN_ERR_FS_CATEGORY_START + 25,
              "A representation vanished or changed between reads.")

  SVN_ERRDEF (SVN_ERR_FS_REP_NOT_MUTABLE,
              SVN_ERR_FS_CATEGORY_START + 26,
              "Tried to change an immutable representation.")

  SVN_ERRDEF (SVN_ERR_FS_MALFORMED_SKEL,
              SVN_ERR_FS_CATEGORY_START + 27,
              "Malformed skeleton data")

  SVN_ERRDEF (SVN_ERR_FS_TXN_OUT_OF_DATE,
              SVN_ERR_FS_CATEGORY_START + 28,
              "Transaction is out of date")

  SVN_ERRDEF (SVN_ERR_FS_BERKELEY_DB,
              SVN_ERR_FS_CATEGORY_START + 29,
              "Berkeley DB error")

  /* repos errors */

  SVN_ERRDEF (SVN_ERR_REPOS_LOCKED,
              SVN_ERR_REPOS_CATEGORY_START + 0,
              "The repository is locked, perhaps for db recovery.")

  SVN_ERRDEF (SVN_ERR_REPOS_HOOK_FAILURE,
              SVN_ERR_REPOS_CATEGORY_START + 1,
              "A repository hook failed.")

  SVN_ERRDEF (SVN_ERR_REPOS_BAD_ARGS,
              SVN_ERR_REPOS_CATEGORY_START + 2,
              "Incorrect arguments supplied.")

  SVN_ERRDEF (SVN_ERR_REPOS_NO_DATA_FOR_REPORT,
              SVN_ERR_REPOS_CATEGORY_START + 3,
              "A report cannot be generated because no data was supplied.")

  SVN_ERRDEF (SVN_ERR_REPOS_BAD_REVISION_REPORT,
              SVN_ERR_REPOS_CATEGORY_START + 4,
              "Bogus revision report")
 
  SVN_ERRDEF (SVN_ERR_REPOS_UNSUPPORTED_VERSION,
              SVN_ERR_REPOS_CATEGORY_START + 5,
              "Unsupported repository version")

  SVN_ERRDEF (SVN_ERR_REPOS_DISABLED_FEATURE,
              SVN_ERR_REPOS_CATEGORY_START + 6,
              "Disabled repository feature")

  /* generic ra errors */

  SVN_ERRDEF (SVN_ERR_RA_ILLEGAL_URL,
              SVN_ERR_RA_CATEGORY_START + 0,
              "Bad URL passed to RA layer")

  SVN_ERRDEF (SVN_ERR_RA_NOT_AUTHORIZED,
              SVN_ERR_RA_CATEGORY_START + 1,
              "Authorization failed")

  SVN_ERRDEF (SVN_ERR_RA_UNKNOWN_AUTH,
              SVN_ERR_RA_CATEGORY_START + 2,
              "Unknown authorization method")

  SVN_ERRDEF (SVN_ERR_RA_NOT_IMPLEMENTED,
              SVN_ERR_RA_CATEGORY_START + 3,
              "Repository access method not implemented")
       
  /* ra_dav errors */

  SVN_ERRDEF (SVN_ERR_RA_DAV_SOCK_INIT,
              SVN_ERR_RA_DAV_CATEGORY_START + 0,
              "RA layer failed to init socket layer")

  SVN_ERRDEF (SVN_ERR_RA_DAV_CREATING_REQUEST,
              SVN_ERR_RA_DAV_CATEGORY_START + 1,
              "RA layer failed to create HTTP request")

  SVN_ERRDEF (SVN_ERR_RA_DAV_REQUEST_FAILED,
              SVN_ERR_RA_DAV_CATEGORY_START + 2,
              "RA layer request failed")

  SVN_ERRDEF (SVN_ERR_RA_DAV_OPTIONS_REQ_FAILED,
              SVN_ERR_RA_DAV_CATEGORY_START + 3,
              "RA layer didn't receive requested OPTIONS info")
    
  SVN_ERRDEF (SVN_ERR_RA_DAV_PROPS_NOT_FOUND,
              SVN_ERR_RA_DAV_CATEGORY_START + 4,
              "RA layer failed to fetch properties")

  SVN_ERRDEF (SVN_ERR_RA_DAV_ALREADY_EXISTS,
              SVN_ERR_RA_DAV_CATEGORY_START + 5,
              "RA layer file already exists")

  SVN_ERRDEF (SVN_ERR_RA_DAV_INVALID_TIMEOUT,
              SVN_ERR_RA_DAV_CATEGORY_START + 6,
              "invalid timeout")

  /* ra_local errors */
  
  SVN_ERRDEF (SVN_ERR_RA_LOCAL_REPOS_NOT_FOUND,
              SVN_ERR_RA_LOCAL_CATEGORY_START + 0,
              "Couldn't find a repository.")

  /* svndiff errors */

  SVN_ERRDEF (SVN_ERR_SVNDIFF_INVALID_HEADER,
              SVN_ERR_SVNDIFF_CATEGORY_START + 0,
              "Svndiff data has invalid header")

  SVN_ERRDEF (SVN_ERR_SVNDIFF_CORRUPT_WINDOW,
              SVN_ERR_SVNDIFF_CATEGORY_START + 1,
              "Svndiff data contains corrupt window")

  SVN_ERRDEF (SVN_ERR_SVNDIFF_BACKWARD_VIEW,
              SVN_ERR_SVNDIFF_CATEGORY_START + 2,
              "Svndiff data contains backward-sliding source view")

  SVN_ERRDEF (SVN_ERR_SVNDIFF_INVALID_OPS,
              SVN_ERR_SVNDIFF_CATEGORY_START + 3,
              "Svndiff data contains invalid instruction")

  SVN_ERRDEF (SVN_ERR_SVNDIFF_UNEXPECTED_END,
              SVN_ERR_SVNDIFF_CATEGORY_START + 4,
              "Svndiff data ends unexpectedly")

  /* mod_dav_svn errors */

  SVN_ERRDEF (SVN_ERR_APMOD_MISSING_PATH_TO_FS,
              SVN_ERR_APMOD_CATEGORY_START + 0,
              "Apache has no path to an SVN filesystem")

  SVN_ERRDEF (SVN_ERR_APMOD_MALFORMED_URI,
              SVN_ERR_APMOD_CATEGORY_START + 1,
              "Apache got a malformed URI")

  SVN_ERRDEF (SVN_ERR_APMOD_ACTIVITY_NOT_FOUND,
              SVN_ERR_APMOD_CATEGORY_START + 2,
              "Activity not found")

  SVN_ERRDEF (SVN_ERR_APMOD_BAD_BASELINE,
              SVN_ERR_APMOD_CATEGORY_START + 3,
              "Baseline incorrect")

  /* libsvn_client errors */

  SVN_ERRDEF (SVN_ERR_CLIENT_VERSIONED_PATH_REQUIRED,
              SVN_ERR_CLIENT_CATEGORY_START + 0,
              "A path under revision control is needed for this operation")

  SVN_ERRDEF (SVN_ERR_CLIENT_RA_ACCESS_REQUIRED,
              SVN_ERR_CLIENT_CATEGORY_START + 1,
              "Repository access is needed for this operation")

  SVN_ERRDEF (SVN_ERR_CLIENT_BAD_REVISION,
              SVN_ERR_CLIENT_CATEGORY_START + 2,
              "Bogus revision information given")

  SVN_ERRDEF (SVN_ERR_CLIENT_DUPLICATE_COMMIT_URL,
              SVN_ERR_CLIENT_CATEGORY_START + 3,
              "Attempting to commit to a URL more than once")

  SVN_ERRDEF (SVN_ERR_CLIENT_UNVERSIONED,
              SVN_ERR_CLIENT_CATEGORY_START + 4,
              "Attempting restricted operation for unversioned resource")

  SVN_ERRDEF (SVN_ERR_CLIENT_INVALID_EXTERNALS_DESCRIPTION,
              SVN_ERR_CLIENT_CATEGORY_START + 5,
              "Format of an " SVN_PROP_EXTERNALS " property was invalid.")

  SVN_ERRDEF (SVN_ERR_CLIENT_MODIFIED,
              SVN_ERR_CLIENT_CATEGORY_START + 6,
              "Attempting restricted operation for modified resource")

  /* misc errors */

  SVN_ERRDEF (SVN_ERR_BASE,
              SVN_ERR_MISC_CATEGORY_START + 0,
              "A problem occured; see later errors for details")

  SVN_ERRDEF (SVN_ERR_PLUGIN_LOAD_FAILURE,
              SVN_ERR_MISC_CATEGORY_START + 1,
              "Failure loading plugin")

  SVN_ERRDEF (SVN_ERR_MALFORMED_FILE,
              SVN_ERR_MISC_CATEGORY_START + 2,
              "Malformed file")

  SVN_ERRDEF (SVN_ERR_INCOMPLETE_DATA,
              SVN_ERR_MISC_CATEGORY_START + 3,
              "Incomplete data")

  SVN_ERRDEF (SVN_ERR_INCORRECT_PARAMS,
              SVN_ERR_MISC_CATEGORY_START + 4,
              "Incorrect parameters given")

  SVN_ERRDEF (SVN_ERR_UNVERSIONED_RESOURCE,
              SVN_ERR_MISC_CATEGORY_START + 5,
              "Tried a versioning operation on an unversioned resource")

  SVN_ERRDEF (SVN_ERR_TEST_FAILED,
              SVN_ERR_MISC_CATEGORY_START + 6,
              "Test failed")
       
  SVN_ERRDEF (SVN_ERR_UNSUPPORTED_FEATURE,
              SVN_ERR_MISC_CATEGORY_START + 7,
              "Trying to use an unsupported feature")

  SVN_ERRDEF (SVN_ERR_BAD_PROP_KIND,
              SVN_ERR_MISC_CATEGORY_START + 8,
              "Unexpected or unknown property kind")

  SVN_ERRDEF (SVN_ERR_ILLEGAL_TARGET,
              SVN_ERR_MISC_CATEGORY_START + 9,
              "Illegal target for the requested operation")

  SVN_ERRDEF (SVN_ERR_DELTA_MD5_CHECKSUM_ABSENT,
              SVN_ERR_MISC_CATEGORY_START + 10,
              "MD5 checksum is missing")

  SVN_ERRDEF (SVN_ERR_DIR_NOT_EMPTY,
              SVN_ERR_MISC_CATEGORY_START + 11,
              "Directory needs to be empty but is not")

  SVN_ERRDEF (SVN_ERR_EXTERNAL_PROGRAM,
              SVN_ERR_MISC_CATEGORY_START + 12,
              "Error calling external program")

  SVN_ERRDEF (SVN_ERR_SWIG_PY_EXCEPTION_SET,
              SVN_ERR_MISC_CATEGORY_START + 13,
              "Python exception has been set with the error")

  /* command-line client errors */

  SVN_ERRDEF (SVN_ERR_CL_ARG_PARSING_ERROR,
              SVN_ERR_CL_CATEGORY_START + 0,
              "Client error in parsing arguments")

  SVN_ERRDEF (SVN_ERR_CL_INSUFFICIENT_ARGS,
              SVN_ERR_CL_CATEGORY_START + 1,
              "Not enough args provided")

  SVN_ERRDEF (SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS,
              SVN_ERR_CL_CATEGORY_START + 2,
              "Mutually exclusive arguments specified.")                   

  SVN_ERRDEF (SVN_ERR_CL_ADM_DIR_RESERVED,
              SVN_ERR_CL_CATEGORY_START + 3,
              "Attempted command in administrative dir")

  SVN_ERRDEF (SVN_ERR_CL_LOG_MESSAGE_IS_VERSIONED_FILE,
              SVN_ERR_CL_CATEGORY_START + 4,
              "The log message file is under version control")

  SVN_ERRDEF (SVN_ERR_CL_LOG_MESSAGE_IS_PATHNAME,
              SVN_ERR_CL_CATEGORY_START + 5,
              "The log message is a pathname")

  SVN_ERRDEF (SVN_ERR_CL_COMMIT_IN_ADDED_DIR,
              SVN_ERR_CL_CATEGORY_START + 6,
              "Commiting in directory scheduled for addition")

  SVN_ERRDEF (SVN_ERR_CL_NO_EXTERNAL_EDITOR,
              SVN_ERR_CL_CATEGORY_START + 7,
              "No external editor available")

SVN_ERROR_END


#undef SVN_ERROR_START
#undef SVN_ERRDEF
#undef SVN_ERROR_END

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* defined(SVN_ERROR_BUILD_ARRAY) || !defined(SVN_ERROR_ENUM_DEFINED) */
