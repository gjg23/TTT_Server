// game_cmds.cpp
#include "server.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <string>
#include <iomanip>

// List all current games
void Server::cmd_game(int fd, std::istringstream& ss) {
    if (games.empty()) {
        dprintf(fd, "No active games.\n");
        return;
    }
    dprintf(fd, "Total %d games\n", (int)games.size());
    for (auto& kv : games) {
        const Game& g = kv.second;
        dprintf(fd, "Game %d: %s .vs. %s, %d moves\n", g.id, g.white.c_str(), g.black.c_str(), g.numMoves);
    }
}

void Server::cmd_match(int fd, std::istringstream& ss) {
    ClientSession& session = clients[fd];
    std::string opponent, colorStr;
    int timeSec = 300;
    ss >> opponent >> colorStr >> timeSec;

    // Ton of checks
    if (opponent.empty()) {
        dprintf(fd, "Usage: match <name> <b|w> [seconds]\n");
        return;
    }
    if (colorStr.empty()) colorStr = "b";   // default color
    if (colorStr != "b" && colorStr != "w") {
        dprintf(fd, "Color must be b or w.\n");
        return;
    }
    if (!accounts.userExists(opponent)) {
        dprintf(fd, "User %s doesn't exist.\n", opponent.c_str());
        return;
    }

    User* opUser = accounts.getUser(opponent);
    if (opUser == session.user) {
        dprintf(fd, "Can't send a game to yourself\n");
        return;
    }
    if (opUser->online == false) {
        dprintf(fd, "Opponent is offline.\n");
        return;
    }
    if (opUser->isBlocking(session.username)) {
        dprintf(fd, "You are blocked by %s\n", opponent.c_str());
        return;       
    }
    if (session.gameId != -1) {
        dprintf(fd, "Please finish current match before starting new.\n");
        return;
    }
    if (opUser->gameId != -1) {
        dprintf(fd, "Opponent is in a game already.\n");
        return;
    }

    // init game
    std::string myColor = colorStr;
    std::string opColor = (myColor == "b") ? "w" : "b";

    // Check for active invite
    auto& recivedFrom = pendingInvites[session.username];
    auto invIt = recivedFrom.find(opponent);
    if (invIt != recivedFrom.end()) {
        // Start game
        PendingInvite& inv = invIt->second;

        if (inv.color == opColor) {
            int gid = nextGameId++;

            // Assign players
            std::string black = (myColor == "b") ? session.username : opponent;
            std::string white = (myColor == "w") ? session.username : opponent;
            int useTime = (timeSec > 0) ? timeSec : inv.timeSec;

            games.emplace(gid, Game(gid, black, white, useTime));

            // set gids
            session.gameId              = gid;
            session.user->gameId        = gid;
            opUser->gameId              = gid;
            clients[opUser->fd].gameId  = gid;

            // clear invite
            recivedFrom.erase(invIt);

            // Game start message
            broadcastGame(gid);
            sendPrompt(opUser->fd);

            return;
        }
        // match conflict - send both their wants
        std::string myWants    = "match " + opponent         + " " + myColor   + " " + std::to_string(timeSec);
        std::string theirWants = "match " + session.username + " " + inv.color + " " + std::to_string(inv.timeSec);
        std::string suggestion = "match " + opponent         + " " + opColor   + " " + std::to_string(inv.timeSec);

        // send to them
        dprintf(opUser->fd, "%s wants <%s>; %s wants <%s>\n",
            session.username.c_str(), myWants.c_str(), opponent.c_str(), theirWants.c_str());
        sendPrompt(opUser->fd);

        // overwrite with updated invite
        PendingInvite newInv;
        newInv.color = myColor;
        newInv.timeSec = timeSec;
        pendingInvites[opponent][session.username] = newInv;
        return;
    }

    // No pending invites yet (first outgoing invite)
    PendingInvite newInv;
    newInv.color = myColor;
    newInv.timeSec = timeSec;
    pendingInvites[opponent][session.username] = newInv;

    // Send notification of invite
    std::string invMsg = "match " + session.username + " " + opColor + " " + std::to_string(timeSec);

    dprintf(opUser->fd, "%s wants <match %s %s %d>\n",
        session.username.c_str(), opponent.c_str(), myColor.c_str(), timeSec);
    sendPrompt(opUser->fd);

    dprintf(fd, "Challenge sent.\n");
}

