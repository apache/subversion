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
 * @file svn_utf.h
 * @brief UTF-8 conversion routines
 */



#ifndef SVN_UTF_H
#define SVN_UTF_H

#include <apr_xlate.h>
#include <apr_lib.h>

#include "svn_error.h"
#include "svn_string.h"

#if AS400
  /* This is defined in fs_loader.c by configure on platforms which
   * use configure, but we need to define a fallback for the iSeries.
   * As the iSeries port presumes no bdb support, fsfs it is.
   */
  #define OS400_UTF8_CCSID (int)1208
  #define OS400_NATIVE_CCSID (int)0
#endif

/* ASCII hex escaped symbolic constants for commonly
 * used string and char literals */
 
/* Whitespace */
#define SVN_UTF8_CR              '\x0D' /* '\r' */
#define SVN_UTF8_CR_STR          "\x0D" /* "\r" */
#define SVN_UTF8_FF              '\x0C' /* '\f' */
#define SVN_UTF8_FF_STR          "\x0C" /* "\f" */
#define SVN_UTF8_NEWLINE         '\x0A' /* '\n' */
#define SVN_UTF8_NEWLINE_STR     "\x0A" /* "\n" */
#define SVN_UTF8_SPACE           '\x20' /* ' '  */
#define SVN_UTF8_SPACE_STR       "\x20" /* " "  */
#define SVN_UTF8_TAB             '\x09' /* '\t' */
#define SVN_UTF8_TAB_STR         "\x09" /* "\t" */
#define SVN_UTF8_VTAB            '\x0B' /* '\v' */
#define SVN_UTF8_VTAB_STR        "\x0B" /* "\v" */
/* Symbols */
#define SVN_UTF8_AMP             '\x26' /* '&' */
#define SVN_UTF8_ASTERISK        '\x2A' /* '*' */
#define SVN_UTF8_ASTERISK_STR    "\x2A" /* "*" */
#define SVN_UTF8_AT              '\x40' /* '@' */
#define SVN_UTF8_AT_STR          "\x40" /* "@" */
#define SVN_UTF8_BSLASH          '\x5C' /* '\' */
#define SVN_UTF8_COMMA           '\x2C' /* ',' */
#define SVN_UTF8_COMMA_STR       "\x2C" /* "," */
#define SVN_UTF8_COLON           '\x3A' /* ':' */
#define SVN_UTF8_COLON_STR       "\x3A" /* ":" */
#define SVN_UTF8_DOT             '\x2E' /* '.' */
#define SVN_UTF8_DOT_STR         "\x2E" /* "." */
#define SVN_UTF8_DQUOTE          '\x22' /* '"' */
#define SVN_UTF8_DQUOTE_STR      "\x22" /* "\"" */
#define SVN_UTF8_EQUALS          '\x3D' /* '=' */
#define SVN_UTF8_EQUALS_STR      "\x3D" /* "=" */
#define SVN_UTF8_EXCLAMATION     '\x21' /* '!' */
#define SVN_UTF8_EXCLAMATION_STR "\x21" /* "!" */
#define SVN_UTF8_FSLASH          '\x2F' /* '/' */
#define SVN_UTF8_FSLASH_STR      "\x2F" /* "/" */
#define SVN_UTF8_GT              '\x3E' /* '>' */
#define SVN_UTF8_GT_STR          "\x3E" /* ">" */
#define SVN_UTF8_LBRACKET        '\x5B' /* '[' */
#define SVN_UTF8_LPAREN          '\x28' /* '(' */
#define SVN_UTF8_LPAREN_STR      "\x28" /* "(" */
#define SVN_UTF8_LT              '\x3C' /* '<' */
#define SVN_UTF8_LT_STR          "\x3C" /* "<" */
#define SVN_UTF8_MINUS           '\x2D' /* '-' */
#define SVN_UTF8_MINUS_STR       "\x2D" /* "-" */
#define SVN_UTF8_PERCENT         '\x25' /* '%' */
#define SVN_UTF8_PERCENT_STR     "\x25" /* "%"  */
#define SVN_UTF8_PIPE            '\x7C' /* '|' */
#define SVN_UTF8_PIPE_STR        "\x7C" /* "|"  */
#define SVN_UTF8_PLUS            '\x2B' /* '+' */
#define SVN_UTF8_PLUS_STR        "\x2B" /* "+" */
#define SVN_UTF8_POUND           '\x23' /* '#' */
#define SVN_UTF8_QUESTION        '\x3F' /* '?' */
#define SVN_UTF8_RBRACKET        '\x5D' /* ']' */
#define SVN_UTF8_RPAREN          '\x29' /* ')' */
#define SVN_UTF8_RPAREN_STR      "\x29" /* ")" */
#define SVN_UTF8_SQUOTE          '\x27' /* '\'' */
#define SVN_UTF8_SQUOTE_STR      "\x27" /* "\'" */
#define SVN_UTF8_UNDERSCORE      '\x5F' /* '_' */
#define SVN_UTF8_UNDERSCORE_STR  "\x5F" /* "_" */
/* Alphas */
#define SVN_UTF8_A               '\x41' /* 'A' */
#define SVN_UTF8_B               '\x42' /* 'B' */
#define SVN_UTF8_C               '\x43' /* ''C */
#define SVN_UTF8_D               '\x44' /* 'D' */
#define SVN_UTF8_E               '\x45' /* 'E' */
#define SVN_UTF8_F               '\x46' /* 'F' */
#define SVN_UTF8_G               '\x47' /* 'G' */
#define SVN_UTF8_H               '\x48' /* 'H' */
#define SVN_UTF8_I               '\x49' /* 'I' */
#define SVN_UTF8_J               '\x4A' /* 'J' */
#define SVN_UTF8_K               '\x4B' /* 'K' */
#define SVN_UTF8_L               '\x4C' /* 'L' */
#define SVN_UTF8_M               '\x4D' /* 'M' */
#define SVN_UTF8_N               '\x4E' /* 'N' */
#define SVN_UTF8_O               '\x4F' /* 'O' */
#define SVN_UTF8_P               '\x50' /* 'P' */
#define SVN_UTF8_Q               '\x51' /* 'Q' */
#define SVN_UTF8_R               '\x52' /* 'R' */
#define SVN_UTF8_S               '\x53' /* 'S' */
#define SVN_UTF8_T               '\x54' /* 'T' */
#define SVN_UTF8_U               '\x55' /* 'U' */
#define SVN_UTF8_V               '\x56' /* 'V' */
#define SVN_UTF8_W               '\x57' /* 'W' */
#define SVN_UTF8_X               '\x58' /* 'X' */
#define SVN_UTF8_Y               '\x59' /* 'Y' */
#define SVN_UTF8_Z               '\x5A' /* 'Z' */
#define SVN_UTF8_a               '\x61' /* 'a' */
#define SVN_UTF8_b               '\x62' /* 'b' */
#define SVN_UTF8_c               '\x63' /* 'c' */
#define SVN_UTF8_d               '\x64' /* 'd' */
#define SVN_UTF8_e               '\x65' /* 'e' */
#define SVN_UTF8_f               '\x66' /* 'f' */
#define SVN_UTF8_g               '\x67' /* 'g' */
#define SVN_UTF8_h               '\x68' /* 'h' */
#define SVN_UTF8_i               '\x69' /* 'i' */
#define SVN_UTF8_j               '\x6A' /* 'j' */
#define SVN_UTF8_k               '\x6B' /* 'k' */
#define SVN_UTF8_l               '\x6C' /* 'l' */
#define SVN_UTF8_m               '\x6D' /* 'm' */
#define SVN_UTF8_n               '\x6E' /* 'n' */
#define SVN_UTF8_o               '\x6F' /* 'o' */
#define SVN_UTF8_p               '\x70' /* 'p' */
#define SVN_UTF8_q               '\x71' /* 'q' */
#define SVN_UTF8_r               '\x72' /* 'r' */
#define SVN_UTF8_s               '\x73' /* 's' */
#define SVN_UTF8_t               '\x74' /* 't' */
#define SVN_UTF8_u               '\x75' /* 'u' */
#define SVN_UTF8_v               '\x76' /* 'v' */
#define SVN_UTF8_w               '\x77' /* 'w' */
#define SVN_UTF8_x               '\x78' /* 'x' */
#define SVN_UTF8_y               '\x79' /* 'y' */
#define SVN_UTF8_z               '\x7A' /* 'z' */
/* Numerics */
#define SVN_UTF8_0               '\x30' /* '0' */
#define SVN_UTF8_0_STR           "\x30" /* "0" */
#define SVN_UTF8_1               '\x31' /* '1' */
#define SVN_UTF8_1_STR           "\x31" /* "1" */
#define SVN_UTF8_2               '\x32' /* '2' */
#define SVN_UTF8_2_STR           "\x32" /* "2" */
#define SVN_UTF8_3               '\x33' /* '3' */
#define SVN_UTF8_3_STR           "\x33" /* "3" */
#define SVN_UTF8_4               '\x34' /* '4' */
#define SVN_UTF8_4_STR           "\x34" /* "4" */
#define SVN_UTF8_5               '\x35' /* '5' */
#define SVN_UTF8_5_STR           "\x35" /* "5" */
#define SVN_UTF8_6               '\x36' /* '6' */
#define SVN_UTF8_6_STR           "\x36" /* "6" */
#define SVN_UTF8_7               '\x37' /* '7' */
#define SVN_UTF8_7_STR           "\x37" /* "7" */
#define SVN_UTF8_8               '\x38' /* '8' */
#define SVN_UTF8_8_STR           "\x38" /* "8" */
#define SVN_UTF8_9               '\x39' /* '9' */
#define SVN_UTF8_9_STR           "\x39" /* "9" */

