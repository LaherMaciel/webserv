#include "Log.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

static size_t writeCalls = 0;
static size_t writeLimit = static_cast<size_t>(-1);
static size_t largestWrite = 0;
static bool failWrite = false;

// Only the test build of Log.cpp redirects write() to this controllable sink.
extern "C" ssize_t logTestWrite(int fd, const void *data, size_t count)
{
    ++writeCalls;
    if (count > largestWrite)
        largestWrite = count;
    if (failWrite)
        return -1;
    return ::write(fd, data, count < writeLimit ? count : writeLimit);
}

static void expect(bool condition, const char *message)
{
    if (!condition)
        throw std::runtime_error(message);
}

static std::string readAll(const std::string &path)
{
    std::ifstream input(path.c_str(), std::ios::binary);
    std::ostringstream contents;
    contents << input.rdbuf();
    return contents.str();
}

static void testFiltering(const std::string &root)
{
    const std::string path = root + "/levels.log";
    int ownedFd;
    {
        Log log(path, Log::WARN);
        ownedFd = log.getFd();
        expect(log.enabled(Log::ERROR) && log.enabled(Log::WARN)
               && !log.enabled(Log::INFO) && !log.enabled(Log::DEBUG), "level filtering");
        log.record(Log::DEBUG, "Test", "hidden debug");
        log.record(Log::INFO, "Test", "hidden info");
        log.record(Log::WARN, "Test", "warning");
        log.record(Log::ERROR, "Test", "failure");
        expect(readAll(path).empty(), "record must not perform output I/O");
        expect(log.flush(), "level flush");
        expect(readAll(path) == "[WARN] Test: warning\n[ERROR] Test: failure\n", "formatted levels");
        expect(log.droppedCount() == 0, "filtered entries are not queue drops");
    }
    expect(::write(ownedFd, "x", 1) == -1, "destructor closes the owned descriptor");
    Log defaults(root + "/default.log");
    expect(defaults.enabled(Log::INFO) && !defaults.enabled(Log::DEBUG), "default INFO level");
    std::cout << "PASS  filtering, formatting, deferred I/O and descriptor lifetime\n";
}

static void testTruncation(const std::string &root)
{
    const std::string path = root + "/entries.log";
    Log log(path, Log::DEBUG);
    log.record(Log::INFO, "Component\nInjected", "first\r\nforged entry");
    log.record(Log::DEBUG, "Test", std::string(10000, 'x'));
    expect(log.flush(), "entry flush");
    const std::string contents = readAll(path);
    size_t firstEnd = contents.find('\n');
    expect(contents.substr(0, firstEnd) == "[INFO] Component Injected: first  forged entry",
           "entries cannot introduce extra lines");
    std::string second = contents.substr(firstEnd + 1);
    expect(second.size() == Log::MAX_ENTRY_BYTES, "entry cap includes newline and prefix");
    expect(second.substr(second.size() - 4) == "...\n", "truncation marker");
    std::cout << "PASS  capped entries and single-line formatting\n";
}

