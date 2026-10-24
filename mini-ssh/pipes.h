// =============================================================================
// pipes.h  -  Start a program with two pipes (the English version of pijpen.h)
//
// This is an English translation of pijpen.h. It does exactly the same thing;
// only the names and comments are in English. The Dutch original is kept as
// pijpen.h, and the server and cmd-pipe still use that one. Use this file if
// you prefer English, for example with #include "pipes.h" and ChildProcess.
//
// The idea from SHELL-UITLEG.md, for real: we start a program and connect its
// stdin and stdout not to a keyboard and screen, but to two pipes that the
// parent (for example the server) holds on to.
//
//      parent                          child program (assistent)
//   writeLine() ──── pipe 1 ────►  stdin   (std::cin)
//   readLine()  ◄─── pipe 2 ─────  stdout  (std::cout)
//
// The program we start is the "child" (child process); the program that starts
// it is the "parent". Those are the standard names in the documentation too.
//
// Windows and Linux do this very differently, so there are two versions:
//   Windows: CreatePipe + CreateProcess
//   Linux:   pipe + fork + dup2 + exec
// You don't have to understand every line; it's about the idea.
// =============================================================================

#pragma once

#include <string>

#ifdef _WIN32
    // windows.h pulls in the old winsock.h (version 1) by default. If another
    // file also uses winsock2.h (like minissh.h), the two clash and the
    // compiler warns: "Please include winsock2.h before windows.h".
    // WIN32_LEAN_AND_MEAN tells windows.h to leave winsock.h out. The pipe
    // functions (CreatePipe, CreateProcess, ...) stay available, and now the
    // order of the #includes no longer matters.
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <unistd.h>     // pipe, fork, dup2, execl, read, write, close
    #include <sys/wait.h>   // waitpid
    #include <csignal>      // signal, SIGPIPE
#endif

class ChildProcess {
public:
    ChildProcess() = default;

    // A ChildProcess may not be copied: each program and each pipe has exactly
    // one owner. (Otherwise two copies would close the same pipe twice.)
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;

    // The destructor cleans up automatically when the object ceases to exist,
    // even if we return halfway through a function. This is called RAII:
    // "cleanup happens by itself at the end of the lifetime".
    ~ChildProcess() { stop(); }

    // Start 'program' with one fixed argument. Note: we do NOT use a shell
    // (no cmd.exe, no popen). Whatever the user types later only goes to the
    // program's stdin, so it can never start another command.
    bool start(const std::string& program, const std::string& argument);

    // Send one line of text to the child's stdin (with a newline appended).
    bool writeLine(const std::string& line);

    // Read one line from the child's stdout (without the newline).
    // Returns false if the child has stopped.
    bool readLine(std::string& line);

    // Close ONLY our write side (pipe 1). The child then gets "end of input"
    // and usually stops by itself, while we can still read its last output
    // from pipe 2. Handy before stop(): do this first, drain the output, then
    // call stop().
    void closeInput();

    // Close the pipes and wait until the child has stopped.
    void stop();

private:
    bool started = false;
#ifdef _WIN32
    HANDLE toChild   = NULL;   // our end of pipe 1 (we write)
    HANDLE fromChild = NULL;   // our end of pipe 2 (we read)
    PROCESS_INFORMATION process{};
#else
    int toChild   = -1;
    int fromChild = -1;
    pid_t pid     = -1;
#endif

    // Read one byte. Simple (and not super fast), but easy to follow.
    bool readByte(char& c);
};

// =============================================================================
// Windows version
// =============================================================================
#ifdef _WIN32

