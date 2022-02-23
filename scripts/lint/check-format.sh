#!/bin/bash
SCRIPTPATH="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
ROOTPATH=$SCRIPTPATH/../../

$ROOTPATH/scripts/lint/git-clang-format.sh origin/main
$ROOTPATH/scripts/lint/git-black.sh origin/main
