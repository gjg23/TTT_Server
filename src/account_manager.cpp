// account_manager.cpp
#include "account_manager.h"
#include <cstdio>
#include <sstream>

AccountManager::AccountManager(const std::string& dataFile) : dataFile(dataFile) {
    load();

    // Make guest user if none
    if (!userExists("guest")) {
        User u;
        u.name = "guest";
        u.password = "";
        users["guest"] = u;
    }
}

bool AccountManager::registerUser(const std::string& name, const std::string& pwd) {
    if (users.count(name)) return false; // exists
    User u;
    u.name = name;
    u.password = pwd;
    users[name] = u;
    save();
    return true;
}

User* AccountManager::login(const std::string& name, const std::string& pwd) {
    if (!users.count(name)) return nullptr; // no user
    User& u = users[name];
    if (u.password != pwd) return nullptr;  // wrong password
    if (u.online) return nullptr;  // already logged in elsewhere
    u.online = true;
    return &u;
}

void AccountManager::logout(const std::string& name) {
    if (users.count(name)) users[name].online = false;
}

User* AccountManager::getUser(const std::string& name) {
    if (!users.count(name)) return nullptr;
    return &users[name];
}

bool AccountManager::userExists(const std::string& name) const {
    return users.count(name) > 0;
}

std::string AccountManager::listOnline() const {
    std::string result = "Online users:\n";
    for (const auto& kv : users) {
        if (kv.second.online) result += "   " + kv.first + "\n";
    }
    return result;
}

void AccountManager::save() const {
    FILE* f = fopen(dataFile.c_str(), "w");
    if (!f) { perror("fopen"); return; }

    for (const auto& kv : users) {
        const User& u = kv.second;
        // User data
        fprintf(f, "USER %s %s %d %d %.2f %d\n", u.name.c_str(), u.password.c_str(), u.wins, u.losses, u.rating, (int)u.quiet);
        // User info
        fprintf(f, "INFO %s\n", u.info.c_str());
        // User blocking
        for (const auto& b : u.blocked) fprintf(f, "BLOCK %s\n", b.c_str());
        // User mailbox
        for (const auto& m : u.mailbox) {
            fprintf(f, "MAIL %d %s\n", (int)m.read, m.from.c_str());
            fprintf(f, "TITLE %s\n", m.title.c_str());
            fprintf(f, "DATE %s\n", m.date.c_str());
            fprintf(f, "%s\nENDMAIL\n", m.body.c_str());
        }
    }

    fclose(f);
}

void AccountManager::load() {
    FILE* f = fopen(dataFile.c_str(), "r");
    if (!f) return; // Save will make file

    char line[2048];
    User* current = nullptr;
    Mail currentMail;
    bool inMail = false;
    std::string mailBody;

    while(fgets(line, sizeof(line), f)) {
        std::string s(line);                                // turn line into string
        if (!s.empty() && s.back() == '\n') s.pop_back();   // remove \n
        std::istringstream ss(s);                           // make a stream
        std::string tag; ss >> tag;                         // read first word
        
        if (tag == "USER") {
            User u;
            int q;
            ss >> u.name >> u.password >> u.wins >> u.losses >> u.rating >> q;
            u.quiet = (bool)q;
            users[u.name] = u;
            current = &users[u.name];
        } else if (tag == "INFO" && current) {
            std::getline(ss, current->info);
            if (!current->info.empty() && current->info[0] == ' ') {
                current->info = current->info.substr(1);    // remove leading space
            }
        } else if (tag == "BLOCK" && current) {
            std::string b; ss >> b;
            current->blocked.push_back(b);
        } else if (tag == "MAIL" && current) {
            inMail = true;
            mailBody = "";
            int r;
            ss >> r >> currentMail.from >> currentMail.date;
            currentMail.read = (bool)r;
            currentMail.title = "";
        } else if (tag == "TITLE" && inMail) {
            std::getline(ss, currentMail.title);
            if (!currentMail.title.empty() && currentMail.title[0] == ' ')
                currentMail.title = currentMail.title.substr(1);
        } else if (tag == "DATE" && inMail) {
            std::getline(ss, currentMail.date);
            if (!currentMail.date.empty() && currentMail.date[0] == ' ')
                currentMail.date = currentMail.date.substr(1);
        } else if (tag == "ENDMAIL" && current && inMail) {
            if (!mailBody.empty() && mailBody.back() == '\n')
                mailBody.pop_back();   // strip trailing newline
            currentMail.body = mailBody;
            current->mailbox.push_back(currentMail);
            inMail = false;
        } else if (inMail) {
            mailBody += s + "\n";
        }
    }
    fclose(f);
}


