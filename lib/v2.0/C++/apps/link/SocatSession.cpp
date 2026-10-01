// SPDX-License-Identifier: GPL-2.0-or-later
#include "SocatSession.h"
#include <stdexcept>

#ifdef __linux__
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <sys/file.h>
extern "C" {
#include <sys/pidfd.h>
}
#include <sys/stat.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>

namespace link_app {
  namespace {
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;

    struct FileDescriptor {
      int value;
      explicit FileDescriptor(int descriptor) : value(descriptor) {}
      FileDescriptor(const FileDescriptor &) = delete;
      FileDescriptor &operator=(const FileDescriptor &) = delete;
      ~FileDescriptor() { if (value >= 0) ::close(value); }
    };

    struct Session {
      pid_t pid = 0;
      unsigned long long started = 0;
      std::string portA;
      std::string portB;
    };

    [[noreturn]] void systemFailure(const char *operation)
    {
      throw std::system_error(errno, std::generic_category(), operation);
    }

    fs::path sessionDirectory()
    {
      const auto runtime = std::getenv("XDG_RUNTIME_DIR");
      const auto directory = runtime && *runtime
        ? fs::path(runtime) / "linkpp-socat"
        : fs::path("/tmp") / std::format("linkpp-socat-{}", ::geteuid());
      if (::mkdir(directory.c_str(), 0700) < 0 && errno != EEXIST)
        systemFailure("Cannot create the socat session directory");
      struct stat status{};
      if (::lstat(directory.c_str(), &status) < 0)
        systemFailure("Cannot inspect the socat session directory");
      if (!S_ISDIR(status.st_mode) || status.st_uid != ::geteuid() || (status.st_mode & 0077))
        throw std::runtime_error("The socat session directory must be private and owned by the current user");
      return directory;
    }

    unsigned long long processStart(pid_t pid)
    {
      std::ifstream stream(std::format("/proc/{}/stat", pid));
      std::string line;
      if (!std::getline(stream, line)) return 0;
      const auto end = line.rfind(')'); // The process name can itself contain spaces or parentheses.
      if (end == std::string::npos) return 0;
      std::istringstream fields(line.substr(end + 1));
      std::string value;
      for (int field = 3; field <= 22; ++field) {
        if (!(fields >> value)) return 0;
      }
      return std::stoull(value);
    }

    Session readSession(const fs::path &directory)
    {
      Session session;
      std::ifstream stream(directory / "session");
      if (stream >> session.pid >> session.started >> session.portA >> session.portB)
        return session;
      return {};
    }

    bool exited(int descriptor, int timeout = 0)
    {
      pollfd process{descriptor, POLLIN, 0};
      int result;
      do { result = ::poll(&process, 1, timeout); } while (result < 0 && errno == EINTR);
      if (result < 0) systemFailure("Cannot wait for socat");
      return result > 0;
    }

    // Pin the process before checking its birth time, so PID reuse cannot make stop kill another process.
    int openSession(const Session &session)
    {
      if (session.pid <= 1 || !session.started) return -1;
      const int descriptor = ::pidfd_open(session.pid, 0);
      if (descriptor < 0) {
        if (errno == ESRCH) return -1;
        systemFailure("Cannot inspect the socat process");
      }
      if (exited(descriptor) || processStart(session.pid) != session.started) {
        ::close(descriptor);
        return -1;
      }
      return descriptor;
    }

    void cleanup(const fs::path &directory)
    {
      for (const auto name : {"port-a", "port-b", "link", "simulator", "session", "socat.log"})
        fs::remove(directory / name);
      // Keep the lock inode: removing it would let concurrent commands acquire different locks.
    }

    void stopProcess(int descriptor)
    {
      if (::pidfd_send_signal(descriptor, SIGTERM, nullptr, 0) < 0 && errno != ESRCH)
        systemFailure("Cannot stop socat");
      if (!exited(descriptor, 2000)) {
        if (::pidfd_send_signal(descriptor, SIGKILL, nullptr, 0) < 0 && errno != ESRCH)
          systemFailure("Cannot stop socat");
        if (!exited(descriptor, 2000)) throw std::runtime_error("socat has not stopped yet; retry -socat stop");
      }
    }

    void printPorts(const Session &session, const char *executable, bool existing)
    {
      std::cout << (existing ? "socat is already running.\n" : "socat started.\n")
                << "Port A: " << session.portA << '\n'
                << "Port B: " << session.portB << '\n'
                << "Ports are interchangeable; connect one program to each.\n"
                << "Example: " << executable << " -d " << session.portA << '\n'
                << "     or: " << executable << " -d " << session.portB << '\n';
    }

