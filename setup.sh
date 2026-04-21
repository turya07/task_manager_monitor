#!/bin/bash

cmake --build build
cp build/task_monitor .
echo "\33[1;32mCompilation complete. You can run the Task Monitor with ./task_monitor\33[0m"
