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
 * @file svn_xml_private.h
 * @brief Private XML API.
 */

#include <apr.h>
#include <apr_pools.h>

#include "svn_io.h"     /* for svn_stream_t */
#include "svn_xml.h"

/** Create a stream that wraps the XML parser described at @a parser.
 *
 * The stream produced will implement 'write' and 'close' methods. It
 * will push the data to the parser on write operation, and flush it on
 * close.
 *
 * This stream can be used as a generic writable stream, so the callers
 * may pipe any data there, for example, using the svn_stream_copy3
 * function, in case of a file source.
 */
svn_stream_t *
svn_xml__make_parse_stream(svn_xml_parser_t *parser,
                           apr_pool_t *result_pool);