#if !APR_CHARSET_EBCDIC
  #define APR_IS_ASCII_ALPHA(x) apr_isalpha(x)
  #define APR_IS_ASCII_ALNUM(x) apr_isalnum(x)
  #define APR_IS_ASCII_DIGIT(x) apr_isdigit(x)
  #define APR_IS_ASCII_SPACE(x) apr_isspace(x)
  #define APR_IS_ASCII_XDIGIT(x) apr_isxdigit(x)
  #define TO_ASCII_LOWER(x) tolower(x)
#else
/* Some helpful ascii aware macros to replace apr_isalpha, apr_isdigit,
 * apr_isspace, apr_isxdigit, and tolower on ebcdic platforms.
 */
  #define APR_IS_ASCII_ALPHA(x) \
    (((unsigned char)x >= SVN_UTF8_a && (unsigned char)x <= SVN_UTF8_z) || \
     ((unsigned char)x >= SVN_UTF8_A && (unsigned char)x <= SVN_UTF8_Z)) 
      
  #define APR_IS_ASCII_DIGIT(x) ( (unsigned char)x >= SVN_UTF8_0 && \
                                  (unsigned char)x <= SVN_UTF8_9 )
  
  #define APR_IS_ASCII_ALNUM(x) \
    (((unsigned char)x >= SVN_UTF8_0 && (unsigned char)x <= SVN_UTF8_9) ||  \
     ((unsigned char)x >= SVN_UTF8_a && (unsigned char)x <= SVN_UTF8_z) ||  \
     ((unsigned char)x >= SVN_UTF8_A && (unsigned char)x <= SVN_UTF8_Z))
                               
  #define APR_IS_ASCII_SPACE(x) ( (unsigned char)x == SVN_UTF8_SPACE   || \
                                  (unsigned char)x == SVN_UTF8_TAB     || \
                                  (unsigned char)x == SVN_UTF8_VTAB    || \
                                  (unsigned char)x == SVN_UTF8_FF      || \
                                  (unsigned char)x == SVN_UTF8_NEWLINE || \
                                  (unsigned char)x == SVN_UTF8_CR )
                                 
  #define APR_IS_ASCII_XDIGIT(x) \
    (((unsigned char)x >= SVN_UTF8_0 && (unsigned char)x <= SVN_UTF8_9) || \
     ((unsigned char)x >= SVN_UTF8_a && (unsigned char)x <= SVN_UTF8_f) || \
     ((unsigned char)x >= SVN_UTF8_A && (unsigned char)x <= SVN_UTF8_F)) 
     
  #define TO_ASCII_LOWER(x) ( ( (unsigned char)x < SVN_UTF8_A ||  \
                                (unsigned char)x > SVN_UTF8_Z ) ? \
                                 x : x + 32 ) 
