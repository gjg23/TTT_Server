// game.cpp
#include "game.h"
#include <sstream>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <string>
#include <iomanip>

// ===== Create Game =====================================================================
Game::Game() {
    memset(board, 0, sizeof(board));
    turnStart = time(nullptr);
}

Game::Game(int id, const std::string& white, const std::string& black, int timeSec)
    : id(id), white(white), black(black)
{
    memset(board, 0, sizeof(board));
    timeLimit = timeSec;
    whiteTimeLeft = (timeSec > 0) ? timeSec : 300.0;
    blackTimeLeft = (timeSec > 0) ? timeSec : 300.0;
    turnStart = time(nullptr);
    active = true;
}


// ===== Board ===========================================================================
std::string Game::boardString() const {
    // msg response
    std::ostringstream oss;

    // top player info
    oss << std::left
        << std::setw(8) << "Black:" << std::setw(12) << black
        << std::setw(8) << "White:" << std::setw(12) << white << "\n";

    oss << std::left
        << std::setw(8) << "Time:"  << std::setw(12) << (std::to_string((int)blackTimeLeft) + " seconds")
        << std::setw(8) << "Time:"  << std::setw(12) << (std::to_string((int)whiteTimeLeft) + " seconds")
        << "\n";

    // col headers
    oss << "   1  2  3\n";

    // board grid
    for (int row = 0; row < 3; ++row) {
        char rowLabel = 'A' + row;
        oss << rowLabel << "  ";
        for (int col = 0; col < 3; ++col) {
            char cell = board[row][col];
            char out = '.';

            if (cell == 'B') out = '#';
            else if (cell == 'W') out = 'O';

            oss << out << "  ";
        }
        oss << "\n";
    }

    oss << "\n";

    // send board message
    return oss.str();
}

bool Game::placeStone(int row, int col) {
    if (board[row][col]) return false;
    board[row][col] = blackTurn ? 'B' : 'W';
    return true;
}

bool Game::checkWin(char stone) const {
    for (int i = 0; i < 3; i++) {
        // check rows
        if (board[i][0]==stone && board[i][1]==stone && board[i][2]==stone) return true;
        // check cols
        if (board[0][i]==stone && board[1][i]==stone && board[2][i]==stone) return true;
    }
    // both diagonals
    if (board[0][0]==stone && board[1][1]==stone && board[2][2]==stone) return true;
    if (board[0][2]==stone && board[1][1]==stone && board[2][0]==stone) return true;
    return false;
}

bool Game::isFull() const {
    for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++) {
        if (!board[r][c]) return false;
    }
    return true;
}

void Game::tickTime() {
    double elapsed = difftime(time(nullptr), turnStart);
    if (blackTurn) blackTimeLeft -= elapsed;
    else           whiteTimeLeft -= elapsed;
    turnStart = time(nullptr);
}

double Game::currentPlayerTimeLeft() const {
    return blackTurn ? blackTimeLeft : whiteTimeLeft;
}

bool Game::isTimeUp() const {
    return currentPlayerTimeLeft() <= 0;
}

std::string Game::currentPlayer() const {
    return blackTurn ? black : white;
}

std::string Game::opponent(const std::string& name) const {
    return (name == black) ? white : black;
}

bool Game::isPlayer(const std::string& name) const {
    return name == black || name == white;
}

bool Game::isBlackPlayer(const std::string& name) const {
    return name == black;
}

char Game::stoneFor(const std::string& name) const {
    return (name == black) ? 'B' : 'W';
}

void Game::addObserver(int fd) {
    observers.push_back(fd);
}

void Game::removeObserver(int fd) {
    observers.erase(std::remove(observers.begin(), observers.end(), fd), observers.end());
}