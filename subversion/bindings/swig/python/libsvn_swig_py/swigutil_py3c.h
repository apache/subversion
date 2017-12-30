/*
 * swigutil_py3c.c: utility header for the SWIG Python binding interface with
 * the py3c library
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

#ifndef SVN_SWIG_SWIGUTIL_PY3C_H
#define SVN_SWIG_SWIGUTIL_PY3C_H

/* This file needs to be included after any swig includes, as it undefines
 * certain conflicting items, where the py3c variants are preferred over those
 * defined within SWIG.
 */

#include <Python.h>

#if PY_VERSION_HEX >= 0x03000000
/* SWIG and py3c both define a few Python 3compat defines, so undef here to give
   preference to the py3c versions. */
#undef PyLong_FromSize_t
#undef PyLong_AsLong
#undef PyInt_FromLong
#undef PyInt_AsLong
#undef PyInt_Check
#undef PyInt_FromSize_t

#endif

#include <py3c.h>

#endif
