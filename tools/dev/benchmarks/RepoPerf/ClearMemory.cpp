/* ClearMemory.cpp --- A simple Window memory cleaning tool
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

int _tmain(int argc, _TCHAR* argv[])
{
  // Get the current memory usage stats
  MEMORYSTATUSEX statex;
  statex.dwLength = sizeof (statex);
  GlobalMemoryStatusEx(&statex);

  // (Clean) cache memory will be listed under "available".
  // So, allocate all available RAM, touch it and release it again.
  unsigned char *memory = new unsigned char[statex.ullAvailPhys];
  if (memory)
    {
      // Make every page dirty.
      for (DWORDLONG i = 0; i < statex.ullAvailPhys; i += 4096)
        memory[i]++;

      // Give everything back to the OS.
      // The in-RAM file read cache is empty now. There may still be bits in
      // the swap file as well as dirty write buffers.  But we don't care
      // much about these here ...
      delete memory;
    }

  return 0;
}

