// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "Glim.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <format>

using namespace Glim;

namespace {

constexpr Snowflake Me = 999;
constexpr Snowflake Forum = 10;
constexpr Snowflake Post = 5000;
constexpr Snowflake Owner = 100;
constexpr Snowflake Stranger = 200;
constexpr Snowflake StaffMember = 300;
constexpr Snowflake StaffRole = 77;

/// A Discord forum with one post, and a GitHub that takes issues.
struct World
{
    Json thread = {{"id", std::to_string(Post)},           {"type", 11},      {"parent_id", std::to_string(Forum)},
                   {"owner_id", std::to_string(Owner)},    {"guild_id", "1"}, {"name", "Zones jump between screens"},
                   {"applied_tags", Json::array({"t-pz"})}};
    Json forum = {{"id", std::to_string(Forum)},
                  {"type", 15},
                  {"available_tags",
                   Json::array({{{"id", "t-pz"}, {"name", "plasmazones"}},
                                {{"id", "t-ag"}, {"name", "afterglow"}},
                                {{"id", "t-solved"}, {"name", "solved"}}})}};
    std::vector<Json> messages; ///< the post's
    std::vector<Json> sent; ///< Glim's messages
    std::vector<std::pair<std::string, Json>> issues; ///< repo, body
    std::vector<Json> comments;
    std::string issueState = "open";
    std::string stateReason;
    Snowflake nextId = Post;

    Config config;
    std::unique_ptr<Bot> bot;

    explicit World(std::string filesDir = {})
    {
        config.filesDir = filesDir;
        config.filesUrl = filesDir.empty() ? std::string() : std::string("https://issues.example");
        config.token = "t";
        config.forums = {Forum};
        config.tagRepos = {{"plasmazones", "PlasmaZones"}, {"afterglow", "Afterglow"}};
        config.aliases = {{"pz", "PlasmaZones"}};
        config.staffRoles = {StaffRole};
        bot = std::make_unique<Bot>(
            config,
            [this](const HttpRequest& r) {
                return discord(r);
            },
            [this](const HttpRequest& r) {
                return github(r);
            },
            [] {
                return std::string("installation-token");
            });
        bot->handle({"READY", {{"user", {{"id", std::to_string(Me)}, {"username", "Glim"}}}}});
    }

    static HttpResponse json(const Json& body)
    {
        return {200, {}, body.dump(), {}};
    }

    HttpResponse discord(const HttpRequest& r)
    {
        if (r.url.starts_with("https://cdn.discordapp.com/attachments/shot.png"))
            return {200,
                    {},
                    std::string("\x89PNG\r\n\x1a\n\0\0\0\x0dIHDR\1\1\1\1\1\1\1\1\1\1\1\1\1\0\0\0\0"
                                "\0\0\0\x0btEXtAuthor\0Jane\0\0\0\0\0\0\0\0IEND\0\0\0\0",
                                8 + 25 + 23 + 12),
                    {}};
        if (r.url.starts_with("https://cdn.discordapp.com/attachments/run.exe"))
            return {200, {}, "MZ an executable", {}};
        if (r.url.starts_with("https://cdn.discordapp.com/"))
            return {200, {}, "log line 1\nlog line 2 /home/jane/.config", {}};
        const std::string path = r.url.substr(std::string("https://discord.com/api/v10").size());
        if (r.method == "GET" && path == std::format("/channels/{}", Post))
            return json(thread);
        if (r.method == "GET" && path == std::format("/channels/{}", Forum))
            return json(forum);
        if (r.method == "GET" && path.starts_with(std::format("/channels/{}/messages", Post))) {
            const Snowflake after = std::stoull(path.substr(path.find("after=") + 6));
            Json out = Json::array();
            for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
                if (std::stoull((*it)["id"].get<std::string>()) > after)
                    out.push_back(*it);
            }
            return json(out);
        }
        if (r.method == "POST" && path == std::format("/channels/{}/messages", Post)) {
            sent.push_back(Json::parse(r.body));
            return json({{"id", "1"}});
        }
        if (r.method == "PATCH" && path == std::format("/channels/{}", Post)) {
            thread["applied_tags"] = Json::parse(r.body)["applied_tags"];
            return json(thread);
        }
        return {404, {}, "{}", {}};
    }

