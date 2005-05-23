/**
 * @copyright
 * ====================================================================
 * Copyright (c) 2000-2004 CollabNet.  All rights reserved.
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
 * @file svn_ebcdic.h
 * @brief Macros and functions used on EBCDIC platforms.
 * 
 * The four printf style functions in this file, 
 * 
 *   svn_ebcdic_pvsprintf
 *   svn_ebcdic_pvsprintf2
 *   svn_ebcdic_psprintf
 *   svn_ebcdic_psprintf2
 * 
 * and their related substitution macros exist to make the impact of the
 * ebcdic port's impact on the subversion code base as non-intrusive as
 * possible (e.g. minimize the amount of APR_CHARSET_EBCDIC blocked code).
 * 
 * Note: Unlike apr_p(v)sprintf, these functions do not support the alternate
 *       format specification 
 *       __%__arg-number$__[flags]__[width]__[.precision]__[h|L|l|ll]__type
 */

#ifndef SVN_APR_WRAP_H
#define SVN_APR_WRAP_H

#if APR_CHARSET_EBCDIC
#include <apr.h>
#include <apr_pools.h>
#include <apr_thread_proc.h>

#include "svn_error.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#if APR_CHARSET_EBCDIC
#pragma comment(copyright,"(c) 2000-2005 CollabNet.   All rights reserved.")
#endif


#if !APR_CHARSET_EBCDIC
  /* Substitution macros which facilitate handling of printf-style format
   * strings on an ebcdic platform. */
  #define APR_PVSPRINTF       apr_pvsprintf
  #define APR_PSPRINTF        apr_psprintf
  #define APR_PVSPRINTF2      apr_pvsprintf
  #define APR_PSPRINTF2       apr_psprintf
  #define SVN_CMDLINE_FPRINTF svn_cmdline_fprintf  
  #define SVN_CMDLINE_PRINTF  svn_cmdline_printf
#else
  #define APR_PVSPRINTF       svn_ebcdic_pvsprintf
  #define APR_PSPRINTF        svn_ebcdic_psprintf
  #define APR_PVSPRINTF2      svn_ebcdic_pvsprintf2
  #define APR_PSPRINTF2       svn_ebcdic_psprintf2
  #define SVN_CMDLINE_FPRINTF svn_cmdline_fprintf_ebcdic
  #define SVN_CMDLINE_PRINTF  svn_cmdline_printf_ebcdic 

/**
 * printf-style style printing routine similar to apr_pvsprintf except that
 * any character or string arguments in the va_list are assumed to be in utf-8.
 * The data is output to an ebcdic encoded string allocated from a pool
 * @param p The pool to allocate out of
 * @param fmt The ebcdic encoded format of the string
 * @param ap The arguments to use while printing the data
 * @return The new ebcdic encoded string
 */       
char *
svn_ebcdic_pvsprintf (apr_pool_t *p,
                      const char *fmt,
                      va_list ap);

                      
/**
 * printf-style style printing routine similar to svn_ebcdic_pvsprintf except
 * that the string returned is utf-8 encoded.
 * The data is output to a utf-8 encoded string allocated from a pool.
 * @param p The pool to allocate out of
 * @param fmt The ebcdic encoded format of the string
 * @param ap The arguments to use while printing the data
 * @return The new utf-8 encoded string
 */       
char *
svn_ebcdic_pvsprintf2 (apr_pool_t *p,
                       const char *fmt,
                       va_list ap);

                      
/**
 * printf-style style printing routine similar to apr_psprintf except that
 * any character or string variable arguments are assumed to be in utf-8.
 * The data is output to a string allocated from a pool.
 * @param p The pool to allocate out of
 * @param fmt The ebcdic encoded format of the string
 * @param ... The arguments to use while printing the data
 * @return The new ebcdic encoded string
 */   
char *
svn_ebcdic_psprintf(apr_pool_t *p,
                    const char *fmt,
                    ...);
                    

/**
 * printf-style style printing routine similar to svn_ebcdic_psprintf except
 * that the string returned is utf-8 encoded.
 * The data is output to a string allocated from a pool.
 * @param p The pool to allocate out of
 * @param fmt The ebcdic encoded format of the string
 * @param ... The arguments to use while printing the data
 * @return The new utf-8 encoded string
 */   
char *
svn_ebcdic_psprintf2(apr_pool_t *p,
                     const char *fmt,
                     ...);
                                                            
#endif /* APR_CHARSET_EBCDIC */

#if AS400
/** 
 * Set the ccsid of file @a path to @a ccsid.  
 * @a path The utf-8 encoded file path
 * @a pool The pool to use for conversion of @a path to an ebcdic path
 */
svn_error_t *
svn_ebcdic_set_file_ccsid (const char *path,
                           int ccsid,
                           apr_pool_t *pool);
                          

/** Handles unix-type qsh(ell) scripts on the iSeries.  
 * 
 * Invoke @a cmd with @a args, using utf8-encoded @a path as working 
 * directory.  
 *
 * @a args is a list of utf8-encoded (<tt>const char *</tt>)'s, terminated by
 * @c NULL.  @c ARGS[0] is the name of the program, though it need not be the
 * same as @a cmd.
 * 
 * If @a check_exitcode is FALSE, @c SVN_NO_ERROR is returned.
 * @a exitcode is set to 0 if the script ran successfully or a non-zero iSeries
 * specific error code if not, @a exitwhy is undefined.
 * 
 * If @a check_exitcode is TRUE:
 * 
 *   If @a exitcode == 0 then the script ran successfully and returned no error,
 *   @a exitwhy is set to APR_PROC_EXIT.
 * 
 *   If @a exitcode == -1 the script failed to run, @a exitwhy is set to
 *   APR_PROC_EXIT and an svn_error_t is returned with the iSeries specific
 *   errno contained within the message:
 *
 *     "Unable to run script 'script_name'.  Returned error number =  errno".
 *
 *   For all other values of @a exitcode an @c SVN_ERR_EXTERNAL_PROGRAM error is
 *   returned and @a exitwhy is set one of the following values:
 *   
 *     APR_PROC_EXIT   - The script process ran to completion but returned an
 *                       error exit code.  
 *
 *     APR_PROC_SIGNAL - The script process ended because of the receipt of a
 *                       terminating signal that was not caught by the script
 *                       process.
 *
 *     APR_PROC_EXIT   - The script process ended because of an error condition.
 */
svn_error_t *
svn_ebcdic_run_unix_type_script (const char *path,
                                 const char *cmd,
                                 const char *const *args,
                                 int *exitcode,
                                 apr_exit_why_e *exitwhy,
                                 svn_boolean_t check_exitcode,
                                 apr_pool_t *pool);

                                 
/** Copy @a from_path to @a to_path atomically.  Both @a from_path and
 * @a to_path are utf8-encoded filenames.  If @a to_perms is true, set 
 * @a to_path's permissions to match those of @a from_path.
 * @a flags are the Or'ed value of @c apr_file_open() flags to use when
 * opening @a to_path.  @a from_path is always opened as binary.
 */
apr_status_t
svn_ebcdic_file_transfer_contents(const char *from_path,
                                  const char *to_path,
                                  apr_int32_t flags,
                                  apr_fileperms_t to_perms,
                                  apr_pool_t *pool);                                 
#endif /* AS400 */                                 

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_APR_WRAP_H */
