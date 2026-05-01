// server_cmds.cpp
#include "server.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <sstream>
#include <algorithm>
#include <ctime>
#include <iomanip>

// ===== Account management ==================================================
void Server::cmd_register(int fd, std::istringstream& ss) {
    std::string username; ss >> username;
    if (accounts.userExists(username)) {
        dprintf(fd, "User exists: %s\n", username.c_str());
        return;
    }
    std::string password; ss >> password;
    if (password.size() < 4) {
        dprintf(fd, "Password length must be > 3.\n");
        return;
    }
    accounts.registerUser(username, password);
}

void Server::cmd_passwd(int fd, std::istringstream& ss) {
    User* u = clients[fd].user;
    std::string password; ss >> password;
    if (password.size() < 4) {
        dprintf(fd, "Password length must be > 3.\n");
        return;
    }
    u->password = password;
    accounts.save();
    dprintf(fd, "Password changed.\n");
}


// ===== Stats and info ======================================================
void Server::cmd_who(int fd) {
    std::string msg;
    msg = "Online users: ";
    for (auto& kv : clients) {
        if (kv.second.loggedIn == true)
            msg += kv.second.username + ", ";
    }
    if (msg == "Online users: ") msg += "<none>";
    else msg.resize(msg.size() - 2);
    msg += ".\n";
    write(fd, msg.c_str(), msg.size());
}

void Server::cmd_stats(int fd, std::istringstream& ss) {
    std::string name; ss >> name;
    if (!accounts.userExists(name)) {
        dprintf(fd, "User doesn't exist: %s.\n", name.c_str());
        return;
    }

    // info
    User* u = accounts.getUser(name);
    int wins = u->wins;
    int losses = u->losses;
    double rating = u->rating;
    std::string info = u->info;
    
    // quiet
    std::string quiet;
    if (u->quiet) {
        quiet = "yes";
    } else quiet = "no";

    // blocking
    std::string blocking = "Blocked users: ";
    if (u->blocked.empty()) {
        blocking += "<none>";
    } else {
        for (auto& b : u->blocked) {
            blocking += b + ", ";
        }
        blocking.resize(blocking.size() - 2); // remove trailing ", "
    }

    // online
    std::string online;
    if (u->online) {
        online = name + " is currently online.\n";
    } else online = name + " is currently offline.\n";

    dprintf(fd, "\n%s's stats\n Wins: %d\n Losses: %d\n Rating: %.2f\n Info: %s\n Quiet: %s\n Blocked users: %s\n %s\n",
        name.c_str(), wins, losses, rating, info.c_str(), quiet.c_str(), blocking.c_str(), online.c_str());
}

void Server::cmd_info(int fd, std::istringstream& ss) {
    User* u = clients[fd].user;
    std::string info;
    std::getline(ss, info);

    // remove leading space
    if (!info.empty() && info[0] == ' ') info.erase(0, 1);

    u->info = info;
    accounts.save();
    dprintf(fd, "Info changed.\n");
}


// ===== Mute and block ======================================================
void Server::cmd_quiet(int fd, bool setQuiet) {
    User* u = clients[fd].user;
    u->quiet = setQuiet;
    accounts.save();
    if (setQuiet) dprintf(fd, "Set to quiet.\n");
    else dprintf(fd, "Set to nonquiet.\n");
}

void Server::cmd_block(int fd, std::istringstream& ss) {
    User* u = clients[fd].user;
    std::string blockUser; ss >> blockUser;
    if (!accounts.userExists(blockUser)) {
        dprintf(fd, "User doesn't exist: %s.\n", blockUser.c_str());
        return;
    }
    u->blocked.push_back(blockUser);
    accounts.save();
    dprintf(fd, "Blocked %s.\n", blockUser.c_str());
}

void Server::cmd_unblock(int fd, std::istringstream& ss) {
    User* u = clients[fd].user;
    std::string blockUser; ss >> blockUser;
    if (!accounts.userExists(blockUser)) {
        dprintf(fd, "User doesn't exist: %s.\n", blockUser.c_str());
        return;
    }
    auto& v = u->blocked;
    v.erase(std::remove(v.begin(), v.end(), blockUser), v.end());
    accounts.save();
    dprintf(fd, "%s unblocked.\n", blockUser.c_str());
}


