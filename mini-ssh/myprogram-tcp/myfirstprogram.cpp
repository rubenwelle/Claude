// =============================================================================
// myfirstprogram.cpp  -  A sample "MyFirstProgram" (the helper the server runs)
//
// This is an EXAMPLE of the kind of program Ruben described. It is a plain
// command-line program: it reads one request line from standard input (stdin),
// does something, and writes the answer to standard output (stdout). It does
// NOT know anything about networks or sockets. The server starts it and
// connects its stdin/stdout to two pipes; the network part lives in server.cpp.
//
// Commands it understands:
//   reken AxB   -> multiplies A and B (also accepts A*B), e.g. "reken 7x7" -> 49
//   Liedje      -> prints one random song out of 5
//   exit        -> stops
//
// THE CONTRACT (important if you drop in your own program):
//   1. Read one line at a time from stdin (std::getline).
//   2. Write your answer to stdout. It may be multiple lines.
//   3. After each answer, print one line that is exactly  <<DONE>>  so the
//      server knows the answer is complete.
//   4. FLUSH after each answer, so it is sent right away and does not get stuck
//      in a buffer. Using std::endl (instead of "\n") flushes for you.
//
// Build:  g++ -std=c++17 myfirstprogram.cpp -o myfirstprogram        (Linux)
//         g++ -std=c++17 myfirstprogram.cpp -o myfirstprogram.exe    (Windows)
// =============================================================================

#include <iostream>
#include <string>
#include <vector>
#include <random>
#include <chrono>

int main() {
    // Five little (made-up) songs. A song may span several lines.
    const std::vector<std::string> songs = {
        "Twinkle twinkle little star,\nhow I wonder what you are.",
        "Row, row, row your boat,\ngently down the stream.",
        "Doe a deer, a female deer,\nray a drop of golden sun.",
        "We will, we will rock you!\nWe will, we will rock you!",
        "La la la, this is the song\nthat never ends, it goes on and on."
    };

    // A simple random generator, seeded from the clock so you get a different
    // song each run. (Not cryptographic randomness, but fine for picking songs.)
    std::mt19937 gen(static_cast<unsigned>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<size_t> pick(0, songs.size() - 1);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line == "exit" || line == "quit") break;

        if (line.rfind("reken ", 0) == 0) {
            // Everything after "reken " is the sum, e.g. "7x7" or "7 * 7".
            std::string sum = line.substr(6);
            // Find the 'x' or '*' that separates the two numbers.
            size_t op = sum.find_first_of("x*X");
            if (op == std::string::npos) {
                std::cout << "I don't understand that sum. Try: reken 7x7\n";
            } else {
                try {
                    long long a = std::stoll(sum.substr(0, op));
                    long long b = std::stoll(sum.substr(op + 1));
                    std::cout << a << " x " << b << " = " << (a * b) << "\n";
                } catch (...) {
                    std::cout << "Those don't look like numbers. Try: reken 7x7\n";
                }
            }
        } else if (line == "Liedje" || line == "liedje") {
            std::cout << songs[pick(gen)] << "\n";
        } else {
            std::cout << "Unknown command. Try:  reken 7x7   or   Liedje\n";
        }

        // Mark the end of this answer and flush (std::endl flushes).
        std::cout << "<<DONE>>" << std::endl;
    }
    return 0;
}
