#include "fs.hpp"
#include "lib.hpp"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

static nn::fs::FileHandle s_LogHandle;
static bool s_LogFileReady = false;
static int64_t s_CurrentFileOffset = 0;

// Recursion guard.  A log call can itself trigger a log call (e.g. from inside
// nn::fs); that is strictly a same-thread condition, so this stays thread_local
// and is checked *before* the lock below.
//
// Nested lines are no longer dropped: they are queued here and written by the
// outermost call once the fs is idle again.  Dropping them was silently losing
// exactly the lines that sit next to the crash ("serve: parse=...", "writing
// container", "mkv: header flushed ..."), which made the end of the log point
// at the wrong syscall.
static thread_local bool s_InsideLog = false;
static thread_local char s_Pending[2048];
static thread_local size_t s_PendingLen = 0;

// Serializes the file state above.
//
// The previous version had no lock at all, so two game threads could both read
// s_CurrentFileOffset, both write at that offset, and both add their length --
// one line silently overwrote the other.  That is why "[USM] IS_USM",
// "cancelMovieTask -> N" and friends were missing from hook_log.txt, and why
// "IVF at +0x" printed with no digits at all: a concurrent writer had clobbered
// exactly those bytes.  Nothing downstream of a corrupt log is trustworthy, so
// this lock has to come first.
//
// Function-local static (rather than a file-scope one) so it cannot be used
// before its constructor has run -- WriteLog is reachable very early.
static std::mutex& LogMutex() {
    static std::mutex m;
    return m;
}

static void InitLoggerOnceLocked() {
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

// Takes the lock, writes one already-formatted line.  Deliberately does not
// call WriteLog, so nothing in here can recurse into the guard.
static void EmitLocked(const char* data, int len) {
    std::lock_guard<std::mutex> lock(LogMutex());
    InitLoggerOnceLocked();
    if (!s_LogFileReady) return;

    // Offset and handle are only ever touched under the lock, so a line written
    // by one thread can no longer land on top of another's.
    const auto opt = nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush);
    Result res = nn::fs::WriteFile(s_LogHandle, s_CurrentFileOffset, data, len, opt);
    if (R_FAILED(res)) {
        // One retry.  A failed write used to lose its line while the offset
        // still advanced, so the next line landed after a hole the size of the
        // lost one -- the log then read as if the missing line never ran, and
        // that put the crash at the wrong syscall more than once.
        res = nn::fs::WriteFile(s_LogHandle, s_CurrentFileOffset, data, len, opt);
    }
    if (R_SUCCEEDED(res)) s_CurrentFileOffset += len;
}

void WriteLog(const char* format, ...) {
    char logBuffer[512];
    va_list args;
    va_start(args, format);
    int len = vsnprintf(logBuffer, sizeof(logBuffer), format, args);
    va_end(args);

    if (len <= 0) return;
    // vsnprintf returns the length it *would* have written; clamp so we never
    // hand the fs a length larger than the buffer.
    if (len > (int)sizeof(logBuffer) - 1) len = (int)sizeof(logBuffer) - 1;

    if (s_InsideLog) {
        if (s_PendingLen + (size_t)len <= sizeof(s_Pending)) {
            std::memcpy(s_Pending + s_PendingLen, logBuffer, (size_t)len);
            s_PendingLen += (size_t)len;
        }
        return;
    }

    s_InsideLog = true;
    EmitLocked(logBuffer, len);
    if (s_PendingLen != 0) {
        EmitLocked(s_Pending, (int)s_PendingLen);
        s_PendingLen = 0;
    }
    s_InsideLog = false;
}
