/**
 * @file svnxx/tristate.hpp
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

#ifndef SVNXX_TRISTATE_HPP
#define SVNXX_TRISTATE_HPP

#include "svn_types_impl.h"

#include <cstdint>

#if defined(SVNXX_USE_BOOST) || defined(DOXYGEN)
#include <boost/logic/tribool.hpp>
#endif

namespace apache {
namespace subversion {
namespace svnxx {

/**
 * @brief A three-state Boolean-like type.
 *
 * @c tristate values represent one of three states:
 * @li the @e true state (equivalent to Boolean @c true);
 * @li the @e false state (equivalent to Boolean @c false);
 * @li the @e unknown state.
 *
 * @c tristate constructors, methods and operators are all
 * compile-time constant expressions and can be used to initialize
 * other @c constexpr values. And unlike most other types,
 * comparisons and logical operations between @c tristate values
 * return a @c tristate, not a @c bool.
 *
 * Given a @c tristate value @a t, the state it represents can be
 * uniquely determined by the following coding pattern:
 * @code{.cpp}
 *   if (t) {
 *       // t is true
 *   }
 *   else if (!t) {
 *       // t is false
 *   else {
 *       // t is unknown
 *   }
 * @endcode
 *
 * @note Inspired by <tt>boost::tribool</tt>
 */
class tristate
{
  struct impl
  {
    void trueval() {};
  };
  using safe_bool = void (impl::*)();

  // The default constructor creates the unkonwn state.
  constexpr tristate() noexcept
    : value(unknown_value)
    {}

public:
  /**
   * @brief Factory method for the @e unknown state.
   */
  static constexpr tristate unknown() noexcept
    {
      return tristate(/*unknown_value*/);
    }

  /**
   * @brief Constructor for the @e true and @e false states.
   */
  constexpr tristate(bool initial_value) noexcept
    : value(initial_value ? true_value : false_value)
    {}

  /**
   * @brief Safe conversion to @c bool.
   * @returns a @e true-like value only when this @c tristate is the
   * @e true state.
   */
  constexpr operator safe_bool() const noexcept
    {
      return value == true_value ? &impl::trueval : 0;
    }

  /**
   * @brief Logical negation.
   * @returns the logical negation of a @c tristate, according to
   * the following table:
   *   <table border=1>
   *     <tr>
   *       <th><center><code>!</code></center></th>
   *       <th/>
   *     </tr>
   *     <tr>
   *       <th><center><em>false</em></center></th>
   *       <td><center><em>true</em></center></td>
   *     </tr>
   *     <tr>
   *       <th><center><em>true</em></center></th>
   *       <td><center><em>false</em></center></td>
   *     </tr>
   *     <tr>
   *       <th><center><em>unknown</em></center></th>
   *       <td><center><em>unknown</em></center></td>
   *     </tr>
   *   </table>
   */
  constexpr tristate operator!() const noexcept
    {
      return (value == false_value ? tristate(true)
              : (value == true_value ? tristate(false)
                 : tristate::unknown()));
    }

private:
  // NOTE: Keep these values identical to those in svn_tristate_t!
  enum : std::uint8_t {
    false_value   = svn_tristate_false,
    true_value    = svn_tristate_true,
    unknown_value = svn_tristate_unknown
  } value;

#if defined(SVNXX_USE_BOOST) || defined(DOXYGEN)
public:
  /**
   * @brief Conversion from <tt>boost::tribool</tt>.
   * @returns a @c tribool value equivalent to the @a t.
   * @note Avalible only if @c SVNXX_USE_BOOST is defined.
   */
  constexpr tristate(boost::tribool t) noexcept
    : value(boost::indeterminate(t) ? unknown_value
            : (t ? true_value : false_value))
    {}

  /**
   * @brief Conversion to <tt>boost::tribool</tt>.
   * @returns a <tt>boost::tribool</tt> value equivalent to the @c
   * tristate value.
   * @note Avalible only if @c SVNXX_USE_BOOST is defined.
   */
  constexpr operator boost::tribool() const noexcept
    {
      return (value == true_value ? boost::tribool(true)
              : (value == false_value ? boost::tribool(false)
                 : boost::tribool(boost::indeterminate)));
    }
#endif
};

