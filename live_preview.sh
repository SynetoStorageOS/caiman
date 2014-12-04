#!/usr/bin/env bash

# Run text installer in debug mode
if [ ! -d proto ]; then
	echo "You must have the sources compiled before running live preview"
	exit 1
fi

WS=`pwd`
export PYTHONPATH=${WS}/proto/root_i386/usr/snadm/lib:${WS}/proto/root_i386/usr/lib/python2.6/vendor-packages

cd usr/src/cmd/text-install/osol_install/
pfexec python2.6 text_install/text-install -d