    HttpResponse github(const HttpRequest& r)
    {
        const std::string path = r.url.substr(std::string("https://api.github.com").size());
        EXPECT_NE(std::find(r.headers.begin(), r.headers.end(), "Authorization: Bearer installation-token"),
                  r.headers.end());
        if (path.starts_with("/orgs/phosphor-works/repos"))
            return json(Json::array({{{"name", "PlasmaZones"}}, {{"name", "Afterglow"}}, {{"name", "Lambent"}}}));
        if (r.method == "POST" && path.ends_with("/issues")) {
            const std::string repo = path.substr(22, path.size() - 22 - 7);
            issues.emplace_back(repo, Json::parse(r.body));
            return {201,
                    {},
                    Json{{"number", 145}, {"html_url", "https://github.com/i/145"}, {"state", "open"}}.dump(),
                    {}};
        }
        if (r.method == "POST" && path.ends_with("/comments")) {
            comments.push_back(Json::parse(r.body));
            return {201, {}, Json{{"html_url", "https://github.com/i/145#c"}}.dump(), {}};
        }
        if (r.method == "GET" && path.ends_with("/issues/145"))
            return json({{"number", 145},
                         {"state", issueState},
                         {"state_reason", stateReason},
                         {"html_url", "https://github.com/i/145"}});
        return {404, {}, "{}", {}};
    }

    Json message(Snowflake author, const std::string& content, std::vector<Snowflake> roles = {}, bool isBot = false)
    {
        Json member = {{"roles", Json::array()}};
        for (const auto role : roles)
            member["roles"].push_back(std::to_string(role));
        Json m = {{"id", std::to_string(++nextId)},
                  {"channel_id", std::to_string(Post)},
                  {"author", {{"id", std::to_string(author)}, {"bot", isBot}}},
                  {"member", member},
                  {"content", content},
                  {"timestamp", "2026-11-18T14:05:31.000000+00:00"},
                  {"attachments", Json::array()}};
        return m;
    }

    /// A message posted in the post, and seen by Glim.
    void say(Snowflake author, const std::string& content, std::vector<Snowflake> roles = {})
    {
        messages.push_back(message(author, content, roles));
        bot->handle({"MESSAGE_CREATE", messages.back()});
    }

    std::string lastReply() const
    {
        if (sent.empty())
            return {};
        const Json& m = sent.back();
        if (m.contains("embeds"))
            return m["embeds"][0].value("title", "") + " | " + m["embeds"][0].value("description", "");
        return m.value("content", "");
    }
};

} // namespace

TEST(Glim, NothingWithoutAMention)
{
    World w;
    w.say(Owner, "my zones jump");
    EXPECT_TRUE(w.sent.empty());
    EXPECT_TRUE(w.issues.empty());
}

TEST(Glim, TheAuthorFilesThePostInItsTagsRepository)
{
    World w;
    w.say(Owner, "My zones jump between screens <@123>");
    w.say(Stranger, "Same with <@100> here");
    w.say(Owner, "<@999>");
    ASSERT_EQ(w.issues.size(), 1u);
    EXPECT_EQ(w.issues[0].first, "PlasmaZones");
    const auto& issue = w.issues[0].second;
    EXPECT_EQ(issue["title"], "Zones jump between screens");
    const std::string body = issue["body"];
    EXPECT_NE(body.find("#### Reporter"), std::string::npos);
    EXPECT_NE(body.find("Same with Reporter here"), std::string::npos);
    EXPECT_EQ(body.find("<@999>"), std::string::npos) << "the bare mention is no message";
    EXPECT_EQ(body.find("100"), std::string::npos) << "no Discord id leaves";
    EXPECT_NE(w.lastReply().find("Filed as PlasmaZones#145"), std::string::npos);
    EXPECT_EQ(w.bot->state().posts.at(Post).number, 145);
}

TEST(Glim, AnotherMemberMayNot)
{
    World w;
    w.say(Stranger, "<@999> file this");
    EXPECT_TRUE(w.issues.empty());
    EXPECT_NE(w.lastReply().find("Only the post's author and staff"), std::string::npos);
}

TEST(Glim, StaffMay)
{
    World w;
    w.say(Owner, "It crashes");
    w.say(StaffMember, "<@999>", {StaffRole});
    EXPECT_EQ(w.issues.size(), 1u);
    EXPECT_NE(std::string(w.issues[0].second["body"]).find("at the request of Member 1"), std::string::npos);
}

