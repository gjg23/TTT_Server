// commander.cpp
#include "server.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <iomanip>

static const int MAX_CLIENTS = 20;
static const int BUFFER_SIZE = 4096;

static const char* MSG_HELP =
    "Commands supported:\n"
    "  who                     # List all online users\n"
    "  stats [name]            # Display user information\n"
    "  game                    # list all current games\n"
    "  observe <game_num>      # Observe a game\n"
    "  unobserve               # Unobserve a game\n"
    "  match <name> <b|w> [t]  # Try to start a game\n"
    "  <A|B|C><1|2|3>          # Make a move in a game\n"
    "  resign                  # Resign a game\n"
    "  refresh                 # Refresh a game\n"
    "  shout <msg>             # shout <msg> to every one online\n"
    "  tell <name> <msg>       # tell user <name> message\n"
    "  kibitz <msg>            # Comment on a game when observing\n"
    "  ' <msg>                 # Comment on a game\n"
    "  quiet                   # Quiet mode, no broadcast messages\n"
    "  nonquiet                # Non-quiet mode\n"
    "  block <id>              # No more communication from <id>\n"
    "  unblock <id>            # Allow communication from <id>\n"
    "  listmail                # List the header of the mails\n"
    "  readmail <msg_num>      # Read the particular mail\n"
    "  deletemail <msg_num>    # Delete the particular mail\n"
    "  mail <id> <title>       # Send id a mail\n"
    "  info <msg>              # change your information to <msg>\n"
    "  passwd <new>            # change password\n"
    "  exit                    # quit the system\n"
    "  quit                    # quit the system\n"
    "  help                    # print this message\n"
    "  ?                       # print this message\n";

static const char* MSG_ACCESS =             
    "               -=-= AUTHORIZED USERS ONLY =-=-\n"
    "You are attempting to log into online tic-tac-toe Server.\n"
    "Please be advised by continuing that you agree to the terms of the\n"
    "Computer Access and Usage Policy of online tic-tac-toe Server.\n";

Server::Server(int port, std::string dataFile) : port(port), accounts(dataFile) {
    /* ===== Create listening port ========================================= */
    listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd < 0) { perror("socket"); exit(EXIT_FAILURE); }

    // Allow port reuse after server restart
    int opt = 1;
    setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));


    /* ===== Bind to port ================================================== */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listenFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        exit(EXIT_FAILURE);
    }


    /* ===== Listen ======================================================== */
    if (listen(listenFd, 10) < 0) { perror("listen"); exit(EXIT_FAILURE); }
    printf("Server listening on port %d\n", port);
}

void Server::run() {
    time_t lastSave = time(nullptr);
    while (true) {
        fd_set readFds;
        FD_ZERO(&readFds);

        // Match listening socket
        FD_SET(listenFd, &readFds);
        int maxFd = listenFd;

        // Watch connected clients
        for (auto& kv : clients) {
            FD_SET(kv.first, &readFds);
            if (kv.first > maxFd) {
                maxFd = kv.first;
            }
        }

        // periodic save timeout
        struct timeval tv;
        tv.tv_sec = 300;  // 5 minutes
        tv.tv_usec = 0;

        // Block until ready
        if (select(maxFd + 1, &readFds, nullptr, nullptr, &tv) < 0) {
            perror("select");
            break;
        }

        // periodic save every 5 mins
        time_t now = time(nullptr);
        if (now - lastSave >= 300) {
            accounts.save();
            lastSave = now;
        }

        // New connection
        if (FD_ISSET(listenFd, &readFds)) handleNewConnection();

        // Data from existing client
        for (auto& kv : clients) {
            int fd = kv.first;
            if (!FD_ISSET(fd, &readFds)) continue;
            handleClient(fd);
        }
        
        // Remove clients marked for removal
        for (int fd : toRemove) {
            printf("Client disconnecting fd: %d\n", fd);
            const char* msg = "Goodbye!\n";
            write(fd, msg, strlen(msg));
            close(fd);
            clients.erase(fd);
        }
        toRemove.clear();
    }
}

