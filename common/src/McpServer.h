// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <istream>
#include <map>
#include <memory>
#include <mutex>
#include <ostream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

namespace Bots {

/// A tool's refusal or failure, said to the session as the tool's error.
struct ToolError : std::runtime_error
{
    using std::runtime_error::runtime_error;
};

/// An MCP server on stdio for one client (Claude Code), newline-delimited
/// JSON-RPC: the initialize handshake, tools, and the notifications Claude
/// Code takes from a channel (notifications/claude/channel). Each tool call
/// runs on a thread of its own, so a tool that waits never holds the others.
class McpServer
{
public:
    struct Tool
    {
        std::string name;
        std::string description;
        nlohmann::json inputSchema;
        /// The tool's answer, given to the session as text (JSON dumped when
        /// it is no string); a ToolError is its error.
        std::function<nlohmann::json(const nlohmann::json& arguments)> call;
    };

    struct Options
    {
        std::string name;
        std::string version;
        std::string instructions;
        bool channel = false; ///< declares experimental claude/channel
        /// clientInfo names it serves; empty: any. Another is refused at initialize.
        std::set<std::string> clients;
        /// declares experimental claude/channel/permission: Claude Code relays
        /// its permission prompts (only for a channel that knows its sender)
        bool permission = false;
    };

    /// The protocol revisions it speaks, newest first.
    static const std::vector<std::string>& protocolVersions();

    McpServer(Options options, std::istream& in, std::ostream& out);
    ~McpServer();

    void addTool(Tool tool);
    /// Called once, on the client's notifications/initialized.
    std::function<void()> onInitialized;
    /// Each other notification of the client (method, params), on the reading thread.
    std::function<void(const std::string& method, const nlohmann::json& params)> onNotification;

    /// Serves until the input ends, then waits for the tool calls still
    /// running (each sees stopping()).
    void run();
    bool stopping() const
    {
        return m_stopping;
    }

    /// A notification to the client, from any thread.
    void notify(const std::string& method, const nlohmann::json& params);
    /// A channel event: @p meta's keys must be identifiers (Claude Code drops
    /// any other); the values strings.
    void channel(const std::string& content, const std::map<std::string, std::string>& meta);

    /// One message of the client, for the tests: what run() does with a line.
    void handleLine(const std::string& line);
    /// Waits for the tool calls running now (the tests).
    void joinCalls();

private:
    void respond(const nlohmann::json& id, const nlohmann::json& result);
    void fail(const nlohmann::json& id, int code, const std::string& message);
    void write(const nlohmann::json& message);
    void callTool(const nlohmann::json& id, const nlohmann::json& params);

    Options m_options;
    std::istream& m_in;
    std::ostream& m_out;
    std::mutex m_writeMutex;
    std::map<std::string, Tool> m_tools;
    std::vector<std::string> m_order;
    bool m_initialized = false;
    std::atomic<bool> m_stopping{false};
    struct Call
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    std::mutex m_callsMutex;
    std::vector<Call> m_calls;
};

} // namespace Bots
