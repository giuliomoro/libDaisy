#!/bin/bash
# Uses clang-format-10.0.0
#TODO:
# - fix all the hard-coding
STARTDIR='./src/'
find $STARTDIR -iname '*.h' -type f -exec sed -i 's/\t/    /g' {} +
find $STARTDIR -iname '*.cpp' -type f -exec sed -i 's/\t/    /g' {} +
clang-format -style=file -i $STARTDIR/*.cpp $STARTDIR/*.h
