/*
 * ebcdic.c:  UTF-8 conversion routines
 *
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
 */



#include <apr_lib.h>
#include <qshell.h> /* For QzshSystem */

#include "svn_ebcdic.h"
#include "svn_pools.h"
#include "svn_string.h"
#include "svn_utf.h"


#if APR_CHARSET_EBCDIC

/* Private Utility Functions */

/* Append char c to svn_stringbuf_t sb */
void add_ch_to_sbuf(char c, svn_stringbuf_t *sb)
{
  char ch[2] = {'\0', '\0'};
  ch[0] = c;
  svn_stringbuf_appendcstr(sb, ch);
}

/* Test chars for various valid printf-style formats */
#define svn_ebcdic_valid_flag(c)         (!(c^'-') | !(c^'+') | !(c^' ') | \
                                          !(c^'#') | !(c^'0') )

#define svn_ebcdic_valid_int_types(c)    (!(c^'d') | !(c^'i') | !(c^'o') | \
                                          !(c^'u') | !(c^'x') | !(c^'X') | \
                                          !(c^'n'))

#define svn_ebcdic_valid_sint_types(c)   (!(c^'d') | !(c^'i') )

#define svn_ebcdic_valid_uint_types(c)   (!(c^'o') | !(c^'u') | !(c^'x') | \
                                          !(c^'X') | !(c^'n') )

#define svn_ebcdic_valid_double_types(c) (!(c^'e') | !(c^'E') | !(c^'f') | \
                                          !(c^'g') | !(c^'G') )

#define svn_ebcdic_valid_wide_types(c)   (!(c^'c') | !(c^'s') )

#define svn_ebcdic_valid_single_types(c) (!(c^'d') | !(c^'i') | !(c^'o') | \
                                          !(c^'u') | !(c^'x') | !(c^'X') | \
                                          !(c^'f') | !(c^'e') | !(c^'E') | \
                                          !(c^'g') | !(c^'G') | !(c^'c') | \
                                          !(c^'s') | !(c^'n') | !(c^'p') | \
                                          !(c^'C') | !(c^'S') )


/* Public Function Definitions */