static void testQueue(const std::string &root)
{
    const std::string path = root + "/queue.log";
    Log log(path);
    const std::string large(1000, 'q');
    const size_t entries = Log::MAX_QUEUE_BYTES / Log::MAX_ENTRY_BYTES;
    size_t before = writeCalls;
    for (size_t i = 0; i < entries; ++i)
        log.record(Log::INFO, "Test", large);
    log.record(Log::ERROR, "Test", "queue full");
    expect(log.droppedCount() == 1 && writeCalls == before, "full queue drops without writing");
    expect(log.flush() && writeCalls == before + 1, "one write per flush");
    expect(readAll(path).size() == Log::MAX_FLUSH_BYTES, "flush byte budget");
    for (size_t i = 0; i < Log::MAX_FLUSH_BYTES / Log::MAX_ENTRY_BYTES; ++i)
        log.record(Log::INFO, "Test", large);
    log.record(Log::ERROR, "Test", "queue full again");
    expect(log.droppedCount() == 2, "space from partial draining can be reused");
    for (size_t i = 0; i < entries; ++i)
    {
        before = writeCalls;
        expect(log.flush() && writeCalls - before <= 1, "bounded flush attempts");
    }
    expect(largestWrite <= Log::MAX_FLUSH_BYTES, "every requested write is capped");
    const std::string summary = "[WARN] Log: dropped=2 messages (queue full)\n";
    const std::string contents = readAll(path);
    expect(contents.size() == Log::MAX_QUEUE_BYTES + Log::MAX_FLUSH_BYTES + summary.size(),
           "queue cap and accepted entry accounting");
    size_t summaryAt = contents.find(summary);
    expect(summaryAt != std::string::npos && contents.find(summary, summaryAt + 1) == std::string::npos,
           "dropped entries are summarized once when space becomes available");
    expect(log.droppedCount() == 2, "reporting drops does not count as another drop");
    before = writeCalls;
    expect(log.flush() && writeCalls == before, "empty queue does not repeat drop summary");
    std::cout << "PASS  queue limit, dropped summary, flush budget and queue reuse\n";
}

static void testPartialWrites(const std::string &root)
{
    const std::string path = root + "/partial.log";
    Log log(path);
    writeLimit = 7;
    log.record(Log::INFO, "Test", "first");
    expect(log.flush() && readAll(path).size() == 7, "partial write retained");
    log.record(Log::WARN, "Test", "second");
    for (size_t i = 0; i < 16; ++i)
        expect(log.flush(), "partial flush");
    writeLimit = static_cast<size_t>(-1);
    expect(readAll(path) == "[INFO] Test: first\n[WARN] Test: second\n",
           "partial writes neither lose nor duplicate bytes when appending");
    std::cout << "PASS  partial writes preserve entries across new records\n";
}

static void testFailure(const std::string &root)
{
    Log log(root + "/failure.log");
    log.record(Log::ERROR, "Test", "pending");
    failWrite = true;
    expect(!log.flush() && log.getFd() == -1, "failed output disables and closes the sink");
    size_t before = writeCalls;
    log.record(Log::ERROR, "Test", "ignored after failure");
    expect(!log.enabled(Log::ERROR) && !log.flush() && writeCalls == before,
           "failed output is not retried");
    failWrite = false;
    Log zero(root + "/zero.log");
    zero.record(Log::INFO, "Test", "pending");
    writeLimit = 0;
    expect(!zero.flush() && zero.getFd() == -1, "zero-byte writes disable the sink");
    writeLimit = static_cast<size_t>(-1);
    std::cout << "PASS  output failure disables logging without retries\n";
}

static void testAppendAndStartup(const std::string &root)
{
    const std::string path = root + "/append.log";
    {
        std::ofstream previous(path.c_str());
        previous << "previous\n";
    }
    {
        Log log(path);
        log.record(Log::INFO, "Test", "new");
        log.flush();
        log.record(Log::INFO, "Test", "not flushed by destructor");
    }
    expect(readAll(path) == "previous\n[INFO] Test: new\n", "append mode and explicit flushing");
    bool rejected = false;
    try { Log invalid(root); }
    catch (const std::exception &) { rejected = true; }
    expect(rejected, "invalid output fails at startup");
    rejected = false;
    try { Log fifo(root + "/fifo"); }
    catch (const std::exception &) { rejected = true; }
    expect(rejected, "FIFO is rejected without waiting for a reader");
    std::cout << "PASS  append mode, explicit shutdown policy and invalid destinations\n";
}

int main(int argc, char **argv)
{
    if (argc != 2)
        return 1;
    try
    {
        testFiltering(argv[1]);
        testTruncation(argv[1]);
        testQueue(argv[1]);
        testPartialWrites(argv[1]);
        testFailure(argv[1]);
        testAppendAndStartup(argv[1]);
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL  " << error.what() << '\n';
        return 1;
    }
    return 0;
}
