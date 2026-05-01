// game.h
#pragma once
#include <string>
#include <vector>
#include <ctime>

class Game {
public:
    // Game setup
    int id;
    std::string white;
    std::string black;
    bool active = false;
    int numMoves = 0;

    // board
    char board[3][3];    // 3 x 3 grid
    bool blackTurn = false;

    // Timer
    int timeLimit = 0;
    double whiteTimeLeft = 300;
    double blackTimeLeft = 300;
    time_t turnStart;

    // Game observers
    std::vector<int> observers; // use fds

    // ===== Methods =====
    // Game
    Game();
    Game(int id, const std::string& white, const std::string& black, int timeSec);

    // Board
    std::string boardString() const;
    bool placeStone(int row, int col);
    bool checkWin(char stone) const;
    bool isFull() const;

    // Time
    void tickTime();    // take elapsed time from player
    double currentPlayerTimeLeft() const;
    bool isTimeUp() const;

    // Players
    std::string currentPlayer() const;
    std::string opponent(const std::string& name) const;
    
    bool isBlackPlayer(const std::string& name) const;
    bool isPlayer(const std::string& name) const;
    char stoneFor(const std::string& name) const; // '1' or '2'

    // Observers
    void addObserver(int fd);
    void removeObserver(int fd);
};