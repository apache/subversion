/*
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

#include <httpd.h>
#include <http_core.h>
#include <http_config.h>
#include <http_request.h>
#include <http_protocol.h>

#include "dav_svn.h"
#include "private/svn_cache.h"
#include "private/svn_fs_private.h"

#ifdef HAVE_UNISTD_H
#include <unistd.h>   /* For getpid() */
#endif

#if APR_HAVE_PROCESS_H
#include <process.h>
#endif

/* The apache headers define these and they conflict with our definitions. */
#ifdef PACKAGE_BUGREPORT
#undef PACKAGE_BUGREPORT
#endif
#ifdef PACKAGE_NAME
#undef PACKAGE_NAME
#endif
#ifdef PACKAGE_STRING
#undef PACKAGE_STRING
#endif
#ifdef PACKAGE_TARNAME
#undef PACKAGE_TARNAME
#endif
#ifdef PACKAGE_VERSION
#undef PACKAGE_VERSION
#endif
#include "svn_private_config.h"

#ifndef DEFAULT_TIME_FORMAT
#define DEFAULT_TIME_FORMAT "%Y-%m-%d %H:%M:%S %Z"
#endif

/* A bit like mod_status: add a location:

     <Location /svn-status>
       SetHandler svn-status
     </Location>

  and then point a browser at http://server/svn-status.
*/
int dav_svn__status(request_rec *r)
{
  svn_cache__info_t *info;
  svn_string_t *text_stats;
  apr_array_header_t *lines;
  int i;

  if (r->method_number != M_GET || strcmp(r->handler, "svn-status"))
    return DECLINED;

  info = svn_cache__membuffer_get_global_info(r->pool);
  text_stats = svn_cache__format_info(info, FALSE, r->pool);
  lines = svn_cstring_split(text_stats->data, "\n", FALSE, r->pool);

  ap_set_content_type(r, "text/html; charset=ISO-8859-1");

  ap_rvputs(r,
            DOCTYPE_HTML_3_2
            "<html><head>\n"
            "<title>Apache SVN Status</title>\n"
            "</head><body>\n"
            "<h1>Apache SVN Cache Status for ",
            ap_escape_html(r->pool, ap_get_server_name(r)),
            " (via ",
            r->connection->local_ip,
            ")</h1>\n<dl>\n<dt>Server Version: ",
            ap_get_server_description(),
            "</dt>\n<dt>Current Time: ",
            ap_ht_time(r->pool, apr_time_now(), DEFAULT_TIME_FORMAT, 0),
            "</dt>\n", SVN_VA_NULL);

#if defined(WIN32) || (defined(HAVE_UNISTD_H) && defined(HAVE_GETPID))
  /* On Unix the server is generally multiple processes and this
     request only shows the status of the single process that handles
     the request. Ideally we would iterate over all processes but that
     would need some MPM support, so we settle for simply showing the
     process ID. */
  ap_rprintf(r, "<dt>Server process id: %d</dt>\n", (int)getpid());
#endif

  for (i = 0; i < lines->nelts; ++i)
    {
      const char *line = APR_ARRAY_IDX(lines, i, const char *);
      ap_rvputs(r, "<dt>", line, "</dt>\n", SVN_VA_NULL);
    }

  ap_rvputs(r, "</dl></body></html>\n", SVN_VA_NULL);

  return 0;
}
