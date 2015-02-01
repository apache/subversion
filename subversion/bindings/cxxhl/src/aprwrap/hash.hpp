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

#ifndef SVN_CXXHL_PRIVATE_APRWRAP_HASH_H
#define SVN_CXXHL_PRIVATE_APRWRAP_HASH_H

#include <apr_hash.h>
#include "pool.hpp"

namespace apache {
namespace subversion {
namespace cxxhl {
namespace apr {

// Template forward declaration
template<typename T, typename V, apr_ssize_t KeySize = APR_HASH_KEY_STRING>
class Hash;

/**
 * Proxy for an APR hash table.
 * This is a template specialization for the default hash type.
 */
template<>
class Hash<void, void>
{
public:
  struct Iteration;

  /**
   * Iterate over all the key-value pairs in the hash table, invoking
   * @a callback for each pair.
   * Uses @a scratch_pool for temporary allocations.
   */
  void iterate(Iteration& callback, const Pool& scratch_pool);

public:
  /**
   * Proxy for a key in an APR hash table.
   * This is a template specialization for the default hash key type.
   */
  class Key
  {
  public:
    typedef const void* key_type;
    typedef apr_ssize_t size_type;

    /**
     * Public constructor. Uses the template @a Size parameter.
     */
    Key(key_type key) throw()
      : m_key(key), m_size(APR_HASH_KEY_STRING)
      {}

    /**
     * Get the value of the key.
     */
    key_type get() const throw() { return m_key; }

    /**
     * Get the size of the key.
     */
    size_type size() const throw() { return m_size; }

  protected:
    /**
     * Constructor used by the generic template, specializations and
     * hash table wrapper. Does not make assumptions about the key size.
     */
    Key(key_type key, size_type size) throw()
      : m_key(key), m_size(size)
      {}

    /**
     * The hash table wrapper must be able to call the protected constructor.
     */
    friend void Hash::iterate(Hash::Iteration&, const Pool&);

  private:
    const key_type m_key;       ///< Immutable reference to the key
    const size_type m_size;     ///< The size of the key
  };

public:
  typedef Key::key_type key_type;
  typedef void* value_type;
  typedef unsigned int size_type;

  /**
   * Create and proxy a new APR hash table in @a pool.
   */
  explicit Hash(const Pool& pool) throw()
    : m_hash(apr_hash_make(pool.get()))
    {}

  /**
   * Create and proxy a new APR hash table in @a pool, using @a
   * hash_func as the hash function.
   */
  explicit Hash(const Pool& pool, apr_hashfunc_t hash_func) throw()
    : m_hash(apr_hash_make_custom(pool.get(), hash_func))
    {}

  /**
   * Create a proxy for the APR hash table @a hash.
   */
  explicit Hash(apr_hash_t* hash)
    : m_hash(hash)
    {}

  /**
   * Return the wrapped APR hash table.
   */
  apr_hash_t* hash() const throw()
    {
      return m_hash;
    }

  /**
   * Return the number of key-value pairs in the wrapped hash table.
   */
  size_type size() const throw()
    {
      return apr_hash_count(m_hash);
    }

  /**
   * Set @a key = @a value in the wrapped hash table.
   */
  void set(const Key& key, value_type value) throw()
    {
      apr_hash_set(m_hash, key.get(), key.size(), value);
    }

  /**
   * Retrieve the value associated with @a key.
   */
  value_type get(const Key& key) const throw()
    {
      return apr_hash_get(m_hash, key.get(), key.size());
    }

  /**
   * Delete the entry for @a key.
   */
  void del(const Key& key) throw()
    {
      apr_hash_set(m_hash, key.get(), key.size(), NULL);
    }

  /**
   * Abstract base class for iteration callback functors.
   */
  struct Iteration
  {
    /**
     * Called by Hash::iterate for every key-value pair in the hash table.
     * @return @c false to terminate the iteration, @c true otherwise.
     */
    virtual bool operator() (const Key& key, value_type value) = 0;
  };

protected:
  typedef const void* const_value_type;

