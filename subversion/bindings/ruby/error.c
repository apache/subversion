/*
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
#include <ruby.h>
#include <svn_error.h>

#include "svn_ruby.h"
#include "error.h"

static VALUE mSvnError, eGeneral;

/* ### This will break if first or last error is changed. */
#define SVN_RUBY_ERR_START SVN_WARNING
#define SVN_RUBY_ERR_END SVN_ERR_CL_ADM_DIR_RESERVED
#define SVN_RUBY_ERR_PROTECTED APR_OS_START_SYSERR - 1
#define SVN_RUBY_ERR_OBJECT "svn-ruby-error-object"

static VALUE error_list[SVN_RUBY_ERR_END - SVN_RUBY_ERR_START + 1];
/* To protect exception from GC. */
static VALUE error_hash;

EXTERN VALUE ruby_errinfo;

svn_error_t *
svn_ruby_error (const char *msg, apr_pool_t *pool)
{
  svn_error_t *err;
  err = svn_error_createf (SVN_RUBY_ERR_PROTECTED, 0, 0, pool,
			    "%s", msg);
  apr_pool_userdata_set ((void *) ruby_errinfo, SVN_RUBY_ERR_OBJECT,
			 apr_pool_cleanup_null, err->pool);
  if (ruby_errinfo != Qnil)
    rb_hash_aset (error_hash, ruby_errinfo, Qnil);
  return err;
}

void
svn_ruby_raise (svn_error_t *err)
{
  VALUE err_obj;

  if (err->apr_err == SVN_RUBY_ERR_PROTECTED)
    {
      void *value;

      apr_pool_userdata_get (&value, SVN_RUBY_ERR_OBJECT, err->pool);
      err_obj = (VALUE) value;
      if (err_obj == Qnil)
	err_obj = rb_exc_new2 (rb_eException, err->message);
      else
	rb_funcall (error_hash, rb_intern ("delete"), 1, err_obj);
    }
  else
    {
      VALUE err_class;

      if (SVN_RUBY_ERR_START <= err->apr_err && err->apr_err <= SVN_RUBY_ERR_END)
	err_class = error_list[err->apr_err - SVN_RUBY_ERR_START];
      else
	err_class = eGeneral;
      /* #### What about err->child?  Shouldn't we accumulate error messages? */
      err_obj = rb_exc_new2 (err_class, err->message);
      rb_iv_set (err_obj, "aprErr", INT2FIX (err->apr_err));
      rb_iv_set (err_obj, "srcErr", INT2FIX (err->apr_err));
    }

  rb_exc_raise (err_obj);
}

static void
define_error (int svn_err, const char *err_class)
{
  VALUE obj;

  obj = rb_define_class_under (mSvnError, err_class, rb_eStandardError);
  error_list[svn_err - SVN_RUBY_ERR_START] = obj;
}

