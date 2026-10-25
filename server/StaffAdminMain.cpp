// Command-line staff grant/revoke tool. Staff membership is deliberately not
// editable from the staff console in v1: a staff member cannot self-demote.
//
// Usage:
//   staff_admin --db-dir <dir> grant <username|account_id> <role>
//   staff_admin --db-dir <dir> revoke <username|account_id>
//   staff_admin --db-dir <dir> list
//   staff_admin --db-dir <dir> roles

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "Database.hpp"
#include "init_db.hpp"
#include "staff/StaffAuth.hpp"
#include "staff/StaffStore.hpp"

namespace {

std::optional<int64_t> resolve_target(const std::string& value) {
    if (value.empty()) return std::nullopt;
    const bool numeric = std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
    if (numeric) {
        int64_t id = std::stoll(value);
        auto& db = Database::getInstance().gameDB();
        bool exists = false;
        db << "SELECT 1 FROM users WHERE id = ? LIMIT 1;" << id >> [&](int) { exists = true; };
        if (!exists) return std::nullopt;
        return id;
    }
    return staff::account_id_for_username(value);
}

std::string operator_label() {
    if (const char* user = std::getenv("USER")) return std::string("cli:") + user;
    return "cli";
}

void journal_cli(const std::string& action_type, int64_t account_id, const std::string& role,
                 bool grant) {
    staff::staff_action_journal_entry entry;
    entry.actor_type = "cli";
    entry.authority = "cli";
    entry.action_type = action_type;
    entry.target_type = "account";
    entry.target_id = std::to_string(account_id);
    entry.reason = "command-line staff administration";
    entry.before_state = {{"staff", !grant}, {"role", role}};
    entry.after_state = {{"staff", grant}, {"role", role}};
    entry.status = "applied";
    entry.applied_at = staff::now_seconds();
    entry.undo.reversible = false;
    std::string error;
    const std::string id = staff::StaffStore::instance().append_action(entry, error);
    if (id.empty()) {
        std::cerr << "warning: could not journal staff change: " << error << std::endl;
    }
}

void print_usage() {
    std::cout << "Usage: staff_admin [--db-dir PATH] <command>\n\n"
              << "Commands:\n"
              << "  grant <username|account_id> <role>\n"
              << "  revoke <username|account_id>\n"
              << "  list\n"
              << "  roles\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string db_dir = ".";
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--db-dir") == 0 && i + 1 < argc) {
            db_dir = argv[++i];
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage();
            return 0;
        } else {
            args.emplace_back(argv[i]);
        }
    }
    if (args.empty()) {
        print_usage();
        return 1;
    }

    try {
        Database::getInstance().init(db_dir + "/game.db", db_dir + "/chat.db",
                                     db_dir + "/staff.db");
        initializeAllDatabases(Database::getInstance().gameDB(),
                               Database::getInstance().chatDB(),
                               Database::getInstance().staffDB());
    } catch (const std::exception& e) {
        std::cerr << "failed to open databases: " << e.what() << std::endl;
        return 1;
    }

    const std::string command = args[0];
    if (command == "roles") {
        for (const auto& role : staff::StaffStore::instance().list_roles()) {
            std::cout << role.id << "\t" << role.name << "\t" << role.description << "\n";
            for (const auto& capability : role.capabilities) {
                std::cout << "  - " << capability << "\n";
            }
        }
        return 0;
    }
    if (command == "list") {
        for (const auto& account : staff::StaffStore::instance().list_staff()) {
            std::cout << account.account_id << "\t" << account.username << "\t";
            for (size_t i = 0; i < account.roles.size(); ++i) {
                if (i > 0) std::cout << ",";
                std::cout << account.roles[i];
            }
            if (account.revoked_at > 0) std::cout << "\t(revoked)";
            std::cout << "\n";
        }
        return 0;
    }
    if (command == "grant") {
        if (args.size() < 3) {
            print_usage();
            return 1;
        }
        const auto account_id = resolve_target(args[1]);
        if (!account_id) {
            std::cerr << "unknown account: " << args[1] << std::endl;
            return 1;
        }
        std::string error;
        if (!staff::StaffStore::instance().grant_staff(*account_id, args[2], operator_label(),
                                                       error)) {
            std::cerr << "grant failed: " << error << std::endl;
            return 1;
        }
        journal_cli("system.staff_grant", *account_id, args[2], true);
        std::cout << "granted staff role '" << args[2] << "' to account " << *account_id << "\n";
        return 0;
    }
    if (command == "revoke") {
        if (args.size() < 2) {
            print_usage();
            return 1;
        }
        const auto account_id = resolve_target(args[1]);
        if (!account_id) {
            std::cerr << "unknown account: " << args[1] << std::endl;
            return 1;
        }
        std::string error;
        if (!staff::StaffStore::instance().revoke_staff(*account_id, operator_label(), error)) {
            std::cerr << "revoke failed: " << error << std::endl;
            return 1;
        }
        journal_cli("system.staff_revoke", *account_id, std::string(), false);
        std::cout << "revoked staff from account " << *account_id << "\n";
        return 0;
    }

    print_usage();
    return 1;
}
