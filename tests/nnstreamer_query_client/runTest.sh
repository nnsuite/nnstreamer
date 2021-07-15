#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-only
#
# @file runTest.sh
# @author Gichan Jang <gichan2.jang@samsung.com>
# @date July 15 2021
# @brief SSAT Test Cases for tensor query client
#
if [[ "$SSATAPILOADED" != "1" ]]; then
    SILENT=0
    INDEPENDENT=1
    search="ssat-api.sh"
    source $search
    printf "${Blue}Independent Mode${NC}"
fi

# This is compatible with SSAT (https://github.com/myungjoo/SSAT)
testInit $1

PATH_TO_PLUGIN="../../build"
SRC_PORT=`python3 ../nnstreamer_grpc/get_available_port.py`
SINK_PORT=`python3 ../nnstreamer_grpc/get_available_port.py`

# Run echo server
gst-launch-1.0 tcpserversrc port=${SRC_PORT} ! tcpserversink port=${SINK_PORT} & sleep 5
gstTest "--gst-plugin-path=${PATH_TO_PLUGIN} videotestsrc num-buffers=3 ! videoconvert ! videoscale ! video/x-raw, width=30, height=30 ! tensor_converter ! tee name = t t. ! queue ! multifilesink location= raw_%1d.log t. ! queue ! tensor_query_client sink_port=${SINK_PORT} src_port=${SRC_PORT} ! multifilesink location=result_%1d.log" 1 0 0 $PERFORMANCE
callCompareTest raw_0.log result_0.log 1-1 "Compare 1-1" 1 0
callCompareTest raw_1.log result_1.log 1-2 "Compare 1-2" 1 0
callCompareTest raw_2.log result_2.log 1-3 "Compare 1-3" 1 0

rm -f *.log

report
