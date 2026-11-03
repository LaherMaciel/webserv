#include "Log.hpp"
#include <fcntl.h>
#include <stdexcept>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
    const char *const LABELS[] = { "ERROR", "WARN", "INFO", "DEBUG" };

    bool appendClean(std::string &entry, const std::string &text, size_t limit)
    {
        size_t i = 0;
        while (i < text.size() && entry.size() < limit)
        {
            unsigned char c = static_cast<unsigned char>(text[i++]);
            entry += (c < 32 || c == 127) ? ' ' : static_cast<char>(c);
        }
        return i < text.size();
    }
}

Log::Log(const std::string &path, Level threshold)
    : fd_(-1), threshold_(threshold), offset_(0), dropped_(0), reportedDropped_(0)
{
    pending_.reserve(MAX_QUEUE_BYTES);
    // Non-blocking open also prevents waiting on a FIFO supplied as a log path.
    fd_ = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_NONBLOCK, 0644);
    if (fd_ == -1)
        throw std::runtime_error("log: cannot open " + path);
    struct stat info;
    if (stat(path.c_str(), &info) == -1 || !S_ISREG(info.st_mode))
    {
        close(fd_);
        fd_ = -1;
        throw std::runtime_error("log: output must be a regular file: " + path);
    }
}

Log::~Log()
{
    if (fd_ != -1)
        close(fd_);
}

bool Log::enabled(Level level) const
{
    return fd_ != -1 && level >= ERROR && level <= DEBUG && level <= threshold_;
}

void Log::record(Level level, const std::string &component,
                 const std::string &message)
{
    if (!enabled(level))
        return;
    std::string entry = std::string("[") + LABELS[level] + "] ";
    bool truncated = appendClean(entry, component, entry.size() + 64);
    entry += ": ";
    if (appendClean(entry, message, MAX_ENTRY_BYTES - 4))
        truncated = true;
    if (truncated)
        entry += "...";
    entry += '\n';
    if (entry.size() > MAX_QUEUE_BYTES - (pending_.size() - offset_))
    {
        ++dropped_;
        return;
    }
    if (offset_ != 0)
    {
        pending_.erase(0, offset_);
        offset_ = 0;
    }
    pending_ += entry;
}

void Log::reportDropped()
{
    if (dropped_ == reportedDropped_ || !enabled(WARN))
        return;
    std::ostringstream message;
    message << "[WARN] Log: dropped=" << dropped_ - reportedDropped_
            << " messages (queue full)\n";
    const std::string entry = message.str();
    if (entry.size() > MAX_QUEUE_BYTES - (pending_.size() - offset_))
        return;
    if (offset_ != 0)
    {
        pending_.erase(0, offset_);
        offset_ = 0;
    }
    pending_ += entry;
    reportedDropped_ = dropped_;
}

bool Log::flush()
{
    if (fd_ == -1)
        return false;
    reportDropped();
    size_t remaining = pending_.size() - offset_;
    if (remaining == 0)
        return true;
    size_t count = remaining < MAX_FLUSH_BYTES ? remaining : MAX_FLUSH_BYTES;
    // Regular files are exempt from poll; the byte budget bounds each attempt,
    // but a disk write can still wait on storage.
    ssize_t written = write(fd_, pending_.c_str() + offset_, count);
    if (written <= 0)
    {
        close(fd_);
        fd_ = -1;
        pending_.clear();
        offset_ = 0;
        return false;
    }
    offset_ += static_cast<size_t>(written);
    if (offset_ == pending_.size())
    {
        pending_.clear();
        offset_ = 0;
    }
    return true;
}

int Log::getFd() const { return fd_; }

size_t Log::droppedCount() const { return dropped_; }
