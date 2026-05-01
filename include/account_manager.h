// account_manager.h
#pragma once
#include "user.h"
#include <map>
#include <string>

class AccountManager {
public:
    AccountManager(const std::string& dataFile);

    // Register
    bool registerUser(const std::string& name, const std::string& pwd);
    
    // Login logout
    User* login(const std::string& name, const std::string& pwd);
    void logout(const std::string& name);

    // Get user and search user
    User* getUser(const std::string& name);
    bool userExists(const std::string& name) const;
    std::string listOnline() const;

    // Save and load users
    void save() const;
    void load();

private:
    std::string dataFile;
    std::map<std::string, User> users;
};