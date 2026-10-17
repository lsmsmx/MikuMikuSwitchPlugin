#include "fs.hpp"
#include "lib.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>

static nn::fs::FileHandle s_LogHandle;
static bool s_LogFileReady = false;
static int64_t s_CurrentFileOffset = 0;

static thread_local bool s_InsideLog = false;

void InitLoggerOnce() {
    if (s_LogFileReady) return;

    const char* logPath = "ExlSD:/hook_log.txt";

    nn::fs::DeleteFile(logPath);
    nn::fs::CreateFile(logPath, 0);

    Result res = nn::fs::OpenFile(&s_LogHandle, logPath, nn::fs::OpenMode_Write | nn::fs::OpenMode_Append);
    if (R_SUCCEEDED(res)) {
        s_LogFileReady = true;
        s_CurrentFileOffset = 0;
    }
}

void WriteLog(const char* format, ...) {
    if (s_InsideLog) return;
    s_InsideLog = true;

    char logBuffer[512];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(logBuffer, sizeof(logBuffer), format, args);
    va_end(args);

    if (len > 0) {
        InitLoggerOnce();

        if (s_LogFileReady) {
            nn::fs::WriteFile(
                s_LogHandle,
                s_CurrentFileOffset,
                logBuffer,
                len,
                nn::fs::WriteOption::CreateOption(0)
            );
            s_CurrentFileOffset += len;
        }
    }

    s_InsideLog = false;
}
