/**
 * @copyright
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
 * @endcopyright
 */

#ifndef SVNXX_PRIVATE_APRWRAP_ARRAY_H
#define SVNXX_PRIVATE_APRWRAP_ARRAY_H

#include <stdexcept>

#include "pool.hpp"

#include <apr_tables.h>

namespace apache {
namespace subversion {
namespace svnxx {
namespace apr {

/**
 * @brief Proxy for an APR array.
 *
 * This class does not own the array. The array's lifetime is tied to
 * its pool. The caller is responsible for making sure that the
 * array's lifetime is longer than this proxy object's.
 */
template<typename T> class array
{
public:
  using value_type = T;
  using size_type = int;
  using iterator = value_type*;
  using const_iterator = const value_type*;

  /**
   * Create and proxy a new APR array allocated from @a result_pool.
   * Reserve space for @a nelts array elements.
   */
  explicit array(const pool& result_pool, size_type nelts = 0)
    : proxied(apr_array_make(result_pool.get(), nelts, sizeof(value_type)))
    {}

  /**
   * Create a new proxy for the APR array @a array_.
   */
  explicit array(apr_array_header_t* array_)
    : proxied(array_)
    {
      if (proxied->elt_size != sizeof(value_type))
        throw std::invalid_argument("apr::array element size mismatch");
    }

  /**
   * @return The wrapped APR array.
   */
  apr_array_header_t* get_array() const noexcept
    {
      return proxied;
    }

  /**
   * @return the number of elements in the wrapped APR array.
   */
  size_type size() const noexcept
    {
      return proxied->nelts;
    }

  /**
   * @return the reserved space in the wrapped APR array.
   */
  size_type capacity() const noexcept
    {
      return proxied->nalloc;
    }

  /**
   * @return An immutable reference to the array element at @a index.
   */
  const value_type& operator[](size_type index) const noexcept
    {
      return APR_ARRAY_IDX(proxied, index, value_type);
    }

  /**
   * @return An immutable reference to the array element at @a index.
   * Like operator[] but perfoms a range check on the index.
   */
  const value_type& at(size_type index) const
    {
      if (index < 0 || index >= size())
        throw std::out_of_range("apr::array index out of range");
      return (*this)[index];
    }

  /**
   * @return A mutable reference to the array element at @a index.
   */
  value_type& operator[](size_type index) noexcept
    {
      return APR_ARRAY_IDX(proxied, index, value_type);
    }

  /**
   * @return A mutable reference to the array element at @a index.
   * Like operator[] but perfoms a range check on the index.
   */
  value_type& at(size_type index)
    {
      if (index < 0 || index >= size())
        throw std::out_of_range("apr::array index out of range");
      return (*this)[index];
    }

  /**
   * Push @a value onto the end of the APR array.
   */
  void push(const value_type& value)
    {
      APR_ARRAY_PUSH(proxied, value_type) = value;
    }

  /**
   * Pop a value from the end of the array.
   * @return A pointer to the value that was removed, or @c NULL if
   * the array was empty.
   */
  value_type* pop() noexcept
    {
      return static_cast<value_type*>(apr_array_pop(proxied));
    }

  /**
   * @brief Return an interator to the beginning of the array.
   */
  iterator begin() noexcept
    {
      return &APR_ARRAY_IDX(proxied, 0, value_type);
    }

  /**
   * @brief Return a constant interator to the beginning of the array.
   */
  const_iterator begin() const noexcept
    {
      return &APR_ARRAY_IDX(proxied, 0, const value_type);
    }

  /**
   * @brief Return a constant interator to the beginning of the array.
   */
  const_iterator cbegin() const noexcept
    {
      return begin();
    }

  /**
   * @brief Return an interator to the end of the array.
   */
  iterator end() noexcept
    {
      return &APR_ARRAY_IDX(proxied, size(), value_type);
    }

  /**
   * @brief Return a constant interator to the end of the array.
   */
  const_iterator end() const noexcept
    {
      return &APR_ARRAY_IDX(proxied, size(), const value_type);
    }

  /**
   * @brief Return a constant interator to the end of the array.
   */
  const_iterator cend() const noexcept
    {
      return end();
    }

private:
  apr_array_header_t* const proxied; ///< The wrapperd APR array.
};

} // namespace apr
} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif // SVNXX_PRIVATE_APRWRAP_HASH_H