  /**
   * Set @a key = @a value in the wrapped hash table.  Overloaded for
   * deroved template instantiations with constant values; for
   * example, Hash<char, const char>.
   */
  void set(const Key& key, const_value_type const_value) throw()
    {
      set(key, const_cast<value_type>(const_value));
    }

private:
  apr_hash_t* const m_hash;     ///< The wrapped APR hash table.
};


/**
 * Proxy for an APR hash table: the template.
 *
 * This class does not own the hash table. The hash table's lifetime
 * is tied to its pool. The caller is responsible for making sure that
 * the hash table's lifetime is longer than this proxy object's.
 */
template<typename K, typename V, apr_ssize_t KeySize>
class Hash : private Hash<void, void>
{
  typedef Hash<void, void> inherited;

public:
  /**
   * Proxy for a key in an APR hash table.
   *
   * This class does not own the key; it is the caller's responsibility
   * to make sure that the key's lifetime is longer than the proxy
   * object's.
   */
  class Key : private inherited::Key
  {
    typedef Hash<void, void>::Key inherited;

    /**
     * The wrapper must be able to call the private constructor and
     * convert references to the base class.
     */
    friend class Hash;

  public:
    typedef const K* key_type;
    typedef inherited::size_type size_type;

    Key(key_type key) throw()
      : inherited(key, KeySize)
      {}

    /**
     * Get the value of the key.
     */
    key_type get() const throw()
      {
        return static_cast<key_type>(inherited::get());
      }

    /**
     * Get the size of the key.
     */
    size_type size() const throw()
      {
        return inherited::size();
      }

  private:
    /**
     * Conversion constructor used by the derived iteration class.
     */
    explicit Key(const inherited& that) throw()
      : inherited(that)
      {}
  };

public:
  typedef typename Key::key_type key_type;
  typedef V* value_type;

  /**
   * Create and proxy a new APR hash table allocated from @a pool.
   */
  explicit Hash(const Pool& pool) throw()
    : inherited(pool)
    {}

  /**
   * Create and proxy a new APR hash table allocated from @a pool,
   * using @a hash_func as the hash function.
   */
  explicit Hash(const Pool& pool, apr_hashfunc_t hash_func) throw()
    : inherited(pool, hash_func)
    {}

  /**
   * Create a proxy for the APR hash table @a hash.
   */
  explicit Hash(apr_hash_t* hash)
    : inherited(hash)
    {}

  /**
   * @return The wrapped APR hash table.
   */
  apr_hash_t* hash() const throw()
    {
      return inherited::hash();
    }

  /**
   * Return the number of key-value pairs in the wrapped hash table.
   */
  size_type size() const throw()
    {
      return inherited::size();
    }

  /**
   * Set @a key = @a value in the wrapped hash table.
   */
  void set(const Key& key, value_type value) throw()
    {
      inherited::set(inherited::Key(key), value);
    }

  /**
   * Retrieve the value associated with @a key.
   */
  value_type get(const Key& key) const throw()
    {
      return static_cast<value_type>(inherited::get(inherited::Key(key)));
    }

  /**
   * Delete the entry for @a key.
   */
  void del(const Key& key) throw()
    {
      inherited::del(inherited::Key(key));
    }

  /**
   * Abstract base class for iteration callback functors.
   */
  struct Iteration : protected inherited::Iteration
  {
    /**
     * Called by Hash::iterate for every key-value pair in the hash table.
     * @return @c false to terminate the iteration, @c true otherwise.
     */
    virtual bool operator() (const Key& key, value_type value) = 0;

  private:
    friend void Hash::iterate(Iteration& callback, const Pool& scratch_pool);

    /**
     * Implementation of the derived virtual operator().
     * Adapts the callback to the instantiated types.
     */
    virtual bool operator() (const inherited::Key& raw_key,
                             inherited::value_type raw_value)
      {
        return (*this)(Key(raw_key), static_cast<value_type>(raw_value));
      }
  };

  /**
   * Iterate over all the key-value pairs in the hash table, invoking
   * @a callback for each pair.
   * Uses @a scratch_pool for temporary allocations.
   */
  void iterate(Iteration& callback, const Pool& scratch_pool)
    {
      inherited::iterate(callback, scratch_pool);
    }
};


} // namespace apr
} // namespace cxxhl
} // namespace subversion
} // namespace apache

#endif // SVN_CXXHL_PRIVATE_APRWRAP_HASH_H
