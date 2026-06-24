#include "Engine.h"

#include <algorithm>

static int moveScore(chess::Board& board,
                     const chess::Move& move)
{
    int score = 0;

    auto checkType = board.givesCheck(move);

    if (checkType == chess::CheckType::DIRECT_CHECK)
        score += 100000;

    else if (checkType == chess::CheckType::DISCOVERY_CHECK)
        score += 80000;

    if (board.isCapture(move))
        score += 5000;

    if (move.typeOf() == chess::Move::PROMOTION)
        score += 3000;

    return score;
}

static void orderMoves(chess::Board& board,
                       chess::Movelist& moves)
{
    std::sort(
        moves.begin(),
        moves.end(),
        [&](const chess::Move& a,
            const chess::Move& b)
        {
            return moveScore(board, a)
                 > moveScore(board, b);
        });
}

uint64_t Engine::makeKey(const chess::Board& board,
                         int depth,
                         bool attackerSide) const
{
    uint64_t key = board.hash();

    key ^= (static_cast<uint64_t>(depth) << 56);

    if (attackerSide)
        key ^= 0x9e3779b97f4a7c15ULL;
    else
        key ^= 0xc3a5c85c97cb3127ULL;

    return key;
}

bool Engine::findMate(chess::Board& board,
                      int depth,
                      std::vector<chess::Move>& line)
{
    nodes = 0;

    tt.clear();

    tt.reserve(1000000);

    line.clear();

    return attacker(board,
                    depth,
                    line);
}

bool Engine::attacker(chess::Board& board,
                      int depth,
                      std::vector<chess::Move>& line)
{
    ++nodes;

    uint64_t key =
        makeKey(board,
                depth,
                true);

    auto it = tt.find(key);

    if (it != tt.end() &&
        it->second.depth >= depth)
    {
        return it->second.result;
    }

    auto result = board.isGameOver();

    if (result.first ==
        chess::GameResultReason::CHECKMATE)
    {
        tt[key] = {true, depth};
        return true;
    }

    if (depth == 0)
    {
        tt[key] = {false, depth};
        return false;
    }

    chess::Movelist moves;
    chess::movegen::legalmoves(moves,
                               board);

    orderMoves(board,
               moves);

    for (const auto& move : moves)
    {
        board.makeMove(move);

        std::vector<chess::Move> childLine;

        bool success =
            defender(board,
                     depth - 1,
                     childLine);

        board.unmakeMove(move);

        if (success)
        {
            line.clear();

            line.push_back(move);

            line.insert(
                line.end(),
                childLine.begin(),
                childLine.end());

            tt[key] = {true, depth};

            return true;
        }
    }

    tt[key] = {false, depth};

    return false;
}

bool Engine::defender(chess::Board& board,
                      int depth,
                      std::vector<chess::Move>& line)
{
    ++nodes;

    uint64_t key =
        makeKey(board,
                depth,
                false);

    auto it = tt.find(key);

    if (it != tt.end() &&
        it->second.depth >= depth)
    {
        return it->second.result;
    }

    auto result = board.isGameOver();

    if (result.first ==
        chess::GameResultReason::CHECKMATE)
    {
        tt[key] = {true, depth};
        return true;
    }

    if (depth == 0)
    {
        tt[key] = {false, depth};
        return false;
    }

    chess::Movelist moves;
    chess::movegen::legalmoves(moves,
                               board);

    if (moves.empty())
    {
        tt[key] = {false, depth};
        return false;
    }

    orderMoves(board,
               moves);

    chess::Move bestReply;
    std::vector<chess::Move> bestLine;

    bool first = true;

    for (const auto& move : moves)
    {
        board.makeMove(move);

        std::vector<chess::Move> childLine;

        bool mateStillExists =
            attacker(board,
                     depth - 1,
                     childLine);

        board.unmakeMove(move);

        if (!mateStillExists)
        {
            tt[key] = {false, depth};

            return false;
        }

        if (first)
        {
            first = false;

            bestReply = move;

            bestLine = std::move(childLine);
        }
    }

    line.clear();

    line.push_back(bestReply);

    line.insert(
        line.end(),
        bestLine.begin(),
        bestLine.end());

    tt[key] = {true, depth};

    return true;
}

long long Engine::getNodes() const
{
    return nodes;
}

size_t Engine::getTTSize() const
{
    return tt.size();
}
