#include <sys/statvfs.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>

using namespace std;

// ------------------------------------------------------------
// Read CPU statistics from /proc/stat
// ------------------------------------------------------------
struct CPUStats {
    long long user = 0;
    long long nice = 0;
    long long system = 0;
    long long idle = 0;
    long long iowait = 0;
    long long irq = 0;
    long long softirq = 0;
    long long steal = 0;

    long long total() const {
        return user + nice + system + idle +
               iowait + irq + softirq + steal;
    }

    long long idle_time() const {
        return idle + iowait;
    }
};

bool read_cpu_stats(CPUStats& stats) {

    ifstream file("/proc/stat");

    if (!file) {
        return false;
    }

    string line;

    while (getline(file, line)) {

        if (line.rfind("cpu ", 0) == 0) {

            string cpu_label;

            stringstream ss(line);

            ss >> cpu_label
               >> stats.user
               >> stats.nice
               >> stats.system
               >> stats.idle
               >> stats.iowait
               >> stats.irq
               >> stats.softirq
               >> stats.steal;

            return true;
        }
    }

    return false;
}

// ------------------------------------------------------------
// Calculate CPU usage
// ------------------------------------------------------------
double get_cpu_usage(
    const CPUStats& previous,
    const CPUStats& current) {

    long long previous_total = previous.total();
    long long current_total = current.total();

    long long total_difference =
        current_total - previous_total;

    long long idle_difference =
        current.idle_time() -
        previous.idle_time();

    if (total_difference <= 0) {
        return 0.0;
    }

    double usage =
        100.0 *
        (1.0 -
         static_cast<double>(idle_difference) /
         static_cast<double>(total_difference));

    return usage;
}

// ------------------------------------------------------------
// Read memory information from /proc/meminfo
// ------------------------------------------------------------
bool read_memory(
    double& total_gb,
    double& used_gb,
    double& available_gb) {

    ifstream file("/proc/meminfo");

    if (!file) {
        return false;
    }

    string line;

    long long total_kb = 0;
    long long available_kb = 0;

    while (getline(file, line)) {

        string key;
        long long value;
        string unit;

        stringstream ss(line);

        ss >> key >> value >> unit;

        if (key == "MemTotal:") {
            total_kb = value;
        }

        else if (key == "MemAvailable:") {
            available_kb = value;
        }
    }

    if (total_kb <= 0) {
        return false;
    }

    long long used_kb =
        total_kb - available_kb;

    total_gb =
        static_cast<double>(total_kb) /
        (1024.0 * 1024.0);

    used_gb =
        static_cast<double>(used_kb) /
        (1024.0 * 1024.0);

    available_gb =
        static_cast<double>(available_kb) /
        (1024.0 * 1024.0);

    return true;
}

// ------------------------------------------------------------
// Read disk usage using statvfs()
// ------------------------------------------------------------
bool read_disk_usage(
    double& total_gb,
    double& used_gb,
    double& free_gb) {

    struct statvfs disk_info{};

    if (statvfs(".", &disk_info) != 0) {
        return false;
    }

    unsigned long long total_bytes =
        static_cast<unsigned long long>(
            disk_info.f_blocks
        ) *
        disk_info.f_frsize;

    unsigned long long free_bytes =
        static_cast<unsigned long long>(
            disk_info.f_bavail
        ) *
        disk_info.f_frsize;

    unsigned long long used_bytes =
        total_bytes - free_bytes;

    total_gb =
        static_cast<double>(total_bytes) /
        (1024.0 * 1024.0 * 1024.0);

    used_gb =
        static_cast<double>(used_bytes) /
        (1024.0 * 1024.0 * 1024.0);

    free_gb =
        static_cast<double>(free_bytes) /
        (1024.0 * 1024.0 * 1024.0);

    return true;
}

// ------------------------------------------------------------
// Display one monitoring snapshot
// ------------------------------------------------------------
void display_monitor_data(
    double cpu_usage,
    double memory_total,
    double memory_used,
    double memory_available,
    double disk_total,
    double disk_used,
    double disk_free) {

    cout << "\n========================================\n";
    cout << " Linux System Resource Monitor\n";
    cout << "========================================\n";

    cout << fixed << setprecision(2);

    cout << "CPU Usage       : "
         << cpu_usage
         << " %\n";

    cout << "Memory Total    : "
         << memory_total
         << " GB\n";

    cout << "Memory Used     : "
         << memory_used
         << " GB\n";

    cout << "Memory Available: "
         << memory_available
         << " GB\n";

    cout << "Disk Total      : "
         << disk_total
         << " GB\n";

    cout << "Disk Used       : "
         << disk_used
         << " GB\n";

    cout << "Disk Free       : "
         << disk_free
         << " GB\n";

    cout << "========================================\n";
}

// ------------------------------------------------------------
// Main
// ------------------------------------------------------------
int main() {

    cout << "========================================\n";
    cout << " Linux-Based System Resource Monitor\n";
    cout << "========================================\n";

    cout << "Monitoring Linux system resources...\n";
    cout << "Press Ctrl+C to stop.\n";

    CPUStats previous_cpu;

    if (!read_cpu_stats(previous_cpu)) {

        cerr << "Error reading /proc/stat\n";

        return 1;
    }

    while (true) {

        // Wait before collecting second CPU sample
        this_thread::sleep_for(
            chrono::seconds(2)
        );

        CPUStats current_cpu;

        if (!read_cpu_stats(current_cpu)) {

            cerr << "Error reading CPU statistics.\n";

            continue;
        }

        double cpu_usage =
            get_cpu_usage(
                previous_cpu,
                current_cpu
            );

        previous_cpu = current_cpu;

        double memory_total = 0.0;
        double memory_used = 0.0;
        double memory_available = 0.0;

        if (!read_memory(
                memory_total,
                memory_used,
                memory_available)) {

            cerr << "Error reading memory information.\n";
            continue;
        }

        double disk_total = 0.0;
        double disk_used = 0.0;
        double disk_free = 0.0;

        if (!read_disk_usage(
                disk_total,
                disk_used,
                disk_free)) {

            cerr << "Error reading disk information.\n";
            continue;
        }

        display_monitor_data(
            cpu_usage,
            memory_total,
            memory_used,
            memory_available,
            disk_total,
            disk_used,
            disk_free
        );
    }

    return 0;
}
