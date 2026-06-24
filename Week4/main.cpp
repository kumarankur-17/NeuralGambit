#include <iostream>
#include <sstream>
#include <string>

#include "engine.h"
#include "chess/include/chess.hpp"

int main()
{
    Engine engine;

    chess::Board board;

    std::string line;

    while (std::getline(std::cin, line))
    {
        if (line == "uci")
        {
            std::cout
                << "id name NeuralGambit\n"
                << "id author Ankur\n"
                << "uciok\n";

            continue;
        }

        if (line == "isready")
        {
            std::cout << "readyok\n";
            continue;
        }

        if (line == "ucinewgame")
        {
            engine.newGame();
            board = chess::Board();
            continue;
        }

        if (line.rfind("position", 0) == 0)
        {
            std::stringstream ss(line);

            std::string token;
            ss >> token; // position

            ss >> token;

            if (token == "startpos")
            {
                board = chess::Board();

                if (ss >> token)
                {
                    if (token == "moves")
                    {
                        while (ss >> token)
                        {
                            auto move =
                                chess::uci::uciToMove(
                                    board,
                                    token
                                );

                            board.makeMove(move);
                        }
                    }
                }
            }
            else if (token == "fen")
            {
                std::string fen;

                std::string part;

                for (int i = 0; i < 6; ++i)
                {
                    ss >> part;

                    if (i)
                        fen += ' ';

                    fen += part;
                }

                board = chess::Board(fen);

                if (ss >> token)
                {
                    if (token == "moves")
                    {
                        while (ss >> token)
                        {
                            auto move =
                                chess::uci::uciToMove(
                                    board,
                                    token
                                );

                            board.makeMove(move);
                        }
                    }
                }
            }

            continue;
        }

        if (line.rfind("go", 0) == 0)
        {
            int moveTime = 1000;

            std::stringstream ss(line);

            std::string token;

            ss >> token;

            while (ss >> token)
            {
                if (token == "movetime")
                {
                    ss >> moveTime;
                }
            }

            auto bestMove =
                engine.findBestMove(
                    board,
                    moveTime
                );

            if (bestMove == chess::Move::NO_MOVE)
            {
                std::cout << "bestmove 0000\n";
            }
            else
            {
                std::cout
                    << "bestmove "
                    << chess::uci::moveToUci(bestMove)
                    << '\n';
            }

            continue;
        }

        if (line == "quit")
        {
            break;
        }
    }

    return 0;
}