void Server::handleNewConnection() {
    struct sockaddr_in clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    int newFd = accept(listenFd, (struct sockaddr*)&clientAddr, &clientLen);

    if (newFd < 0) {
        perror("accept");
    } else if ((int)clients.size() >= MAX_CLIENTS) {
        std::string msg = "Server full\n";
        write(newFd, msg.c_str(), msg.size());
        close(newFd);
    } else {
        printf("New connection: fd=%d\n", newFd);
        clients[newFd] = ClientSession();
        write(newFd, MSG_ACCESS, strlen(MSG_ACCESS));
        write(newFd, "Login (guest): ", 15);
    }
}

void Server::handleClient(int fd) {
    if (toRemove.count(fd)) return;
    char buf[BUFFER_SIZE];
    int n = read(fd, buf, sizeof(buf));

    // Client side disconnect / error
    if (n <= 0) {
        printf("Client side disconnect.\n");
        toRemove.insert(fd);
        ClientSession& session = clients[fd];
        if (session.loggedIn) accounts.logout(session.username);

        // end game
        if (session.gameId != -1) {
            auto it = games.find(session.gameId);
            if (it != games.end()) 
                endGame(session.gameId, it->second.opponent(session.username), "wins by disconnection");
        }
        // stop observing
        if (session.observeId != -1) cmd_unobserve(fd);
        return;
    }

    // append to this clients buffer
    std::string& recvBuf = clients[fd].recvBuf;
    recvBuf.append(buf, n);

    // process all complete lines with \r\n
    size_t pos;
    while ((pos = recvBuf.find('\n')) != std::string::npos) {
        std::string line = recvBuf.substr(0, pos);
        recvBuf.erase(0, pos + 1);

        // strip \r
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (run_command(line, fd) == 1) return;    // client quit if 1
    }
}

