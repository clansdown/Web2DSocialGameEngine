#include "Database.hpp"
#include "init_db.hpp"
#include "registration/InvitationStore.hpp"

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>

namespace {

void print_usage() {
    std::cout
        << "Usage: invite_admin <command> [options]\n\n"
        << "Commands:\n"
        << "  create --count N [--kind cli|staff]\n"
        << "  list [--status all|unused|used|revoked] [--kind cli|staff|player]\n"
        << "       [--query TEXT] [--limit N] [--offset N]\n"
        << "  revoke <invitation-id>\n\n"
        << "Connection: RAVENEST_INVITES_DB_URL (default dbname=ravenest_invites).\n";
}

std::int64_t now_seconds() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

int command_create(const std::vector<std::string>& args) {
    int count = 1;
    std::string kind = "cli";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--count" && i + 1 < args.size()) {
            count = std::max(1, std::atoi(args[++i].c_str()));
        } else if (args[i] == "--kind" && i + 1 < args.size()) {
            kind = args[++i];
        } else {
            std::cerr << "unknown option: " << args[i] << std::endl;
            return 1;
        }
    }
    if (kind != "cli" && kind != "staff") {
        std::cerr << "kind must be cli or staff" << std::endl;
        return 1;
    }

    Database::getInstance().init_invites_from_env();
    initializeInvitesDB(Database::getInstance().invitesDB());

    for (int i = 0; i < count; ++i) {
        std::string error;
        auto record = InvitationStore::instance().create_code(0, kind, error);
        if (!record) {
            std::cerr << "create failed: " << error << std::endl;
            return 1;
        }
        std::cout << display_invitation_code(record->code) << "\t" << record->id << std::endl;
    }
    return 0;
}

int command_list(const std::vector<std::string>& args) {
    invitation_code_filter filter;
    filter.status = "all";
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--status" && i + 1 < args.size()) {
            filter.status = args[++i];
        } else if (args[i] == "--kind" && i + 1 < args.size()) {
            filter.created_by_kind = args[++i];
        } else if (args[i] == "--query" && i + 1 < args.size()) {
            filter.query = args[++i];
        } else if (args[i] == "--limit" && i + 1 < args.size()) {
            filter.limit = std::atoi(args[++i].c_str());
        } else if (args[i] == "--offset" && i + 1 < args.size()) {
            filter.offset = std::atoi(args[++i].c_str());
        } else {
            std::cerr << "unknown option: " << args[i] << std::endl;
            return 1;
        }
    }

    Database::getInstance().init_invites_from_env();
    initializeInvitesDB(Database::getInstance().invitesDB());
    for (const auto& record : InvitationStore::instance().list_all(filter)) {
        std::cout << record.id << "\t" << display_invitation_code(record.code)
                  << "\t" << record.created_by_kind
                  << "\tcreated=" << record.created_at
                  << "\tused=" << record.used_at
                  << "\trevoked=" << record.revoked_at << std::endl;
    }
    return 0;
}

int command_revoke(const std::vector<std::string>& args) {
    if (args.size() != 1) {
        std::cerr << "revoke requires an invitation id" << std::endl;
        return 1;
    }
    Database::getInstance().init_invites_from_env();
    initializeInvitesDB(Database::getInstance().invitesDB());
    std::string error;
    if (!InvitationStore::instance().revoke_code(args[0], 0, now_seconds(), error)) {
        std::cerr << "revoke failed: " << error << std::endl;
        return 1;
    }
    std::cout << "revoked " << args[0] << std::endl;
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }
    const std::string command = argv[1];
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i) args.emplace_back(argv[i]);

    try {
        if (command == "create") return command_create(args);
        if (command == "list") return command_list(args);
        if (command == "revoke") return command_revoke(args);
    } catch (const std::exception& exception) {
        std::cerr << "invite_admin: " << exception.what() << std::endl;
        return 1;
    }
    print_usage();
    return 1;
}
