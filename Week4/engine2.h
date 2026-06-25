#pragma once

#include <array>
#include <chrono>
#include <cstdint>

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

    static constexpr int INF        = 1'000'000;
    static constexpr int MATE_SCORE = 900'000;
    static constexpr int MAX_PLY    = 128;
    static constexpr int ASP_WINDOW = 50;

    // Fixed-size TT: 1 << 22 = ~4M entries (~96 MB)
    static constexpr size_t TT_SIZE = 1 << 22;
    static constexpr size_t TT_MASK = TT_SIZE - 1;

    enum TTFlag : uint8_t
    {
        EXACT,
        LOWERBOUND,
        UPPERBOUND
    };

    struct TTEntry
    {
        uint64_t    key      = 0;
        int32_t     score    = 0;
        int16_t     depth    = -1;
        TTFlag      flag     = EXACT;
        chess::Move bestMove = chess::Move::NO_MOVE;
    };

    // Precomputed LMR reduction table [depth][moveIndex]
    static constexpr int LMR_MAX_D = 64;
    static constexpr int LMR_MAX_M = 64;
    int lmrTable[LMR_MAX_D][LMR_MAX_M];

    long long nodes;
    bool      stopSearch;
    long long nextTimeCheck; // check time every N nodes

    std::chrono::steady_clock::time_point stopTime;

    TTEntry* tt; // heap-allocated fixed array

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

    bool timeUp();  // non-const: updates nextTimeCheck

    int iterativeDeepening(
        chess::Board& board,
        int maxTimeMs
    );

    int search(
        chess::Board& board,
        int depth,
        int alpha,
        int beta,
        int ply,
        bool isPvNode,
        bool canNullMove
    );

    int quiescence(
        chess::Board& board,
        int alpha,
        int beta,
        int ply
    );

    int evaluate(const chess::Board& board) const;

    // Pick the highest-scored remaining move starting at `start`
    void pickMove(chess::Movelist& moves, int start);

    void scoreMoves(
        chess::Board&    board,
        chess::Movelist& moves,
        int              ply,
        chess::Move      ttMove
    );

    int captureScore(
        chess::Board&      board,
        const chess::Move& move
    ) const;

    int pieceValue(chess::PieceType piece) const;

    void storeTT(
        uint64_t    key,
        int         depth,
        int         score,
        TTFlag      flag,
        chess::Move bestMove
    );

    bool probeTT(
        uint64_t     key,
        int          depth,
        int          alpha,
        int          beta,
        int&         score,
        chess::Move& bestMove
    );

    int scoreToTT(int score, int ply) const;
    int scoreFromTT(int score, int ply) const;
};