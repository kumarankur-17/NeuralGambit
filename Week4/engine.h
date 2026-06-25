#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <unordered_map>

#include "chess/include/chess.hpp"

class Engine
{
public:
    Engine();

    void newGame();

    chess::Move findBestMove(
        chess::Board& board,
        int moveTimeMs
    );

    chess::Move bestCompletedMove;
    
    long long getNodes() const;

private:

    static constexpr int INF = 1000000;
    static constexpr int MATE_SCORE = 900000;
    static constexpr int MAX_PLY = 128;

    enum TTFlag
    {
        EXACT,
        LOWERBOUND,
        UPPERBOUND
    };

    struct TTEntry
    {
        uint64_t key = 0;

        int depth = -1;

        int score = 0;

        TTFlag flag = EXACT;

        chess::Move bestMove;
    };

    long long nodes;

    bool stopSearch;

    std::chrono::steady_clock::time_point stopTime;

    std::unordered_map<uint64_t, TTEntry> tt;

    std::array<
        std::array<chess::Move, 2>,
        MAX_PLY
    > killerMoves;

    std::array<
        std::array<int, 64>,
        64
    > historyTable;

    chess::Move rootBestMove;

private:

    bool timeUp() const;

    int iterativeDeepening(
        chess::Board& board,
        int maxTimeMs
    );

    int search(
        chess::Board& board,
        int depth,
        int alpha,
        int beta,
        int ply
    );

    int quiescence(
        chess::Board& board,
        int alpha,
        int beta,
        int ply
    );

    int evaluate(
        const chess::Board& board
    ) const;

    void scoreMoves(
        chess::Board& board,
        chess::Movelist& moves,
        int ply,
        chess::Move ttMove
    );

    int captureScore(
        chess::Board& board,
        const chess::Move& move
    ) const;

    int pieceValue(
        chess::PieceType piece
    ) const;

    void storeTT(
        uint64_t key,
        int depth,
        int score,
        TTFlag flag,
        chess::Move bestMove
    );

    bool probeTT(
        uint64_t key,
        int depth,
        int alpha,
        int beta,
        int& score,
        chess::Move& bestMove
    );
};