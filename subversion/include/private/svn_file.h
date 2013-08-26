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
 * @file svn_file.h
 * @brief Functions efficient handling of buffered files
 */

#if !defined(SVN_FILE_H) && defined(ENABLE_SVN_FILE)
#define SVN_FILE_H

#include "svn_types.h"
#include "svn_error.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* svn_file_t build upon apr_file_t but come with the following improvements
 * and limitations:
 *
 * + unlimited number of instances while limiting the number of open
 *   file handles used per process
 * + efficient forward & backward navigation due to using 2 buffers
 *   (code allows for even more)
 * + aligned data access
 * + user-defined buffer sizes
 * + seek()s will be deferred until the next data access
 * + read-after-write does not force buffer to be flushed
 * + efficient file size / eof detection
 * + low-overhead getc / putc functions
 *
 * - only applicable to random access files
 * - concurrent access to the same file must not change the file size
 * - file open flags limited to SVN_FILE__SUPPORTED_FLAGS
 */

/* Returns the maximum number of OS-level file handles that shall be used.
 * 
 * Please note that this limit may be exceeded in heavily multi-threaded
 * applications if more threads than this limit are interacting with files
 * at the same time.
 */
apr_size_t
svn_file__get_max_shared_handles(void);

/* Allow up to NEW_MAX OS-level file handles to be open at the same time.
 * The limit may be changed at any time.  0 is a valid limit.
 */
svn_error_t *
svn_file__set_max_shared_handles(apr_size_t new_max);

/* Opaque file data type.
 */
typedef struct svn_file_t svn_file_t;

/* These file open flags are set implicitly on all files.  Specifying
 * them or not in svn_file__open will have no effect.
 */
#define SVN_FILE__IMPLICIT_FLAGS \
        (APR_BINARY | APR_BUFFERED | APR_XTHREAD)

/* Only these file open flags are allowed with svn_file__open.
 * Using other flags will trigger an assertion.
 */
#define SVN_FILE__SUPPORTED_FLAGS \
        (SVN_FILE__IMPLICIT_FLAGS | APR_READ | APR_WRITE | APR_CREATE \
                                  | APR_APPEND | APR_TRUNCATE | APR_EXCL)

/* Create a file object in *RESULT for the file NAME and the given open
 * FLAGs.  Use data buffers of BUFFER_SIZE each (must be a power of 2).
 * If DEFER_CREATION is set, no APR file handle will be allocated and
 * the disc contents remain unchanged until the first data access.
 * 
 * Allocate data in POOL.  The file will be closed automatically when
 * POOL gets cleaned up.
 */
svn_error_t *
svn_file__open(svn_file_t **result,
               const char *name,
               apr_int32_t flag,
               apr_size_t buffer_size,
               svn_boolean_t defer_creation,
               apr_pool_t *pool);

/* Close file object FILE.  All modified buffers will written back to
 * disk and the underlying APR file handle (if any) be closed.
 */
svn_error_t *
svn_file__close(svn_file_t *file);

/* Read TO_READ bytes from the current position in FILE and write them
 * into DATA and set *READ to the number of bytes actually read.  The only
 * reason why *READ may differ from  TO_READ is hitting eof.  If READ is
 * NULL in that case, the function will return an error.  If HIT_EOF is
 * not NULL, it will also indicate whether eof is just after the last
 * byte returned.
 */
svn_error_t *
svn_file__read(svn_file_t *file,
               void *data,
               apr_size_t to_read,
               apr_size_t *read,
               svn_boolean_t *hit_eof);

/* Read one byte from FILE and return it in *DATA.  The file pointer must
 * not be on eof.
 */
svn_error_t *
svn_file__getc(svn_file_t *file,
               char *data);

/* Write TO_WRITE bytes from DATA to the current position in FILE.
 */
svn_error_t *
svn_file__write(svn_file_t *file,
                const void *data,
                apr_size_t to_write);

/* Write one byte DATA to the current position in FILE.
 */
svn_error_t *
svn_file__putc(svn_file_t *file,
               char data);

/* Return the size of FILE in *FILE_SIZE.
 */
svn_error_t *
svn_file__get_size(apr_off_t *file_size,
                   svn_file_t *file);

/* Move the file pointer of FILE to the absolute POSITION >= 0.
 */
svn_error_t *
svn_file__seek(svn_file_t *file,
               apr_off_t position);

/* Return the current position of the read / write pointer of FILE.
 */
apr_off_t
svn_file__get_position(svn_file_t *file);

/* Set the file size of FILE to its current position.
 */
svn_error_t *
svn_file__truncate(svn_file_t *file);

/* Set *EOF to TRUE, if FILE's position is at or behind eof.
 */
svn_error_t *
svn_file__at_eof(svn_boolean_t *eof,
                 svn_file_t *file);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_FILE_H */
