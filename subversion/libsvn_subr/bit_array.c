/*
 * bit_array.c :  implement a simple packed bit array
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


#include "svn_sorts.h"
#include "private/svn_subr_private.h"

struct svn_bit_array__t
{
  /* Data buffer of DATA_SIZE bytes.  Never NULL. */
  unsigned char *data;

  /* Number of bytes allocated to DATA.  Never shrinks. */
  apr_size_t data_size;

  /* Reallocate DATA form this POOL when growing. */
  apr_pool_t *pool;
};

/* Given that MAX shall be an actual bit index in a packed bit array,
 * return the number of bytes to allocate for the data buffer. */
static apr_size_t
select_data_size(apr_size_t max)
{
  /* We allocate a power of two of bytes but at least 16 bytes */
  apr_size_t size = 16;

  /* Caution: MAX / 8 == SIZE still means that MAX is out of bounds.
   * OTOH, 2*(MAX/8) is always within the value range of APR_SIZE_T. */
  while (size <= max / 8)
    size *= 2;

  return size;
}

svn_bit_array__t *
svn_bit_array__create(apr_size_t max,
                      apr_pool_t *pool)
{
  svn_bit_array__t *array = apr_pcalloc(pool, sizeof(*array));

  array->data_size = select_data_size(max);
  array->pool = pool;
  array->data = apr_pcalloc(pool, array->data_size);

  return array;
}

void
svn_bit_array__set(svn_bit_array__t *array,
                   apr_size_t idx,
                   svn_boolean_t value)
{
  /* If IDX is outside the allocated range, we _may_ have to grow it.
   *
   * Be sure to use division instead of multiplication as we need to cover
   * the full value range of APR_SIZE_T for the bit indexes.
   */
  if (idx / 8 >= array->data_size)
    {
      apr_size_t new_size;
      unsigned char *new_data;

      /* Unallocated indexes are implicitly 0, so no actual allocation
       * required in that case.
       */
      if (!value)
        return;

      /* Grow data buffer to cover IDX.
       * Clear the new buffer to guarantee our array[idx]==0 default.
       */
      new_size = select_data_size(idx);
      new_data = apr_pcalloc(array->pool, new_size);
      memcpy(new_data, array->data, array->data_size);
      array->data = new_data;
      array->data_size = new_size;
    }

  /* IDX is covered by ARRAY->DATA now. */

  /* Set / reset one bit.  Be sure to use unsigned shifts. */
  if (value)
    array->data[idx / 8] |= (unsigned char)(1u << (idx % 8));
  else
    array->data[idx / 8] &= (unsigned char)(255u - (1u << (idx % 8)));
}

svn_boolean_t
svn_bit_array__get(svn_bit_array__t *array,
                   apr_size_t idx)
{
  /* Indexes outside the allocated range are implictly 0. */
  if (idx / 8 >= array->data_size)
    return 0;

  /* Extract one bit (get the byte, shift bit to LSB, extract it). */
  return (array->data[idx / 8] >> (idx % 8)) & 1;
}

