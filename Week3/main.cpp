#include <iostream>
#include "chess/include/chess.hpp"

int main()
{
    chess::Board board;

    std::cout << board.getFen() << '\n';
}