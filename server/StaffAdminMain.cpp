// Command-line staff grant/revoke tool. Staff membership is deliberately not
// editable from the staff console in v1: a staff member cannot self-demote.
//
// Usage:
//   staff_admin grant <username|account_id> <role|alias>
//   staff_admin revoke <username|account_id>
//   staff_admin list
//   staff_admin roles [--verbose]
//   staff_admin role <role|alias>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "Database.hpp"
#include "init_db.hpp"
#include "staff/StaffAuth.hpp"
#include "staff/StaffStore.hpp"

namespace {

struct role_preset {
    std::string id;
    std::string alias;
    std::string summary;
};

const std::vector<role_preset>& role_presets() {
    static const std::vector<role_preset> presets = {
        {"superadmin", "superuser", "Full access; use for the owner/superuser."},
        {"customer_service", "staff",
         "Standard staff admin; accounts, support, moderation, invites."},
        {"moderator", "", "Chat, barony, identity, and support-ticket moderation."},
        {"support_agent", "support", "Support queue and unstick tools."},
        {"avatar_moderator", "avatar", "Avatar and identity moderation."},
    };
    return presets;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::string normalize_role_id(const std::string& value) {
    const std::string lowered = lowercase(value);
    for (const auto& preset : role_presets()) {
        if (lowered == preset.id || (!preset.alias.empty() && lowered == preset.alias)) {
            return preset.id;
        }
    }
    return lowered;
}

std::string role_alias(const std::string& role_id) {
    for (const auto& preset : role_presets()) {
        if (preset.id == role_id) return preset.alias;
    }
    return std::string();
}

std::string role_summary(const staff::staff_role& role) {
    for (const auto& preset : role_presets()) {
        if (preset.id == role.id) return preset.summary;
    }
    return role.description.empty() ? "No description." : role.description;
}

int role_rank(const std::string& role_id) {
    const auto& presets = role_presets();
    for (size_t i = 0; i < presets.size(); ++i) {
        if (presets[i].id == role_id) return static_cast<int>(i);
    }
    return static_cast<int>(presets.size());
}

std::vector<staff::staff_role> sorted_roles() {
    std::vector<staff::staff_role> roles = staff::StaffStore::instance().list_roles();
    std::sort(roles.begin(), roles.end(), [](const staff::staff_role& a,
                                              const staff::staff_role& b) {
        const int a_rank = role_rank(a.id);
        const int b_rank = role_rank(b.id);
        if (a_rank != b_rank) return a_rank < b_rank;
        return a.id < b.id;
    });
    return roles;
}

const staff::staff_role* find_role(const std::vector<staff::staff_role>& roles,
                                   const std::string& role_id) {
    for (const auto& role : roles) {
        if (role.id == role_id) return &role;
    }
    return nullptr;
}

std::string join_values(const std::vector<std::string>& values, const std::string& separator) {
    std::string joined;
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) joined += separator;
        joined += values[i];
    }
    return joined;
}

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
              << "Common commands:\n"
              << "  grant <username|account_id> superadmin          # superuser / full access\n"
              << "  grant <username|account_id> customer_service    # standard staff admin\n"
              << "  grant <username|account_id> support_agent       # support queue only\n"
              << "  revoke <username|account_id>\n\n"
              << "Other commands:\n"
              << "  list\n"
              << "  roles\n"
              << "  role <role_id|alias>\n"
              << "  roles --verbose\n\n"
              << "Role aliases: superuser=superadmin, staff=customer_service,\n"
              << "              support=support_agent, avatar=avatar_moderator\n\n"
              << "`grant` replaces the account's current staff role.\n"
              << "Run `staff_admin roles` for the preset summary.\n";
}

void print_role_detail(const staff::staff_role& role) {
    std::cout << role.id << " (" << role.name << ")\n";
    std::cout << "  " << role_summary(role) << "\n";
    const std::string alias = role_alias(role.id);
    if (!alias.empty()) {
        std::cout << "  alias: " << alias << "\n";
    }
    std::cout << "  capabilities (" << role.capabilities.size() << "):\n";
    for (const auto& capability : role.capabilities) {
        std::cout << "    " << capability << "\n";
    }
    std::cout << "  grant: staff_admin grant <username|account_id> " << role.id << "\n";
}

