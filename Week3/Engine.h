#pragma once

#include <vector>
#include <unordered_map>

#include "chess/include/chess.hpp"

class Engine
{
public:
    bool findMate(chess::Board& board,
                  int depth,
                  std::vector<chess::Move>& line);

    long long getNodes() const;
    size_t getTTSize() const;

private:

    struct TTEntry
    {
        bool result;
        int depth;
    };

    long long nodes = 0;

    std::unordered_map<uint64_t, TTEntry> tt;

    bool attacker(chess::Board& board,
                  int depth,
                  std::vector<chess::Move>& line);

    bool defender(chess::Board& board,
                  int depth,
                  std::vector<chess::Move>& line);

    uint64_t makeKey(const chess::Board& board,
                     int depth,
                     bool attackerSide) const;
};