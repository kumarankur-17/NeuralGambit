#pragma once

#include <array>
#include <chrono>
#include <cstdint>

#include "chess/include/chess.hpp"

class Engine
{
public:
    Engine();
    ~Engine();

    void newGame();

    chess::Move findBestMove(chess::Board& board, int moveTimeMs);

    chess::Move bestCompletedMove;
    long long   getNodes() const;

private:

    static constexpr int INF        = 1'000'000;
    static constexpr int MATE_SCORE = 900'000;
    static constexpr int MAX_PLY    = 128;
    static constexpr int ASP_WINDOW = 50;

    // 1<<23 = ~8M entries, each entry is 32 bytes = 256 MB
    static constexpr size_t TT_SIZE = 1 << 23;
    static constexpr size_t TT_MASK = TT_SIZE - 1;

    enum TTFlag : uint8_t { EXACT, LOWERBOUND, UPPERBOUND };

    // Packed to 32 bytes so two entries fit in one cache line
    struct alignas(32) TTEntry
    {
        uint64_t    key      = 0;
        int32_t     score    = 0;
        int16_t     depth    = -1;
        TTFlag      flag     = EXACT;
        uint8_t     _pad     = 0;
        chess::Move bestMove = chess::Move::NO_MOVE;
    };

    static_assert(sizeof(TTEntry) == 32, "TTEntry must be 32 bytes");

    // LMR table [depth][moveIndex]
    static constexpr int LMR_D = 64;
    static constexpr int LMR_M = 64;
    int lmrTable[LMR_D][LMR_M];

    long long nodes;
    long long nextTimeCheck;
    bool      stopSearch;

    std::chrono::steady_clock::time_point stopTime;

    TTEntry* tt;

    // Two killer moves per ply
    chess::Move killerMoves[MAX_PLY][2];

    // History indexed by [from][to]
    int historyTable[64][64];

    chess::Move rootBestMove;

    // Internal helpers
    bool timeUp();

    int  iterativeDeepening(chess::Board& board, int maxTimeMs);

    int  search(chess::Board& board,
                int depth, int alpha, int beta,
                int ply, bool isPvNode, bool canNullMove);

    int  quiescence(chess::Board& board,
                    int alpha, int beta, int ply);

    int  evaluate(const chess::Board& board) const;

    void scoreMoves(chess::Board& board, chess::Movelist& moves,
                    int ply, chess::Move ttMove);

    void pickMove(chess::Movelist& moves, int start);

    inline int  mvvLva(chess::Board& board, const chess::Move& move) const;
    inline int  pieceVal(chess::PieceType pt) const;

    void storeTT(uint64_t key, int depth, int score,
                 TTFlag flag, chess::Move bestMove);

    bool probeTT(uint64_t key, int depth, int alpha, int beta,
                 int& score, chess::Move& bestMove);

    inline int scoreToTT  (int score, int ply) const;
    inline int scoreFromTT(int score, int ply) const;
};