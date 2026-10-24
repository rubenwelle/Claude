// =============================================================================
// cmd-pipe-en.cpp  -  A LOCAL demo: a real shell through two pipes (English)
//
// This is the English version of cmd-pipe.cpp. It does exactly the same thing;
// only the names and comments are in English, and it uses the English pipe
// helper pipes.h (class ChildProcess) instead of the Dutch pijpen.h. The Dutch
// original is kept as cmd-pipe.cpp. Use whichever you prefer.
//
// What is this for? To FEEL the difference between the tidy assistant from the
// "sessie" command and a real shell (cmd.exe on Windows, /bin/sh on Linux).
// You see exactly why real SSH does so much extra work.
//
// IMPORTANT - please read:
//   * This program does NOT talk over the network. No port is listening,
//     there is no login. It only starts the shell on YOUR computer and
//     connects it to your own keyboard and screen, through pipes.
//   * So this is not a "remote shell" and not a backdoor: nobody else can
//     reach it. It is purely to see how pipes + a shell work together.
//   * NEVER connect a shell to a network connection (like the mini-SSH
//     server). That would give everyone who reaches the port full access to
//     your computer. That is exactly why mini-SSH does not do it.
//
// What happens? Two things at once, so two threads:
//   - a "reader" thread: everything the shell prints, straight to your screen;
//   - the main thread: everything you type, to the shell's stdin.
// That "at the same time" is the same problem a real SSH server solves.
//
// Build on Windows (MinGW):  g++ -std=c++17 cmd-pipe-en.cpp -o cmd-pipe-en.exe
// Build on Linux:            g++ -std=c++17 -pthread cmd-pipe-en.cpp -o cmd-pipe-en
// =============================================================================

#include "pipes.h"
#include <iostream>
#include <string>
#include <thread>    // std::thread, to do two things at once
#include <atomic>    // std::atomic, a flag you may safely read and write from
                     // two threads at the same time

// Which shell do we start? On Windows cmd.exe, on Linux /bin/sh.
#ifdef _WIN32
const std::string SHELL = "cmd.exe";
#else
const std::string SHELL = "/bin/sh";
#endif

int main() {
    std::cout << "=== LOCAL pipe demo with a real shell ===\n"
              << "This runs only on your own computer, without a network.\n"
              << "Type shell commands (e.g. 'dir' or 'ls'). Type 'exit' to stop.\n"
              << "Notice the difference with the assistant session!\n\n";

    ChildProcess shell;
    if (!shell.start(SHELL, "")) {   // empty argument = shell without an argument
        std::cerr << "Could not start " << SHELL << ".\n";
        return 1;
    }

    // This flag stays true while the shell is alive. As soon as the reader
    // thread notices the pipe is closed (the shell has stopped), it sets it
    // to false.
    std::atomic<bool> running{true};

    // The reader thread: read what the shell prints line by line and put it on
    // the screen. Here you see a difference right away: there is NO <<KLAAR>>.
    // The shell never says "I'm done answering", so the only way to know there
    // is something is to just keep reading.
    std::thread reader([&]() {
        std::string line;
        while (shell.readLine(line)) {
            std::cout << line << "\n";
            std::cout.flush();   // show it immediately, don't keep it in a buffer
        }
        running = false;   // pipe closed: the shell has stopped
    });

    // The main thread: read what you type and send it to the shell.
    std::string input;
    while (running && std::getline(std::cin, input)) {
        if (!shell.writeLine(input)) break;   // pipe closed
        if (input == "exit" || input == "quit") break;
    }

    // Clean up, in the right order:
    //   1. Close only our write side. The shell gets "end of input" and stops,
    //      but we can still read its last output.
    //   2. Wait until the reader thread is done (it stops by itself as soon as
    //      the shell closes its output pipe). join() = wait for the thread.
    //   3. Only then clean up the rest.
    // If we called stop() straight away, we would cut the output pipe before
    // the last output was read, and you would never see the answer.
    shell.closeInput();
    if (reader.joinable()) reader.join();
    shell.stop();

    std::cout << "\n=== demo stopped ===\n";
    return 0;
}