char *
svn_ebcdic_pvsprintf(apr_pool_t *pool, const char *fmt, va_list arg_ptr)
{
  apr_pool_t *subpool_perm = svn_pool_create_ex(pool, NULL);
  apr_pool_t *subpool_temp = svn_pool_create_ex(pool, NULL);
  svn_stringbuf_t *result = svn_stringbuf_create("", pool);
  svn_stringbuf_t *temp_fmt = svn_stringbuf_create("", pool);
  char *s = apr_pstrdup(subpool_temp, fmt);
  char *temp_result = NULL;
  int test = -1;
  signed int             temp_si;
  signed long int        temp_sli;
  unsigned int           temp_ui;
  unsigned long int      temp_uli;
  signed long long int   temp_slli;
  unsigned long long int temp_ulli;
  signed short int       temp_ssi;
  unsigned short int     temp_usi;
  double                 temp_dbl;
  long double            temp_ldbl;
  unsigned int/*char*/   temp_ch;
  char                   *temp_str;
  wchar_t                temp_wct;
  apr_int64_t            temp_apr_si64;
  apr_uint64_t           temp_apr_ui64;

  while(s[0] != '\0')
  {
    if(s[0] != '%')
    {
      /* If current c is not a format element just
       * append it to the return string
       */
      add_ch_to_sbuf(s++[0], result);
    }
    else /* s[0] == '%' */
    {
      /* Start building a format string for call to apr_psprintf() */
      svn_stringbuf_set(temp_fmt, "%");
      s++;
      if(svn_ebcdic_valid_flag(*s))
        add_ch_to_sbuf(s++[0], temp_fmt);
      if( *s == '*' )
        add_ch_to_sbuf(s++[0], temp_fmt);
      else if(apr_isdigit(*s))
      {
        add_ch_to_sbuf(s++[0], temp_fmt);
        while(apr_isdigit(*s))
          add_ch_to_sbuf(s++[0], temp_fmt);
      }
      if( *s == '.' )
      {
        /* Add the '.' */
        add_ch_to_sbuf(s++[0], temp_fmt);
        if(s[0] == '*')
        {
          const char *ptr;
          svn_string_t *temp_si_str;
          s++;
          temp_si = va_arg(arg_ptr, signed int);
          temp_si_str = svn_string_createf (pool, "%d", temp_si);
          ptr = temp_si_str->data;
          while (ptr[0] != '\0')
            add_ch_to_sbuf(ptr++[0], temp_fmt);
        }
        /* Gather all precision digits, ok if there are none, the user must
         * want the default precision for this type. */  
        while(apr_isdigit(*s))
          add_ch_to_sbuf(s++[0], temp_fmt);
      }
        switch(*s)
        {
          case 'I' :
            add_ch_to_sbuf(s++[0], temp_fmt);
            if(s[0] == '6' && s[1] == '4')
            {
              add_ch_to_sbuf(s++[0], temp_fmt);
              add_ch_to_sbuf(s++[0], temp_fmt);
              switch(*s)
              {
                case 'd' :
                  /* SUCCESS apr_int64_t
                   * %I64d */
                  add_ch_to_sbuf(s++[0], temp_fmt);
                  temp_apr_si64 = va_arg(arg_ptr, apr_int64_t);
                  temp_result = apr_psprintf(subpool_temp, temp_fmt->data,
                                             temp_apr_si64);
                  svn_stringbuf_appendcstr(result, temp_result ? temp_result :
                                           "");
                  break;
                case 'u' :
                case 'x' :
                  /* SUCCESS apr_uint64_t
                   * %I64u | %I64x */
                  add_ch_to_sbuf(s++[0], temp_fmt);
                  temp_apr_ui64 = va_arg(arg_ptr, apr_uint64_t);
                  temp_result = apr_psprintf(subpool_temp, temp_fmt->data,
                                             temp_apr_ui64);
                  svn_stringbuf_appendcstr(result, temp_result ? temp_result :
                                           "");
                  break;
                default :
                {
                  /* "%I" followed by chars that have no special format
                   * meaning in combination with "I" is probably just a
                   * bug, regardless it doesn't get interpreted as a valid
                   * format.  Just print the first invalid character in the
                   * sequence and move on.
                   */
                  svn_stringbuf_setempty(temp_fmt);
                  add_ch_to_sbuf(s++[0], temp_fmt);
                  svn_stringbuf_appendcstr(result, temp_fmt->data);
                }                
              }
            }
            break;
          case 'L' :
            add_ch_to_sbuf(s++[0], temp_fmt);
            if(svn_ebcdic_valid_double_types(*s))
            {
              /* SUCCESS long double
               * %Le | %LE | %Lf | %Lg | %LG */
              add_ch_to_sbuf(s++[0], temp_fmt);
              temp_ldbl = va_arg(arg_ptr, long double);
              temp_result = apr_psprintf(subpool_temp, temp_fmt->data,
                                         temp_ldbl);
              svn_stringbuf_appendcstr(result, temp_result ? temp_result : "");
            }
            else
            {
              /* "%L" followed by a char that has no special format
               * meaning in combination with "L" is probably just a
               * bug, regardless it doesn't get interpreted as a valid
               * format.  Just print the first invalid character in the
               * sequence and move on.
               */
              svn_stringbuf_setempty(temp_fmt);
              add_ch_to_sbuf(s++[0], temp_fmt);
              svn_stringbuf_appendcstr(result, temp_fmt->data);
            }
            break;
          case 'h' :
            add_ch_to_sbuf(s++[0], temp_fmt);
            if(svn_ebcdic_valid_uint_types(*s))
            {
              unsigned int t;	
              /* SUCCESS unsigned short ints
               * %ho | %hu | %hx | %hX */
              add_ch_to_sbuf(s++[0], temp_fmt);
              /* iSeries needs unsigned int here, not unsigned short int */
              temp_usi = va_arg(arg_ptr, unsigned int);
              temp_result = apr_psprintf(subpool_temp, temp_fmt->data, 
                                         temp_usi);
              svn_stringbuf_appendcstr(result, temp_result ? temp_result : "");
            }
            else if(svn_ebcdic_valid_sint_types(*s))
            {
              /* SUCCESS signed short ints
               * %hd | %hi */
              add_ch_to_sbuf(s++[0], temp_fmt);
              /* iSeries needs signed int here, not unsigned short int */
              temp_ssi = va_arg(arg_ptr, signed int);
              temp_result = apr_psprintf(subpool_temp, temp_fmt->data, 
                                         temp_ssi);
              svn_stringbuf_appendcstr(result, temp_result ? temp_result : "");
            }
            else
            {
              /* "%h" followed by a char that has no special format
               * meaning in combination with "h" is probably just a
               * bug, regardless it doesn't get interpreted as a valid
               * format.  Just print the first invalid character in the
               * sequence and move on.
               */
              svn_stringbuf_setempty(temp_fmt);
              add_ch_to_sbuf(s++[0], temp_fmt);
              svn_stringbuf_appendcstr(result, temp_fmt->data);
              break;
            }
          case 'l' :
            add_ch_to_sbuf(s++[0], temp_fmt);
            if( *s == 'l')
            {
              /* long longs */
              add_ch_to_sbuf(s++[0], temp_fmt);
              if(svn_ebcdic_valid_uint_types(*s))
              {
                /* SUCCESS unsigned long long ints
                 * %llo | %llu | %llx | %llX */
                add_ch_to_sbuf(s++[0], temp_fmt);
                temp_ulli = va_arg(arg_ptr, unsigned long long int);
                temp_result = apr_psprintf(subpool_temp, temp_fmt->data,
                                           temp_ulli);
                svn_stringbuf_appendcstr(result, temp_result ? 
                                         temp_result : "");
              }
              else if(svn_ebcdic_valid_sint_types(*s))
              {
                /* SUCCESS signed long long ints
                 * lld | lli */
                add_ch_to_sbuf(s++[0], temp_fmt);
                temp_slli = va_arg(arg_ptr, signed long long int);
                temp_result = apr_psprintf(subpool_temp, temp_fmt->data,
                                           temp_slli);
                svn_stringbuf_appendcstr(result, temp_result ?
                                         temp_result : "");
              }
              else
              {
                /* "%ll" followed by a char that has no special format
                 * meaning in combination with "ll" is probably just a
                 * bug, regardless it doesn't get interpreted as a valid
                 * format.  Just print the first invalid character in the
                 * sequence and move on.
                 */
                svn_stringbuf_setempty(temp_fmt);
                add_ch_to_sbuf(s++[0], temp_fmt);
                svn_stringbuf_appendcstr(result, temp_fmt->data);
              }              
            }
            else if(svn_ebcdic_valid_uint_types(*s))
            {
            /* SUCCESS long unsigned int
             * %lo | %lu | %lx | %lX */
              add_ch_to_sbuf(s++[0], temp_fmt);
              temp_uli = va_arg(arg_ptr, unsigned long int);
              temp_result = apr_psprintf(subpool_temp, temp_fmt->data,
                                         temp_uli);
              svn_stringbuf_appendcstr(result, temp_result ? temp_result : "");
            }
            else if(svn_ebcdic_valid_sint_types(*s))
            {
            /* SUCCESS long signed int
            * %ld | %li */
              add_ch_to_sbuf(s++[0], temp_fmt);
              temp_sli = va_arg(arg_ptr, signed long int);
              temp_result = apr_psprintf(subpool_temp, temp_fmt->data,
                                         temp_sli);
              svn_stringbuf_appendcstr(result, temp_result ? temp_result : "");
            }
            else if( svn_ebcdic_valid_wide_types(*s) )
            {
              /* SUCCESS wchar_t
               * %lc | %ls */
              add_ch_to_sbuf(s++[0], temp_fmt);
              temp_wct = va_arg(arg_ptr, wchar_t);
              temp_result = apr_psprintf(subpool_temp, temp_fmt->data,
                                         temp_wct);
              svn_stringbuf_appendcstr(result, temp_result ? temp_result : "");
            }
            else
            {
              /* "%l" followed by a char that has no special format
               * meaning in combination with "l" is probably just a
               * bug, regardless it doesn't get interpreted as a valid
               * format.  Just print the first invalid character in the
               * sequence and move on.
               */
              svn_stringbuf_setempty(temp_fmt);
              add_ch_to_sbuf(s++[0], temp_fmt);
              svn_stringbuf_appendcstr(result, temp_fmt->data);
            }
            break;   
          case 'd' :
          case 'i' :
            /* SUCCESS signed int
             * %d | %i */
            add_ch_to_sbuf(s++[0], temp_fmt);
            temp_si = va_arg(arg_ptr, signed int);
            temp_result = apr_psprintf(subpool_temp, temp_fmt->data, temp_si);
            svn_stringbuf_appendcstr(result, temp_result ? temp_result : "");
            break;
          case 'o' :
          case 'u' :
          case 'x' :
          case 'X' :
          case 'n' :
            /* SUCCESS unsigned int
             * %o | %u | %x | %X | %n */
            add_ch_to_sbuf(s++[0], temp_fmt);
            temp_ui = va_arg(arg_ptr, unsigned int);
            temp_result = apr_psprintf(subpool_temp, temp_fmt->data, temp_ui);
            svn_stringbuf_appendcstr(result, temp_result ? temp_result : "");
            break;
          case 'c' :
            /* SUCCESS char
             * %c */
            add_ch_to_sbuf(s++[0], temp_fmt);
            /* va_arg() won't accept char as a 2nd arg, so int it must be */
            temp_ch = va_arg(arg_ptr, int);
            temp_result = apr_psprintf(subpool_temp, temp_fmt->data, temp_ch);
            if(temp_result)
              svn_utf_cstring_from_utf8(&temp_result, temp_result,
                                        subpool_temp);            
            svn_stringbuf_appendcstr(result, temp_result ? temp_result : "");
            break;
          case 'f' :
          case 'e' :
          case 'E' :
          case 'g' :
          case 'G' :
            /* SUCCESS double
             * %f | %e | %E | %g | %G
             */
            add_ch_to_sbuf(s++[0], temp_fmt);
            temp_dbl = va_arg(arg_ptr, double);
            temp_result = apr_psprintf(subpool_temp, temp_fmt->data, temp_dbl);
            svn_stringbuf_appendcstr(result, temp_result ? temp_result : "");
            break;
          case 's' :
            temp_str = va_arg(arg_ptr, char*);
            if(temp_str) 
              svn_utf_cstring_from_utf8(&temp_str, temp_str, subpool_temp);
            svn_stringbuf_appendcstr(result, temp_str ? temp_str : "");
            s++;
            break;
          case 'C' :
          case 'S' :
            /* SUCCESS wchar_t
             * %S | %C
             */
            add_ch_to_sbuf(s++[0], temp_fmt);
            temp_wct = va_arg(arg_ptr, wchar_t);
            temp_result = apr_psprintf(subpool_temp, temp_fmt->data, temp_wct);
            if(temp_result)
              svn_utf_cstring_from_utf8(&temp_result, temp_result,
                                        subpool_temp);
            svn_stringbuf_appendcstr(result, temp_result ? temp_result : "");
          default :
            /* % followed by a char that has no special format
             * meaning results in that char being printed.
             * So clear out the temp format, store the char
             * following the % in temp_fmt, and append it
             * to result.
             */
            svn_stringbuf_setempty(temp_fmt);
            add_ch_to_sbuf(s++[0], temp_fmt);
            svn_stringbuf_appendcstr(result, temp_fmt->data);
            break;
        } /* End switch statement */

        svn_stringbuf_setempty(temp_fmt);

        } /* End else s[0] != '%' */

  } /* End while(s[0] != '\0') */

  svn_pool_destroy(subpool_temp);
  return result->data;
}


