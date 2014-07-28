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
 *
 * @file svn_config_private.h
 * @brief Private config file parsing API.
 */

#ifndef SVN_CONFIG_PRIVATE_H
#define SVN_CONFIG_PRIVATE_H

#include <apr_pools.h>

#include "svn_error.h"
#include "svn_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Description of a constructor for in-memory config file
 * representations.
 */
typedef struct svn_config__constructor_t svn_config__constructor_t;

/*
 * Constructor callback: called when the parsing of a new SECTION
 * begins. If the implementation stores the value of SECTION, it
 * must copy it into a permanent pool.
 *
 * May return SVN_ERR_CEASE_INVOCATION to stop further parsing.
 */
typedef svn_error_t *(*svn_config__open_section_fn)(
    void *baton, const char *section);

/*
 * Constructor callback: called when the parsing of SECTION ends. If
 * the implementation stores the value of SECTION, it must copy it
 * into a permanent pool.
 *
 * May return SVN_ERR_CEASE_INVOCATION to stop further parsing.
 */
typedef svn_error_t *(*svn_config__close_section_fn)(
    void *baton, const char *section);

/*
 * Constructor callback: called OPTION with VALUE in SECTION was
 * parsed. If the implementation stores the values of SECTION, OPTION
 * or VALUE, it must copy them into a permanent pool.
 *
 * May return SVN_ERR_CEASE_INVOCATION to stop further parsing.
 */
typedef svn_error_t *(*svn_config__add_value_fn)(
    void *baton, const char *section,
    const char *option, const char *value);


/*
 * Create a new constuctor allocated from RESULT_POOL.
 * Any of the callback functions may be NULL.
 * SECTION_NAMES_CASE_SENSITIVE and OPTION_NAMES_CASE_SENSITIVE
 * are ignored unless EXPAND_PARSED_VALUES is true, in which
 * case the parser behaviour changes as follows:
 *   - the "DEFAULT" section is never reported to the constructor;
 *   - values reported to ADD_VALUE_CALLBACK are always fully expanded;
 *   - if a section is re-opened, the value expansion for this section
 *     does *not* consider the previous contents of the section but
 *     only the current set of values, along with whatever is set in
 *     the "DEFAULT" section;
 *   - changes to the "DEFAULT" section that appear after a section
 *     has been parsed do not affect value expansion for that section.
 */
svn_config__constructor_t *
svn_config__constructor_create(
    svn_boolean_t expand_parsed_values,
    svn_boolean_t section_names_case_sensitive,
    svn_boolean_t option_names_case_sensitive,
    svn_config__open_section_fn open_section_callback,
    svn_config__close_section_fn close_section_callback,
    svn_config__add_value_fn add_value_callback,
    apr_pool_t *result_pool);


/* The default add-value callback, used by the default config parser. */
svn_error_t *svn_config__default_add_value_fn(
    void *baton, const char *section,
    const char *option, const char *value);

/*
 * Parse the configuration from STREAM, using CONSTRUCTOR to build the
 * in-memory representation of the parsed configuration.
 * CONSTRUCTOR_BATON is passed unchanged to the constructor
 * callbacks. The parser guarantees that sections and options will be
 * passed to the callback in the same order as they're defined in
 * STREAM.
 *
 * The lifetome of section names, option names and values passed to
 * the constructor does not extend past the invocation of each
 * callback; see calback docs, above.
 *
 * The parser will use SCRATCH_POOL for its own allocations.
 */
svn_error_t *
svn_config__parse_stream(svn_stream_t *stream,
                         svn_config__constructor_t *constructor,
                         void *constructor_baton,
                         apr_pool_t *scratch_pool);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_CONFIG_PRIVATE_H */
