/**
 * @file svnxx/revision.hpp
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

#ifndef SVNXX_REVISION_HPP
#define SVNXX_REVISION_HPP

#include <chrono>
#include <cstdint>

#include "tristate.hpp"

namespace apache {
namespace subversion {
namespace svnxx {

/**
 * @brief A revision, see @ref svn_opt_revision_t.
 *
 * The @c revision can represent a revision number, a point in time
 * in the repository or a property of the working copy or repository
 * node (see revision::kind).
 */
class revision
{
public:
  /**
   * @brief Revision number type.
   */
  enum class number : long
    {
      invalid = -1,             ///< Invalid revision number.
    };

  /**
   * @brief Revision by date/time uses the system clock.
   */
  template<typename Duration>
  using time = std::chrono::time_point<std::chrono::system_clock, Duration>;

  /**
   * @brief The resolution of the stored date/time.
   */
  using usec = std::chrono::microseconds;

  /**
   * @brief Revision kind discriminator (see @ref svn_opt_revision_kind).
   */
  // NOTE: Keep these values identical to those in svn_opt_revision_kind!
  enum class kind : std::int8_t
    {
      unspecified,
      number,
      date,
      committed,
      previous,
      base,
      working,
      head,
    };

  /**
   * @brief Default constructor.
   * @post get_kind() == kind::unspecified.
   */
  revision() noexcept
    : tag(kind::unspecified)
    {}

  /**
   * @brief Construct a revision of the given kind.
   * @pre The @a revkind argument may be any @c kind value @b except
   *      kind::number or kind::date, which require additional
   *      parameters and therefore have their own constructors.
   * @post get_kind() == @a revkind.
   * @throw std::invalid_argument if the @a revkind value
   *        precondition is not met.
   */
  explicit revision(kind revkind)
    : tag(revkind)
    {
      if (revkind == kind::number || revkind == kind::date)
        throw std::invalid_argument("invalid svn::revision::kind");
    }

  /**
   * @brief Construct a numbered revision.
   * @post get_kind() == kind::number.
   */
  explicit revision(number revnum_) noexcept
    : tag(kind::number),
      revnum(revnum_)
    {}

  /**
   * @brief Construct a dated revision from a system clock time point.
   * @post get_kind() == kind::date.
   */
  template<typename D>
  explicit revision(time<D> time_) noexcept
    : tag(kind::date),
      date(std::chrono::time_point_cast<usec>(time_))
    {}

  /**
   * @brief Return the revision kind.
   */
  kind get_kind() const noexcept
    {
      return tag;
    }

  /**
   * @brief Return the revision number.
   * @pre get_kind() == kind::number.
   * @throw std::logic_error if the precondition is not met.
   */
  number get_number() const
    {
      if (tag != kind::number)
        throw std::logic_error("svn::revision kind != number");
      return revnum;
    }

  /**
   * @brief Return the revision date/time as a system clock time point.
   * @pre get_kind() == kind::date.
   * @throw std::logic_error if the precondition is not met.
   */
  template<typename D>
  time<D> get_date() const
    {
      if (tag != kind::date)
        throw std::logic_error("svn::revision kind != date");
      return std::chrono::time_point_cast<D>(date);
    }

private:
  // Even if we were using C++17, we wouldn't use std::variant because we
  // already maintain an explicit discriminator tag for the union.
  kind tag;           // Union discriminator
  union {
    number revnum;    // (tag == kind::number): revision number.
    time<usec> date;  // (tag == kind::date): microseconds from epoch.
  };
};

/**
 * @related revision
 * @brief revision::number alias for convenience.
 */
using revnum = revision::number;

/**
 * @related revision
 * @brief Equality comparison.
 */
inline bool operator==(const revision& a, const revision& b)
{
  const auto kind = a.get_kind();
  if (kind != b.get_kind())
    return false;
  else if (kind == revision::kind::number)
    return a.get_number() == b.get_number();
  else if (kind == revision::kind::date)
    return a.get_date<revision::usec>() == b.get_date<revision::usec>();
  else
    return true;
}

/**
 * @related revision
 * @brief Inequality comparison.
 */
inline bool operator!=(const revision& a, const revision& b)
{
  return !(a == b);
}