// ===== Messages ============================================================
void Server::cmd_tell(int fd, std::istringstream& ss) {
    std::string toName; ss >> toName;
    if (!accounts.userExists(toName)) {
        dprintf(fd, "User doesn't exist: %s.\n", toName.c_str());
        return;
    }
    User* toUser = accounts.getUser(toName);
    if (!toUser->online) {
        dprintf(fd, "%s is offline.\n", toName.c_str());
        return;
    }

    std::string thisName = clients[fd].username;
    if (toUser->isBlocking(thisName)) {
        dprintf(fd, "You are blocked by: %s.\n", toName.c_str());
        return;
    }
    std::string msg;
    getline(ss, msg);
    dprintf(toUser->fd, "<From %s>%s\n", thisName.c_str(), msg.c_str());
    sendPrompt(toUser->fd);
    dprintf(fd, "Message sent.\n");
}

void Server::cmd_shout(int fd, std::istringstream& ss) {
    std::string thisName = clients[fd].username;
    std::string msg;
    getline(ss, msg);
    
    for (auto& kv : clients) {
        std::string toName = kv.second.username;
        if (toName == thisName) continue;   // Don't send back to same user
        User* toUser = accounts.getUser(toName);
        if (toUser->isBlocking(thisName) || toUser->quiet) {
            continue;
        }

        dprintf(toUser->fd, "!Shout! *%s*:%s\n", thisName.c_str(), msg.c_str());
        sendPrompt(toUser->fd);
    }
}


// ===== Mail ================================================================
void Server::cmd_mail(int fd, std::istringstream& ss) {
    ClientSession& session = clients[fd];
    ss >> session.mailTo;
    getline(ss, session.mailTitle);
    // remove leading space
    if (!session.mailTitle.empty() && session.mailTitle[0] == ' ') session.mailTitle.erase(0, 1);

    session.mailBody = "";

    if (session.mailTitle.empty()) {
        dprintf(fd, "Usage: mail <userID> <Title>\n");
        return;
    }
    if (!accounts.userExists(session.mailTo)) {
        dprintf(fd, "User doesn't exist: %s.\n", session.mailTo.c_str());
        return;        
    }
    User* toUser = accounts.getUser(session.mailTo);
    if (toUser->isBlocking(session.username)) {
        dprintf(fd, "You are blocked by: %s.\n", session.mailTo.c_str());
        return;
    }

    session.mailState = ClientSession::WRITING;
    dprintf(fd, "Enter message, end with '.' on a line by itself:\n");
}

void Server::cmd_listmail(int fd, std::istringstream& ss) {
    User* u = clients[fd].user;
    for (int i = 0; i < (int)u->mailbox.size(); i++) {
        Mail& m = u->mailbox[i];
        std::string read;
        if (m.read) read = "Read";
        else read = "Unread";

        std::ostringstream msg;
        msg << std::left
            << std::setw(5) << i + 1
            << std::setw(8) << read
            << std::setw(10) << m.from
            << std::setw(20) << m.title
            << std::setw(20) << m.date
            << "\n";
        write(fd, msg.str().c_str(), msg.str().size());
    }
}

void Server::cmd_readmail(int fd, std::istringstream& ss) {
    int mailId; ss >> mailId;
    User* u = clients[fd].user;
    if (!u->mailExists(mailId)) {
        dprintf(fd, "Mail id %d does not exist.\n", mailId);
        return;
    }
    Mail* m = u->getMail(mailId);
    dprintf(fd, "\nFrom: %s\nTitle: %s\nTime: %s\n\n%s\n", m->from.c_str(), m->title.c_str(), m->date.c_str(), m->body.c_str());
    m->read = true;
    accounts.save();
}

void Server::cmd_deletemail(int fd, std::istringstream& ss) {
    int mailId; ss >> mailId;
    User* u = clients[fd].user;
    if (!u->mailExists(mailId)) {
        dprintf(fd, "Invalid mail number.\n");
        return;
    }
    u->mailbox.erase(u->mailbox.begin() + (mailId - 1));
    accounts.save();
    dprintf(fd, "Mail %d deleted.\n", mailId);
}

// Random ahh helper to make mail date
std::string Server::getCurrentDateTime() {
    std::time_t now = std::time(nullptr);
    std::tm* local = std::localtime(&now);

    std::ostringstream oss;
    oss << std::put_time(local, "%a %b %d %H:%M:%S %Y");
    return oss.str();
}
