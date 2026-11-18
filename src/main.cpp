// Single-player entry point: one process, the server on its own thread, connected to the client by a
// LocalConnection. main is the only place that sees both the server and the client (section 4).
#include <charconv>
#include <cstring>
#include <random>
#include <string>
#include <string_view>

#include "BlockRegistry.h"
#include "Connection.h"
#include "DebugCounters.h"
#include "Engine.h"
#include "Jobs.h"
#include "Log.h"
#include "Server.h"

namespace {
    template<class T>
    bool ParseNumber(std::string_view text, T &out) {
        const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
        return result.ec == std::errc{} && result.ptr == text.data() + text.size();
    }

    constexpr const char *USAGE =
            "usage: vrtc [--seed N] [--workers N] [--name NAME]\n"
            "            [--run-seconds S] [--autofly M/S] [--autodig]\n"
            "            [--reach M] [--radius M] [--strength 0-15] [--mode blocks|micro] [--block NAME]";
}

int main(int argc, char **argv) {
    uint64_t seed = std::random_device{}();
    unsigned workers = vrtc::JobSystem::DefaultWorkerCount();
    vrtc::ClientConfig client;
    std::string placeBlock = "stone";
    int enemies = 1;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const std::string_view value = i + 1 < argc ? argv[i + 1] : "";
        bool ok = true;
        if (arg == "--seed") ok = ParseNumber(value, seed), ++i;
        else if (arg == "--workers") ok = ParseNumber(value, workers), ++i;
        else if (arg == "--run-seconds") ok = ParseNumber(value, client.runSeconds), ++i;
        else if (arg == "--autofly") ok = ParseNumber(value, client.autoFly), ++i;
        else if (arg == "--autodig") ok = ParseNumber(value, client.autoDig), ++i;
        else if (arg == "--autoplace") ok = ParseNumber(value, client.autoPlace), ++i;
        else if (arg == "--spawn") {
            ok = i + 3 < argc && ParseNumber(argv[i + 1], client.spawn.x) && ParseNumber(argv[i + 2], client.spawn.y)
                 && ParseNumber(argv[i + 3], client.spawn.z);
            client.hasSpawn = true;
            i += 3;
        }
        else if (arg == "--fly") client.fly = true;
        else if (arg == "--enemies") ok = ParseNumber(value, enemies), ++i;
        else if (arg == "--yaw") ok = ParseNumber(value, client.yaw), ++i;
        else if (arg == "--pitch") ok = ParseNumber(value, client.pitch), ++i;
        else if (arg == "--name") client.playerName = std::string(value), ++i;
        else if (arg == "--reach") ok = ParseNumber(value, client.tool.reach), ++i;
        else if (arg == "--radius") ok = ParseNumber(value, client.tool.radius), ++i;
        else if (arg == "--strength") ok = ParseNumber(value, client.tool.strength), ++i;
        else if (arg == "--block") placeBlock = std::string(value), ++i;
        else if (arg == "--mode") {
            if (value == "blocks") client.tool.mode = vrtc::EditMode::WholeBlocks;
            else if (value == "micro") client.tool.mode = vrtc::EditMode::Micro;
            else ok = false;
            ++i;
        } else {
            vrtc::log::Fatal("{}", USAGE);
        }
        if (!ok) vrtc::log::Fatal("bad value '{}' for {}\n{}", value, arg, USAGE);
    }

    vrtc::BlockRegistry blocks;
    vrtc::RegisterDefaultBlocks(blocks);
    client.tool.block = blocks.Require(placeBlock);

    vrtc::JobSystem jobs(workers);
    vrtc::LocalConnection link;
    vrtc::ServerStats serverStats;

    vrtc::Server server(jobs, blocks, seed, serverStats, enemies);
    server.AddConnection(link.ServerSide());
    server.Start();
    {
        vrtc::Engine engine(jobs, link.ClientSide(), blocks, &serverStats, client);
        engine.Run();
        server.Stop(); // before the client's replica and mesh tables are destroyed
    }
    return 0;
}
