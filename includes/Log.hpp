#ifndef LOG_HPP
# define LOG_HPP

# include <cstddef>
# include <string>

class Log
{
    public:
        enum Level { ERROR, WARN, INFO, DEBUG };
        enum Limits { MAX_ENTRY_BYTES = 512, MAX_QUEUE_BYTES = 32768,
                      MAX_FLUSH_BYTES = 4096 };

        Log(const std::string &path, Level threshold = INFO);
        ~Log();

        bool enabled(Level level) const;
        void record(Level level, const std::string &component,
                    const std::string &message);
        bool flush();
        int getFd() const;
        size_t droppedCount() const;

    private:
        int fd_;
        Level threshold_;
        std::string pending_;
        size_t offset_;
        size_t dropped_;
        size_t reportedDropped_;

        void reportDropped();

        Log(const Log &other);
        Log &operator=(const Log &other);
};

#endif
