#include <iostream>
#include <vector>
#include <chrono>
#include <fstream>

#include "Engine.h"
#include "chess/include/chess.hpp"
#include "json.hpp"

using json = nlohmann::json;

void runFile(const std::string& filename,
             int depth)
{
    std::ifstream file(filename);

    json puzzles;
    file >> puzzles;

    Engine engine;

    int solved = 0;
    int total = 0;

    long long totalNodes = 0;

    auto start =
        std::chrono::steady_clock::now();

    for (auto& [fen, solution] : puzzles.items())
    {
        total++;

        chess::Board board(fen);

        std::vector<chess::Move> line;

        if (engine.findMate(board, depth, line))
            solved++;

        totalNodes += engine.getNodes();
    }

    auto end =
        std::chrono::steady_clock::now();

    std::cout
        << filename
        << '\n';

    std::cout
        << "Solved: "
        << solved
        << "/"
        << total
        << '\n';

    std::cout
        << "Average nodes: "
        << totalNodes / total
        << '\n';

    std::cout
        << "Total time: "
        << std::chrono::duration_cast<
               std::chrono::milliseconds
           >(end - start).count()
        << " ms\n";

    std::cout
        << "TT size: "
        << engine.getTTSize()
        << "\n\n";
}

int main()
{
    runFile("mate_in_2.json", 3);

    runFile("mate_in_3.json", 5);

    runFile("mate_in_4.json", 7);
}

// int main()
// {
//     chess::Board board(
//         "r5rk/2p1Nppp/3p3P/pp2p1P1/4P3/2qnPQK1/8/R6R w - - 1 0"
//     );

//     Engine engine;

//     std::vector<chess::Move> line;

//     auto start = std::chrono::steady_clock::now();

//     bool found = engine.findMate(board, 7, line);

//     auto end = std::chrono::steady_clock::now();

//     std::cout << "Found: " << found << '\n';

//     for (const auto& move : line)
//     {
//         std::cout << chess::uci::moveToUci(move) << '\n';
//     }

//     std::cout << "Nodes: "
//               << engine.getNodes()
//               << '\n';

//     std::cout << "Time: "
//               << std::chrono::duration_cast<std::chrono::milliseconds>(
//                      end - start
//                  ).count()
//               << " ms\n";

// }