#endif

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/**
 * @since New in 1.1.
 *
 * Initialize the UTF-8 encoding/decoding routines.
 * Allocate cached translation handles in a subpool of @a pool.
 *
 * @note It is optional to call this function, but if it is used, no other
 * svn function may be in use in other threads during the call of this
 * function or when @a pool is cleared or destroyed.
 * Initializing the UTF-8 routines will improve performance.
 */
void svn_utf_initialize (apr_pool_t *pool);

/** Set @a *dest to a utf8-encoded stringbuf from native stringbuf @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_stringbuf_to_utf8 (svn_stringbuf_t **dest,
                                        const svn_stringbuf_t *src,
                                        apr_pool_t *pool);


/** Set @a *dest to a utf8-encoded string from native string @a src; allocate
 * @a *dest in @a pool.
 */
svn_error_t *svn_utf_string_to_utf8 (const svn_string_t **dest,
                                     const svn_string_t *src,
                                     apr_pool_t *pool);


/** Set @a *dest to a utf8-encoded C string from native C string @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_cstring_to_utf8 (const char **dest,
                                      const char *src,
                                      apr_pool_t *pool);


/** Set @a *dest to a utf8-encoded C string from @a frompage C string
 * @a src; allocate @a *dest in @a pool.  Use @a convset_key as the
 * cache key for the charset converter; if it's NULL, don't cache the
 * converter.
 */