void
svn_ruby_init_error (void)
{
  mSvnError = rb_define_module_under (svn_ruby_mSvn, "Error");
  eGeneral = rb_define_class_under (mSvnError, "General",
				    rb_eStandardError);

  define_error (SVN_WARNING, "Warning");
  define_error (SVN_ERR_PLUGIN_LOAD_FAILURE, "PluginLoadFailure");
  define_error (SVN_ERR_UNKNOWN_FS_ACTION, "UnknownFsAction");
  define_error (SVN_ERR_UNEXPECTED_EOF, "UnexpectedEof");
  define_error (SVN_ERR_MALFORMED_FILE, "MalformedFile");
  define_error (SVN_ERR_INCOMPLETE_DATA, "IncompleteData");
  define_error (SVN_ERR_MALFORMED_XML, "MalformedXml");
  define_error (SVN_ERR_UNVERSIONED_RESOURCE, "UnversionedResource");
  define_error (SVN_ERR_UNEXPECTED_NODE_KIND, "UnexpectedNodeKind");
  define_error (SVN_ERR_UNFRUITFUL_DESCENT, "UnfruitfulDescent");
  define_error (SVN_ERR_BAD_FILENAME, "BadFilename");
  define_error (SVN_ERR_BAD_URL, "BadURL");
  define_error (SVN_ERR_UNSUPPORTED_FEATURE, "UnsupportedFeature");
  define_error (SVN_ERR_UNKNOWN_NODE_KIND, "UnknownNodeKind");
  define_error (SVN_ERR_DELTA_MD5_CHECKSUM_ABSENT, "DeltaMd5ChecksumAbsent");
  define_error (SVN_ERR_DIR_NOT_EMPTY, "DirNotEmpty");
  define_error (SVN_ERR_XML_ATTRIB_NOT_FOUND, "XmlAttribNotFound");
  define_error (SVN_ERR_XML_MISSING_ANCESTRY, "XmlMissingAncestry");
  define_error (SVN_ERR_XML_UNKNOWN_ENCODING, "XmlUnknownEncoding");
  define_error (SVN_ERR_IO_INCONSISTENT_EOL, "IoInconsistentEOL");
  define_error (SVN_ERR_IO_UNKNOWN_EOL, "IoUnknownEOL");
  define_error (SVN_ERR_IO_CORRUPT_EOL, "IoCorruptEOL");
  define_error (SVN_ERR_ENTRY_NOT_FOUND, "EntryNotFound");
  define_error (SVN_ERR_ENTRY_EXISTS, "EntryExists");
  define_error (SVN_ERR_ENTRY_MISSING_REVISION, "EntryMissingRevision");
  define_error (SVN_ERR_ENTRY_MISSING_URL, "EntryMissingURL");
  define_error (SVN_ERR_ENTRY_ATTRIBUTE_INVALID, "EntryAttributeInvalid");
  define_error (SVN_ERR_WC_OBSTRUCTED_UPDATE, "WcObstructedUpdate");
  define_error (SVN_ERR_WC_UNWIND_MISMATCH, "WcUnwindMismatch");
  define_error (SVN_ERR_WC_UNWIND_EMPTY, "WcUnwindEmpty");
  define_error (SVN_ERR_WC_UNWIND_NOT_EMPTY, "WcUnwindNotEmpty");
  define_error (SVN_ERR_WC_LOCKED, "WcLocked");
  define_error (SVN_ERR_WC_NOT_DIRECTORY, "WcNotDirectory");
  define_error (SVN_ERR_WC_NOT_FILE, "WcNotFile");
  define_error (SVN_ERR_WC_BAD_ADM_LOG, "WcBadAdmLog");
  define_error (SVN_ERR_WC_PATH_NOT_FOUND, "WcPathNotFound");
  define_error (SVN_ERR_WC_NOT_UP_TO_DATE, "WcNotUpToDate");
  define_error (SVN_ERR_WC_LEFT_LOCAL_MOD, "WcLeftLocalMod");
  define_error (SVN_ERR_IO_UNIQUE_NAMES_EXHAUSTED, "IoUniqueNamesExhausted");
  define_error (SVN_ERR_WC_FOUND_CONFLICT, "WcFoundConflict");
  define_error (SVN_ERR_WC_CORRUPT, "WcCorrupt");
  define_error (SVN_ERR_FS_GENERAL, "FsGeneral");
  define_error (SVN_ERR_FS_CLEANUP, "FsCleanup");
  define_error (SVN_ERR_FS_ALREADY_OPEN, "FsAlreadyOpen");
  define_error (SVN_ERR_FS_NOT_OPEN, "FsNotOpen");
  define_error (SVN_ERR_FS_CORRUPT, "FsCorrupt");
  define_error (SVN_ERR_FS_PATH_SYNTAX, "FsPathSyntax");
  define_error (SVN_ERR_FS_NO_SUCH_REVISION, "FsNoSuchRevision");
  define_error (SVN_ERR_FS_NO_SUCH_TRANSACTION, "FsNoSuchTransaction");
  define_error (SVN_ERR_FS_NO_SUCH_ENTRY, "FsNoSuchEntry");
  define_error (SVN_ERR_FS_NO_SUCH_REPRESENTATION, "FsNoSuchRepresentation");
  define_error (SVN_ERR_FS_NO_SUCH_STRING, "FsNoSuchString");
  define_error (SVN_ERR_FS_NOT_FOUND, "FsNotFound");
  define_error (SVN_ERR_FS_ID_NOT_FOUND, "FsIdNotFound");
  define_error (SVN_ERR_FS_NOT_ID, "FsNotId");
  define_error (SVN_ERR_FS_NOT_DIRECTORY, "FsNotDirectory");
  define_error (SVN_ERR_FS_NOT_FILE, "FsNotFile");
  define_error (SVN_ERR_FS_NOT_SINGLE_PATH_COMPONENT, "FsNotSinglePathComponent");
  define_error (SVN_ERR_FS_NOT_MUTABLE, "FsNotMutable");
  define_error (SVN_ERR_FS_ALREADY_EXISTS, "FsAlreadyExists");
  define_error (SVN_ERR_FS_ROOT_DIR, "FsRootDir");
  define_error (SVN_ERR_FS_NOT_TXN_ROOT, "FsNotTxnRoot");
  define_error (SVN_ERR_FS_NOT_REVISION_ROOT, "FsNotRevisionRoot");
  define_error (SVN_ERR_FS_CONFLICT, "FsConflict");
  define_error (SVN_ERR_FS_REP_CHANGED, "FsRepChanged");
  define_error (SVN_ERR_FS_REP_NOT_MUTABLE, " SvnErrFsRepNotMutable");
  define_error (SVN_ERR_TXN_OUT_OF_DATE, "TxnOutOfDate");
  define_error (SVN_ERR_REPOS_LOCKED, "ReposLocked");
  define_error (SVN_ERR_REPOS_HOOK_FAILURE, "ReposHookFailure");
  define_error (SVN_ERR_EXTERNAL_PROGRAM, "ExternalProgram");
  define_error (SVN_ERR_BERKELEY_DB, "BerkeleyDb");
  define_error (SVN_ERR_RA_ILLEGAL_URL, "RaIllegalUrl");
  define_error (SVN_ERR_RA_NOT_AUTHORIZED, "RaNotAuthorized");
  define_error (SVN_ERR_RA_UNKNOWN_AUTH, "RaUnknownAuth");
  define_error (SVN_ERR_RA_SOCK_INIT, "RaSockInit");
  define_error (SVN_ERR_RA_HOSTNAME_LOOKUP, "RaHostnameLookup");
  define_error (SVN_ERR_RA_CREATING_REQUEST, "RaCreatingRequest");
  define_error (SVN_ERR_RA_REQUEST_FAILED, "RaRequestFailed");
  define_error (SVN_ERR_RA_PROPS_NOT_FOUND, "RaPropsNotFound");
  define_error (SVN_ERR_RA_NOT_VERSIONED_RESOURCE, "RaNotVersionedResource");
  define_error (SVN_ERR_RA_BAD_REVISION_REPORT, "RaBadRevisionReport");
  define_error (SVN_ERR_SVNDIFF_INVALID_HEADER, "SvndiffInvalidHeader");
  define_error (SVN_ERR_SVNDIFF_CORRUPT_WINDOW, "SvndiffCorruptWindow");
  define_error (SVN_ERR_SVNDIFF_BACKWARD_VIEW, "SvndiffBackwardView");
  define_error (SVN_ERR_SVNDIFF_INVALID_OPS, "SvndiffInvalidOps");
  define_error (SVN_ERR_SVNDIFF_UNEXPECTED_END, "SvndiffUnexpectedEnd");
  define_error (SVN_ERR_BAD_CONTAINING_POOL, "BadContainingPool");
  define_error (SVN_ERR_APMOD_MISSING_PATH_TO_FS, "ApmodMissingPathToFs");
  define_error (SVN_ERR_APMOD_MALFORMED_URI, "ApmodMalformedUri");
  define_error (SVN_ERR_TEST_FAILED, "TestFailed");
  define_error (SVN_ERR_CL_ARG_PARSING_ERROR, "ClArgParsingError");
  define_error (SVN_ERR_CL_MUTUALLY_EXCLUSIVE_ARGS, "ClMutuallyExclusiveArgs");
  define_error (SVN_ERR_CL_ADM_DIR_RESERVED, "ClAdmDirReserved");

  error_hash = rb_hash_new ();
  rb_global_variable (&error_hash);
}
