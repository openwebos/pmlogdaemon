#!/bin/sh

# @@@LICENSE
#
#      Copyright (c) 2007-2013 LG Electronics, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# LICENSE@@@
TMP=`mktemp -t logd_rdx.XXXXXXXXXXXX`

DIR=$1
if [ -z "$DIR" ]; then
    DIR=.
fi

find $1 -type f | xargs du -sk > $TMP

echo $TMP