/**
 * @related tristate
 * @brief Test for the @e unknown @c tristate state.
 * @returns @c true only if @a t is the @e unknown state.
 */
constexpr inline bool unknown(tristate t) noexcept
{
  return bool(t) == bool(!t);
}

/**
 * @related tristate
 * @brief Logical conjunction.
 * @returns the result of a logical @c AND of two @c tristate
 * values, according to the following table:
 *   <table border=1>
 *     <tr>
 *       <th><center><code>&amp;&amp;</code></center></th>
 *       <th><center><em>false</em></center></th>
 *       <th><center><em>true</em></center></th>
 *       <th><center><em>unknown</em></center></th>
 *     </tr>
 *     <tr>
 *       <th><center><em>false</em></center></th>
 *       <td><center><em>false</em></center></td>
 *       <td><center><em>false</em></center></td>
 *       <td><center><em>false</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>true</em></center></th>
 *       <td><center><em>false</em></center></td>
 *       <td><center><em>true</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>unknown</em></center></th>
 *       <td><center><em>false</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *   </table>
 */
constexpr inline tristate operator&&(tristate t, tristate u) noexcept
{
  return (bool(!t) || bool(!u) ? tristate(false)
          : (bool(t) && bool(u) ? tristate(true)
             : tristate::unknown()));
}

/**
 * @related tristate
 * @overload
 */
constexpr inline tristate operator&&(tristate t, bool b) noexcept
{
  return b ? t : tristate(false);
}

/**
 * @related tristate
 * @overload
 */
constexpr inline tristate operator&&(bool b, tristate t) noexcept
{
  return b ? t : tristate(false);
}

#if defined(SVNXX_USE_BOOST) || defined(DOXYGEN)
/**
 * @related tristate
 * @overload
 * @note Avalible only if @c SVNXX_USE_BOOST is defined.
 */
constexpr inline tristate operator&&(tristate t, boost::tribool b) noexcept
{
  return t && tristate(b);
}

/**
 * @related tristate
 * @overload
 * @note Avalible only if @c SVNXX_USE_BOOST is defined.
 */
constexpr inline tristate operator&&(boost::tribool b, tristate t) noexcept
{
  return tristate(b) && t;
}
#endif

/**
 * @related tristate
 * @brief Logical disjunction.
 * @returns the result of a logical @c OR of two @c tristate
 * values, according to the following table:
 *   <table border=1>
 *     <tr>
 *       <th><center><code>||</code></center></th>
 *       <th><center><em>false</em></center></th>
 *       <th><center><em>true</em></center></th>
 *       <th><center><em>unknown</em></center></th>
 *     </tr>
 *     <tr>
 *       <th><center><em>false</em></center></th>
 *       <td><center><em>false</em></center></td>
 *       <td><center><em>true</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>true</em></center></th>
 *       <td><center><em>true</em></center></td>
 *       <td><center><em>true</em></center></td>
 *       <td><center><em>true</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>unknown</em></center></th>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>true</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *   </table>
 */
constexpr inline tristate operator||(tristate t, tristate u) noexcept
{
  return (bool(!t) && bool(!u) ? tristate(false)
          : (bool(t) || bool(u) ? tristate(true)
             : tristate::unknown()));
}

/**
 * @related tristate
 * @overload
 */
constexpr inline tristate operator||(tristate t, bool b) noexcept
{
  return b ? tristate(true) : t;
}

/**
 * @related tristate
 * @overload
 */
constexpr inline tristate operator||(bool b, tristate t) noexcept
{
  return b ? tristate(true) : t;
}

#if defined(SVNXX_USE_BOOST) || defined(DOXYGEN)
/**
 * @related tristate
 * @overload
 * @note Avalible only if @c SVNXX_USE_BOOST is defined.
 */
constexpr inline tristate operator||(tristate t, boost::tribool b) noexcept
{
  return t || tristate(b);
}

