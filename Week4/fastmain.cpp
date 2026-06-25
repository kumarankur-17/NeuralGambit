#include <iostream>
#include <sstream>
#include <string>

#include "fastengine.h"
#include "chess/include/chess.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Simple time management: given clock time and increment, pick a move time
// ─────────────────────────────────────────────────────────────────────────────
static int calcMoveTime(
    int timeMs,
    int incMs,
    int movesToGo  // 0 means unknown / sudden death
)
{
    if (movesToGo > 0)
    {
        // Divide remaining time evenly, keep a buffer
        int allocated = timeMs / movesToGo + incMs - 50;
        return std::max(50, std::min(allocated, timeMs - 100));
    }
    else
    {
        // Sudden death: use ~1/30 of remaining time + increment
        int allocated = timeMs / 30 + incMs - 50;
        return std::max(50, std::min(allocated, timeMs - 100));
    }
}

int main()
{
    Engine engine;
    chess::Board board;

    std::string line;

    while (std::getline(std::cin, line))
    {
        if (line.empty()) continue;

        // ── uci ─────────────────────────────────────────────────────────
        if (line == "uci")
        {
            std::cout
                << "id name NeuralGambit\n"
                << "id author Ankur\n"
                << "uciok\n"
                << std::flush;
            continue;
        }

        // ── isready ─────────────────────────────────────────────────────
        if (line == "isready")
        {
            std::cout << "readyok\n" << std::flush;
            continue;
        }

        // ── ucinewgame ──────────────────────────────────────────────────
        if (line == "ucinewgame")
        {
            engine.newGame();
            board = chess::Board();
            continue;
        }

        // ── position ────────────────────────────────────────────────────
        if (line.rfind("position", 0) == 0)
        {
            std::stringstream ss(line);
            std::string token;
            ss >> token; // "position"

            ss >> token;
            if (token == "startpos")
            {
                board = chess::Board();
                if (ss >> token && token == "moves")
                {
                    while (ss >> token)
                    {
                        auto move = chess::uci::uciToMove(board, token);
                        board.makeMove(move);
                    }
                }
            }
            else if (token == "fen")
            {
                std::string fen;
                std::string part;
                for (int i = 0; i < 6; ++i)
                {
                    if (!(ss >> part)) break;
                    if (i) fen += ' ';
                    fen += part;
                }
                board = chess::Board(fen);

                if (ss >> token && token == "moves")
                {
                    while (ss >> token)
                    {
                        auto move = chess::uci::uciToMove(board, token);
                        board.makeMove(move);
                    }
                }
            }
            continue;
        }

        // ── go ──────────────────────────────────────────────────────────
        if (line.rfind("go", 0) == 0)
        {
            std::stringstream ss(line);
            std::string token;
            ss >> token; // "go"

            int moveTime  = 0;
            int wtime     = 0;
            int btime     = 0;
            int winc      = 0;
            int binc      = 0;
            int movesToGo = 0;
            int depth     = 0;   // fixed-depth search (0 = ignore)

            while (ss >> token)
            {
                if      (token == "movetime")  ss >> moveTime;
                else if (token == "wtime")     ss >> wtime;
                else if (token == "btime")     ss >> btime;
                else if (token == "winc")      ss >> winc;
                else if (token == "binc")      ss >> binc;
                else if (token == "movestogo") ss >> movesToGo;
                else if (token == "depth")     ss >> depth;
            }

            int allocatedMs = 1000; // default fallback

            if (moveTime > 0)
            {
                // Explicit movetime — use it directly with a small safety margin
                allocatedMs = std::max(10, moveTime - 10);
            }
            else if (wtime > 0 || btime > 0)
            {
                // Time-control mode: use the side-to-move's clock
                bool isWhite = (board.sideToMove() == chess::Color::WHITE);
                int  myTime  = isWhite ? wtime : btime;
                int  myInc   = isWhite ? winc  : binc;
                allocatedMs  = calcMoveTime(myTime, myInc, movesToGo);
            }
            else if (depth > 0)
            {
                // Infinite time, let iterative deepening reach the fixed depth
                allocatedMs = 3600'000; // 1 hour
            }

            auto bestMove = engine.findBestMove(board, allocatedMs);

            if (bestMove == chess::Move::NO_MOVE)
                std::cout << "bestmove 0000\n" << std::flush;
            else
                std::cout
                    << "bestmove "
                    << chess::uci::moveToUci(bestMove)
                    << '\n'
                    << std::flush;

            continue;
        }

        // ── quit ────────────────────────────────────────────────────────
        if (line == "quit")
            break;
    }

    return 0;
}