void Server::cmd_move(int fd, std::string& cmd) {
    ClientSession& session = clients[fd];
    if (session.gameId == -1) {
        dprintf(fd, "You are not in a game.\n");
        return;
    }

    // Get game
    auto it = games.find(session.gameId);
    if (it == games.end() || !it->second.active) {
        dprintf(fd, "Game is not active.\n");
        return;
    }
    Game& g = it->second;

    // Check if its this turn
    if (g.currentPlayer() != session.username) {
        dprintf(fd, "Not your turn\n");
        return;
    }

    // Take player time
    g.tickTime();
    if (g.isTimeUp()) {
        endGame(g.id, g.opponent(session.username), "wins on time");
        return;
    }

    // Get move
    int row = toupper(cmd[0]) - 'A';
    int col = cmd[1] - '1';
    if (row < 0 || row > 2 || col < 0 || col > 2) {
        dprintf(fd, "Invalid coords.\n");
        return; 
    }

    if (!g.placeStone(row, col)) {
        dprintf(fd, "Square already occupied.\n");
        return; 
    }

    char stone = g.stoneFor(session.username);
    if (g.checkWin(stone)) {
        broadcastGame(g.id);
        endGame(g.id, session.username, "wins");
        return;
    }
    if (g.isFull()) {
        broadcastGame(g.id);
        endGame(g.id, "", "draw");
        return;
    }

    g.blackTurn = !g.blackTurn;
    g.numMoves++;
    broadcastGame(g.id);
}

void Server::cmd_observe(int fd, std::istringstream& ss) {
    ClientSession& session = clients[fd];
    int gid; ss >> gid;

    if (session.gameId != -1) { 
        dprintf(fd, "You are in a game.\n"); 
        return; 
    }
    if (session.observeId != -1) { 
        dprintf(fd, "Unobserve your current game first.\n");
        return;
    }

    auto it = games.find(gid);
    if (it == games.end()) { 
        dprintf(fd, "No game with gid %d.\n", gid); 
        return; 
    }

    it->second.addObserver(fd);
    session.observeId = gid;
    session.user->observeId = gid;

    dprintf(fd, "Now observing game %d.\n", gid);
    broadcastGame(gid);
}

void Server::cmd_unobserve(int fd) {
    ClientSession& session = clients[fd];
    if (session.observeId == -1) { 
        dprintf(fd, "You are not observing a game.\n"); 
        return; 
    }

    auto it = games.find(session.observeId);
    if (it != games.end()) it->second.removeObserver(fd);

    session.observeId = -1;
    session.user->observeId = -1;

    dprintf(fd, "Unobserved.\n");
}

void Server::cmd_resign(int fd) {
    ClientSession& session = clients[fd];
    if (session.gameId == -1) {
        dprintf(fd, "You are not in a game.\n");
        return;
    }

    auto it = games.find(session.gameId);
    if (it == games.end()) return;
    Game& g = it->second;

    endGame(g.id, g.opponent(session.username), "wins by resignation");
}

void Server::cmd_refresh(int fd) {
    ClientSession& session = clients[fd];
    // select if observing or in a game
    int gid = (session.gameId != -1) ? session.gameId : session.observeId;

    if (gid == -1) { 
        dprintf(fd, "Not in or observing a game.\n"); 
        return; 
    }

    auto it = games.find(gid);
    if (it == games.end()) { 
        dprintf(fd, "Game not found.\n");
        return; 
    }

    // Send board only to this fd
    dprintf(fd, it->second.boardString().c_str());
}

