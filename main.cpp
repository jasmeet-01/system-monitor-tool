#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <ctime>
#include <string>
#include <dirent.h>
#include <unistd.h>
#include <algorithm>
#include <thread>
#include <chrono>
#include <termios.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>

struct CpuTimes {
    long long user, nice, system, idle, iowait, irq, softirq, steal;
};

struct ProcessInfo {
    int pid;
    std::string name;
    long mem_usage_kb;
    double cpu_usage;  // Placeholder, currently zero
};

// Read CPU times
CpuTimes read_cpu_times() {
    std::ifstream file("/proc/stat");
    std::string line;
    std::getline(file, line);
    std::istringstream iss(line);
    std::string cpu_label;
    CpuTimes times{};
    iss >> cpu_label >> times.user >> times.nice >> times.system >> times.idle >> times.iowait
        >> times.irq >> times.softirq >> times.steal;
    return times;
}

// Calculate CPU usage percentage
double calculate_cpu_usage(const CpuTimes& prev, const CpuTimes& curr) {
    long long prev_idle = prev.idle + prev.iowait;
    long long curr_idle = curr.idle + curr.iowait;

    long long prev_non_idle = prev.user + prev.nice + prev.system + prev.irq + prev.softirq + prev.steal;
    long long curr_non_idle = curr.user + curr.nice + curr.system + curr.irq + curr.softirq + curr.steal;

    long long prev_total = prev_idle + prev_non_idle;
    long long curr_total = curr_idle + curr_non_idle;

    long long totald = curr_total - prev_total;
    long long idled = curr_idle - prev_idle;

    if (totald == 0) return 0.0;
    return (totald - idled) * 100.0 / totald;
}

// Read memory info
std::pair<long long, long long> read_memory() {
    std::ifstream file("/proc/meminfo");
    std::string line;
    long long mem_total = 0, mem_available = 0;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string key;
        long long value;
        std::string unit;
        iss >> key >> value >> unit;
        if (key == "MemTotal:") mem_total = value;
        if (key == "MemAvailable:") mem_available = value;
    }
    return {mem_total, mem_available};
}

// Get list of PIDs
std::vector<int> get_pids() {
    std::vector<int> pids;
    DIR* proc = opendir("/proc");
    if (!proc) return pids;
    struct dirent* entry;
    while ((entry = readdir(proc)) != nullptr) {
        if (entry->d_type == DT_DIR) {
            std::string dir_name = entry->d_name;
            if (std::all_of(dir_name.begin(), dir_name.end(), ::isdigit)) {
                pids.push_back(std::stoi(dir_name));
            }
        }
    }
    closedir(proc);
    return pids;
}

// Get process info
ProcessInfo get_process_info(int pid) {
    ProcessInfo info;
    info.pid = pid;
    info.cpu_usage = 0.0;  // Placeholder

    std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
    if (comm.is_open())
        std::getline(comm, info.name);
    comm.close();

    std::ifstream statm("/proc/" + std::to_string(pid) + "/statm");
    long size = 0, resident = 0;
    if (statm.is_open())
        statm >> size >> resident;
    statm.close();

    long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
    info.mem_usage_kb = resident * page_size_kb;

    return info;
}

// Non-blocking keyboard input check
int kbhit() {
    static const int STDIN = 0;
    static bool initialized = false;
    static struct termios oldt, newt;

    if (!initialized) {
        tcgetattr(STDIN, &oldt); // Save old settings
        newt = oldt;
        newt.c_lflag &= ~(ICANON | ECHO); // Disable buffering and echo
        tcsetattr(STDIN, TCSANOW, &newt);
        setbuf(stdin, NULL);
        initialized = true;
    }

    int bytesWaiting;
    ioctl(STDIN, FIONREAD, &bytesWaiting);
    return bytesWaiting;
}

// Kill process by PID
bool kill_process(int pid) {
    if (kill(pid, SIGKILL) == 0) {
        return true;
    } else {
        perror("kill");
        return false;
    }
}

// Clear screen helper
void clear_screen() {
    std::cout << "\033[2J\033[1;1H";
}

// Display system info
void display_system_info(double cpu_usage, long long mem_used, long long mem_total) {
    std::cout << "System Monitor Tool\n\n";
    std::cout << "CPU Load: " << cpu_usage << " %\n";
    std::cout << "Memory Usage: " << mem_used / 1024 << " MB / " << mem_total / 1024 << " MB\n\n";
}

