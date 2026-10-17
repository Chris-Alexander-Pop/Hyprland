#include "Instance.hpp"
#include "State.hpp"
#include "../helpers/Logger.hpp"
#include "../helpers/Nix.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <ranges>
#include <string_view>

#if defined(__linux__)
#include <sys/prctl.h>
#elif defined(__FreeBSD__)
#include <signal.h>
#include <sys/procctl.h>
#elif defined(__OpenBSD__)
#include <signal.h>
#endif

#include <hyprutils/os/Process.hpp>

using namespace Hyprutils::OS;
using namespace std::string_literals;

static const char* signalName(int sig) {
    switch (sig) {
        case SIGKILL: return "SIGKILL";
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGTERM: return "SIGTERM";
        case SIGBUS: return "SIGBUS";
        case SIGILL: return "SIGILL";
        case SIGFPE: return "SIGFPE";
        case SIGPIPE: return "SIGPIPE";
        case SIGXCPU: return "SIGXCPU";
        case SIGSYS: return "SIGSYS";
        default: return "unknown";
    }
}

static std::filesystem::path hyprlandCacheDir() {
    if (const auto CACHE = getenv("XDG_CACHE_HOME"); CACHE && CACHE[0] != '\0')
        return std::filesystem::path{CACHE} / "hyprland";
    if (const auto HOME = getenv("HOME"); HOME && HOME[0] != '\0')
        return std::filesystem::path{HOME} / ".cache" / "hyprland";
    return "/tmp/hyprland";
}

// Hyprland prints its banner and early DEBUG to stdout/stderr before the
// config turns stdout logs off. If those fds are still the login tty, the
// text lands on the getty. Journal stdout stays put. A tty stderr is moved
// onto that journal fd. If stdout itself is the tty, both go to a file.
static void detachChildFromLoginTty() {
    if (!isatty(STDOUT_FILENO) && !isatty(STDERR_FILENO))
        return;

    if (!isatty(STDOUT_FILENO)) {
        if (dup2(STDOUT_FILENO, STDERR_FILENO) < 0)
            return;
        return;
    }

    const auto      dir = hyprlandCacheDir();
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const int fd = open((dir / "early.log").c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0)
        return;
    dup2(fd, STDOUT_FILENO);
    dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO)
        close(fd);
}

static void copyNewestHyprlandLog() {
    const auto RUNTIME = getenv("XDG_RUNTIME_DIR");
    if (!RUNTIME || RUNTIME[0] == '\0')
        return;

    std::error_code                 ec;
    const std::filesystem::path     root{std::string{RUNTIME} + "/hypr"};
    std::filesystem::path           newest;
    std::filesystem::file_time_type newestTime{};
    for (const auto& ent : std::filesystem::directory_iterator(root, ec)) {
        if (ec)
            break;
        const auto log = ent.path() / "hyprland.log";
        if (!std::filesystem::is_regular_file(log, ec) || ec)
            continue;
        const auto written = std::filesystem::last_write_time(log, ec);
        if (ec)
            continue;
        if (newest.empty() || written > newestTime) {
            newest     = log;
            newestTime = written;
        }
    }
    if (newest.empty())
        return;

    const auto destDir = hyprlandCacheDir() / "last-crash";
    std::filesystem::create_directories(destDir, ec);
    if (ec)
        return;

    const auto now = std::chrono::system_clock::now();
    const auto t   = std::chrono::system_clock::to_time_t(now);
    std::tm    local{};
    localtime_r(&t, &local);
    char stamp[32];
    if (std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local) == 0)
        return;

    const auto stamped = destDir / (std::string{"hyprland-"} + stamp + ".log");
    std::filesystem::copy_file(newest, stamped, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        g_logger->log(Hyprutils::CLI::LOG_ERR, "Failed to copy {} to {}: {}", newest.string(), stamped.string(), ec.message());
        return;
    }
    std::filesystem::copy_file(newest, hyprlandCacheDir() / "last-hyprland.log", std::filesystem::copy_options::overwrite_existing, ec);
    g_logger->log(Hyprutils::CLI::LOG_ERR, "Saved Hyprland log to {}", stamped.string());
}