void Server::cmd_gameMsg(int fd, std::istringstream& ss) {
    ClientSession& session = clients[fd];
    int gid = (session.gameId != -1) ? session.gameId : session.observeId;

    // ' is only for players in the game
    if (session.gameId == -1) {
        dprintf(fd, "You are not in a game.\n");
        return;
    }

    auto it = games.find(gid);
    if (it == games.end()) return;

    std::string rest;
    std::getline(ss, rest);
    if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);

    std::string msg = "[kibitz] " + session.username + ": " + rest + "\n";

    for (int ofd : it->second.observers) {
        User* u = clients[ofd].user;

        // checks
        if (!u) continue;
        if (u->quiet) continue;
        if (u->isBlocking(session.username)) continue;


        dprintf(ofd, msg.c_str());
    }
}

void Server::cmd_kibitz(int fd, std::istringstream& ss) {
    ClientSession& session = clients[fd];

    // kibitz only for observers
    if (session.observeId == -1) {
        dprintf(fd, "You are not observing a game.\n");
        return;
    }

    auto it = games.find(session.observeId);
    if (it == games.end()) return;

    std::string rest;
    std::getline(ss, rest);
    if (!rest.empty() && rest[0] == ' ') rest = rest.substr(1);

    std::string msg = "[kibitz] " + session.username + ": " + rest + "\n";

    for (int ofd : it->second.observers) {
        if (ofd == fd) continue;  // don't echo back to sender
        User* u = clients[ofd].user;
        if (!u) continue;
        if (u->quiet) continue;
        if (u->isBlocking(session.username)) continue;
        dprintf(ofd, msg.c_str());
        sendPrompt(ofd);
    }
}

// Game helpers
void Server::broadcastGame(int gameId) {
    // find game
    auto it = games.find(gameId);
    if (it == games.end()) return;
    Game& g = it->second;

    // send board message
    std::string msg = g.boardString();

    // Send to black player
    User* blackUser = accounts.getUser(g.black);
    if (blackUser && blackUser->online) {
        dprintf(blackUser->fd, "%s", msg.c_str());
    }

    // Send to white player
    User* whiteUser = accounts.getUser(g.white);
    if (whiteUser && whiteUser->online) {
        dprintf(whiteUser->fd, "%s", msg.c_str());
    }

    // send to observers
    for (int ofd : g.observers) {
        dprintf(ofd, "%s", msg.c_str());
    }
}

void Server::endGame(int gameId, const std::string& winner, const std::string& reason) {
    auto it = games.find(gameId);
    if (it == games.end()) return;
    Game& g = it->second;
    g.active = false;

    // Build result message
    std::string msg;
    if (winner.empty()) {
        msg = "Game " + std::to_string(gameId) + ": Draw.\n";
        // Dont need to update elo for draw
    } else {
        std::string loser = g.opponent(winner);
        msg = "Game " + std::to_string(gameId) + ": " + winner + " " + reason + ". " + loser + " loses.\n";

        // Update stats + simple Elo
        User* w = accounts.getUser(winner);
        User* l = accounts.getUser(loser);
        if (w && l && !w->isGuest && !l->isGuest) {
            w->wins++; l->losses++;

            // elo calc
            double exp = 1.0 / (1.0 + pow(10.0, (l->rating - w->rating) / 400.0));
            w->rating += 32.0 * (1.0 - exp);
            l->rating += 32.0 * (0.0 - (1.0 - exp));

            accounts.save();
        }
    }

    // finish up black
    User* uBlack = accounts.getUser(g.black);
    if (uBlack) {
        uBlack->gameId = -1;
        if (uBlack->online) {
            clients[uBlack->fd].gameId = -1;
            dprintf(uBlack->fd, msg.c_str());
        }
    }

    // finish up white
    User* uWhite = accounts.getUser(g.white);
    if (uWhite) {
        uWhite->gameId = -1;
        if (uWhite->online) {
            clients[uWhite->fd].gameId = -1;
            dprintf(uWhite->fd, msg.c_str());
        }
    }

    // Notify and clean up observers
    for (int ofd : g.observers) {
        dprintf(ofd, msg.c_str());

        clients[ofd].observeId = -1;
        if (clients[ofd].user) clients[ofd].user->observeId = -1;
        sendPrompt(ofd);
    }

    games.erase(gameId);
}
