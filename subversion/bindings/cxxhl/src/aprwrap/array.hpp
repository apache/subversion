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

#ifndef SVN_CXXHL_PRIVATE_APRWRAP_ARRAY_H
#define SVN_CXXHL_PRIVATE_APRWRAP_ARRAY_H

#include <stdexcept>

#include <apr_tables.h>
#include "pool.hpp"

#include "svn_private_config.h"

namespace apache {
namespace subversion {
namespace cxxhl {
namespace apr {

/**
 * Proxy for an APR array.
 *
 * This class does not own the array. The array's lifetime is tied to
 * its pool. The caller is responsible for making sure that the
 * array's lifetime is longer than this proxy object's.
 */
template<typename T> class Array
{
public:
  typedef T value_type;
  typedef int size_type;

  /**
   * Create and proxy a new APR array allocated from @a pool.
   * Reserve space for @a nelts array elements.
   */
  explicit Array(const Pool& pool, size_type nelts = 0) throw()
    : m_array(apr_array_make(pool.get(), nelts, sizeof(value_type)))
    {}

  /**
   * Create a new proxy for the APR array @a array.
   */
  explicit Array(apr_array_header_t* array)
    : m_array(array)
    {
      if (m_array->elt_size != sizeof(value_type))
        throw std::invalid_argument(
            _("APR array element size does not match template parameter"));
    }

  /**
   * @return The wrapped APR array.
   */
  apr_array_header_t* array() const throw()
    {
      return m_array;
    }

  /**
   * @return the number of elements in the wrapped APR array.
   */
  size_type size() const throw()
    {
      return m_array->nelts;
    }

  /**
   * @return An immutable reference to the array element at @a index.
   */
  const value_type& operator[](size_type index) const throw()
    {
      return APR_ARRAY_IDX(m_array, index, value_type);
    }

  /**
   * @return An immutable reference to the array element at @a index.
   * Like operator[] but perfoms a range check on the index.
   */
  const value_type& at(size_type index) const
    {
      if (index < 0 || index >= size())
        throw std::out_of_range(_("APR array index is out of range"));
      return (*this)[index];
    }

  /**
   * @return A mutable reference to the array element at @a index.
   */
  value_type& operator[](size_type index) throw()
    {
      return APR_ARRAY_IDX(m_array, index, value_type);
    }

  /**
   * @return A mutable reference to the array element at @a index.
   * Like operator[] but perfoms a range check on the index.
   */
  value_type& at(size_type index)
    {
      if (index < 0 || index >= size())
        throw std::out_of_range(_("APR array index is out of range"));
      return (*this)[index];
    }

  /**
   * Push @a value onto the end of the APR array.
   */
  void push(const value_type& value) throw()
    {
      APR_ARRAY_PUSH(m_array, value_type) = value;
    }

  /**
   * Pop a value from the end of the array.
   * @return A pointer to the value that was removed, or @c NULL if
   * the array was empty.
   */
  value_type* pop() throw()
    {
      return static_cast<value_type*>(apr_array_pop(m_array));
    }

  /**
   * Abstract base class for mutable iteration callback functors.
   */
  struct Iteration
  {
    /**
     * Called by Array::iterate for every value in the array.
     * @return @c false to terminate the iteration, @c true otherwise.
     */
    virtual bool operator() (value_type& value) = 0;
  };

  /**
   * Iterate over all the values pairs in the array, invoking
   * @a callback for each one.
   */
  void iterate(Iteration& callback)
    {
      for (size_type n = 0; n < size(); ++n)
        if (!callback((*this)[n]))
          break;
    }

  /**
   * Abstract base class for immutable iteration callback functors.
   */
  struct ConstIteration
  {
    /**
     * Called by Array::iterate for every value in the array.
     * @return @c false to terminate the iteration, @c true otherwise.
     */
    virtual bool operator() (const value_type& value) = 0;
  };

  /**
   * Iterate over all the values pairs in the array, invoking
   * @a callback for each one.
   */
  void iterate(ConstIteration& callback) const
    {
      for (size_type n = 0; n < size(); ++n)
        if (!callback((*this)[n]))
          break;
    }

private:
  apr_array_header_t* const m_array; ///< The wrapperd APR array.
};


/**
 * Proxy for an immutable APR array.
 */
template<typename T>
class ConstArray : private Array<T>
{
  typedef Array<T> inherited;

public:
  typedef typename inherited::value_type value_type;
  typedef typename inherited::size_type size_type;

  /**
   * Create a new proxy for the APR array wrapped by @a that.
   */
  ConstArray(const ConstArray& that) throw()
    : inherited(that)
    {}

  /**
   * Create a new proxy for the APR array wrapped by @a that.
   */
  explicit ConstArray(const inherited& that) throw()
    : inherited(that)
    {}

  /**
   * Create a new proxy for the APR array @a array.
   */
  explicit ConstArray(const apr_array_header_t* array)
    : inherited(const_cast<apr_array_header_t*>(array))
    {}

  /**
   * @return The wrapped APR array.
   */
  const apr_array_header_t* array() const throw()
    {
      return inherited::array();
    }

  /**
   * @return The number of elements in the wrapped APR array.
   */
  size_type size() const throw()
    {
      return inherited::size();
    }

  /**
   * @return An immutable reference to the array element at @a index.
   */
  const value_type& operator[](size_type index) const throw()
    {
      return inherited::operator[](index);
    }

  /**
   * @return An immutable reference to the array element at @a index.
   * Like operator[] but perfoms a range check on the index.
   */
  const value_type& at(size_type index) const
    {
      return inherited::at(index);
    }

  /**
   * Abstract base class for immutable iteration callback functors.
   */
  typedef typename inherited::ConstIteration Iteration;

  /**
   * Iterate over all the values pairs in the array, invoking
   * @a callback for each one.
   */
  void iterate(Iteration& callback) const
    {
      inherited::iterate(callback);
    }
};

} // namespace apr
} // namespace cxxhl
} // namespace subversion
} // namespace apache

#endif // SVN_CXXHL_PRIVATE_APRWRAP_HASH_H
