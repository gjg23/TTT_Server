// user.h
#pragma once
#include <string>
#include <vector>

struct Mail {
    std::string from;
    std::string title;
    std::string body;
    bool read; // read like bread not reed. English sucks
    std::string date;
};

class User {
public:
    // Account info
    std::string name;
    std::string password;
    std::string info;
    bool isGuest = false;

    // Stats
    int wins = 0;
    int losses = 0;
    double rating = 1500.0;

    // Preferences
    bool quiet = false;
    std::vector<std::string> blocked;

    // Mailbox
    std::vector<Mail> mailbox;

    // Session state
    bool online = false;
    int fd = -1;
    int gameId = -1;
    int observeId = -1;

    // tools
    bool isBlocking(const std::string& name) const;
    int unreadMail() const;
    // In User class
    Mail* getMail(int id);
    bool mailExists(int id) const;
};