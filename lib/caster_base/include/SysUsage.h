#pragma once
#include <chrono>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <fstream>
#endif

class SysUsage
{
public:
    static SysUsage *getInstance()
    {
        static SysUsage *instance = new SysUsage();
        return instance;
    }

    SysUsage()
    {
        lastCPU = lastSysCPU = lastUserCPU = {0};
#if defined(_WIN32)
        SYSTEM_INFO sysInfo;
        FILETIME ftime, fsys, fuser;

        GetSystemInfo(&sysInfo);
        numProcessors = sysInfo.dwNumberOfProcessors;

        GetSystemTimeAsFileTime(&ftime);
        memcpy(&lastCPU, &ftime, sizeof(FILETIME));

        HANDLE self = GetCurrentProcess();
        GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);
        memcpy(&lastSysCPU, &fsys, sizeof(FILETIME));
        memcpy(&lastUserCPU, &fuser, sizeof(FILETIME));
#else
        numProcessors = sysconf(_SC_NPROCESSORS_ONLN);
        lastTime = std::chrono::steady_clock::now();
        readProcStat(lastUTime, lastSTime);
#endif
    }

    // ---------------------------
    // 获取 CPU 占用率 (%)
    // ---------------------------
double getProcessCPU()
{
#if defined(_WIN32)
    FILETIME ftime, fsys, fuser;
    ULARGE_INTEGER now, sys, user;

    GetSystemTimeAsFileTime(&ftime);
    memcpy(&now, &ftime, sizeof(FILETIME));

    HANDLE self = GetCurrentProcess();
    GetProcessTimes(self, &ftime, &ftime, &fsys, &fuser);

    memcpy(&sys, &fsys, sizeof(FILETIME));
    memcpy(&user, &fuser, sizeof(FILETIME));

    double sysDiff = (sys.QuadPart - lastSysCPU.QuadPart);
    double userDiff = (user.QuadPart - lastUserCPU.QuadPart);
    double totalDiff = (now.QuadPart - lastCPU.QuadPart);

    lastCPU = now;
    lastUserCPU = user;
    lastSysCPU = sys;

    // -----------------------------
    // 单核占用：不除核心数
    // -----------------------------
    double cpu = (sysDiff + userDiff) * 100.0 / totalDiff;
    return cpu;
#else
    unsigned long long u, s;
    readProcStat(u, s);

    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - lastTime).count();

    unsigned long long du = u - lastUTime;
    unsigned long long ds = s - lastSTime;

    lastUTime = u;
    lastSTime = s;
    lastTime = now;

    // -----------------------------
    // 单核占用：不除核心数
    // -----------------------------
    double cpu = (du + ds) / (double)sysconf(_SC_CLK_TCK) * 100.0 / dt;
    return cpu;
#endif
}
    // ---------------------------
    // 获取当前进程的内存占用（bytes）
    // ---------------------------
    size_t getProcessMemory()
    {
#if defined(_WIN32)
        PROCESS_MEMORY_COUNTERS pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
        return pmc.WorkingSetSize; // 常驻内存
#else
        std::ifstream file("/proc/self/statm");
        long rss = 0;
        file >> rss >> rss; // 第二个字段是 RSS（页数）
        return (size_t)rss * sysconf(_SC_PAGESIZE);
#endif
    }

private:
#if !defined(_WIN32)
    void readProcStat(unsigned long long &u, unsigned long long &s)
    {
        std::ifstream f("/proc/self/stat");
        if (!f.is_open())
            return;
        std::string tmp;
        for (int i = 0; i < 13; ++i)
            f >> tmp; // skip
        f >> u >> s;  // utime, stime
    }
#endif

private:
    int numProcessors;

#if defined(_WIN32)
    ULARGE_INTEGER lastCPU, lastSysCPU, lastUserCPU;
#else
    unsigned long long lastUTime = 0, lastSTime = 0;
    std::chrono::steady_clock::time_point lastTime;
#endif
};
