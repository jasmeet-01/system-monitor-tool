# System Monitor Tool

A simple cross-platform system monitor for Linux (tested on Ubuntu/WSL), written in C++.  
It shows CPU and memory usage, process list, supports sorting, killing processes, and logs system history.

## Features

- Live display of CPU and memory usage
- View currently running processes (PID, name, memory)
- Sort process list by CPU or memory usage
- Kill any process by entering its PID
- Logs system stats to `sysmonitor_log.csv`
- View usage history directly from the tool

## Build & Run

### Requirements

- Linux (Ubuntu or WSL recommended)
- C++11 or higher
- g++
