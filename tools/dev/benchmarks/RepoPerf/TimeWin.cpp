/* TimeWin.cpp --- A simple Windows tool inspired by Unix' "time".
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

#include "targetver.h"

#include <Windows.h>

#include <stdio.h>
#include <tchar.h>

void usage()
{
  _tprintf(_T("Execute a command, redirect its stdout to NUL and print\n"));
  _tprintf(_T("execution times ELAPSED\\tUSER\\tKERNEL in seconds.\n"));
  _tprintf(_T("\n"));
  _tprintf(_T("Usage: TimeWin.EXE COMMAND [PARAMETERS]\n"));
}

LPCTSTR skip_first_arg(LPCTSTR targv)
{
  LPCTSTR s = _tcschr(targv, ' ');
  while (s && *s == ' ')
    ++s;

  return s;
}

double as_seconds(FILETIME time)
{
  return (double)*reinterpret_cast<LONGLONG *>(&time) / 10000000.0;
}

int _tmain(int argc, LPTSTR argv[])
{
  // Minimal CL help support
  if (argc < 2 || _tcscmp(argv[1], _T("/?")) == 0)
    {
      usage();
      return 0;
    }

  // Get a file handle for NUL.
  SECURITY_ATTRIBUTES sa;
  sa.nLength = sizeof(sa);
  sa.lpSecurityDescriptor = NULL;
  sa.bInheritHandle = TRUE;

  HANDLE nul = CreateFile(_T("nul"), FILE_APPEND_DATA, FILE_SHARE_WRITE,
                          &sa, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

  // Construct a process startup info that uses the same handles as this
  // one but redirects stdout to NUL.
  STARTUPINFO startup_info;
  GetStartupInfo(&startup_info);
  startup_info.dwFlags |= STARTF_USESTDHANDLES;
  startup_info.hStdOutput = nul;

  // Execute the command line.
  PROCESS_INFORMATION process_info;
  CreateProcess(NULL, _tscdup(skip_first_arg(GetCommandLine())), NULL, NULL,
                TRUE, NORMAL_PRIORITY_CLASS, NULL, NULL, &startup_info,
                &process_info);

  // Get a handle with the needed access rights to the child process.
  HANDLE child = INVALID_HANDLE_VALUE;
  DuplicateHandle(GetCurrentProcess(), process_info.hProcess,
                  GetCurrentProcess(), &child,
                  PROCESS_QUERY_INFORMATION | SYNCHRONIZE, FALSE, 0);

  // Wait for the child to finish.
  // If there was problem earlier (application not found etc.), this will fail.
  bool success = false;
  if (WaitForSingleObject(child, INFINITE) == WAIT_OBJECT_0)
    {
      // Finally, query the timers and show the result
      FILETIME start_time, end_time, user_time, kernel_time;
      if (GetProcessTimes(child, &start_time, &end_time, &kernel_time,
                          &user_time))
        {
          _tprintf(_T("%1.3f\t%1.3f\t%1.3f\n"),
                   as_seconds(end_time) - as_seconds(start_time),
                   as_seconds(user_time), as_seconds(kernel_time));
          success = true;
        }
    }

  // In case of failure, give some indication that something went wrong.
  if (!success)
    _tprintf(_T("?.???\t?.???f\t?.???\n"),

  // Be good citizens and clean up our mess
  CloseHandle(child);
  CloseHandle(process_info.hThread);
  CloseHandle(process_info.hProcess);

  CloseHandle(nul);

  return 0;
}
