/*
 * win32_xlate.c : Windows xlate stuff.
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */

/* prevent "empty compilation unit" warning on e.g. UNIX */
typedef int win32_xlate__dummy;

#ifdef WIN32

/* Define _WIN32_DCOM for CoInitializeEx(). */
#define _WIN32_DCOM

/* We must include windows.h ourselves or apr.h includes it for us with
   many ignore options set. Including Winsock is required to resolve IPv6
   compilation errors. APR_HAVE_IPV6 is only defined after including
   apr.h, so we can't detect this case here. */

/* winsock2.h includes windows.h */
#include <winsock2.h>
#include <Ws2tcpip.h>
#include <mlang.h>

#include <apr.h>
#include <apr_errno.h>
#include <apr_portable.h>

#include "svn_pools.h"
#include "svn_string.h"
#include "svn_utf.h"
#include "private/svn_atomic.h"
#include "private/svn_subr_private.h"

#include "win32_xlate.h"

#include "svn_private_config.h"

#define UTF16BE 1201
#define UTF32BE 12001

static svn_atomic_t com_initialized = 0;

/* Initializes COM and keeps COM available until process exit.
   Implements svn_atomic__init_once init_func */
static svn_error_t *
initialize_com(void *baton, apr_pool_t* pool)
{
  /* Try to initialize for apartment-threaded object concurrency. */
  HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

  if (hr == RPC_E_CHANGED_MODE)
    {
      /* COM already initalized for multi-threaded object concurrency. We are
         neutral to object concurrency so try to initalize it in the same way
         for us, to keep an handle open. */
      hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    }

  if (FAILED(hr))
    return svn_error_create(APR_EGENERAL, NULL, NULL);

  return SVN_NO_ERROR;
}

typedef struct win32_xlate_t
{
  UINT from_page_id;
  UINT to_page_id;
} win32_xlate_t;

static apr_status_t
get_page_id_from_name(UINT *page_id_p, const char *page_name, apr_pool_t *pool)
{
  IMultiLanguage * mlang = NULL;
  HRESULT hr;
  MIMECSETINFO page_info;
  WCHAR ucs2_page_name[128];
  svn_error_t *err;

  if (page_name == SVN_APR_DEFAULT_CHARSET)
    {
        *page_id_p = CP_ACP;
        return APR_SUCCESS;
    }
  else if (page_name == SVN_APR_LOCALE_CHARSET)
    {
      *page_id_p = CP_THREAD_ACP; /* Valid on Windows 2000+ */
      return APR_SUCCESS;
    }
  else if (!strcmp(page_name, "UTF-8"))
    {
      *page_id_p = CP_UTF8;
      return APR_SUCCESS;
    }
  else if (!strcmp(page_name, "ISO-10646-UCS-2"))
    {
      *page_id_p = UTF16BE; /* UTF-16 Big Endian, strictly speaking it isn't
                               exactly UCS-2 Big Endian but it's a superset
                               so it works well enough. */
      return APR_SUCCESS;
    }
  else if (!strcmp(page_name, "ISO-10646-UCS-4"))
    {
      *page_id_p = UTF32BE; /* UTF-32 Big Endian, again, it isn't strictly
                               speaking UCS-4 Big Endian, but it's a superset
                               so it works well enough. */
      return APR_SUCCESS;
    }

  /* Use codepage identifier nnn if the codepage name is in the form
     of "CPnnn".
     We need this code since apr_os_locale_encoding() and svn_cmdline_init()
     generates such codepage names even if they are not valid IANA charset
     name. */
  if ((page_name[0] == 'c' || page_name[0] == 'C')
      && (page_name[1] == 'p' || page_name[1] == 'P'))
    {
      *page_id_p = atoi(page_name + 2);
      return APR_SUCCESS;
    }

  err = svn_atomic__init_once(&com_initialized, initialize_com, NULL, pool);
  if (err)
    {
      apr_status_t saved = err->apr_err;
      svn_error_clear(err);
      return saved; /* probably SVN_ERR_ATOMIC_INIT_FAILURE */
    }

  hr = CoCreateInstance(&CLSID_CMultiLanguage, NULL, CLSCTX_INPROC_SERVER,
                        &IID_IMultiLanguage, (void **) &mlang);

  if (FAILED(hr))
    return APR_EGENERAL;

  /* Convert page name to wide string. */
  MultiByteToWideChar(CP_UTF8, 0, page_name, -1, ucs2_page_name,
                      sizeof(ucs2_page_name) / sizeof(ucs2_page_name[0]));
  memset(&page_info, 0, sizeof(page_info));
  hr = mlang->lpVtbl->GetCharsetInfo(mlang, ucs2_page_name, &page_info);
  if (FAILED(hr))
    {
      mlang->lpVtbl->Release(mlang);
      return APR_EINVAL;
    }

  if (page_info.uiInternetEncoding)
    *page_id_p = page_info.uiInternetEncoding;
  else
    *page_id_p = page_info.uiCodePage;

  mlang->lpVtbl->Release(mlang);

  return APR_SUCCESS;
}

apr_status_t
svn_subr__win32_xlate_open(win32_xlate_t **xlate_p, const char *topage,
                           const char *frompage, apr_pool_t *pool)
{
  UINT from_page_id, to_page_id;
  apr_status_t apr_err = APR_SUCCESS;
  win32_xlate_t *xlate;

  apr_err = get_page_id_from_name(&to_page_id, topage, pool);
  if (apr_err == APR_SUCCESS)
    apr_err = get_page_id_from_name(&from_page_id, frompage, pool);

  if (apr_err == APR_SUCCESS)
    {
      xlate = apr_palloc(pool, sizeof(*xlate));
      xlate->from_page_id = from_page_id;
      xlate->to_page_id = to_page_id;

      *xlate_p = xlate;
    }

  return apr_err;
}

