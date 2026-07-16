#!/bin/bash
#
# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
#
# VSched is licensed under the Mulan PSL v2.
# You can use this software according to the terms and conditions of the Mulan PSL v2.
# You may obtain a copy of Mulan PSL v2 at:
#      http://license.coscl.org.cn/MulanPSL2
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
# See the Mulan PSL v2 for more details.
#
set -e

# Define color
BLUE='\033[0;34m'   # blue
NC='\033[0m'        # Colorless

# Get the working directory of the current script
current_directory=$(pwd)

# Obtain the path passed from external sources
HTML_PATH=${1:-"${current_directory}/cmake-build-debug/coverage/index.html"}

# Find all python3 -m http.server processes
process_info=$(pgrep -af "python3 -m http.server")

# Flag variable indicating whether a matching process was found.
found_matching_process=false

if [[ -n "$process_info" ]]; then

    # Traverse each process
    while read -r line; do
        # Extract the PID of the process
        pid=$(echo "$line" | awk '{print $1}')

        # Obtaining the Working Directory
        working_directory=$(readlink -f /proc/$pid/cwd)

        # Determine whether the working directories are the same
        if [[ "$working_directory" == "$current_directory" ]]; then
            # Obtaining the IP address and port number
            PORT=$(echo "$line" | grep -oP '\d{4,5}(?= --bind)')
            if [[ "$line" =~ --bind\ ([0-9]+\.[0-9]+\.[0-9]+\.[0-9]+) ]]; then
                IP=${BASH_REMATCH[1]}
            fi

            echo "Server is already running on IP: $IP and Port: $PORT"
            found_matching_process=true
            break
        fi
    done <<< "$process_info"

    if ! $found_matching_process; then
        echo "No matching server processes found in the current working directory."
    fi
else
    echo "No python3 -m http.server processes are running."
fi

# If no process with the same working directory as the current one is found, start the server.
if ! $found_matching_process; then
    # Obtain available IP addresses
    IP=$(hostname -I | awk '{print $1}')

    # Find Available Ports
    PORT=$(seq 8000 9000 | grep -v -x -f <(nc -z localhost -w 1 -v $(seq 8000 9000) 2>&1 | awk '{print $3}') | head -n 1)

    if [ -n "$PORT" ]; then
        echo "Starting HTTP server at $IP:$PORT"
        python3 -m http.server $PORT --bind $IP >/dev/null 2>/dev/null &
        echo "Server started successfully."
    else
        echo "No available port found."
    fi
fi

echo -e "\nOpen the link below to view the coverage report:\n\n${BLUE}http://$IP:$PORT${HTML_PATH}${NC}\n\n"