void print_roles_table(const std::vector<staff::staff_role>& roles) {
    std::cout << "Staff role presets\n\n";
    std::cout << "  " << std::left << std::setw(20) << "ROLE"
              << std::setw(20) << "NAME"
              << "SUMMARY\n";
    for (const auto& role : roles) {
        std::string summary = role_summary(role);
        const std::string alias = role_alias(role.id);
        if (!alias.empty()) {
            summary += " (alias: " + alias + ")";
        }
        std::cout << "  " << std::left << std::setw(20) << role.id
                  << std::setw(20) << role.name
                  << summary << "\n";
    }
    std::cout << "\nCommon commands\n"
              << "  staff_admin grant <username|account_id> superadmin\n"
              << "  staff_admin grant <username|account_id> customer_service\n"
              << "  staff_admin grant <username|account_id> support_agent\n"
              << "  staff_admin revoke <username|account_id>\n\n"
              << "`grant` replaces the account's current staff role with the named preset.\n"
              << "Run `staff_admin role <role_id>` to see that preset's capabilities.\n";

    const staff::staff_role* superadmin = find_role(roles, "superadmin");
    const staff::staff_role* customer_service = find_role(roles, "customer_service");
    if (superadmin && customer_service &&
        superadmin->capabilities == customer_service->capabilities) {
        std::cout << "\nNote: superadmin and customer_service currently have the same "
                     "capabilities.\n"
                  << "Use superadmin for the owner and customer_service for day-to-day "
                     "staff.\n";
    }
}

void print_roles_verbose(const std::vector<staff::staff_role>& roles) {
    std::cout << "Staff role presets (verbose)\n";
    for (const auto& role : roles) {
        std::cout << "\n";
        print_role_detail(role);
    }
}

void print_staff_list(const std::vector<staff::staff_account_summary>& accounts) {
    if (accounts.empty()) {
        std::cout << "No staff accounts.\n";
        return;
    }
    std::cout << std::left << std::setw(12) << "ACCOUNT"
              << std::setw(24) << "USERNAME"
              << std::setw(24) << "ROLE"
              << "STATUS\n";
    for (const auto& account : accounts) {
        std::string roles = join_values(account.roles, ", ");
        if (roles.empty()) roles = "-";
        std::cout << std::left << std::setw(12) << account.account_id
                  << std::setw(24) << account.username
                  << std::setw(24) << roles
                  << (account.revoked_at > 0 ? "revoked" : "active") << "\n";
    }
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
        if (args.size() > 2 || (args.size() == 2 && args[1] != "--verbose" &&
                                args[1] != "-v")) {
            print_usage();
            return 1;
        }
        const std::vector<staff::staff_role> roles = sorted_roles();
        if (args.size() == 2) {
            print_roles_verbose(roles);
        } else {
            print_roles_table(roles);
        }
        return 0;
    }
    if (command == "role") {
        if (args.size() != 2) {
            print_usage();
            return 1;
        }
        const std::vector<staff::staff_role> roles = sorted_roles();
        const staff::staff_role* role = find_role(roles, normalize_role_id(args[1]));
        if (!role) {
            std::cerr << "unknown role preset: " << args[1] << "\n"
                      << "Run `staff_admin roles` to list presets." << std::endl;
            return 1;
        }
        print_role_detail(*role);
        return 0;
    }
    if (command == "list") {
        print_staff_list(staff::StaffStore::instance().list_staff());
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
        const std::vector<staff::staff_role> roles = sorted_roles();
        const staff::staff_role* role = find_role(roles, normalize_role_id(args[2]));
        if (!role) {
            std::cerr << "unknown role preset: " << args[2] << "\n"
                      << "Run `staff_admin roles` to list presets." << std::endl;
            return 1;
        }
        std::string error;
        if (!staff::StaffStore::instance().grant_staff(*account_id, role->id,
                                                       operator_label(), error)) {
            std::cerr << "grant failed: " << error << std::endl;
            return 1;
        }
        journal_cli("system.staff_grant", *account_id, role->id, true);
        std::cout << "Granted " << role->id << " (" << role->name << ") to account "
                  << *account_id << ".\n"
                  << "This replaced any previous staff role.\n";
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
        std::cout << "Revoked staff access from account " << *account_id << ".\n";
        return 0;
    }

    print_usage();
    return 1;
}
