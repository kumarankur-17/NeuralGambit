#include "fastengine.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// ─────────────────────────────────────────────────────────────────────────────
// Piece-square tables  (white POV; XOR-56 mirrors for black)
// Merged: material + PST in one lookup to halve table reads
// ─────────────────────────────────────────────────────────────────────────────

// Each value = base material + positional bonus
static constexpr int PAWN_PST[64] = {
     100, 100, 100, 100, 100, 100, 100, 100,
     150, 150, 150, 150, 150, 150, 150, 150,
     110, 110, 120, 130, 130, 120, 110, 110,
     105, 105, 110, 125, 125, 110, 105, 105,
     100, 100, 100, 120, 120, 100, 100, 100,
     105,  95,  90, 100, 100,  90,  95, 105,
     105, 110, 110,  80,  80, 110, 110, 105,
     100, 100, 100, 100, 100, 100, 100, 100
};
static constexpr int KNIGHT_PST[64] = {
     270, 280, 290, 290, 290, 290, 280, 270,
     280, 300, 320, 320, 320, 320, 300, 280,
     290, 320, 330, 335, 335, 330, 320, 290,
     290, 325, 335, 340, 340, 335, 325, 290,
     290, 320, 335, 340, 340, 335, 320, 290,
     290, 325, 330, 335, 335, 330, 325, 290,
     280, 300, 320, 325, 325, 320, 300, 280,
     270, 280, 290, 290, 290, 290, 280, 270
};
static constexpr int BISHOP_PST[64] = {
     310, 320, 320, 320, 320, 320, 320, 310,
     320, 335, 330, 330, 330, 330, 335, 320,
     320, 340, 340, 340, 340, 340, 340, 320,
     320, 330, 340, 340, 340, 340, 330, 320,
     320, 335, 335, 340, 340, 335, 335, 320,
     320, 330, 335, 340, 340, 335, 330, 320,
     320, 330, 330, 330, 330, 330, 330, 320,
     310, 320, 320, 320, 320, 320, 320, 310
};
static constexpr int ROOK_PST[64] = {
     500, 500, 500, 505, 505, 500, 500, 500,
     495, 500, 500, 500, 500, 500, 500, 495,
     495, 500, 500, 500, 500, 500, 500, 495,
     495, 500, 500, 500, 500, 500, 500, 495,
     495, 500, 500, 500, 500, 500, 500, 495,
     495, 500, 500, 500, 500, 500, 500, 495,
     505, 510, 510, 510, 510, 510, 510, 505,
     500, 500, 500, 500, 500, 500, 500, 500
};
static constexpr int QUEEN_PST[64] = {
     880, 890, 890, 895, 895, 890, 890, 880,
     890, 900, 905, 900, 900, 900, 900, 890,
     890, 905, 905, 905, 905, 905, 900, 890,
     900, 900, 905, 905, 905, 905, 900, 895,
     895, 900, 905, 905, 905, 905, 900, 895,
     890, 905, 905, 905, 905, 905, 900, 890,
     890, 900, 905, 900, 900, 900, 900, 890,
     880, 890, 890, 895, 895, 890, 890, 880
};
// King: blend of mid (castled) and end (centralise) baked together.
// We use a single table that rewards castling and avoids centre in mid.
static constexpr int KING_PST[64] = {
     -30,-40,-40,-50,-50,-40,-40,-30,
     -30,-40,-40,-50,-50,-40,-40,-30,
     -30,-40,-40,-50,-50,-40,-40,-30,
     -30,-40,-40,-50,-50,-40,-40,-30,
     -20,-30,-30,-40,-40,-30,-30,-20,
     -10,-20,-20,-20,-20,-20,-20,-10,
      20, 20,  0,  0,  0,  0, 20, 20,
      20, 30, 10,  0,  0, 10, 30, 20
};

// File masks for open-file rook bonus
static constexpr uint64_t FILE_BB[8] = {
    0x0101010101010101ULL, 0x0202020202020202ULL,
    0x0404040404040404ULL, 0x0808080808080808ULL,
    0x1010101010101010ULL, 0x2020202020202020ULL,
    0x4040404040404040ULL, 0x8080808080808080ULL
};

// ─────────────────────────────────────────────────────────────────────────────
// Engine ctor / dtor / newGame
// ─────────────────────────────────────────────────────────────────────────────

