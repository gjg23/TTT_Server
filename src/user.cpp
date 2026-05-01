// user.cpp
#include "user.h"

bool User::isBlocking(const std::string& name) const {
    for (const auto& b : blocked) {
        if (b == name) return true;
    }
    return false;
}

int User::unreadMail() const {
    int count = 0;
    for (const auto& m : mailbox) {
        if (!m.read) count++;
    }
    return count;
}

// In User class
Mail* User::getMail(int index) {
    if (index < 1 || index > (int)mailbox.size()) return nullptr;
    return &mailbox[index - 1];
}

bool User::mailExists(int index) const {
    return index >= 1 && index <= (int)mailbox.size();
}