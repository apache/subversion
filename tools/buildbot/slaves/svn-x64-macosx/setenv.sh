#  Licensed to the Apache Software Foundation (ASF) under one
#  or more contributor license agreements.  See the NOTICE file
#  distributed with this work for additional information
#  regarding copyright ownership.  The ASF licenses this file
#  to you under the Apache License, Version 2.0 (the
#  "License"); you may not use this file except in compliance
#  with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing,
#  software distributed under the License is distributed on an
#  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
#  KIND, either express or implied.  See the License for the
#  specific language governing permissions and limitations
#  under the License.

## This script calls a helper that provides the folloing environemnt
## variables:
##
##     PATH                     The search path
##     SVNBB_OPENSSL            OpenSSL installation prefix
##     SVNBB_BDB                Berkeley DB installation prefix
##     SVNBB_SWIG               Swig installation prefix
##     SVNBB_SERF               Serf installation prefix
##                              Note: Serf should be built only
##                                    with the system APR/-Util.
##     SVNBB_APR_13_NOTHREAD    Path of APR-1.3 with threading disabled
##     SVNBB_APR_15             Path of APR-1.5
##     SVNBB_APR_20_DEV         Path of APR-2.0
##     SVNBB_JUNIT              The path of the junit.jar
##
## The invoking script will set local variable named ${scripts} that
## is the absolute path the parent of this file.

# Modify this to suit your deployment
environment=$(cd "${scripts}/../tools" && pwd)/environment.sh

eval $(${environment})
export PATH
export SVNBB_BDB
export SVNBB_SWIG
export SVNBB_SERF
export SVNBB_APR_13_NOTHREAD
export SVNBB_APR_15
export SVNBB_APR_20_DEV
export SVNBB_JUNIT


# A file named 'ramdisk' containing the uniqe RAM disk volume name
# used by the build slave must be present in the same directory as the
# scripts.

if [ ! -f "${scripts}/ramdisk" ]; then
    echo "Missing config file: 'ramdisk'"
    exit 1
fi

# Set the name of the RAMdisk volume
volume_name=$(head -1 "${scripts}/ramdisk")

# Set the absolute source path
abssrc=$(pwd)

# Set the absolute build path
absbld="/Volumes/${volume_name}"
