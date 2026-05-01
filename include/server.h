// commander.h
#pragma once
#include <map>
#include <string>
#include <vector>
#include <unordered_set>
#include <sys/select.h>

#include "account_manager.h"
#include "game.h"

struct ClientSession {
    std::string recvBuf;
    std::string username;
    bool        isGuest = false;
    bool        loggedIn = false;
    int         gameId  = -1;
    int         observeId = -1;
    int         lineCount = 0;

    // User
    User* user = nullptr;

    // login state
    enum LoginState { PROMPT_NAME, PROMPT_PASSWORD, LOGGED_IN }
    loginState = PROMPT_NAME;
    std::string pendingName;  // holds name between prompts

    // Email state
    enum MailState { NONE_MAIL, WRITING }
    mailState = MailState::NONE_MAIL;
    std::string mailTo;
    std::string mailTitle;
    std::string mailBody;

    // reset mail state after send
    void resetMail() {
        mailBody = "";
        mailState = NONE_MAIL;
        mailTitle = "";
        mailTo = "";
    }

    // reset entire state
    void reset () {
        loggedIn    = false;
        isGuest     = false;
        username    = "";
        pendingName = "";
        loginState  = PROMPT_NAME;
        lineCount   = 0;
        gameId      = -1;
        observeId   = -1;
        user        = nullptr;
    }
};

struct PendingInvite {
    std::string color;
    int timeSec = 0;
};

class Server {
public:
    Server(int port, std::string dataFile);
    void run();

private:
    int listenFd, port;
    std::map<int, ClientSession> clients;
    std::unordered_set<int> toRemove;
    AccountManager accounts;

    // Games
    std::map<int, Game> games;
    int nextGameId = 1;
    std::map<std::string, std::map<std::string, PendingInvite>> pendingInvites; // [challened], [challenger]
    void broadcastGame(int gameId);
    void endGame(int gameId, const std::string& winner, const std::string& reason);

    void handleNewConnection();
    void handleClient(int fd);
    int run_command(const std::string& line, int fd);
    void sendPrompt(int fd);

    // Commands
    void cmd_help(int fd);
    void cmd_register(int fd, std::istringstream& ss);
    void cmd_who(int fd);
    void cmd_stats(int fd, std::istringstream& ss);
    void cmd_info(int fd, std::istringstream& ss);
    void cmd_passwd(int fd, std::istringstream& ss);
    void cmd_quiet(int fd, bool setQuiet);
    void cmd_block(int fd, std::istringstream& ss);
    void cmd_unblock(int fd, std::istringstream& ss);
    void cmd_tell(int fd, std::istringstream& ss);
    void cmd_shout(int fd, std::istringstream& ss);
    void cmd_mail(int fd, std::istringstream& ss);
    void cmd_listmail(int fd, std::istringstream& ss);
    void cmd_readmail(int fd, std::istringstream& ss);
    void cmd_deletemail(int fd, std::istringstream& ss);
    void cmd_game(int fd, std::istringstream& ss);
    void cmd_move(int fd, std::string& cmd);
    void cmd_observe(int fd, std::istringstream& ss);
    void cmd_unobserve(int fd);
    void cmd_match(int fd, std::istringstream& ss);
    void cmd_resign(int fd);
    void cmd_refresh(int fd);
    void cmd_kibitz(int fd, std::istringstream& ss);
    void cmd_gameMsg(int fd, std::istringstream& ss);

    // helper for mail date
    std::string getCurrentDateTime();
};