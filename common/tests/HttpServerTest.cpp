// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "HttpServer.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace Bots;

namespace {

std::string ask(int port, const std::string& raw)
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in where{};
    where.sin_family = AF_INET;
    where.sin_port = htons(std::uint16_t(port));
    ::inet_pton(AF_INET, "127.0.0.1", &where.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&where), sizeof where) != 0)
        return "no connection";
    ::send(fd, raw.data(), raw.size(), 0);
    std::string out;
    char buffer[4096];
    for (ssize_t n; (n = ::recv(fd, buffer, sizeof buffer, 0)) > 0;)
        out.append(buffer, std::size_t(n));
    ::close(fd);
    return out;
}

} // namespace

TEST(HttpServer, ParsesAHead)
{
    ServerRequest request;
    ASSERT_TRUE(
        HttpServer::parseHead("POST /staff/api/posts/1/unlink?files=remove HTTP/1.1\r\nHost: x\r\n"
                              "Cf-Access-Jwt-Assertion:  abc \r\n",
                              request));
    EXPECT_EQ(request.method, "POST");
    EXPECT_EQ(request.path, "/staff/api/posts/1/unlink");
    EXPECT_EQ(request.query, "files=remove");
    EXPECT_EQ(request.headers.at("cf-access-jwt-assertion"), "abc");
    EXPECT_FALSE(HttpServer::parseHead("GET nopath HTTP/1.1\r\n", request));
    EXPECT_FALSE(HttpServer::parseHead("garbage", request));
}

TEST(HttpServer, AnswersARequest)
{
    HttpServer server([](const ServerRequest& request) {
        return ServerResponse{200, "application/json", "{\"path\":\"" + request.path + "\"}"};
    });
    ASSERT_TRUE(server.start("127.0.0.1:0"));
    const std::string answer = ask(server.port(), "GET /staff/api/me HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_TRUE(answer.starts_with("HTTP/1.1 200 OK\r\n")) << answer;
    EXPECT_NE(answer.find("Cache-Control: no-store"), std::string::npos);
    EXPECT_TRUE(answer.ends_with("{\"path\":\"/staff/api/me\"}"));
    EXPECT_TRUE(ask(server.port(), "nonsense\r\n\r\n").starts_with("HTTP/1.1 400"));
    server.stop();
}