// Display process list sorted by chosen mode
void display_process_list(char sort_mode) {
    std::vector<int> pids = get_pids();
    std::vector<ProcessInfo> processes;
    for (int pid : pids) {
        processes.push_back(get_process_info(pid));
    }

    if (sort_mode == 'c') {
        std::sort(processes.begin(), processes.end(),
                  [](const ProcessInfo& a, const ProcessInfo& b) {
                      return a.cpu_usage > b.cpu_usage;
                  });
    } else {
        std::sort(processes.begin(), processes.end(),
                  [](const ProcessInfo& a, const ProcessInfo& b) {
                      return a.mem_usage_kb > b.mem_usage_kb;
                  });
    }

    std::cout << "PID\tProcess Name\tMemory (KB)" << std::endl;
    std::cout << "-----------------------------------------" << std::endl;
    for (const auto& proc : processes) {
        std::cout << proc.pid << "\t" << proc.name << "\t\t" << proc.mem_usage_kb << std::endl;
    }
}

// Logging function to append stats to file
void log_stats(const std::string& filename, const std::string& timestamp, double cpu_load, long long mem_used, long long mem_total) {
    std::ofstream logfile(filename, std::ios::app);
    logfile << timestamp << "," << cpu_load << "," << mem_used / 1024 << "," << mem_total / 1024 << "\n";
}

// Read and display last N log entries
void display_history(const std::string& filename, int last_n = 10) {
    std::ifstream logfile(filename);
    if (!logfile) {
        std::cout << "No history log found.\n";
        return;
    }

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(logfile, line)) {
        lines.push_back(line);
    }

    int start = (lines.size() > last_n) ? lines.size() - last_n : 0;
    std::cout << "Timestamp, CPU Load %, Memory Used MB, Memory Total MB\n";
    std::cout << "-------------------------------------------------------\n";
    for (int i = start; i < lines.size(); ++i) {
        std::cout << lines[i] << std::endl;
    }
}


int main() {
    const std::string log_filename = "sysmonitor_log.csv";
    CpuTimes prev_cpu = read_cpu_times();
    char sort_mode = 'm'; // default memory sort
    bool running = true;
    
    std::ifstream check_file(log_filename);
    if (!check_file.good()) {
        std::ofstream logfile(log_filename, std::ios::app);
        logfile << "Timestamp,CPU Load %,Memory Used MB,Memory Total MB\n";
    }
    check_file.close();
    
    while (running) {
        clear_screen();

        CpuTimes curr_cpu = read_cpu_times();
        double cpu_usage = calculate_cpu_usage(prev_cpu, curr_cpu);
        prev_cpu = curr_cpu;

        auto [mem_total, mem_available] = read_memory();
        long long mem_used = mem_total - mem_available;

        display_system_info(cpu_usage, mem_used, mem_total);
        std::cout << "Sort by: [c] CPU%  [m] Memory%  [k] Kill Process [h] History  [q] Quit\n\n";
        display_process_list(sort_mode);

	// Log current stats
        std::time_t t = std::time(nullptr);
        char ts_buf[100];
        std::strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        log_stats(log_filename, ts_buf, cpu_usage, mem_used, mem_total);


        std::cout << "\nEnter command: ";
        char command;
        std::cin >> command;

        switch(command) {
            case 'c':
            case 'm':
                sort_mode = command;
                break;

            case 'k': {
                std::cout << "Enter PID to kill: ";
                int pid;
                std::cin >> pid;
                if (kill_process(pid)) {
                    std::cout << "Process " << pid << " killed successfully.\n";
                } else {
                    std::cout << "Failed to kill process " << pid << ".\n";
                }
                std::cout << "Press enter to continue...";
                std::cin.ignore();
                std::cin.get();
                break;
            }

	    case 'h': {
                clear_screen();
                std::cout << "System Monitor History (last 10 records):\n\n";
                display_history(log_filename);
                std::cout << "\nPress enter to return...";
                std::cin.ignore();
                std::cin.get();
                break;
            }

            case 'q':
                running = false;
                break;

            default:
                std::cout << "Invalid command.\n";
                std::cout << "Press enter to continue...";
                std::cin.ignore();
                std::cin.get();
                break;
        }
    }
    return 0;
}
