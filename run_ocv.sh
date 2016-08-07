#!/bin/bash

OCV_PATH=opencv-3.1.0/lib/

make v4l2_ocv_fd_ot
if [ $? != 0 ]; then
	echo "unable to compile"
	exit -1
fi
LD_LIBRARY_PATH=$OCV_PATH/ ./v4l2_ocv_fd_ot