/**
 * @related revision
 * @brief Ordering: less-than (<tt>operator @<</tt>).
 * @returns a @c tristate result of comparing two @c revision values,
 * according to the following table:
 *   <table border=1>
 *     <tr>
 *       <th><center><code>@<</code></center></th>
 *       <th><center><tt>number</tt></center></th>
 *       <th><center><tt>date</tt></center></th>
 *       <th><center><em>other</em></center></th>
 *     </tr>
 *     <tr>
 *       <th><center><tt>number</tt></center></th>
 *       <td><center><tt>a.get_number() < b.get_number()</tt></td>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><tt>date</tt></center></th>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><tt>a.get_date() < b.get_date()</tt></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>other</em></center></th>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *   </table>
 */
inline tristate operator<(const revision& a, const revision& b)
{
  const auto kind = a.get_kind();
  if (kind != b.get_kind())
    return tristate::unknown();
  else if (kind == revision::kind::number)
    return a.get_number() < b.get_number();
  else if (kind == revision::kind::date)
    return a.get_date<revision::usec>() < b.get_date<revision::usec>();
  else
    return tristate::unknown();
}

/**
 * @related revision
 * @brief Ordering: greater-than (<tt>operator @></tt>).
 * @returns a @c tristate result of comparing two @c revision values,
 * according to the following table:
 *   <table border=1>
 *     <tr>
 *       <th><center><code>@></code></center></th>
 *       <th><center><tt>number</tt></center></th>
 *       <th><center><tt>date</tt></center></th>
 *       <th><center><em>other</em></center></th>
 *     </tr>
 *     <tr>
 *       <th><center><tt>number</tt></center></th>
 *       <td><center><tt>a.get_number() > b.get_number()</tt></td>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><tt>date</tt></center></th>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><tt>a.get_date() > b.get_date()</tt></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>other</em></center></th>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *   </table>
 */
inline tristate operator>(const revision& a, const revision& b)
{
  const auto kind = a.get_kind();
  if (kind != b.get_kind())
    return tristate::unknown();
  else if (kind == revision::kind::number)
    return a.get_number() > b.get_number();
  else if (kind == revision::kind::date)
    return a.get_date<revision::usec>() > b.get_date<revision::usec>();
  else
    return tristate::unknown();
}

/**
 * @related revision
 * @brief Ordering: less-or-equal (<tt>operator @<=</tt>).
 * @returns a @c tristate result of comparing two @c revision values,
 * according to the following table:
 *   <table border=1>
 *     <tr>
 *       <th><center><code>@<=</code></center></th>
 *       <th><center><tt>number</tt></center></th>
 *       <th><center><tt>date</tt></center></th>
 *       <th><center><em>other</em></center></th>
 *     </tr>
 *     <tr>
 *       <th><center><tt>number</tt></center></th>
 *       <td><center><tt>a.get_number() <= b.get_number()</tt></td>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><tt>date</tt></center></th>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><tt>a.get_date() <= b.get_date()</tt></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>other</em></center></th>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>true</em>&dagger; or <em>unknown</em></center></td>
 *     </tr>
 *   </table>
 * &dagger; <em>true</em> when <tt>a.get_kind() == b.get_kind()</tt>.
 */
inline tristate operator<=(const revision& a, const revision& b)
{
  return (a == b || !(a > b));
}

/**
 * @related revision
 * @brief Ordering: greater-or-equal (<tt>operator @>=</tt>).
 * @returns a @c tristate result of comparing two @c revision values,
 * according to the following table:
 *   <table border=1>
 *     <tr>
 *       <th><center><code>@>=</code></center></th>
 *       <th><center><tt>number</tt></center></th>
 *       <th><center><tt>date</tt></center></th>
 *       <th><center><em>other</em></center></th>
 *     </tr>
 *     <tr>
 *       <th><center><tt>number</tt></center></th>
 *       <td><center><tt>a.get_number() >= b.get_number()</tt></td>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><tt>date</tt></center></th>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><tt>a.get_date() >= b.get_date()</tt></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>other</em></center></th>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>true</em>&dagger; or <em>unknown</em></center></td>
 *     </tr>
 *   </table>
 * &dagger; <em>true</em> when <tt>a.get_kind() == b.get_kind()</tt>.
 */
inline tristate operator>=(const revision& a, const revision& b)
{
  return (a == b || !(a < b));
}

} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif  // SVNXX_REVISION_HPP