TEST(Glim, ANamedRepositoryWins)
{
    World w;
    w.say(Owner, "Afterglow flickers");
    w.say(Owner, "<@!999> afterglow");
    ASSERT_EQ(w.issues.size(), 1u);
    EXPECT_EQ(w.issues[0].first, "Afterglow");
    w.issues.clear();
    World v;
    v.say(Owner, "<@999> nosuchrepo");
    EXPECT_TRUE(v.issues.empty());
    EXPECT_NE(v.lastReply().find("no repository called \"nosuchrepo\""), std::string::npos);
}

TEST(Glim, NoTagAsksForOne)
{
    World w;
    w.thread["applied_tags"] = Json::array();
    w.say(Owner, "<@999>");
    EXPECT_TRUE(w.issues.empty());
    EXPECT_NE(w.lastReply().find("no project tag"), std::string::npos);
}

TEST(Glim, AnUpdateAddsOnlyWhatIsNew)
{
    World w;
    w.say(Owner, "It crashes");
    w.say(Owner, "<@999>");
    w.say(Owner, "<@999>");
    EXPECT_TRUE(w.comments.empty());
    EXPECT_NE(w.lastReply().find("Nothing is new"), std::string::npos);
    w.say(Stranger, "Here is a backtrace");
    w.say(Owner, "<@999> thanks");
    ASSERT_EQ(w.comments.size(), 1u);
    const std::string body = w.comments[0]["body"];
    EXPECT_NE(body.find("Here is a backtrace"), std::string::npos);
    EXPECT_EQ(body.find("It crashes"), std::string::npos);
    EXPECT_NE(body.find("thanks"), std::string::npos) << "a mention with words is a message";
    EXPECT_NE(w.lastReply().find("Updated PlasmaZones#145"), std::string::npos);
}

TEST(Glim, AReplyToGlimIsNoRequest)
{
    World w;
    Json reply = w.message(Owner, "thanks!");
    reply["mentions"] = Json::array({{{"id", std::to_string(Me)}}});
    w.bot->handle({"MESSAGE_CREATE", reply});
    EXPECT_TRUE(w.sent.empty());
}

TEST(Glim, OutsideTheForumItPointsThere)
{
    World w;
    w.thread["parent_id"] = "11";
    w.say(Owner, "<@999>");
    EXPECT_TRUE(w.issues.empty());
    EXPECT_NE(w.lastReply().find("I file posts of <#10>"), std::string::npos);
}

TEST(Glim, AClosedIssueIsToldAndTagged)
{
    World w;
    w.say(Owner, "It crashes");
    w.say(Owner, "<@999>");
    w.sent.clear();
    w.bot->round();
    EXPECT_TRUE(w.sent.empty()) << "still open";
    w.issueState = "closed";
    w.stateReason = "completed";
    w.bot->round();
    EXPECT_NE(w.lastReply().find("PlasmaZones#145 was closed"), std::string::npos);
    EXPECT_NE(std::find(w.thread["applied_tags"].begin(), w.thread["applied_tags"].end(), "t-solved"),
              w.thread["applied_tags"].end());
    w.sent.clear();
    w.bot->round();
    EXPECT_TRUE(w.sent.empty()) << "told once";
    w.issueState = "open";
    w.bot->round();
    EXPECT_NE(w.lastReply().find("opened again"), std::string::npos);
}

TEST(Glim, NotPlannedIsNoSolution)
{
    World w;
    w.say(Owner, "It crashes");
    w.say(Owner, "<@999>");
    w.issueState = "closed";
    w.stateReason = "not_planned";
    w.bot->round();
    EXPECT_NE(w.lastReply().find("not planned"), std::string::npos);
    EXPECT_EQ(std::find(w.thread["applied_tags"].begin(), w.thread["applied_tags"].end(), "t-solved"),
              w.thread["applied_tags"].end());
}

TEST(Glim, TextFilesAreRead)
{
    World w;
    Json m = w.message(Owner, "log attached");
    m["attachments"] = Json::array({{{"filename", "kwin.log"},
                                     {"content_type", "text/plain"},
                                     {"size", 22},
                                     {"url", "https://cdn.discordapp.com/attachments/kwin.log"}}});
    w.messages.push_back(m);
    w.say(Owner, "<@999>");
    ASSERT_EQ(w.issues.size(), 1u);
    EXPECT_NE(std::string(w.issues[0].second["body"]).find("log line 2"), std::string::npos);
}

