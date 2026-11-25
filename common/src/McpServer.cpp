// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "McpServer.h"

#include "Log.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace Bots {

using Json = nlohmann::json;

namespace {

constexpr int ParseError = -32700;
constexpr int InvalidRequest = -32600;
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams = -32602;

bool identifier(const std::string& key)
{
    return !key.empty() && std::all_of(key.begin(), key.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '_';
    });
}

} // namespace

const std::vector<std::string>& McpServer::protocolVersions()
{
    // Not 2026-07-28: Claude Code registers no channel on it.
    static const std::vector<std::string> versions = {"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
    return versions;
}

McpServer::McpServer(Options options, std::istream& in, std::ostream& out)
    : m_options(std::move(options))
    , m_in(in)
    , m_out(out)
{
}

McpServer::~McpServer()
{
    m_stopping = true;
    joinCalls();
}

void McpServer::addTool(Tool tool)
{
    m_order.push_back(tool.name);
    m_tools[tool.name] = std::move(tool);
}

void McpServer::write(const Json& message)
{
    const std::string line = message.dump(-1, ' ', false, Json::error_handler_t::replace) + "\n";
    const std::lock_guard lock(m_writeMutex);
    m_out << line;
    m_out.flush();
}

void McpServer::respond(const Json& id, const Json& result)
{
    write({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

void McpServer::fail(const Json& id, int code, const std::string& message)
{
    write({{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", message}}}});
}

void McpServer::notify(const std::string& method, const Json& params)
{
    write({{"jsonrpc", "2.0"}, {"method", method}, {"params", params}});
}

void McpServer::channel(const std::string& content, const std::map<std::string, std::string>& meta)
{
    Json attributes = Json::object();
    for (const auto& [key, value] : meta) {
        if (identifier(key) && !value.empty())
            attributes[key] = value;
    }
    notify("notifications/claude/channel", {{"content", content}, {"meta", attributes}});
}

void McpServer::run()
{
    std::string line;
    while (std::getline(m_in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.empty())
            handleLine(line);
    }
    m_stopping = true;
    joinCalls();
}

void McpServer::joinCalls()
{
    std::vector<Call> calls;
    {
        const std::lock_guard lock(m_callsMutex);
        calls.swap(m_calls);
    }
    for (auto& call : calls) {
        if (call.thread.joinable())
            call.thread.join();
    }
}

void McpServer::handleLine(const std::string& line)
{
    const Json message = Json::parse(line, nullptr, false);
    if (message.is_discarded() || !message.is_object()) {
        fail(nullptr, ParseError, "not JSON");
        return;
    }
    const std::string method = message.value("method", "");
    const bool request = message.contains("id");
    const Json id = request ? message["id"] : Json();
    const Json params = message.contains("params") ? message["params"] : Json::object();

    if (method == "initialize") {
        const std::string client =
            params.contains("clientInfo") ? params["clientInfo"].value("name", std::string()) : std::string();
        if (!m_options.clients.empty() && !m_options.clients.contains(client)) {
            Log::info(m_options.name, "refused the client {}", client.empty() ? "with no name" : client);
            fail(id, InvalidRequest,
                 std::format("{} serves {} alone, not {}", m_options.name, *m_options.clients.begin(),
                             client.empty() ? "a client with no name" : client));
            return;
        }
        const std::string asked = params.value("protocolVersion", "");
        const auto& versions = protocolVersions();
        const std::string version =
            std::find(versions.begin(), versions.end(), asked) != versions.end() ? asked : versions.front();
        Json capabilities = {{"tools", Json::object()}};
        if (m_options.channel) {
            capabilities["experimental"] = {{"claude/channel", Json::object()}};
            if (m_options.permission)
                capabilities["experimental"]["claude/channel/permission"] = Json::object();
        }
        Json result = {{"protocolVersion", version},
                       {"capabilities", capabilities},
                       {"serverInfo", {{"name", m_options.name}, {"version", m_options.version}}}};
        if (!m_options.instructions.empty())
            result["instructions"] = m_options.instructions;
        respond(id, result);
        return;
    }
    if (method == "notifications/initialized") {
        if (!m_initialized) {
            m_initialized = true;
            if (onInitialized)
                onInitialized();
        }
        return;
    }
    if (!request) {
        if (onNotification && !method.empty())
            onNotification(method, params);
        return;
    }
    if (method == "ping") {
        respond(id, Json::object());
        return;
    }
    if (method == "tools/list") {
        Json tools = Json::array();
        for (const auto& name : m_order) {
            const Tool& tool = m_tools[name];
            tools.push_back(
                {{"name", tool.name}, {"description", tool.description}, {"inputSchema", tool.inputSchema}});
        }
        respond(id, {{"tools", tools}});
        return;
    }
    if (method == "tools/call") {
        callTool(id, params);
        return;
    }
    fail(id, MethodNotFound, "no such method: " + method);
}

void McpServer::callTool(const Json& id, const Json& params)
{
    const std::string name = params.value("name", "");
    const auto tool = m_tools.find(name);
    if (tool == m_tools.end()) {
        fail(id, InvalidParams, "no such tool: " + name);
        return;
    }
    const Json arguments =
        params.contains("arguments") && params["arguments"].is_object() ? params["arguments"] : Json::object();
    auto done = std::make_shared<std::atomic<bool>>(false);
    const auto run = [this, id, call = tool->second.call, arguments, name, done] {
        Json result;
        try {
            const Json answer = call(arguments);
            result = {{"content",
                       Json::array({{{"type", "text"},
                                     {"text", answer.is_string() ? answer.get<std::string>() : answer.dump(2)}}})}};
        } catch (const ToolError& error) {
            result = {{"content", Json::array({{{"type", "text"}, {"text", error.what()}}})}, {"isError", true}};
        } catch (const std::exception& error) {
            Log::info(m_options.name, "{}: {}", name, error.what());
            result = {{"content", Json::array({{{"type", "text"}, {"text", std::string("failed: ") + error.what()}}})},
                      {"isError", true}};
        }
        respond(id, result);
        *done = true;
    };
    const std::lock_guard lock(m_callsMutex);
    // The calls that ended are joined here, so a long session keeps none.
    for (auto it = m_calls.begin(); it != m_calls.end();) {
        if (*it->done) {
            it->thread.join();
            it = m_calls.erase(it);
        } else {
            ++it;
        }
    }
    m_calls.push_back({std::thread(run), done});
}

} // namespace Bots
