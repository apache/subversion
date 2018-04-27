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
##     SVNBB_BDB                Berkeley DB installation prefix
##     SVNBB_SWIG               Swig installation prefix
##     SVNBB_SERF               Serf installation prefix
##                              Note: Serf should be built only
##                                    with the system APR/-Util.
##     SVNBB_APR_13_NOTHREAD    Path of APR-1.3 with threading disabled
##     SVNBB_APR_15             Path of APR-1.5
##     SVNBB_APR_20_DEV         Path of APR-2.0
##     SVNBB_JUNIT              The path of the junit.jar
##     SVNBB_PARALLEL           Optional: parallelization; defaults to 2
##     SVNBB_PYTHON3ENV         Optional: Python 3 virtual environment
##
## The invoking script will set local variable named ${scripts} that
## is the absolute path the parent of this file.

# Modify this to suit your deployment
environment=$(cd "${scripts}/../.." && pwd)/environment.sh

eval $(${environment})
SVNBB_PARALLEL="${SVNBB_PARALLEL-2}"

export PATH
export SVNBB_BDB
export SVNBB_SWIG
export SVNBB_SERF
export SVNBB_APR_13_NOTHREAD
export SVNBB_APR_15
export SVNBB_APR_20_DEV
export SVNBB_JUNIT
export SVNBB_PARALLEL
export SVNBB_PYTHON3ENV


# Set the absolute source path
abssrc=$(pwd)

# Set the path to the RAMdisk device name file
ramconf=$(dirname "${abssrc}")/ramdisk.conf

# The RAMdisk volume name is the same as the name of the builder
volume_name=$(basename $(dirname "${abssrc}"))
if [ -z "${volume_name}" ]; then
    echo "Missing config parameter: RAMdisk volume name"
    exit 1
fi

# Set the absolute build path
absbld="/Volumes/${volume_name}"