int Server::run_command(const std::string& line, int fd) {
    ClientSession& session = clients[fd];
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;

    // Always allow
    if (cmd == "help" || cmd == "?") { 
        write(fd, MSG_HELP, strlen(MSG_HELP));
        sendPrompt(fd);
        return 0; 
    }
    if (cmd == "quit" || cmd == "exit") {
        printf("Client disconnecting fd: %d\n", fd);
        toRemove.insert(fd);
        if (session.loggedIn) accounts.logout(session.username);

        // end game
        if (session.gameId != -1) {
            auto it = games.find(session.gameId);
            if (it != games.end()) 
                endGame(session.gameId, it->second.opponent(session.username), "wins by disconnection");
        }
        // stop observing
        if (session.observeId != -1) cmd_unobserve(fd);
        return 1; 
    }

    // ===== Login Prompt ==========================================================
    if (session.loginState == ClientSession::PROMPT_NAME) {
        if (cmd == "guest") {
            session.isGuest = true;
            session.loggedIn = true;
            session.username = "guest";
            session.loginState = ClientSession::LOGGED_IN;
            accounts.login("guest", "");
            write(fd, "Logged in as guest.\n", 20);
            sendPrompt(fd);
        } else if (accounts.userExists(cmd)) {
            session.pendingName = cmd;
            session.loginState = ClientSession::PROMPT_PASSWORD;
            write(fd, "password: ", 10);
        } else {
            session.reset();
            write(fd, "User not found.\nLogin (guest): ", 32);
        }
        return 0;
    }

    // ===== Password Prompt =======================================================
    if (session.loginState == ClientSession::PROMPT_PASSWORD) {
        // check if already online before trying login
        User* existing = accounts.getUser(session.pendingName);
        if (existing && existing->online) {
            session.reset();
            write(fd, "User already logged in.\nLogin (guest): ", 39);
            return 0;
        }

        User* u = accounts.login(session.pendingName, line);
        if (u) {
            session.username = session.pendingName;
            session.loggedIn = true;
            session.loginState = ClientSession::LOGGED_IN;
            session.user = u;
            u->fd = fd;

            // unread mail notif
            char buf[64];
            int unread = u->unreadMail();
            if (unread > 0) {
                snprintf(buf, sizeof(buf), "You have %d unread mail.\n", unread);
            } else {
                snprintf(buf, sizeof(buf), "You have no unread mail.\n");
            }
            write(fd, buf, strlen(buf));
            sendPrompt(fd);
        } else {
            session.loginState  = ClientSession::PROMPT_NAME;
            session.pendingName = "";
            write(fd, "Wrong password.\nlogin (guest): ", 31);
        }
        return 0;
    }

    // Ensure user is logged in past this point (should be logged in, if not there is a bug)
    if (session.loginState != ClientSession::LOGGED_IN) return 0;

    // ===== Writing Mail =========================================================
    if (session.mailState == ClientSession::WRITING) {
        // Send mail
        if (line == ".") {
            User* mailToUser = accounts.getUser(session.mailTo);
            Mail mail;
            mail.body = session.mailBody;
            mail.from = session.username;
            mail.title = session.mailTitle;
            mail.read = false;
            mail.date = getCurrentDateTime();

            mailToUser->mailbox.push_back(mail);
            accounts.save();
            
            // Notify if online
            if (mailToUser->online) {
                dprintf(mailToUser->fd, "New mail from: %s.\n", session.username.c_str());
                sendPrompt(mailToUser->fd);
            }

            dprintf(fd, "Mail sent.\n");
            sendPrompt(fd);
            session.resetMail();
        } else {
            // add line to body
            session.mailBody += line + "\n";
        }
        return 0;
    }

    // ===== Commands =============================================================
    // Guest & users can logout
    if (cmd == "logout" && session.loggedIn) {
        accounts.logout(session.username);
        session.reset();
        write(fd, "Login (guest): ", 15);
        return 0;
    }

    // Guests can register
    if (session.isGuest) {
        if (cmd == "register") cmd_register(fd, ss);
        else write(fd, "Guests can only use: register, logout, help, quit\n", 50);
        sendPrompt(fd);
        return 0;
    }

    // Other user commands
    bool sendprompt = true;
    if      (cmd == "who")          cmd_who(fd);
    else if (cmd == "stats")        cmd_stats(fd, ss);
    else if (cmd == "info")         cmd_info(fd, ss);
    else if (cmd == "passwd")       cmd_passwd(fd, ss);
    else if (cmd == "register")     write(fd, "Only guests can use register.\n", 30);
    else if (cmd == "passwd")       cmd_passwd(fd, ss);
    else if (cmd == "quiet")        cmd_quiet(fd, true);
    else if (cmd == "nonquiet")     cmd_quiet(fd, false);
    else if (cmd == "block")        cmd_block(fd, ss);
    else if (cmd == "unblock")      cmd_unblock(fd, ss);
    else if (cmd == "tell")         cmd_tell(fd, ss);
    else if (cmd == "shout")        cmd_shout(fd, ss);
    else if (cmd == "mail")       { cmd_mail(fd, ss); sendprompt = false; }
    else if (cmd == "listmail")     cmd_listmail(fd, ss);
    else if (cmd == "deletemail")   cmd_deletemail(fd, ss);
    else if (cmd == "readmail")     cmd_readmail(fd, ss);
    else if (cmd == "game")         cmd_game(fd, ss);
    else if (cmd == "observe")      cmd_observe(fd, ss);
    else if (cmd == "unobserve")    cmd_unobserve(fd);
    else if (cmd == "match")        cmd_match(fd, ss);
    else if (cmd == "resign")       cmd_resign(fd);
    else if (cmd == "refresh")      cmd_refresh(fd);
    else if (cmd == "kibitz")       cmd_kibitz(fd, ss);
    // Active game only
    else if (session.gameId != -1) {
        if (cmd == "'")                                                     cmd_gameMsg(fd, ss);
        else if (cmd.size() == 2 && isupper(cmd[0]) && isdigit(cmd[1]))     cmd_move(fd, cmd);
        else {
            std::string response = "Unknown command\n";
            write(fd, response.c_str(), response.size());
        }
    }
    else {
        std::string response = "Unknown command\n";
        write(fd, response.c_str(), response.size());
    }
    if (sendprompt) sendPrompt(fd);
    return 0;
}

void Server::sendPrompt(int fd) {
    ClientSession& session = clients[fd];
    char buf[64];
    snprintf(buf, sizeof(buf), "<%s: %d> ", session.username.c_str(), session.lineCount++);
    write(fd, buf, strlen(buf));
}
