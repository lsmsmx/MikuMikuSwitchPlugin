#include "fs.hpp"
#include "lib.hpp"

//
void WriteLog(const char* format, ...) {
     char logBuffer[512];
     va_list args;
     va_start(args, format);
     int len = vsnprintf(logBuffer, sizeof(logBuffer), format, args);
     va_end(args);

     if (len <= 0) return;

     const char* logPath = "ExlSD:/hook_log.txt";
     nn::fs::FileHandle handle;
     std::vector<uint8_t> fileBuffer;

     Result res = nn::fs::OpenFile(&handle, logPath, nn::fs::OpenMode_Write);
     if (R_FAILED(res)) {
         nn::fs::CreateFile(logPath, 0);
         res = nn::fs::OpenFile(&handle, logPath, nn::fs::OpenMode_Write);
     }

     if (R_SUCCEEDED(res)) {
         int64_t fileSize = 0;
         nn::fs::GetFileSize(&fileSize, handle);
         if (fileSize > 0) {
             fileBuffer.resize(fileSize);
             nn::fs::ReadFile(handle, 0, fileBuffer.data(), fileSize);
         }
         nn::fs::CloseFile(handle);
     }

     size_t oldSize = fileBuffer.size();
     fileBuffer.resize(oldSize + len);
     std::memcpy(fileBuffer.data() + oldSize, logBuffer, len);

     nn::fs::DeleteFile(logPath);
     nn::fs::CreateFile(logPath, fileBuffer.size());

     if (R_SUCCEEDED(nn::fs::OpenFile(&handle, logPath, nn::fs::OpenMode_Write))) {
         nn::fs::WriteFile(
             handle,
             0,
             fileBuffer.data(),
             fileBuffer.size(),
             nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush)
         );
         nn::fs::CloseFile(handle);
     }
}
