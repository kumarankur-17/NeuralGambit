#include "engine2.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Piece-square tables  (white's perspective; sq^56 mirrors for black)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int pawnTable[64] =
{
     0,  0,  0,  0,  0,  0,  0,  0,
    50, 50, 50, 50, 50, 50, 50, 50,
    10, 10, 20, 30, 30, 20, 10, 10,
     5,  5, 10, 25, 25, 10,  5,  5,
     0,  0,  0, 20, 20,  0,  0,  0,
     5, -5,-10,  0,  0,-10, -5,  5,
     5, 10, 10,-20,-20, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};
static constexpr int knightTable[64] =
{
   -50,-40,-30,-30,-30,-30,-40,-50,
   -40,-20,  0,  0,  0,  0,-20,-40,
   -30,  0, 10, 15, 15, 10,  0,-30,
   -30,  5, 15, 20, 20, 15,  5,-30,
   -30,  0, 15, 20, 20, 15,  0,-30,
   -30,  5, 10, 15, 15, 10,  5,-30,
   -40,-20,  0,  5,  5,  0,-20,-40,
   -50,-40,-30,-30,-30,-30,-40,-50
};
static constexpr int bishopTable[64] =
{
   -20,-10,-10,-10,-10,-10,-10,-20,
   -10,  5,  0,  0,  0,  0,  5,-10,
   -10, 10, 10, 10, 10, 10, 10,-10,
   -10,  0, 10, 10, 10, 10,  0,-10,
   -10,  5,  5, 10, 10,  5,  5,-10,
   -10,  0,  5, 10, 10,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -20,-10,-10,-10,-10,-10,-10,-20
};
static constexpr int rookTable[64] =
{
     0,  0,  0,  5,  5,  0,  0,  0,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
    -5,  0,  0,  0,  0,  0,  0, -5,
     5, 10, 10, 10, 10, 10, 10,  5,
     0,  0,  0,  0,  0,  0,  0,  0
};
static constexpr int queenTable[64] =
{
   -20,-10,-10, -5, -5,-10,-10,-20,
   -10,  0,  5,  0,  0,  0,  0,-10,
   -10,  5,  5,  5,  5,  5,  0,-10,
     0,  0,  5,  5,  5,  5,  0, -5,
    -5,  0,  5,  5,  5,  5,  0, -5,
   -10,  0,  5,  5,  5,  5,  0,-10,
   -10,  0,  0,  0,  0,  0,  0,-10,
   -20,-10,-10, -5, -5,-10,-10,-20
};
static constexpr int kingMidTable[64] =
{
    20, 30, 10,  0,  0, 10, 30, 20,
    20, 20,  0,  0,  0,  0, 20, 20,
   -10,-20,-20,-20,-20,-20,-20,-10,
   -20,-30,-30,-40,-40,-30,-30,-20,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30,
   -30,-40,-40,-50,-50,-40,-40,-30
};
static constexpr int kingEndTable[64] =
{
   -50,-30,-30,-30,-30,-30,-30,-50,
   -30,-30,  0,  0,  0,  0,-30,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 30, 40, 40, 30,-10,-30,
   -30,-10, 20, 30, 30, 20,-10,-30,
   -30,-20,-10,  0,  0,-10,-20,-30,
   -50,-40,-30,-20,-20,-30,-40,-50
};

// Precomputed file masks (1 bit per square on that file)
static constexpr uint64_t FILE_MASK[8] = {
    0x0101010101010101ULL,
    0x0202020202020202ULL,
    0x0404040404040404ULL,
    0x0808080808080808ULL,
    0x1010101010101010ULL,
    0x2020202020202020ULL,
    0x4040404040404040ULL,
    0x8080808080808080ULL
};

// Precomputed passed-pawn front spans:
// For white pawn on sq, all squares on files [f-1,f,f+1] with rank > sq's rank.
// For black pawn on sq, all squares on files [f-1,f,f+1] with rank < sq's rank.
static uint64_t passedMaskWhite[64];
static uint64_t passedMaskBlack[64];