/**
 * @related tristate
 * @overload
 * @note Avalible only if @c SVNXX_USE_BOOST is defined.
 */
constexpr inline tristate operator||(boost::tribool b, tristate t) noexcept
{
  return tristate(b) || t;
}
#endif

/**
 * @related tristate
 * @brief Equality comparison.
 * @returns the result of comparing two @c tristate values for
 * equality, according to the following table:
 *   <table border=1>
 *     <tr>
 *       <th><center><code>==</code></center></th>
 *       <th><center><em>false</em></center></th>
 *       <th><center><em>true</em></center></th>
 *       <th><center><em>unknown</em></center></th>
 *     </tr>
 *     <tr>
 *       <th><center><em>false</em></center></th>
 *       <td><center><em>true</em></center></td>
 *       <td><center><em>false</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>true</em></center></th>
 *       <td><center><em>false</em></center></td>
 *       <td><center><em>true</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>unknown</em></center></th>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *   </table>
 */
constexpr inline tristate operator==(tristate t, tristate u) noexcept
{
  return (unknown(t) || unknown(u) ? tristate::unknown()
          : ((t && u) || (!t && !u)));
}

/**
 * @related tristate
 * @overload
 */
constexpr inline tristate operator==(tristate t, bool b) noexcept
{
  return t == tristate(b);
}

/**
 * @related tristate
 * @overload
 */
constexpr inline tristate operator==(bool b, tristate t) noexcept
{
  return tristate(b) == t;
}

#if defined(SVNXX_USE_BOOST) || defined(DOXYGEN)
/**
 * @related tristate
 * @overload
 * @note Avalible only if @c SVNXX_USE_BOOST is defined.
 */
constexpr inline tristate operator==(tristate t, boost::tribool b) noexcept
{
  return t == tristate(b);
}

/**
 * @related tristate
 * @overload
 * @note Avalible only if @c SVNXX_USE_BOOST is defined.
 */
constexpr inline tristate operator==(boost::tribool b, tristate t) noexcept
{
  return tristate(b) == t;
}
#endif

/**
 * @related tristate
 * @brief Inquality comparison.
 * @returns the result of comparing two @c tristate values for
 * inequality, according to the following table:
 *   <table border=1>
 *     <tr>
 *       <th><center><code>!=</code></center></th>
 *       <th><center><em>false</em></center></th>
 *       <th><center><em>true</em></center></th>
 *       <th><center><em>unknown</em></center></th>
 *     </tr>
 *     <tr>
 *       <th><center><em>false</em></center></th>
 *       <td><center><em>false</em></center></td>
 *       <td><center><em>true</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>true</em></center></th>
 *       <td><center><em>true</em></center></td>
 *       <td><center><em>false</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *     <tr>
 *       <th><center><em>unknown</em></center></th>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *       <td><center><em>unknown</em></center></td>
 *     </tr>
 *   </table>
 */
constexpr inline tristate operator!=(tristate t, tristate u) noexcept
{
  return (unknown(t) || unknown(u) ? tristate::unknown()
          : !((t && u) || (!t && !u)));
}

/**
 * @related tristate
 * @overload
 */
constexpr inline tristate operator!=(tristate t, bool b) noexcept
{
  return t != tristate(b);
}

/**
 * @related tristate
 * @overload
 */
constexpr inline tristate operator!=(bool b, tristate t) noexcept
{
  return tristate(b) != t;
}

#if defined(SVNXX_USE_BOOST) || defined(DOXYGEN)
/**
 * @related tristate
 * @overload
 * @note Avalible only if @c SVNXX_USE_BOOST is defined.
 */
constexpr inline tristate operator!=(tristate t, boost::tribool b) noexcept
{
  return t != tristate(b);
}

/**
 * @related tristate
 * @overload
 * @note Avalible only if @c SVNXX_USE_BOOST is defined.
 */
constexpr inline tristate operator!=(boost::tribool b, tristate t) noexcept
{
  return tristate(b) != t;
}
#endif

} // namespace svnxx
} // namespace subversion
} // namespace apache

#endif  // SVNXX_TRISTATE_HPP