    Session startProcess(const fs::path &directory, int lockDescriptor)
    {
      FileDescriptor support(::pidfd_open(::getpid(), 0));
      if (support.value < 0) systemFailure("Cannot manage socat processes with pidfd on this system");
      const auto portA = directory / "port-a";
      const auto portB = directory / "port-b";
      const auto addressA = "pty,raw,echo=0,link=" + portA.string();
      const auto addressB = "pty,raw,echo=0,link=" + portB.string();
      FileDescriptor log(::open((directory / "socat.log").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600));
      if (log.value < 0) systemFailure("Cannot create the socat log");
      const pid_t pid = ::fork();
      if (pid < 0) systemFailure("Cannot launch socat");
      if (pid == 0) {
        ::close(lockDescriptor);
        if (::setsid() < 0) ::_exit(126);
        ::signal(SIGHUP, SIG_IGN);
        const int input = ::open("/dev/null", O_RDONLY);
        if (input < 0 || ::dup2(input, STDIN_FILENO) < 0 ||
            ::dup2(log.value, STDOUT_FILENO) < 0 || ::dup2(log.value, STDERR_FILENO) < 0)
          ::_exit(126);
        if (input > STDERR_FILENO) ::close(input);
        ::execlp("socat", "socat", "-d", "-d", addressA.c_str(), addressB.c_str(), nullptr);
        if (errno == ENOENT)
          ::dprintf(STDERR_FILENO, "socat is not installed or is not on PATH. Install socat to use -socat start.\n");
        else
          ::dprintf(STDERR_FILENO, "Cannot execute socat: %s\n", std::generic_category().message(errno).c_str());
        ::_exit(127);
      }

      // Until this child is reaped its PID cannot be reused, including during startup rollback.
      bool reaped = false;
      try {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (true) {
          int status;
          const auto result = ::waitpid(pid, &status, WNOHANG);
          if (result == pid) { reaped = true; break; }
          if (result < 0 && errno != EINTR) systemFailure("Cannot inspect socat startup");
          if (fs::exists(portA) && fs::exists(portB)) {
            Session session{pid, processStart(pid), fs::canonical(portA).string(), fs::canonical(portB).string()};
            if (!session.started) throw std::runtime_error("socat stopped during startup");
            std::ofstream state(directory / "session");
            state << session.pid << '\n' << session.started << '\n'
                  << session.portA << '\n' << session.portB << '\n';
            state.close();
            if (!state) throw std::runtime_error("Cannot save the socat session");
            return session;
          }
          if (std::chrono::steady_clock::now() >= deadline) break;
          std::this_thread::sleep_for(20ms);
        }
        std::ifstream errors(directory / "socat.log");
        std::string detail((std::istreambuf_iterator<char>(errors)), std::istreambuf_iterator<char>());
        throw std::runtime_error("Could not create the socat virtual ports.\n" + detail);
      } catch (...) {
        if (!reaped) {
          FileDescriptor process(::pidfd_open(pid, 0));
          if (process.value >= 0) stopProcess(process.value);
          else ::kill(pid, SIGKILL); // This is still our unreaped child, so its PID cannot be reused.
          int status;
          while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        }
        cleanup(directory);
        throw;
      }
    }
  }

  int manageSocatSession(const std::string &action, const char *executable)
  {
    const auto directory = sessionDirectory();
    FileDescriptor lock(::open((directory / "lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600));
    if (lock.value < 0) systemFailure("Cannot open the socat session lock");
    while (::flock(lock.value, LOCK_EX) < 0) {
      if (errno != EINTR) systemFailure("Cannot lock the socat session");
    }
    const auto session = readSession(directory);
    FileDescriptor process(openSession(session));
    if (action == "stop") {
      if (process.value >= 0) stopProcess(process.value);
      cleanup(directory);
      std::cout << (process.value >= 0 ? "socat stopped.\n" : "No socat session is running.\n");
    } else if (process.value >= 0) {
      printPorts(session, executable, true);
    } else {
      cleanup(directory);
      printPorts(startProcess(directory, lock.value), executable, false);
    }
    return 0;
  }
}
#else
namespace link_app {
  int manageSocatSession(const std::string &, const char *)
  {
    throw std::runtime_error("-socat is supported on Linux only");
  }
}
#endif
