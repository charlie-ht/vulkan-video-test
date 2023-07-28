#!/bin/bash

env LD_LIBRARY_PATH=$HOME/src/openh264/builddir gdb -i=mi --args ./decoder $@
