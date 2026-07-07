#!/bin/bash
cd "$(dirname "$0")/game" && exec ../server/build/server "$@"
