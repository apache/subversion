/* temp_serializer.c: serialization functions for caching of FSFS structures
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

#include <apr_pools.h>

#include "svn_pools.h"
#include "svn_hash.h"

#include "id.h"
#include "svn_fs.h"

#include "private/svn_fs_util.h"
#include "private/svn_temp_serializer.h"

#include "temp_serializer.h"

/* Utility to encode a signed NUMBER into a variable-length sequence of 
 * 8-bit chars in KEY_BUFFER and return the last writen position.
 *
 * Numbers will be stored in 7 bits / byte and using byte values above 
 * 32 (' ') to make them combinable with other string by simply separating 
 * individual parts with spaces.
 */
static char*
encode_number(apr_int64_t number, char *key_buffer)
{
  /* encode the sign in the first byte */
  if (number < 0)
  {
    number = -number;
    *key_buffer = (number & 63) + ' ' + 65;
  }
  else
    *key_buffer = (number & 63) + ' ' + 1;
  number /= 64;

  /* write 7 bits / byte until no significant bits are left */
  while (number)
  {
    *++key_buffer = (number & 127) + ' ' + 1;
    number /= 128;
  }

  /* return the last written position */
  return key_buffer;
}

/* Prepend the NUMBER to the STRING in a space efficient way that no other
 * (number,string) combination can produce the same result. 
 * Allocate temporaries as well as the result from POOL.
 */
const char*
svn_fs_fs__combine_number_and_string(apr_int64_t number,
                                     const char *string,
                                     apr_pool_t *pool)
{
  apr_size_t len = strlen(string);

  /* number part requires max. 10x7 bits + 1 space. 
   * Add another 1 for the terminal 0 */
  char *key_buffer = apr_palloc(pool, len + 12);
  const char *key = key_buffer;

  /* Prepend the number to the string and separate them by space. No other 
   * number can result in the same prefix, no other string in the same 
   * postfix nor can the boundary between them be ambiguous. */
  key_buffer = encode_number(number, key_buffer);
  *++key_buffer = ' ';
  memcpy(++key_buffer, string, len+1);

  /* return the start of the key */
  return key;
}

/* Combine the numbers A and B a space efficient way that no other
 * combination of numbers can produce the same result.
 * Allocate temporaries as well as the result from POOL.
 */
const char*
svn_fs_fs__combine_two_numbers(apr_int64_t a,
                               apr_int64_t b,
                               apr_pool_t *pool)
{
  /* encode numbers as 2x 10x7 bits + 1 space + 1 terminating \0*/
  char *key_buffer = apr_palloc(pool, 22);
  const char *key = key_buffer;

  /* combine the numbers. Since the separator is disjoint from any part
   * of the encoded numbers, there is no other combination that can yield
   * the same result */
  key_buffer = encode_number(a, key_buffer);
  *++key_buffer = ' ';
  key_buffer = encode_number(b, ++key_buffer);
  *++key_buffer = '\0';

  /* return the start of the key */
  return key;
}

/* Utility function to serialize string S in the given serialization CONTEXT.
 */
static void
serialize_svn_string(svn_temp_serializer__context_t *context,
                     const svn_string_t * const *s)
{
  const svn_string_t *string = *s;

  /* Nothing to do for NULL string references. */
  if (string == NULL)
    return;

  svn_temp_serializer__push(context,
                            (const void * const *)s,
                            sizeof(*string));

  /* the "string" content may actually be arbitrary binary data.
   * Thus, we cannot use svn_temp_serializer__add_string. */
  svn_temp_serializer__push(context,
                            (const void * const *)&string->data,
                            string->len);

  /* back to the caller's nesting level */
  svn_temp_serializer__pop(context);
  svn_temp_serializer__pop(context);
}

/* Utility function to deserialize the STRING inside the BUFFER.
 */
static void
deserialize_svn_string(void *buffer, const svn_string_t **string)
{
  if (*string == NULL)
    return;

  svn_temp_deserializer__resolve(buffer, (void **)string);
  svn_temp_deserializer__resolve(buffer, (void **)&(*string)->data);
}


/* Utility function to serialize COUNT svn_txdelta_op_t objects 
 * at OPS in the given serialization CONTEXT.
 */
static void
serialize_txdelta_ops(svn_temp_serializer__context_t *context,
                      const svn_txdelta_op_t * const * ops,
                      apr_size_t count)
{
  if (*ops == NULL)
    return;

  /* the ops form a simple chunk of memory with no further references */
  svn_temp_serializer__push(context,
                            (const void * const *)ops,
                            sizeof(svn_txdelta_op_t[count]));
  svn_temp_serializer__pop(context);
}

/* Utility function to serialize W in the given serialization CONTEXT.
 */
static void
serialize_txdeltawindow(svn_temp_serializer__context_t *context,
                        svn_txdelta_window_t * const * w)
{
  svn_txdelta_window_t *window = *w;

  /* serialize the window struct itself */
  svn_temp_serializer__push(context,
                            (const void * const *)w,
                            sizeof(svn_txdelta_window_t));

  /* serialize its sub-structures */
  serialize_txdelta_ops(context, &window->ops, window->num_ops);
  serialize_svn_string(context, &window->new_data);

  svn_temp_serializer__pop(context);
}

/* Implements serialize_fn_t for svn_fs_fs__txdelta_cached_window_t 
 */
svn_error_t *
svn_fs_fs__serialize_txdelta_window(char **buffer,
                                    apr_size_t *buffer_size,
                                    void *item,
                                    apr_pool_t *pool)
{
  svn_fs_fs__txdelta_cached_window_t *window_info = item;
  svn_stringbuf_t *serialized;

  /* initialize the serialization process and allocate a buffer large
   * enough to do without the need of re-allocations in most cases. */
  apr_size_t text_len = window_info->window->new_data
                      ? window_info->window->new_data->len
                      : 0;
  svn_temp_serializer__context_t *context =
      svn_temp_serializer__init(window_info,
                                sizeof(*window_info),
                                500 + text_len,
                                pool);

  /* serialize the sub-structure(s) */
  serialize_txdeltawindow(context, &window_info->window);

  /* return the serialized result */
  serialized = svn_temp_serializer__get(context);

  *buffer = serialized->data;
  *buffer_size = serialized->len;

  return SVN_NO_ERROR;
}

/* Implements deserialize_fn_t for svn_fs_fs__txdelta_cached_window_t.
 */
svn_error_t *
svn_fs_fs__deserialize_txdelta_window(void **item,
                                      const char *buffer,
                                      apr_size_t buffer_size,
                                      apr_pool_t *pool)
{
  /* Copy the _full_ buffer as it also contains the sub-structures. */
  svn_fs_fs__txdelta_cached_window_t *window_info =
      apr_palloc(pool, buffer_size);

  memcpy(window_info, buffer, buffer_size);

  /* pointer reference fixup */
  svn_temp_deserializer__resolve(window_info,
                                 (void **)&window_info->window);
  svn_temp_deserializer__resolve(window_info,
                                 (void **)&window_info->window->ops);

  deserialize_svn_string(window_info, &window_info->window->new_data);

  /* done */
  *item = window_info;

  return SVN_NO_ERROR;
}

