dnl ===================================================================
dnl   Licensed to the Apache Software Foundation (ASF) under one
dnl   or more contributor license agreements.  See the NOTICE file
dnl   distributed with this work for additional information
dnl   regarding copyright ownership.  The ASF licenses this file
dnl   to you under the Apache License, Version 2.0 (the
dnl   "License"); you may not use this file except in compliance
dnl   with the License.  You may obtain a copy of the License at
dnl
dnl     http://www.apache.org/licenses/LICENSE-2.0
dnl
dnl   Unless required by applicable law or agreed to in writing,
dnl   software distributed under the License is distributed on an
dnl   "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
dnl   KIND, either express or implied.  See the License for the
dnl   specific language governing permissions and limitations
dnl   under the License.
dnl ===================================================================
dnl
dnl   SVN_FUNC_QSORT_R()
dnl
dnl   Check if qsort_r or equivalent exists, and determine the order
dnl   of the arguments.

AC_DEFUN(SVN_FUNC_QSORT_R,
[
  AC_CHECK_FUNCS(qsort_r, [
    AC_MSG_CHECKING([qsort_r argument order])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([
          #include <stdlib.h>
          static int reorder_args;
          static int normal(void *c, const void *ka, const void *kb)
          {
            reorder_args = 0;
            return *(int*)ka - *(int*)kb;
          }
          static int skewed(void *c, const void *ka, const void *kb)
          {
            reorder_args = 1;
            return *(int*)ka - *(int*)kb;
          }
          static int check_qsort_args_order(void)
          {
            static int a[4] = { 5, 11, 3, 7 };
            qsort_r(a, 4, sizeof(int), normal, skewed);
            return reorder_args;
          }
    ],[
          return check_qsort_args_order();
    ])],[
      AC_MSG_RESULT([normal])
      AC_DEFINE(SVN_QSORT_R_NORMAL_ARG_ORDER, 1,
                [Define to 1 if qsort_r takes the context as the last argument])
    ],[
      AC_MSG_RESULT([skewed])
    ])
  ], [])
])