apr_status_t
svn_subr__win32_xlate_to_stringbuf(win32_xlate_t *handle,
                                   const char *src_data,
                                   apr_size_t src_length,
                                   svn_stringbuf_t **dest,
                                   apr_pool_t *pool)
{
  WCHAR * wide_str;
  int retval, wide_size;

  if (src_length == 0)
    {
      *dest = svn_stringbuf_create_empty(pool);
      return APR_SUCCESS;
    }

  /* Convert from current encoding to UTF-16 Little Endian */
  if (handle->from_page_id == UTF16BE)
    {
      int w, s;
 
      /* Incoming length needs to enough for wide characters */ 
      if (src_length % 2)
        return APR_EINVAL;
   
      /* Calcuate the size and allocate the memory for the UTF-16 LE */ 
      wide_size = src_length / 2;
      if (wide_size <= MAX_PATH)
        {
          wide_str = alloca(wide_size * sizeof(WCHAR));
        } 
      else
        {
          wide_str = apr_palloc(pool, wide_size * sizeof(WCHAR));
        }

      /* Since we're converting from UTF-16 Big Endian just swap
       * bytes and copy */
      for (w = 0; w < wide_size; w++)
        {
          s = w * 2; /* position in src_data for start of character's 2 bytes */
          wide_str[w] = ((src_data[s] & 0xFF) << 8) | (src_data[s + 1] & 0xFF);
        }
    }
  else if (handle->from_page_id == UTF32BE)
    {
      int s, c, w = 0, surrogates = 0;

      /* Incoming length needs to be enough for 4 bytes per char */
      if (src_length % 4)
        return APR_EINVAL;

      /* Calculate the size and allocate the memory for UTF-16 LE */
      wide_size = src_length / 4;
      /* Look for characters that won't fit in 16 bits, add a character for
       * the surrogate, note that the documentation for WideCharToMultiByte
       * says that wide_size should be the number of characters, however that's
       * not entirely true, it was true when it only supported UCS-2, but since
       * Windows 2000 it supports UTF-16 but the count is the number of WCHARs
       * which doesn't exactly map to characters since surrogates take up 2
       * WCHARs for one character. TODO: Should we fail here if this isn't
       * at least Windows 2000 since it won't know what to do with the
       * surrogates? */
      for (s = 0; s < wide_size * 4; s += 4)
        {
          if ((src_data[s] & 0xFF) != 0 || (src_data[s + 1] & 0xFF) != 0)
            surrogates++;
        }
      wide_size += surrogates;
      if (wide_size <= MAX_PATH)
        {
          wide_str = alloca(wide_size * sizeof(WCHAR));
        }
      else
        {
          wide_str = apr_palloc(pool, wide_size * sizeof(WCHAR));
        }

      /* Since we're converting from UTF-32 Big Endian we have to swap bytes
       * and deal with converting unicode values that won't fit into 16-bits to
       * use surrogates, iterate by actual characters */
      for (c = 0; c < (wide_size - surrogates); c++)
        {
          s = c * 4; /* position in src_data for start of character's 4 bytes */
          if ((src_data[s] & 0xFF) == 0 && (src_data[s + 1] & 0xFF) == 0)
            {
              /* easy case it fits in UTF-16 just have to swap byte order */
              wide_str[w] = (src_data[s + 2] & 0xFF) << 8;
              wide_str[w++] |= src_data[s + 3] & 0xFF;
            }
          else
            {
              /* character too wide for going straight to UTF-16 have to use
               * a surrogate pair, so first we swap characters to get a
               * Little Endian long */
              unsigned long ch = ((src_data[s] & 0xFF) << 24);
              ch |= ((src_data[s + 1] & 0xFF) << 16);
              ch |= ((src_data[s + 2] & 0xFF) << 8);
              ch |= (src_data[s + 3] & 0xFF);
              /* now calculate the surrogates */
              ch -= 0x10000;
              wide_str[w++] = (WCHAR) (0xD800 + ((ch & 0xFFC00) >> 10));
              wide_str[w++] = (WCHAR) (0xDC00 + (ch & 0x3FF));
            }
        }
    }
  else
    {
      /* Other encodings that hopefully Windows knows how to convert to
       * UTF-16 Little Endian */ 
      retval = MultiByteToWideChar(handle->from_page_id, 0, src_data,
                                   src_length, NULL, 0);
      if (retval == 0)
        return apr_get_os_error();

    wide_size = retval;

    /* Allocate temporary buffer for small strings on stack instead of heap. */
    if (wide_size <= MAX_PATH)
      {
        wide_str = alloca(wide_size * sizeof(WCHAR));
      }
    else
      {
        wide_str = apr_palloc(pool, wide_size * sizeof(WCHAR));
      }

      retval = MultiByteToWideChar(handle->from_page_id, 0, src_data,
                                   src_length, wide_str, wide_size);

      if (retval == 0)
        return apr_get_os_error();
    }

  /* Determine how much space we'll require to store the converted string */
  retval = WideCharToMultiByte(handle->to_page_id, 0, wide_str, wide_size,
                               NULL, 0, NULL, NULL);

  if (retval == 0)
    return apr_get_os_error();

  /* Ensure that buffer is enough to hold result string and termination
     character. */
  *dest = svn_stringbuf_create_ensure(retval + 1, pool);
  (*dest)->len = retval;

  /* Convert from UTF-16 Little Endian to the desired encoding */
  retval = WideCharToMultiByte(handle->to_page_id, 0, wide_str, wide_size,
                               (*dest)->data, (*dest)->len, NULL, NULL);
  if (retval == 0)
    return apr_get_os_error();

  (*dest)->len = retval;
  return APR_SUCCESS;
}

#endif /* WIN32 */
