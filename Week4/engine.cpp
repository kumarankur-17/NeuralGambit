#include "Engine.h"

#include <algorithm>

Engine::Engine()
{
    newGame();
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

    const chess::PieceType pieces[6] =
    {
        chess::PieceType::PAWN,
        chess::PieceType::KNIGHT,
        chess::PieceType::BISHOP,
        chess::PieceType::ROOK,
        chess::PieceType::QUEEN,
        chess::PieceType::KING
    };

    for (auto color : colors)
    {
        int sign =
            (color == chess::Color::WHITE)
            ? 1
            : -1;

        for (auto pt : pieces)
        {
            auto bb = board.pieces(pt, color);

            score +=
                sign *
                pieceValue(pt) *
                static_cast<int>(bb.count());
        }
    }

    return board.sideToMove() == chess::Color::WHITE
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

    if (probeTT(
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
            return 0;

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

    storeTT(
        board.hash(),
        depth,
        alpha,
        flag,
        bestMove
    );

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

    auto result = board.isGameOver();

    if (result.first != chess::GameResultReason::NONE)
    {
        return chess::Move::NO_MOVE;
    }

    iterativeDeepening(
        board,
        moveTimeMs
    );

    return rootBestMove;
}