char *
svn_ebcdic_pvsprintf2 (apr_pool_t *p,
                       const char *fmt,
                       va_list ap)
{
  char *return_str = svn_ebcdic_pvsprintf(p, fmt, ap);
  char *return_str_utf8;
  if(svn_utf_cstring_to_utf8(&return_str_utf8, return_str, p))
    return return_str;
  return return_str_utf8;
}


char *
svn_ebcdic_psprintf(apr_pool_t *p,
                    const char *fmt,
                    ...)
{
  char *result;
  va_list ap;
  va_start(ap, fmt);
  result = svn_ebcdic_pvsprintf(p, fmt, ap);
  va_end(ap);
  return result;
}  


char *
svn_ebcdic_psprintf2(apr_pool_t *p,
                     const char *fmt,
                     ...)
{
  char *result;
  va_list ap;
  va_start(ap, fmt);
  result = svn_ebcdic_pvsprintf2(p, fmt, ap);
  va_end(ap);
  return result;
}  


#if AS400
svn_error_t *
svn_ebcdic_set_file_ccsid (const char *path,
                           int ccsid,
                           apr_pool_t *pool)
{
  const char *cmd, *path_native;
  int exit_code;
  SVN_ERR (svn_utf_cstring_from_utf8(&path_native, path, pool));
  cmd = apr_psprintf(pool, "setccsid %d %s", ccsid, path_native);
  exit_code = QzshSystem(cmd);
  if(exit_code)
    return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                             "Attempt to set ccsid of '%s' to '%d' failed " \
                             "with exit code = '%d'",
                             path, ccsid, exit_code);  
  return SVN_NO_ERROR;
}
                           