static void initPassedMasks()
{
    for (int sq = 0; sq < 64; ++sq)
    {
        int rank = sq >> 3;
        int file = sq & 7;

        uint64_t w = 0, b = 0;
        for (int f = file - 1; f <= file + 1; ++f)
        {
            if (f < 0 || f > 7) continue;
            uint64_t fm = FILE_MASK[f];
            // White: ranks above sq
            for (int r = rank + 1; r < 8; ++r)
                w |= (1ULL << (r * 8 + f));
            // Black: ranks below sq
            for (int r = 0; r < rank; ++r)
                b |= (1ULL << (r * 8 + f));
        }
        passedMaskWhite[sq] = w;
        passedMaskBlack[sq] = b;
    }
}

static inline int mirror(int sq) { return sq ^ 56; }

static constexpr int passedBonus[8] = { 0, 10, 20, 40, 60, 90, 130, 0 };
static constexpr int advanceBonus[8] = { 0, 10, 20, 40, 60, 80, 120, 0 };

// ─────────────────────────────────────────────────────────────────────────────
// Engine ctor / newGame
// ─────────────────────────────────────────────────────────────────────────────

Engine::Engine()
{
    // Allocate fixed-size TT on heap
    tt = new TTEntry[TT_SIZE]();

    // Precompute passed-pawn masks
    initPassedMasks();

    // Precompute LMR table
    for (int d = 0; d < LMR_MAX_D; ++d)
        for (int m = 0; m < LMR_MAX_M; ++m)
        {
            if (d < 3 || m < 4)
                lmrTable[d][m] = 0;
            else
                lmrTable[d][m] = std::max(
                    1,
                    static_cast<int>(
                        0.75 + std::log(d) * std::log(m) / 2.25
                    )
                );
        }

    newGame();
}

void Engine::newGame()
{
    nodes         = 0;
    stopSearch    = false;
    nextTimeCheck = 0;

    // Zero out TT
    std::memset(tt, 0, TT_SIZE * sizeof(TTEntry));

    for (auto& row : killerMoves)
    {
        row[0] = chess::Move::NO_MOVE;
        row[1] = chess::Move::NO_MOVE;
    }

    for (auto& row : historyTable)
        row.fill(0);

    bestCompletedMove = chess::Move::NO_MOVE;
}

long long Engine::getNodes() const { return nodes; }

// Check time only every 1024 nodes to avoid expensive syscalls
bool Engine::timeUp()
{
    if (nodes < nextTimeCheck) return false;
    nextTimeCheck = nodes + 1024;
    return std::chrono::steady_clock::now() >= stopTime;
}

// ─────────────────────────────────────────────────────────────────────────────
// TT
// ─────────────────────────────────────────────────────────────────────────────

int Engine::scoreToTT(int score, int ply) const
{
    if (score >  MATE_SCORE - MAX_PLY) return score + ply;
    if (score < -MATE_SCORE + MAX_PLY) return score - ply;
    return score;
}

int Engine::scoreFromTT(int score, int ply) const
{
    if (score >  MATE_SCORE - MAX_PLY) return score - ply;
    if (score < -MATE_SCORE + MAX_PLY) return score + ply;
    return score;
}

void Engine::storeTT(
    uint64_t    key,
    int         depth,
    int         score,
    TTFlag      flag,
    chess::Move bestMove
)
{
    TTEntry& e = tt[key & TT_MASK];
    // Always-replace: deeper searches overwrite; same depth keeps best move
    if (e.key != key || e.depth <= (int16_t)depth)
    {
        e.key      = key;
        e.depth    = (int16_t)depth;
        e.score    = (int32_t)score;
        e.flag     = flag;
        e.bestMove = bestMove;
    }
}