Engine::Engine()
{
    tt = new TTEntry[TT_SIZE]();

    // Build LMR table
    for (int d = 0; d < LMR_D; ++d)
        for (int m = 0; m < LMR_M; ++m)
            lmrTable[d][m] =
                (d >= 3 && m >= 4)
                ? std::max(1, (int)(0.75 + std::log(d) * std::log(m) / 2.25))
                : 0;

    newGame();
}

Engine::~Engine() { delete[] tt; }

void Engine::newGame()
{
    nodes         = 0;
    nextTimeCheck = 0;
    stopSearch    = false;

    std::memset(tt,           0, TT_SIZE * sizeof(TTEntry));
    std::memset(killerMoves,  0, sizeof(killerMoves));
    std::memset(historyTable, 0, sizeof(historyTable));

    bestCompletedMove = chess::Move::NO_MOVE;
    rootBestMove      = chess::Move::NO_MOVE;
}

long long Engine::getNodes() const { return nodes; }

// Check wall clock every 1024 nodes
bool Engine::timeUp()
{
    if (nodes < nextTimeCheck) return false;
    nextTimeCheck = nodes + 1024;
    return std::chrono::steady_clock::now() >= stopTime;
}

// ─────────────────────────────────────────────────────────────────────────────
// TT
// ─────────────────────────────────────────────────────────────────────────────

inline int Engine::scoreToTT(int s, int ply) const
{
    if (s >  MATE_SCORE - MAX_PLY) return s + ply;
    if (s < -MATE_SCORE + MAX_PLY) return s - ply;
    return s;
}
inline int Engine::scoreFromTT(int s, int ply) const
{
    if (s >  MATE_SCORE - MAX_PLY) return s - ply;
    if (s < -MATE_SCORE + MAX_PLY) return s + ply;
    return s;
}

void Engine::storeTT(uint64_t key, int depth, int score,
                     TTFlag flag, chess::Move bestMove)
{
    TTEntry& e = tt[key & TT_MASK];
    // Replace if: new entry, deeper, or same depth with exact flag
    if (e.key != key || depth >= e.depth || flag == EXACT)
    {
        e.key      = key;
        e.depth    = (int16_t)depth;
        e.score    = (int32_t)score;
        e.flag     = flag;
        e.bestMove = bestMove;
    }
}

