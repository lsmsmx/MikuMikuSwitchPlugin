#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <vector>

#include <nn/fs.hpp>

// Buffered file I/O for the save system.
//
// This heap refuses a single large contiguous allocation long before it is
// actually out of memory (see UsmSupport.cpp: a 34 MB malloc fails with 155 MB
// free). The save files used to be built as one multi-megabyte vector and read
// back the same way, which made every save/load a gamble. Both directions go
// through a 64 KB staging buffer here, so peak cost is 64 KB per file instead
// of several MB, and the file contents are byte-for-byte unchanged.
namespace savestream
{

    constexpr size_t kChunkSize = 256 * 1024;

    // Sequential writer. The file must already exist with its final size.
    class Writer
    {
    public:
        ~Writer() { Close(); }

        bool Open(const char* path, int64_t fileSize)
        {
            Close();
            if (R_FAILED(nn::fs::OpenFile(&m_handle, path, nn::fs::OpenMode_Write)))
                return false;

            m_open = true;
            m_size = fileSize;
            m_offset = 0;
            m_fill = 0;

            if (m_buffer.size() != kChunkSize)
                m_buffer.resize(kChunkSize);

            return true;
        }

        bool Write(const void* src, size_t size)
        {
            const uint8_t* p = static_cast<const uint8_t*>(src);

            while (size > 0)
            {
                const size_t space = kChunkSize - m_fill;
                const size_t n = size < space ? size : space;

                memcpy(&m_buffer[m_fill], p, n);
                m_fill += n;
                p += n;
                size -= n;

                if (m_fill == kChunkSize && !Flush(false))
                    return false;
            }

            return true;
        }

        // The old code wrote a zero-filled vector of the full size, so if the
        // maps shrank after the size was computed the tail stayed zeroed.
        bool PadToEnd()
        {
            static const uint8_t zeros[4096] = { };

            while (Remaining() > 0)
            {
                const size_t n = Remaining() < static_cast<int64_t>(sizeof(zeros))
                    ? static_cast<size_t>(Remaining())
                    : sizeof(zeros);

                if (!Write(zeros, n))
                    return false;
            }

            return true;
        }

        bool Close()
        {
            if (!m_open)
                return true;

            const bool ok = Flush(true);
            nn::fs::CloseFile(m_handle);
            m_open = false;
            return ok;
        }

    private:
        int64_t Remaining() const { return m_size - m_offset - static_cast<int64_t>(m_fill); }

        bool Flush(bool flushToDisk)
        {
            if (m_fill == 0)
                return true;

            if (m_offset + static_cast<int64_t>(m_fill) > m_size)
                return false;

            const nn::fs::WriteOption option = flushToDisk
                ? nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush)
                : nn::fs::WriteOption{};

            if (R_FAILED(nn::fs::WriteFile(m_handle, m_offset, m_buffer.data(), m_fill, option)))
                return false;

            m_offset += static_cast<int64_t>(m_fill);
            m_fill = 0;
            return true;
        }

        nn::fs::FileHandle m_handle = {};
        std::vector<uint8_t> m_buffer;
        int64_t m_size = 0;
        int64_t m_offset = 0;
        size_t m_fill = 0;
        bool m_open = false;
    };

    // Sequential reader, same 64 KB staging buffer.
    class Reader
    {
    public:
        ~Reader() { Close(); }

        bool Open(const char* path)
        {
            Close();
            if (R_FAILED(nn::fs::OpenFile(&m_handle, path, nn::fs::OpenMode_Read)))
                return false;

            long size = 0;
            if (R_FAILED(nn::fs::GetFileSize(&size, m_handle)) || size <= 0)
            {
                nn::fs::CloseFile(m_handle);
                return false;
            }

            m_open = true;
            m_fileSize = size;
            m_readPos = 0;
            m_bufPos = 0;
            m_bufLen = 0;

            if (m_buffer.size() != kChunkSize)
                m_buffer.resize(kChunkSize);

            return true;
        }

        int64_t Size() const { return m_fileSize; }

        // Restart from the beginning of the file.
        void Rewind()
        {
            m_readPos = 0;
            m_bufPos = 0;
            m_bufLen = 0;
        }

        int64_t Remaining() const
        {
            return (m_fileSize - m_readPos) + (static_cast<int64_t>(m_bufLen) - static_cast<int64_t>(m_bufPos));
        }

        // Reads exactly `size` bytes. Returns false on a short file.
        bool Read(void* dst, size_t size)
        {
            uint8_t* d = static_cast<uint8_t*>(dst);

            while (size > 0)
            {
                if (m_bufPos == m_bufLen && !Fill())
                    return false;

                const size_t avail = m_bufLen - m_bufPos;
                const size_t n = size < avail ? size : avail;

                memcpy(d, &m_buffer[m_bufPos], n);
                m_bufPos += n;
                d += n;
                size -= n;
            }

            return true;
        }

        void Close()
        {
            if (!m_open)
                return;

            nn::fs::CloseFile(m_handle);
            m_open = false;
        }

    private:
        bool Fill()
        {
            if (m_readPos >= m_fileSize)
                return false;

            const int64_t left = m_fileSize - m_readPos;
            const size_t n = left < static_cast<int64_t>(kChunkSize) ? static_cast<size_t>(left) : kChunkSize;

            if (R_FAILED(nn::fs::ReadFile(m_handle, m_readPos, m_buffer.data(), n)))
                return false;

            m_bufPos = 0;
            m_bufLen = n;
            m_readPos += static_cast<int64_t>(n);
            return true;
        }

        nn::fs::FileHandle m_handle = {};
        std::vector<uint8_t> m_buffer;
        int64_t m_fileSize = 0;
        int64_t m_readPos = 0;
        size_t m_bufPos = 0;
        size_t m_bufLen = 0;
        bool m_open = false;
    };
}