bool Engine::probeTT(
    uint64_t     key,
    int          depth,
    int          alpha,
    int          beta,
    int&         score,
    chess::Move& bestMove
)
{
    const TTEntry& e = tt[key & TT_MASK];
    if (e.key != key) return false;

    bestMove = e.bestMove;

    if (e.depth < (int16_t)depth) return false;

    int s = scoreFromTT((int)e.score, 0);

    if (e.flag == EXACT)                           { score = s; return true; }
    if (e.flag == LOWERBOUND && s >= beta)         { score = s; return true; }
    if (e.flag == UPPERBOUND && s <= alpha)        { score = s; return true; }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Evaluation
// ─────────────────────────────────────────────────────────────────────────────

int Engine::pieceValue(chess::PieceType pt) const
{
    if (pt == chess::PieceType::PAWN)   return 100;
    if (pt == chess::PieceType::KNIGHT) return 320;
    if (pt == chess::PieceType::BISHOP) return 330;
    if (pt == chess::PieceType::ROOK)   return 500;
    if (pt == chess::PieceType::QUEEN)  return 900;
    return 0;
}

int Engine::evaluate(const chess::Board& board) const
{
    int score = 0;

    // ── Phase ─────────────────────────────────────────────────────────────
    // Count non-pawn, non-king material using popcount on bitboards (fast)
    int mat =
        900 * (int)(board.pieces(chess::PieceType::QUEEN ).count()) +
        500 * (int)(board.pieces(chess::PieceType::ROOK  ).count()) +
        330 * (int)(board.pieces(chess::PieceType::BISHOP).count()) +
        320 * (int)(board.pieces(chess::PieceType::KNIGHT).count()) +
        100 * (int)(board.pieces(chess::PieceType::PAWN  ).count());

    int phase = std::min(256, mat * 256 / 7800);

    // ── Per-side evaluation ───────────────────────────────────────────────
    for (int c = 0; c < 2; ++c)
    {
        chess::Color color = chess::Color(c);
        chess::Color enemy = ~color;
        int sign = (c == 0) ? 1 : -1; // 0=WHITE=+1

        uint64_t myPawnBB  = board.pieces(chess::PieceType::PAWN, color).getBits();
        uint64_t enPawnBB  = board.pieces(chess::PieceType::PAWN, enemy).getBits();
        bool isWhite = (c == 0);

        // ── Pawns ──────────────────────────────────────────────────────────
        uint64_t pb = myPawnBB;
        while (pb)
        {
            int sq = __builtin_ctzll(pb);
            pb &= pb - 1;

            score += sign * 100;
            score += sign * (isWhite ? pawnTable[sq] : pawnTable[sq ^ 56]);

            int rank = isWhite ? (sq >> 3) : (7 - (sq >> 3));
            score += sign * advanceBonus[rank];

            int file = sq & 7;

            // Passed pawn (bitboard mask, no inner loop)
            uint64_t frontSpan = isWhite
                ? passedMaskWhite[sq]
                : passedMaskBlack[sq];
            if (!(enPawnBB & frontSpan))
                score += sign * passedBonus[rank];

            // Doubled pawn: any friendly pawn on same file (excluding self)
            uint64_t fileMask = FILE_MASK[file];
            if (__builtin_popcountll(myPawnBB & fileMask) > 1)
                score -= sign * 10;

            // Isolated pawn: no friendly pawn on adjacent files
            bool isolated = true;
            if (file > 0 && (myPawnBB & FILE_MASK[file - 1])) isolated = false;
            if (file < 7 && (myPawnBB & FILE_MASK[file + 1])) isolated = false;
            if (isolated) score -= sign * 15;
        }

        // ── Knights ───────────────────────────────────────────────────────
        uint64_t nb = board.pieces(chess::PieceType::KNIGHT, color).getBits();
        while (nb)
        {
            int sq = __builtin_ctzll(nb);
            nb &= nb - 1;
            score += sign * 320;
            score += sign * (isWhite ? knightTable[sq] : knightTable[sq ^ 56]);
        }

        // ── Bishops ───────────────────────────────────────────────────────
        uint64_t bb = board.pieces(chess::PieceType::BISHOP, color).getBits();
        int bishopCount = __builtin_popcountll(bb);
        while (bb)
        {
            int sq = __builtin_ctzll(bb);
            bb &= bb - 1;
            score += sign * 330;
            score += sign * (isWhite ? bishopTable[sq] : bishopTable[sq ^ 56]);
        }
        if (bishopCount >= 2) score += sign * 30;

        // ── Rooks ─────────────────────────────────────────────────────────
        uint64_t rb = board.pieces(chess::PieceType::ROOK, color).getBits();
        while (rb)
        {
            int sq = __builtin_ctzll(rb);
            rb &= rb - 1;
            score += sign * 500;
            score += sign * (isWhite ? rookTable[sq] : rookTable[sq ^ 56]);

            int file = sq & 7;
            bool noFriendly = !(myPawnBB & FILE_MASK[file]);
            bool noEnemy    = !(enPawnBB & FILE_MASK[file]);
            if (noFriendly && noEnemy) score += sign * 20;
            else if (noFriendly)       score += sign * 10;

            int rank = sq >> 3;
            if ((isWhite && rank >= 6) || (!isWhite && rank <= 1))
                score += sign * 15;
        }

        // ── Queens ────────────────────────────────────────────────────────
        uint64_t qb = board.pieces(chess::PieceType::QUEEN, color).getBits();
        while (qb)
        {
            int sq = __builtin_ctzll(qb);
            qb &= qb - 1;
            score += sign * 900;
            score += sign * (isWhite ? queenTable[sq] : queenTable[sq ^ 56]);
        }

        // ── King ──────────────────────────────────────────────────────────
        uint64_t kb = board.pieces(chess::PieceType::KING, color).getBits();
        if (kb)
        {
            int sq = __builtin_ctzll(kb);
            int mg = isWhite ? kingMidTable[sq] : kingMidTable[sq ^ 56];
            int eg = isWhite ? kingEndTable[sq] : kingEndTable[sq ^ 56];
            score += sign * ((mg * phase + eg * (256 - phase)) / 256);

            // Pawn shield (only in middlegame)
            if (phase > 128)
            {
                int kfile = sq & 7;
                int krank = sq >> 3;
                int shieldRank = isWhite ? krank + 1 : krank - 1;

                if (shieldRank >= 0 && shieldRank < 8)
                {
                    for (int df = -1; df <= 1; ++df)
                    {
                        int f2 = kfile + df;
                        if (f2 < 0 || f2 > 7) continue;
                        if (myPawnBB & (1ULL << (shieldRank * 8 + f2)))
                            score += sign * 10;
                    }
                }
            }
        }
    }

    return board.sideToMove() == chess::Color::WHITE ? score : -score;
}

// ─────────────────────────────────────────────────────────────────────────────
// Move ordering
// ─────────────────────────────────────────────────────────────────────────────

int Engine::captureScore(
    chess::Board&      board,
    const chess::Move& move
) const
{
    auto victim   = board.at(move.to()).type();
    auto attacker = board.at(move.from()).type();
    return 10 * pieceValue(victim) - pieceValue(attacker);
}

void Engine::scoreMoves(
    chess::Board&    board,
    chess::Movelist& moves,
    int              ply,
    chess::Move      ttMove
)
{
    for (auto& move : moves)
    {
        int s = 0;
        if (move == ttMove)
        {
            s = 2'000'000;
        }
        else if (board.isCapture(move))
        {
            s = 1'000'000 + captureScore(board, move);
        }
        else if (move == killerMoves[ply][0])
        {
            s = 900'000;
        }
        else if (move == killerMoves[ply][1])
        {
            s = 800'000;
        }
        else
        {
            s = historyTable[move.from().index()][move.to().index()];
        }
        move.setScore(s);
    }
}

// Partial sort: swap highest-scored remaining move to position `start`
void Engine::pickMove(chess::Movelist& moves, int start)
{
    int best = start;
    for (int i = start + 1; i < (int)moves.size(); ++i)
        if (moves[i].score() > moves[best].score())
            best = i;
    if (best != start)
        std::swap(moves[best], moves[start]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Quiescence search
// ─────────────────────────────────────────────────────────────────────────────

int Engine::quiescence(
    chess::Board& board,
    int alpha,
    int beta,
    int ply
)
{
    if (timeUp()) { stopSearch = true; return alpha; }
    ++nodes;

    int standPat = evaluate(board);
    if (standPat >= beta) return beta;

    // Delta pruning
    static constexpr int DELTA = 975;
    if (standPat + DELTA < alpha) return alpha;

    alpha = std::max(alpha, standPat);

    // TT probe in qsearch (depth=0)
    {
        int ttScore;
        chess::Move ttMove = chess::Move::NO_MOVE;
        if (probeTT(board.hash(), 0, alpha, beta, ttScore, ttMove))
            return ttScore;
    }

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    scoreMoves(board, moves, ply, chess::Move::NO_MOVE);

    for (int i = 0; i < (int)moves.size(); ++i)
    {
        pickMove(moves, i);
        const auto& move = moves[i];

        if (!board.isCapture(move)) continue;
        if (captureScore(board, move) < -50) continue;

        board.makeMove(move);
        int score = -quiescence(board, -beta, -alpha, ply + 1);
        board.unmakeMove(move);

        if (score >= beta) return beta;
        alpha = std::max(alpha, score);
    }

    return alpha;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main alpha-beta search
// ─────────────────────────────────────────────────────────────────────────────

int Engine::search(
    chess::Board& board,
    int depth,
    int alpha,
    int beta,
    int ply,
    bool isPvNode,
    bool canNullMove
)
{
    if (timeUp()) { stopSearch = true; return alpha; }
    ++nodes;

    // ── Draw detection ────────────────────────────────────────────────────
    if (ply > 0 && (board.isRepetition(1) || board.isHalfMoveDraw()))
        return 0;

    if (ply > 0 && board.isInsufficientMaterial())
        return 0;

    bool inCheck = board.inCheck();
    if (inCheck) ++depth;

    if (depth <= 0)
        return quiescence(board, alpha, beta, ply);

    // ── TT probe ─────────────────────────────────────────────────────────
    int         ttScore = 0;
    chess::Move ttMove  = chess::Move::NO_MOVE;
    bool ttHit = (ply > 0) &&
        probeTT(board.hash(), depth, alpha, beta, ttScore, ttMove);
    if (ttHit && !isPvNode) return ttScore;

    int originalAlpha = alpha;

    // ── Null-move pruning ─────────────────────────────────────────────────
    static constexpr int NMP_REDUCTION = 3;
    if (!isPvNode && !inCheck && canNullMove && depth >= 3 &&
        board.hasNonPawnMaterial(board.sideToMove()))
    {
        board.makeNullMove();
        int nullScore = -search(
            board, depth - NMP_REDUCTION - 1,
            -beta, -beta + 1,
            ply + 1, false, false
        );
        board.unmakeNullMove();

        if (stopSearch) return alpha;
        if (nullScore >= beta) return beta;
    }

    // ── Move generation ───────────────────────────────────────────────────
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    if (moves.empty())
        return inCheck ? -MATE_SCORE + ply : 0;

    scoreMoves(board, moves, ply, ttMove);

    chess::Move bestMove  = chess::Move::NO_MOVE;
    int         moveCount = 0;

    for (int i = 0; i < (int)moves.size(); ++i)
    {
        // Lazy pick: partial sort — only pay for one swap per iteration
        pickMove(moves, i);
        const auto& move = moves[i];

        // Determine if this is a quiet move BEFORE making it
        bool isQuiet = !board.isCapture(move)
                    && move.typeOf() != chess::Move::PROMOTION;

        board.makeMove(move);
        ++moveCount;

        bool givesCheck = board.inCheck(); // after making the move

        int score = 0;
        bool doFullSearch = true;

        // ── Late Move Reductions (LMR) ────────────────────────────────
        int d = std::min(depth - 1, LMR_MAX_D - 1);
        int m = std::min(moveCount,  LMR_MAX_M - 1);
        int R = lmrTable[d][m];

        if (R > 0 && isQuiet && !inCheck && !givesCheck)
        {
            score = -search(
                board,
                depth - 1 - R,
                -alpha - 1, -alpha,
                ply + 1, false, true
            );
            doFullSearch = (!stopSearch && score > alpha);
        }

        if (doFullSearch)
        {
            if (moveCount == 1)
            {
                // First move: full window
                score = -search(
                    board, depth - 1,
                    -beta, -alpha,
                    ply + 1, isPvNode, true
                );
            }
            else
            {
                // PVS null-window
                score = -search(
                    board, depth - 1,
                    -alpha - 1, -alpha,
                    ply + 1, false, true
                );
                // Re-search if it falls inside [alpha, beta]
                if (!stopSearch && score > alpha && score < beta)
                {
                    score = -search(
                        board, depth - 1,
                        -beta, -alpha,
                        ply + 1, true, true
                    );
                }
            }
        }

        board.unmakeMove(move);

        if (stopSearch) return alpha;

        if (score > alpha)
        {
            alpha    = score;
            bestMove = move;
            if (ply == 0) rootBestMove = move;
        }

        if (alpha >= beta)
        {
            // Quiet-move heuristics (isQuiet was computed before makeMove)
            if (isQuiet)
            {
                if (killerMoves[ply][0] != move)
                {
                    killerMoves[ply][1] = killerMoves[ply][0];
                    killerMoves[ply][0] = move;
                }
                int& h = historyTable[move.from().index()][move.to().index()];
                h += depth * depth;
                if (h > 100'000)
                {
                    for (auto& row : historyTable)
                        for (auto& v : row) v >>= 1;
                }
            }
            break;
        }
    }

    // ── Store TT ─────────────────────────────────────────────────────────
    TTFlag flag = EXACT;
    if      (alpha <= originalAlpha) flag = UPPERBOUND;
    else if (alpha >= beta)          flag = LOWERBOUND;

    if (!stopSearch)
        storeTT(board.hash(), depth, scoreToTT(alpha, ply), flag, bestMove);

    return alpha;
}

// ─────────────────────────────────────────────────────────────────────────────
// Iterative deepening with aspiration windows
// ─────────────────────────────────────────────────────────────────────────────

int Engine::iterativeDeepening(chess::Board& board, int maxTimeMs)
{
    stopTime =
        std::chrono::steady_clock::now()
        + std::chrono::milliseconds(maxTimeMs);
    stopSearch    = false;
    nextTimeCheck = 0;

    int score = 0;

    for (int depth = 1; depth <= 64; ++depth)
    {
        int alpha, beta;

        if (depth >= 5)
        {
            alpha = score - ASP_WINDOW;
            beta  = score + ASP_WINDOW;
        }
        else
        {
            alpha = -INF;
            beta  =  INF;
        }

        int current = 0;

        // Aspiration loop
        while (true)
        {
            current = search(board, depth, alpha, beta, 0, true, true);
            if (stopSearch) break;

            if      (current <= alpha) alpha = std::max(alpha - ASP_WINDOW * 4, -INF);
            else if (current >= beta)  beta  = std::min(beta  + ASP_WINDOW * 4,  INF);
            else                       break;
        }

        if (stopSearch) break;

        score = current;
        bestCompletedMove = rootBestMove;

        std::cout
            << "info depth " << depth
            << " score cp "  << score
            << " nodes "     << nodes
            << "\n"
            << std::flush;

        if (score >  MATE_SCORE - 200 ||
            score < -MATE_SCORE + 200)
            break;
    }

    return score;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public entry
// ─────────────────────────────────────────────────────────────────────────────

chess::Move Engine::findBestMove(chess::Board& board, int moveTimeMs)
{
    nodes             = 0;
    rootBestMove      = chess::Move::NO_MOVE;
    bestCompletedMove = chess::Move::NO_MOVE;

    auto result = board.isGameOver();
    if (result.first != chess::GameResultReason::NONE)
        return chess::Move::NO_MOVE;

    iterativeDeepening(board, moveTimeMs);

    return (bestCompletedMove != chess::Move::NO_MOVE)
           ? bestCompletedMove
           : rootBestMove;
}