TEST(Glim, AttachmentsAreKeptWithoutTheirMetadata)
{
    const auto dir = std::filesystem::temp_directory_path() / ("glim-files-" + std::to_string(::getpid()));
    World w(dir.string());
    Json m = w.message(Owner, "screenshot and log");
    m["attachments"] = Json::array({{{"filename", "Screenshot_jane.png"},
                                     {"content_type", "image/png"},
                                     {"size", 67},
                                     {"url", "https://cdn.discordapp.com/attachments/shot.png"}},
                                    {{"filename", "kwin.log"},
                                     {"content_type", "text/plain"},
                                     {"size", 40},
                                     {"url", "https://cdn.discordapp.com/attachments/kwin.log"}},
                                    {{"filename", "run.exe"},
                                     {"content_type", "application/octet-stream"},
                                     {"size", 16},
                                     {"url", "https://cdn.discordapp.com/attachments/run.exe"}}});
    w.messages.push_back(m);
    w.say(Owner, "<@999>");
    ASSERT_EQ(w.issues.size(), 1u);
    const std::string body = w.issues[0].second["body"];
    const std::string id = m["id"];
    EXPECT_NE(body.find(std::format("![Screenshot_jane.png](https://issues.example/{}/{}-1.png)", Post, id)),
              std::string::npos)
        << body;
    EXPECT_NE(body.find(std::format("📄 [The whole file](https://issues.example/{}/{}-2.txt)", Post, id)),
              std::string::npos);
    EXPECT_NE(body.find("~/.config"), std::string::npos) << "a home folder in a log";
    EXPECT_EQ(body.find("jane/.config"), std::string::npos);
    EXPECT_NE(body.find("Attached in the Discord post: `run.exe`"), std::string::npos) << "an unknown kind is named";

    const auto png = dir / std::to_string(Post) / (id + "-1.png");
    ASSERT_TRUE(std::filesystem::exists(png));
    EXPECT_EQ(std::filesystem::status(png).permissions() & std::filesystem::perms::all, std::filesystem::perms(0644));
    std::string kept((std::istreambuf_iterator<char>(*std::make_unique<std::ifstream>(png))), {});
    EXPECT_EQ(kept.find("Jane"), std::string::npos) << "the PNG's text chunk";
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(dir / std::to_string(Post)), {}), 2);
    std::filesystem::remove_all(dir);
}

namespace {

ServerRequest staff(const std::string& method, const std::string& path, const std::string& token = "good",
                    bool header = true)
{
    ServerRequest request;
    request.method = method;
    const auto question = path.find('?');
    request.path = path.substr(0, question);
    request.query = question == std::string::npos ? std::string() : path.substr(question + 1);
    if (!token.empty())
        request.headers["cf-access-jwt-assertion"] = token;
    if (header)
        request.headers["x-glim-staff"] = "1";
    return request;
}

Json readJson(const std::filesystem::path& path)
{
    std::ifstream file(path);
    return Json::parse(file, nullptr, false);
}

struct StaffWorld : World
{
    std::filesystem::path dir;
    explicit StaffWorld(std::filesystem::path d)
        : World(d.string())
        , dir(std::move(d))
    {
        bot->setVerifier([](const std::string& token, std::string* why) -> std::optional<std::string> {
            if (token == "good")
                return "staff@example.org";
            *why = "a bad signature";
            return std::nullopt;
        });
    }
    ~StaffWorld()
    {
        std::filesystem::remove_all(dir);
    }

    void fileWithScreenshot()
    {
        Json m = message(Owner, "screenshot");
        m["attachments"] = Json::array({{{"filename", "shot.png"},
                                         {"content_type", "image/png"},
                                         {"size", 68},
                                         {"url", "https://cdn.discordapp.com/attachments/shot.png"}}});
        messages.push_back(m);
        say(Owner, "<@999>");
    }
};

std::filesystem::path scratch(const char* name)
{
    return std::filesystem::temp_directory_path() / std::format("glim-{}-{}", name, ::getpid());
}

} // namespace

TEST(GlimStaff, ThePageDataNamesNoOne)
{
    StaffWorld w(scratch("publish"));
    w.fileWithScreenshot();
    const Json posts = readJson(w.dir / "api/posts.json");
    ASSERT_EQ(posts["posts"].size(), 1u);
    const Json& post = posts["posts"][0];
    EXPECT_EQ(post["id"], std::to_string(Post));
    EXPECT_EQ(post["repo"], "PlasmaZones");
    EXPECT_EQ(post["number"], 145);
    EXPECT_EQ(post["title"], "Zones jump between screens");
    EXPECT_EQ(post["files"], 1);
    const std::string text = posts.dump() + readJson(w.dir / std::format("api/p/{}.json", Post)).dump();
    EXPECT_EQ(text.find(std::to_string(Owner)), std::string::npos) << "no Discord user's id";
    EXPECT_EQ(text.find("path"), std::string::npos) << "no file paths of the server";
    EXPECT_EQ(readJson(w.dir / std::format("api/p/{}.json", Post))["files"][0]["kind"], "image");
}