inline bool ChildProcess::start(const std::string& program, const std::string& argument) {
    // bInheritHandle = TRUE: the child may "inherit" these pipe ends.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE childReads, childWrites;   // the ends the child gets
    // Pipe 1: we write (toChild), the child reads (childReads = its stdin).
    if (!CreatePipe(&childReads, &toChild, &sa, 0)) return false;
    // Pipe 2: the child writes (childWrites = its stdout), we read (fromChild).
    if (!CreatePipe(&fromChild, &childWrites, &sa, 0)) {
        CloseHandle(childReads);
        CloseHandle(toChild);
        return false;
    }
    // The child must NOT inherit our own ends, otherwise the pipe never closes
    // (the child would then also hold a "write side" of it).
    SetHandleInformation(toChild,   HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(fromChild, HANDLE_FLAG_INHERIT, 0);

    // Tell Windows which channels the child gets.
    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = childReads;    // stdin  = pipe 1
    si.hStdOutput = childWrites;   // stdout = pipe 2
    si.hStdError  = childWrites;   // stderr to pipe 2 as well

    // CreateProcess wants the command line in a buffer it may modify.
    // An empty argument means: start the program without an argument.
    std::string command = argument.empty() ? program : (program + " " + argument);
    BOOL ok = CreateProcessA(
        NULL, &command[0], NULL, NULL,
        TRUE,               // inherit handles: yes (only the ones we allow)
        CREATE_NO_WINDOW,   // don't open a black console window
        NULL, NULL, &si, &process);

    // We no longer need the child's ends: the child has them now.
    CloseHandle(childReads);
    CloseHandle(childWrites);

    if (!ok) {
        CloseHandle(toChild);
        CloseHandle(fromChild);
        toChild = fromChild = NULL;
        return false;
    }
    started = true;
    return true;
}

inline bool ChildProcess::writeLine(const std::string& line) {
    if (!started) return false;
    std::string data = line + "\n";
    DWORD written = 0;
    return WriteFile(toChild, data.data(), static_cast<DWORD>(data.size()),
                     &written, NULL) && written == data.size();
}

inline bool ChildProcess::readByte(char& c) {
    DWORD read = 0;
    return ReadFile(fromChild, &c, 1, &read, NULL) && read == 1;
}

inline void ChildProcess::closeInput() {
    if (toChild != NULL) {
        CloseHandle(toChild);   // the child gets "end of input"
        toChild = NULL;
    }
}

inline void ChildProcess::stop() {
    if (!started) return;
    started = false;
    closeInput();   // close pipe 1 (if that hasn't happened yet)
    // Give the child 2 seconds; after that we shut it down ourselves.
    if (WaitForSingleObject(process.hProcess, 2000) == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
    }
    CloseHandle(fromChild);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
}

// =============================================================================
// Linux version
// =============================================================================
#else

inline bool ChildProcess::start(const std::string& program, const std::string& argument) {
    // If the child stops while we are still writing to pipe 1, Linux sends a
    // SIGPIPE signal that would stop the whole parent. We turn that off;
    // write() then simply returns an error.
    signal(SIGPIPE, SIG_IGN);

    int pipe1[2], pipe2[2];          // [0] = read end, [1] = write end
    if (pipe(pipe1) != 0) return false;
    if (pipe(pipe2) != 0) {
        close(pipe1[0]); close(pipe1[1]);
        return false;
    }

    // fork() makes a copy of our own program. After it, two are running:
    // in the child fork() returns 0, in the parent the child's id.
    pid = fork();
    if (pid < 0) {
        close(pipe1[0]); close(pipe1[1]); close(pipe2[0]); close(pipe2[1]);
        return false;
    }
    if (pid == 0) {
        // --- This part runs in the CHILD ---
        // dup2 puts a pipe end in the place of stdin (0) or stdout (1).
        dup2(pipe1[0], 0);   // stdin  = read end of pipe 1
        dup2(pipe2[1], 1);   // stdout = write end of pipe 2
        dup2(pipe2[1], 2);   // stderr too
        close(pipe1[0]); close(pipe1[1]); close(pipe2[0]); close(pipe2[1]);
        // exec replaces this child with the real program.
        // An empty argument means: start the program without an argument.
        if (argument.empty())
            execl(program.c_str(), program.c_str(), (char*)NULL);
        else
            execl(program.c_str(), program.c_str(), argument.c_str(), (char*)NULL);
        _exit(127);          // only if exec fails (program not found)
    }

    // --- This part runs in the PARENT ---
    close(pipe1[0]);         // we don't keep the child's ends
    close(pipe2[1]);
    toChild   = pipe1[1];
    fromChild = pipe2[0];
    started   = true;
    return true;
}

inline bool ChildProcess::writeLine(const std::string& line) {
    if (!started) return false;
    std::string data = line + "\n";
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = write(toChild, data.data() + sent, data.size() - sent);
        if (n <= 0) return false;
        sent += static_cast<size_t>(n);
    }
    return true;
}

inline bool ChildProcess::readByte(char& c) {
    return read(fromChild, &c, 1) == 1;
}

inline void ChildProcess::closeInput() {
    if (toChild != -1) {
        close(toChild);      // "end of input" for the child
        toChild = -1;
    }
}

inline void ChildProcess::stop() {
    if (!started) return;
    started = false;
    closeInput();            // close pipe 1 (if that hasn't happened yet)
    close(fromChild);
    waitpid(pid, nullptr, 0);
}

#endif

// =============================================================================
// Shared: reading a line = reading bytes until the newline.
// =============================================================================
inline bool ChildProcess::readLine(std::string& line) {
    if (!started) return false;
    line.clear();
    char c;
    while (readByte(c)) {
        if (c == '\n') {
            // Windows puts "\r\n" at the end of a line; we strip the \r.
            if (!line.empty() && line.back() == '\r') line.pop_back();
            return true;
        }
        line += c;
        if (line.size() > 4096) return true;   // extremely long line: cut it off
    }
    return false;   // pipe closed: the child has stopped
}
