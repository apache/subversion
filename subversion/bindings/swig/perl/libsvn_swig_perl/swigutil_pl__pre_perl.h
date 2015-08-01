/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#ifndef SVN_SWIG_SWIGUTIL_PL__PRE_PERL_H
#define SVN_SWIG_SWIGUTIL_PL__PRE_PERL_H

/* Ruby 5.8 somehow expects Visual C++ to be gcc compatible for __inline__.
   Add a #define to make the default headers happy and avoid an insane
   number of warnings */

#ifdef _MSC_VER
#define __inline__ __inline
#endif

#endif