TEST(GlimStaff, OnlyAccessesPeopleGetIn)
{
    StaffWorld w(scratch("auth"));
    EXPECT_EQ(w.bot->staffRequest(staff("GET", "/staff/api/me", "")).status, 403) << "no token";
    EXPECT_EQ(w.bot->staffRequest(staff("GET", "/staff/api/me", "forged")).status, 403);
    const auto me = w.bot->staffRequest(staff("GET", "/staff/api/me"));
    EXPECT_EQ(me.status, 200);
    EXPECT_EQ(Json::parse(me.body)["email"], "staff@example.org");
    EXPECT_EQ(w.bot->staffRequest(staff("POST", "/staff/api/posts/1/update", "good", false)).status, 400)
        << "no X-Glim-Staff: maybe another site's form";
    EXPECT_EQ(w.bot->staffRequest(staff("POST", "/staff/api/posts/1/update")).status, 404) << "no such post";
}

TEST(GlimStaff, TheStaffListLimitsWhoActs)
{
    const auto dir = scratch("list");
    World w(dir.string());
    w.config.staffEmails = {"boss@example.org"};
    w.bot = std::make_unique<Bot>(
        w.config,
        [&w](const HttpRequest& r) {
            return w.discord(r);
        },
        [&w](const HttpRequest& r) {
            return w.github(r);
        },
        [] {
            return std::string("installation-token");
        });
    w.bot->setVerifier([](const std::string&, std::string*) {
        return std::optional<std::string>("Staff@Example.org");
    });
    EXPECT_EQ(w.bot->staffRequest(staff("GET", "/staff/api/me")).status, 403);
    std::filesystem::remove_all(dir);
}

TEST(GlimStaff, AFileIsTakenDown)
{
    StaffWorld w(scratch("remove"));
    w.fileWithScreenshot();
    const auto& file = w.bot->state().posts.at(Post).files.at(0);
    const auto path = w.dir / file.path;
    ASSERT_TRUE(std::filesystem::exists(path));
    const auto answer = w.bot->staffRequest(staff("POST", std::format("/staff/api/posts/{}/files/1/remove", Post)));
    EXPECT_EQ(answer.status, 200) << answer.body;
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(w.bot->state().posts.at(Post).files.at(0).removed);
    EXPECT_TRUE(readJson(w.dir / std::format("api/p/{}.json", Post))["files"].empty());
    EXPECT_EQ(w.bot->staffRequest(staff("POST", std::format("/staff/api/posts/{}/files/1/remove", Post))).status, 404)
        << "once";
}

TEST(GlimStaff, AnUpdateFromThePage)
{
    StaffWorld w(scratch("update"));
    w.fileWithScreenshot();
    w.messages.push_back(w.message(Stranger, "More details"));
    const auto answer = w.bot->staffRequest(staff("POST", std::format("/staff/api/posts/{}/update", Post)));
    EXPECT_EQ(answer.status, 200);
    ASSERT_EQ(w.comments.size(), 1u);
    EXPECT_NE(std::string(w.comments[0]["body"]).find("at the request of staff, on the issues page"),
              std::string::npos);
    EXPECT_NE(w.lastReply().find("Updated PlasmaZones#145"), std::string::npos) << "the post is told";
}

TEST(GlimStaff, AnUnlinkedPostLeavesThePage)
{
    StaffWorld w(scratch("unlink"));
    w.fileWithScreenshot();
    const auto answer =
        w.bot->staffRequest(staff("POST", std::format("/staff/api/posts/{}/unlink?files=remove", Post)));
    EXPECT_EQ(answer.status, 200);
    EXPECT_TRUE(w.bot->state().posts.empty());
    EXPECT_FALSE(std::filesystem::exists(w.dir / std::to_string(Post)));
    EXPECT_FALSE(std::filesystem::exists(w.dir / std::format("api/p/{}.json", Post)));
    EXPECT_TRUE(readJson(w.dir / "api/posts.json")["posts"].empty());
}
