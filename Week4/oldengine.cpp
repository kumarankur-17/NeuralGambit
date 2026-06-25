#include "Engine.h"

#include <algorithm>

Engine::Engine()
{
    newGame();
}

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

static inline int mirror(int sq)
{
    return sq ^ 56;
}

void Engine::newGame()
{
    nodes = 0;
    stopSearch = false;

    tt.clear();

    for (auto& row : killerMoves)
    {
        row[0] = chess::Move::NO_MOVE;
        row[1] = chess::Move::NO_MOVE;
    }

    for (auto& row : historyTable)
    {
        row.fill(0);
    }

    bestCompletedMove = chess::Move::NO_MOVE;
}

long long Engine::getNodes() const
{
    return nodes;
}

bool Engine::timeUp() const
{
    return std::chrono::steady_clock::now() >= stopTime;
}

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

    const chess::Color colors[2] =
    {
        chess::Color::WHITE,
        chess::Color::BLACK
    };

    for (auto color : colors)
    {
        int sign =
            (color == chess::Color::WHITE)
            ? 1
            : -1;

        auto pawns =
            board.pieces(
                chess::PieceType::PAWN,
                color
            );

        while (pawns)
        {
            int sq = pawns.pop();

            score += sign * 100;

            if (color == chess::Color::WHITE)
                score += sign * pawnTable[sq];
            else
                score += sign * pawnTable[mirror(sq)];
        }

        auto knights =
            board.pieces(
                chess::PieceType::KNIGHT,
                color
            );

        while (knights)
        {
            int sq = knights.pop();

            score += sign * 320;

            if (color == chess::Color::WHITE)
                score += sign * knightTable[sq];
            else
                score += sign * knightTable[mirror(sq)];
        }

        auto bishops =
            board.pieces(
                chess::PieceType::BISHOP,
                color
            );

        while (bishops)
        {
            int sq = bishops.pop();

            score += sign * 330;

            if (color == chess::Color::WHITE)
                score += sign * bishopTable[sq];
            else
                score += sign * bishopTable[mirror(sq)];
        }

        score += sign * 500 *
            static_cast<int>(
                board.pieces(
                    chess::PieceType::ROOK,
                    color
                ).count());

        score += sign * 900 *
            static_cast<int>(
                board.pieces(
                    chess::PieceType::QUEEN,
                    color
                ).count());
    }

    return
        board.sideToMove() ==
        chess::Color::WHITE
        ? score
        : -score;
}

int Engine::captureScore(
    chess::Board& board,
    const chess::Move& move
) const
{
    if (!board.isCapture(move))
        return 0;

    auto victim =
        board.at(move.to()).type();

    auto attacker =
        board.at(move.from()).type();

    return
        10 * pieceValue(victim)
        - pieceValue(attacker);
}

void Engine::scoreMoves(
    chess::Board& board,
    chess::Movelist& moves,
    int ply,
    chess::Move ttMove
)
{
    for (auto& move : moves)
    {
        int score = 0;

        if (move == ttMove)
        {
            score = 2000000;
        }
        else if (board.isCapture(move))
        {
            score =
                1000000 +
                captureScore(board, move);
        }
        else if (move == killerMoves[ply][0])
        {
            score = 900000;
        }
        else if (move == killerMoves[ply][1])
        {
            score = 800000;
        }
        else
        {
            score =
                historyTable[
                    move.from().index()
                ][
                    move.to().index()
                ];
        }

        move.setScore(score);
    }

    std::sort(
        moves.begin(),
        moves.end(),
        [](const chess::Move& a,
           const chess::Move& b)
        {
            return a.score() > b.score();
        });
}

void Engine::storeTT(
    uint64_t key,
    int depth,
    int score,
    TTFlag flag,
    chess::Move bestMove
)
{
    tt[key] =
    {
        key,
        depth,
        score,
        flag,
        bestMove
    };
}

bool Engine::probeTT(
    uint64_t key,
    int depth,
    int alpha,
    int beta,
    int& score,
    chess::Move& bestMove
)
{
    auto it = tt.find(key);

    if (it == tt.end())
        return false;

    const auto& entry = it->second;

    bestMove = entry.bestMove;

    if (entry.depth < depth)
        return false;

    if (entry.flag == EXACT)
    {
        score = entry.score;
        return true;
    }

    if (entry.flag == LOWERBOUND &&
        entry.score >= beta)
    {
        score = entry.score;
        return true;
    }

    if (entry.flag == UPPERBOUND &&
        entry.score <= alpha)
    {
        score = entry.score;
        return true;
    }

    return false;
}