svn_error_t *
svn_ebcdic_run_unix_type_script (const char *path,
                                 const char *cmd,
                                 const char *const *args,
                                 int *exitcode,
                                 apr_exit_why_e *exitwhy,
                                 svn_boolean_t check_exitcode,
                                 apr_pool_t *pool)
{                                    	
  /* Special handling of hook scripts on iSeries */
  apr_pool_t *temp_subpool = svn_pool_create_ex(pool, NULL);
  svn_stringbuf_t *native_cmd = svn_stringbuf_create("", temp_subpool);
  const char **native_args = NULL;
  if(args)
  {
    apr_size_t args_arr_size = 0;
    apr_size_t i = 0;

    /* Find number of elements in args array */
    while(args[args_arr_size] != NULL)
      args_arr_size++;

    /* Allocate memory for the native_args string array */
    native_args = apr_palloc(temp_subpool, sizeof(char *) * args_arr_size);

    while(args[i] != NULL)
    {
      SVN_ERR(svn_utf_cstring_from_utf8((const char**)(&(native_args[i])),
                                        args[i], temp_subpool));
      svn_stringbuf_appendcstr(native_cmd, "'");
      svn_stringbuf_appendcstr(native_cmd, native_args[i++]);
      svn_stringbuf_appendcstr(native_cmd, "' ");
    }
  }
  
  *exitcode = QzshSystem(native_cmd->data);
  svn_pool_destroy(temp_subpool);
  if (!check_exitcode)
    return SVN_NO_ERROR;  
  else if (WIFEXITED(*exitcode))
  {
    /* WIFEXITED - Evaluates to a nonzero value if the status was returned
     * for a child process that ended normally
     */
    *exitwhy = APR_PROC_EXIT;
    if(*exitcode == 0)
      return NULL;
    else
      return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
                               "Script '%s' returned error exitcode %d",
                               cmd, *exitcode);
  }
  else if (WIFSIGNALED(*exitcode))
  {
    /* WIFSIGNALED - Evaluates to a nonzero value if the status was returned
     * for a child process that ended because of the receipt of a terminating
     * signal that was not caught by the process.
     */
    *exitwhy = APR_PROC_SIGNAL;
    return svn_error_createf
      (SVN_ERR_EXTERNAL_PROGRAM, NULL,
       "Process '%s' failed (exitwhy %d)", cmd, *exitwhy);
  }
  else if (WIFEXCEPTION(*exitcode))
  {
    /* WIFEXCEPTION - Evaluates to a nonzero value if the status was returned
     * for a child process that ended because of an error condition.
     */
    *exitwhy = APR_PROC_EXIT; /* Not sure what to set this to in this
                                 circumstance so this will have to do */
    
    return svn_error_createf(SVN_ERR_EXTERNAL_PROGRAM, NULL,
      "Unable to run script '%s'.  Returned error number =  %d",
      cmd, errno);
  }
}
#endif /* AS400 */

#endif /* APR_CHARSET_EBCDIC */