bool Engine::probeTT(uint64_t key, int depth, int alpha, int beta,
                     int& score, chess::Move& bestMove)
{
    const TTEntry& e = tt[key & TT_MASK];
    if (e.key != key) return false;

    bestMove = e.bestMove;
    if (e.depth < (int16_t)depth) return false;

    int s = scoreFromTT(e.score, 0);
    if (e.flag == EXACT)                    { score = s; return true; }
    if (e.flag == LOWERBOUND && s >= beta)  { score = s; return true; }
    if (e.flag == UPPERBOUND && s <= alpha) { score = s; return true; }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Evaluation — deliberately lean and fast
// One pass over bitboards, material + PST folded together,
// only one structural bonus (open-file rook). No loops inside loops.
// ─────────────────────────────────────────────────────────────────────────────

int Engine::evaluate(const chess::Board& board) const
{
    int score = 0;

    for (int c = 0; c < 2; ++c)
    {
        const chess::Color color = chess::Color(c);
        const bool isWhite = (c == 0);
        const int  sign    = isWhite ? 1 : -1;

        uint64_t myPawns = board.pieces(chess::PieceType::PAWN,   color).getBits();
        uint64_t enPawns = board.pieces(chess::PieceType::PAWN,  ~color).getBits();

        // Pawns
        for (uint64_t b = myPawns; b; b &= b - 1)
        {
            int sq = __builtin_ctzll(b);
            score += sign * PAWN_PST[isWhite ? sq : sq ^ 56];
        }

        // Knights
        for (uint64_t b = board.pieces(chess::PieceType::KNIGHT, color).getBits(); b; b &= b - 1)
        {
            int sq = __builtin_ctzll(b);
            score += sign * KNIGHT_PST[isWhite ? sq : sq ^ 56];
        }

        // Bishops + pair bonus
        {
            uint64_t b = board.pieces(chess::PieceType::BISHOP, color).getBits();
            if (__builtin_popcountll(b) >= 2) score += sign * 30;
            for (; b; b &= b - 1)
            {
                int sq = __builtin_ctzll(b);
                score += sign * BISHOP_PST[isWhite ? sq : sq ^ 56];
            }
        }

        // Rooks + open-file bonus
        for (uint64_t b = board.pieces(chess::PieceType::ROOK, color).getBits(); b; b &= b - 1)
        {
            int sq   = __builtin_ctzll(b);
            int file = sq & 7;
            score += sign * ROOK_PST[isWhite ? sq : sq ^ 56];
            if (!(myPawns & FILE_BB[file]) && !(enPawns & FILE_BB[file]))
                score += sign * 20;
            else if (!(myPawns & FILE_BB[file]))
                score += sign * 10;
        }

        // Queens
        for (uint64_t b = board.pieces(chess::PieceType::QUEEN, color).getBits(); b; b &= b - 1)
        {
            int sq = __builtin_ctzll(b);
            score += sign * QUEEN_PST[isWhite ? sq : sq ^ 56];
        }

        // King
        {
            uint64_t b = board.pieces(chess::PieceType::KING, color).getBits();
            if (b)
            {
                int sq = __builtin_ctzll(b);
                score += sign * KING_PST[isWhite ? sq : sq ^ 56];
            }
        }
    }

    return board.sideToMove() == chess::Color::WHITE ? score : -score;
}

// ─────────────────────────────────────────────────────────────────────────────
// Move ordering
// ─────────────────────────────────────────────────────────────────────────────

inline int Engine::pieceVal(chess::PieceType pt) const
{
    // Indexed by the underlying int value of PieceType
    static constexpr int val[7] = { 100, 320, 330, 500, 900, 0, 0 };
    return val[(int)pt];
}

inline int Engine::mvvLva(chess::Board& board, const chess::Move& move) const
{
    return 10 * pieceVal(board.at(move.to()).type())
              -  pieceVal(board.at(move.from()).type());
}

void Engine::scoreMoves(chess::Board& board, chess::Movelist& moves,
                        int ply, chess::Move ttMove)
{
    for (auto& move : moves)
    {
        int s;
        if      (move == ttMove)              s = 2'000'000;
        else if (board.isCapture(move))       s = 1'000'000 + mvvLva(board, move);
        else if (move == killerMoves[ply][0]) s = 900'000;
        else if (move == killerMoves[ply][1]) s = 800'000;
        else s = historyTable[move.from().index()][move.to().index()];
        move.setScore(s);
    }
}

// Bring the best remaining move to position `start`
void Engine::pickMove(chess::Movelist& moves, int start)
{
    int best = start;
    const int n = (int)moves.size();
    for (int i = start + 1; i < n; ++i)
        if (moves[i].score() > moves[best].score()) best = i;
    if (best != start) std::swap(moves[start], moves[best]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Quiescence
// ─────────────────────────────────────────────────────────────────────────────

int Engine::quiescence(chess::Board& board, int alpha, int beta, int ply)
{
    if (timeUp()) { stopSearch = true; return alpha; }
    ++nodes;

    int standPat = evaluate(board);
    if (standPat >= beta)            return beta;
    if (standPat + 975 < alpha)      return alpha;   // delta prune
    if (alpha < standPat)             alpha = standPat;

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);
    scoreMoves(board, moves, ply, chess::Move::NO_MOVE);

    const int n = (int)moves.size();
    for (int i = 0; i < n; ++i)
    {
        pickMove(moves, i);
        const auto& move = moves[i];
        if (!board.isCapture(move))        continue;
        if (mvvLva(board, move) < -50)     continue;  // skip bad captures

        board.makeMove(move);
        int score = -quiescence(board, -beta, -alpha, ply + 1);
        board.unmakeMove(move);

        if (score >= beta)  return beta;
        if (score > alpha)  alpha = score;
    }
    return alpha;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main search
// ─────────────────────────────────────────────────────────────────────────────

int Engine::search(chess::Board& board,
                   int depth, int alpha, int beta,
                   int ply, bool isPvNode, bool canNullMove)
{
    if (timeUp()) { stopSearch = true; return alpha; }
    ++nodes;

    // Draw detection
    if (ply > 0 && (board.isRepetition(1) || board.isHalfMoveDraw()
                    || board.isInsufficientMaterial()))
        return 0;

    const bool inCheck = board.inCheck();
    if (inCheck) ++depth;          // check extension

    if (depth <= 0)
        return quiescence(board, alpha, beta, ply);

    // TT probe
    int         ttScore = 0;
    chess::Move ttMove  = chess::Move::NO_MOVE;
    if (ply > 0 && probeTT(board.hash(), depth, alpha, beta, ttScore, ttMove)
        && !isPvNode)
        return ttScore;

    const int originalAlpha = alpha;

    // Null-move pruning
    if (!isPvNode && !inCheck && canNullMove && depth >= 3
        && board.hasNonPawnMaterial(board.sideToMove()))
    {
        board.makeNullMove();
        int ns = -search(board, depth - 4, -beta, -beta + 1,
                         ply + 1, false, false);
        board.unmakeNullMove();
        if (stopSearch) return alpha;
        if (ns >= beta) return beta;
    }

    // Move generation
    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    if (moves.empty())
        return inCheck ? -MATE_SCORE + ply : 0;

    scoreMoves(board, moves, ply, ttMove);

    chess::Move bestMove  = chess::Move::NO_MOVE;
    int         moveCount = 0;
    const int   n         = (int)moves.size();

    for (int i = 0; i < n; ++i)
    {
        pickMove(moves, i);
        const auto& move = moves[i];

        const bool isQuiet = !board.isCapture(move)
                          && move.typeOf() != chess::Move::PROMOTION;

        board.makeMove(move);
        ++moveCount;
        const bool givesCheck = board.inCheck();

        int score     = 0;
        bool doFull   = true;

        // LMR
        const int d = std::min(depth - 1, LMR_D - 1);
        const int m = std::min(moveCount,  LMR_M - 1);
        const int R = lmrTable[d][m];

        if (R > 0 && isQuiet && !inCheck && !givesCheck)
        {
            score   = -search(board, depth - 1 - R, -alpha - 1, -alpha,
                              ply + 1, false, true);
            doFull  = (!stopSearch && score > alpha);
        }

        if (doFull)
        {
            if (moveCount == 1)
            {
                score = -search(board, depth - 1, -beta, -alpha,
                                ply + 1, isPvNode, true);
            }
            else
            {
                score = -search(board, depth - 1, -alpha - 1, -alpha,
                                ply + 1, false, true);
                if (!stopSearch && score > alpha && score < beta)
                    score = -search(board, depth - 1, -beta, -alpha,
                                    ply + 1, true, true);
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
                    for (auto& row : historyTable)
                        for (auto& v : row) v >>= 1;
            }
            break;
        }
    }

    TTFlag flag = EXACT;
    if      (alpha <= originalAlpha) flag = UPPERBOUND;
    else if (alpha >= beta)          flag = LOWERBOUND;

    if (!stopSearch)
        storeTT(board.hash(), depth, scoreToTT(alpha, ply), flag, bestMove);

    return alpha;
}

// ─────────────────────────────────────────────────────────────────────────────
// Iterative deepening + aspiration windows
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
        int alpha = (depth >= 5) ? score - ASP_WINDOW : -INF;
        int beta  = (depth >= 5) ? score + ASP_WINDOW :  INF;

        int current = 0;
        while (true)
        {
            current = search(board, depth, alpha, beta, 0, true, true);
            if (stopSearch) break;
            if      (current <= alpha) alpha = std::max(alpha - ASP_WINDOW * 4, -INF);
            else if (current >= beta)  beta  = std::min(beta  + ASP_WINDOW * 4,  INF);
            else                       break;
        }
        if (stopSearch) break;

        score             = current;
        bestCompletedMove = rootBestMove;

        std::cout << "info depth " << depth
                  << " score cp "  << score
                  << " nodes "     << nodes
                  << "\n" << std::flush;

        if (score > MATE_SCORE - 200 || score < -MATE_SCORE + 200) break;
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

    if (board.isGameOver().first != chess::GameResultReason::NONE)
        return chess::Move::NO_MOVE;

    iterativeDeepening(board, moveTimeMs);

    return (bestCompletedMove != chess::Move::NO_MOVE)
           ? bestCompletedMove : rootBestMove;
}