int Engine::quiescence(
    chess::Board& board,
    int alpha,
    int beta,
    int ply
)
{
    ++nodes;

    int standPat = evaluate(board);

    if (standPat >= beta)
        return beta;

    alpha = std::max(alpha, standPat);

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    for (const auto& move : moves)
    {
        if (!board.isCapture(move))
            continue;

        board.makeMove(move);

        int score =
            -quiescence(
                board,
                -beta,
                -alpha,
                ply + 1
            );

        board.unmakeMove(move);

        if (score >= beta)
            return beta;

        alpha = std::max(alpha, score);
    }

    return alpha;
}

int Engine::search(
    chess::Board& board,
    int depth,
    int alpha,
    int beta,
    int ply
)
{
    if (timeUp())
    {
        stopSearch = true;
        return 0;
    }

    ++nodes;

    auto result = board.isGameOver();

    if (result.first ==
        chess::GameResultReason::CHECKMATE)
    {
        return -MATE_SCORE + ply;
    }

    if (result.first !=
        chess::GameResultReason::NONE)
    {
        return 0;
    }

    if (depth == 0)
    {
        return quiescence(
            board,
            alpha,
            beta,
            ply
        );
    }

    int ttScore;
    chess::Move ttMove;

    if (ply>0 && probeTT(
            board.hash(),
            depth,
            alpha,
            beta,
            ttScore,
            ttMove))
    {
        return ttScore;
    }

    int originalAlpha = alpha;

    chess::Movelist moves;
    chess::movegen::legalmoves(moves, board);

    scoreMoves(
        board,
        moves,
        ply,
        ttMove
    );

    chess::Move bestMove =
        chess::Move::NO_MOVE;

    for (const auto& move : moves)
    {
        board.makeMove(move);

        int score =
            -search(
                board,
                depth - 1,
                -beta,
                -alpha,
                ply + 1
            );

        board.unmakeMove(move);

        if (stopSearch)
            return alpha;

        if (score > alpha)
        {
            alpha = score;
            bestMove = move;

            if (ply == 0)
            {
                rootBestMove = move;
            }
        }

        if (alpha >= beta)
        {
            if (!board.isCapture(move))
            {
                killerMoves[ply][1] =
                    killerMoves[ply][0];

                killerMoves[ply][0] =
                    move;

                historyTable[
                    move.from().index()
                ][
                    move.to().index()
                ] += depth * depth;
            }

            break;
        }
    }

    TTFlag flag = EXACT;

    if (alpha <= originalAlpha)
        flag = UPPERBOUND;
    else if (alpha >= beta)
        flag = LOWERBOUND;

    if (!stopSearch){
        storeTT(
            board.hash(),
            depth,
            alpha,
            flag,
            bestMove
        );
    }

    return alpha;
}

int Engine::iterativeDeepening(
    chess::Board& board,
    int maxTimeMs
)
{
    stopTime =
        std::chrono::steady_clock::now()
        + std::chrono::milliseconds(maxTimeMs);

    stopSearch = false;

    int score = 0;

    for (int depth = 1;
         depth <= 64;
         ++depth)
    {
        int current =
            search(
                board,
                depth,
                -INF,
                INF,
                0
            );

        if (stopSearch)
            break;

        score = current;

        bestCompletedMove = rootBestMove;

        std::cout
            << "info depth "
            << depth
            << " score cp "
            << current
            << " nodes "
            << nodes
            << std::endl;
    }
    
    return score;
}

chess::Move Engine::findBestMove(
    chess::Board& board,
    int moveTimeMs
)
{
    nodes = 0;

    rootBestMove = chess::Move::NO_MOVE;

    bestCompletedMove = chess::Move::NO_MOVE;

    auto result = board.isGameOver();

    if (result.first != chess::GameResultReason::NONE)
    {
        return chess::Move::NO_MOVE;
    }

    iterativeDeepening(
        board,
        moveTimeMs
    );

    if (bestCompletedMove != chess::Move::NO_MOVE) 
    {
        return bestCompletedMove;
    }

    return rootBestMove;
}