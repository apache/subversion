#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to You under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import subprocess as __subprocess

# check_output() is only available in Python 2.7. Allow us to run with
# earlier versions
try:
    __check_output = __subprocess.check_output
    def check_output(args, env=None, universal_newlines=False):
        return __check_output(args, shell=False, env=env,
                              universal_newlines=universal_newlines)
except AttributeError:
    def check_output(args, env=None, universal_newlines=False):
        # note: we only use these three args
        pipe = __subprocess.Popen(args, shell=False, env=env,
                                  stdout=__subprocess.PIPE,
                                  universal_newlines=universal_newlines)
        output, _ = pipe.communicate()
        if pipe.returncode:
            raise subprocess.CalledProcessError(pipe.returncode, args)
        return output