svn_error_t *svn_utf_cstring_to_utf8_ex (const char **dest,
                                         const char *src,
                                         const char *frompage,
                                         const char *convset_key,
                                         apr_pool_t *pool);


/** Set @a *dest to a natively-encoded stringbuf from utf8 stringbuf @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_stringbuf_from_utf8 (svn_stringbuf_t **dest,
                                          const svn_stringbuf_t *src,
                                          apr_pool_t *pool);


/** Set @a *dest to a natively-encoded string from utf8 string @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_string_from_utf8 (const svn_string_t **dest,
                                       const svn_string_t *src,
                                       apr_pool_t *pool);


/** Set @a *dest to a natively-encoded C string from utf8 C string @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_cstring_from_utf8 (const char **dest,
                                        const char *src,
                                        apr_pool_t *pool);


/** Set @a *dest to a @a frompage encoded C string from utf8 C string
 * @a src; allocate @a *dest in @a pool.  Use @a convset_key as the
 * cache key for the charset converter; if it's NULL, don't cache the
 * converter.
 */
svn_error_t *svn_utf_cstring_from_utf8_ex (const char **dest,
                                           const char *src,
                                           const char *topage,
                                           const char *convset_key,
                                           apr_pool_t *pool);


/** Return a fuzzily native-encoded C string from utf8 C string @a src,
 * allocated in @a pool.  A fuzzy recoding leaves all 7-bit ascii
 * characters the same, and substitutes "?\\XXX" for others, where XXX
 * is the unsigned decimal code for that character.
 *
 * This function cannot error; it is guaranteed to return something.
 * First it will recode as described above and then attempt to convert
 * the (new) 7-bit UTF-8 string to native encoding.  If that fails, it
 * will return the raw fuzzily recoded string, which may or may not be
 * meaningful in the client's locale, but is (presumably) better than
 * nothing.
 *
 * ### Notes:
 *
 * Improvement is possible, even imminent.  The original problem was
 * that if you converted a UTF-8 string (say, a log message) into a
 * locale that couldn't represent all the characters, you'd just get a
 * static placeholder saying "[unconvertible log message]".  Then
 * Justin Erenkrantz pointed out how on platforms that didn't support
 * conversion at all, "svn log" would still fail completely when it
 * encountered unconvertible data.
 *
 * Now for both cases, the caller can at least fall back on this
 * function, which converts the message as best it can, substituting
 * ?\\XXX escape codes for the non-ascii characters.
 *
 * Ultimately, some callers may prefer the iconv "//TRANSLIT" option,
 * so when we can detect that at configure time, things will change.
 * Also, this should (?) be moved to apr/apu eventually.
 *
 * See http://subversion.tigris.org/issues/show_bug.cgi?id=807 for
 * details.
 */
const char *svn_utf_cstring_from_utf8_fuzzy (const char *src,
                                             apr_pool_t *pool);


/** Set @a *dest to a natively-encoded C string from utf8 stringbuf @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_cstring_from_utf8_stringbuf (const char **dest,
                                                  const svn_stringbuf_t *src,
                                                  apr_pool_t *pool);


/** Set @a *dest to a natively-encoded C string from utf8 string @a src;
 * allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_cstring_from_utf8_string (const char **dest,
                                               const svn_string_t *src,
                                               apr_pool_t *pool);

#if APR_CHARSET_EBCDIC
/** Set @a *dest to a utf-8-preserving ebcdic string from utf8 encoded string
 *  @a src;  allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_string_from_netccsid (const svn_string_t **dest,
                                           const svn_string_t *src,
                                           apr_pool_t *pool);
                                           

/** Set @a *dest to a utf-8-preserving ebcdic C string from utf8 encoded 
 *  C string @a src; allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_cstring_from_netccsid (const char **dest,
                                            const char *src,
                                            apr_pool_t *pool);
                                            
                                              
/** Set @a *dest to a utf-8 string from the utf-8-preserving ebcdic encoded
 *  string @a src; allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_string_to_netccsid (const svn_string_t **dest,
                                         const svn_string_t *src,
                                         apr_pool_t *pool);  
                                           

/** Set @a *dest to a utf-8 C string from the utf-8-preserving ebcdic encoded 
 *  C string @a src; allocate @a *dest in @a pool.
 */
svn_error_t *svn_utf_cstring_to_netccsid (const char **dest,
                                          const char *src,
                                          apr_pool_t *pool);
                                          
/* Return TRUE if @a src of length @a len is a valid UTF-8 encoding
 * according to the rules laid down by the Unicode 4.0 standard, FALSE
 * otherwise.
 */
svn_boolean_t svn_utf_is_valid_utf (const char *src, apr_size_t len);                                          
#endif /* APR_CHARSET_EBCDIC */
                                          
#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_UTF_H */