//
void CHyprlandInstance::runHyprlandThread(bool safeMode, bool lockedCrash) {
    std::vector<std::string> argsStd;
    argsStd.emplace_back("--watchdog-fd");
    argsStd.emplace_back(std::format("{}", m_toHlPid.get()));
    if (safeMode)
        argsStd.emplace_back("--safe-mode");

    if (lockedCrash)
        argsStd.emplace_back("--locked");

    for (const auto& a : g_state->rawArgvNoBinPath) {
        argsStd.emplace_back(a);
    }

    // spawn a process manually. Hyprutils' Async is detached, while Sync redirects stdout
    // TODO: make Sync respect fds?

    std::vector<char*> args = {strdup(g_state->customPath.value_or("Hyprland").c_str())};
    for (const auto& a : argsStd) {
        args.emplace_back(strdup(a.c_str()));
    }
    args.emplace_back(nullptr);

    int forkRet = fork();
    if (forkRet == 0) {
        // Make hyprland die on our SIGKILL
#if defined(__linux__)
        prctl(PR_SET_PDEATHSIG, SIGKILL);
#elif defined(__FreeBSD__)
        int sig = SIGKILL;
        procctl(P_PID, getpid(), PROC_PDEATHSIG_CTL, &sig);
#endif

        detachChildFromLoginTty();

        if (Nix::shouldUseNixGL()) {
            argsStd.insert(argsStd.begin(), g_state->customPath.value_or("Hyprland"));
            args.insert(args.begin(), strdup(argsStd.front().c_str()));
            execvp("nixGL", args.data());
        } else
            execvp(g_state->customPath.value_or("Hyprland").c_str(), args.data());

        g_logger->log(Hyprutils::CLI::LOG_ERR, "fork(): execvp failed: {}", strerror(errno));
        std::fflush(stdout);
        exit(1);
    } else
        m_hlPid = forkRet;

    m_hlThread = std::thread([this] {
        bool saveLog = false;
        while (true) {
            int status = 0;
            int ret    = waitpid(m_hlPid, &status, 0);
            if (ret == -1) {
                if (errno == EINTR)
                    continue;
                g_logger->log(Hyprutils::CLI::LOG_ERR, "Couldn't waitpid for hyprland: {}", strerror(errno));
                saveLog = true;
                break;
            }

            if (WIFEXITED(status)) {
                const int code = WEXITSTATUS(status);
                g_logger->log(code == 0 ? Hyprutils::CLI::LOG_DEBUG : Hyprutils::CLI::LOG_ERR, "Hyprland exited with status {}", code);
                saveLog = code != 0;
                break;
            }

            if (WIFSIGNALED(status)) {
                const int sig = WTERMSIG(status);
                g_logger->log(Hyprutils::CLI::LOG_ERR, "Hyprland killed by signal {} ({})", sig, signalName(sig));
                saveLog = true;
                break;
            }
        }

        if (saveLog)
            copyNewestHyprlandLog();

        if (write(m_wakeupWrite.get(), "vax", 3) < 0)
            g_logger->log(Hyprutils::CLI::LOG_ERR, "Failed to write to wakeup fd {}: {}", m_wakeupWrite.get(), strerror(errno));

        std::fflush(stdout);
        std::fflush(stderr);
    });
}

void CHyprlandInstance::forceQuit() {
    m_hyprlandExiting = true;
    kill(m_hlPid, SIGTERM); // gracefully, can get stuck but it's unlikely

    m_hlThread.join(); // needs this otherwise can crash
}

void CHyprlandInstance::clearFd(const Hyprutils::OS::CFileDescriptor& fd) {
    if (!fd.isReadable()) {
        g_logger->log(Hyprutils::CLI::LOG_ERR, "Can't clear a unreadable fd");
        return;
    }

    static std::array<char, 1024> buf;
    if (read(fd.get(), buf.data(), 1023) < 0)
        g_logger->log(Hyprutils::CLI::LOG_ERR, "Failed clearing fd {}: {}", fd.get(), strerror(errno));
}

void CHyprlandInstance::dispatchHyprlandEvent() {
    std::string                   recvd = "";
    static std::array<char, 4096> buf;
    ssize_t                       n = read(m_fromHlPid.get(), buf.data(), 4096);
    if (n < 0) {
        g_logger->log(Hyprutils::CLI::LOG_ERR, "Failed dispatching hl events");
        return;
    }

    recvd.append(buf.data(), n);

    if (recvd.empty())
        return;

    for (const auto& s : std::views::split(recvd, '\n')) {
        const std::string_view sv = std::string_view{s};
        if (sv == "vax") {
            // init passed
            m_hyprlandInitialized = true;
            continue;
        }

        if (sv == "end") {
            // exiting
            m_hyprlandExiting = true;
            continue;
        }

        if (sv == "normal") {
            // recovery config asked to boot the real config
            m_restartNormal   = true;
            m_hyprlandExiting = true;
            continue;
        }

        if (sv == "lock") {
            // session locked
            m_hyprlandLocked = true;
            continue;
        }

        if (sv == "unlock") {
            // session unlocked
            m_hyprlandLocked = false;
            continue;
        }
    }
}

bool CHyprlandInstance::run(bool safeMode, bool lockedCrash) {
    int pipefds[2];
    if (pipe(pipefds) != 0) {
        g_logger->log(Hyprutils::CLI::LOG_ERR, "pipe() failed, exiting");
        exit(1);
    }

    m_fromHlPid = CFileDescriptor{pipefds[0]};
    m_toHlPid   = CFileDescriptor{pipefds[1]};

    if (pipe(pipefds) != 0) {
        g_logger->log(Hyprutils::CLI::LOG_ERR, "pipe() failed, exiting");
        exit(1);
    }

    m_wakeupRead  = CFileDescriptor{pipefds[0]};
    m_wakeupWrite = CFileDescriptor{pipefds[1]};

    m_fromHlPid.setFlags(m_fromHlPid.getFlags() | FD_CLOEXEC);
    m_wakeupRead.setFlags(m_wakeupRead.getFlags() | FD_CLOEXEC);
    m_wakeupWrite.setFlags(m_wakeupWrite.getFlags() | FD_CLOEXEC);

    m_hyprlandLocked = lockedCrash;

    runHyprlandThread(safeMode, lockedCrash);

    pollfd pollfds[2] = {
        {
            .fd      = m_wakeupRead.get(),
            .events  = POLLIN,
            .revents = 0,
        },
        {
            .fd      = m_fromHlPid.get(),
            .events  = POLLIN,
            .revents = 0,
        },
    };

    while (true) {
        int ret = poll(pollfds, 2, -1);

        if (ret < 0) {
            g_logger->log(Hyprutils::CLI::LOG_ERR, "poll() failed, exiting");
            exit(1);
        }

        if (pollfds[1].revents & POLLIN) {
            g_logger->log(Hyprutils::CLI::LOG_DEBUG, "got an event from hyprland");
            dispatchHyprlandEvent();
            continue;
        }

        if (pollfds[0].revents & POLLIN) {
            g_logger->log(Hyprutils::CLI::LOG_DEBUG, "hyprland exit, breaking poll, checking state");
            clearFd(m_wakeupRead);
            break;
        }
    }

    m_hlThread.join();

    return !m_hyprlandInitialized || m_hyprlandExiting;
}
