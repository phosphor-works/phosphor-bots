// SPDX-FileCopyrightText: 2026 fuddlesworth
// SPDX-License-Identifier: GPL-3.0-or-later

#include "GitHubApp.h"

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

using namespace Bots;
using Json = nlohmann::json;

namespace {

struct Key
{
    EVP_PKEY* key = EVP_RSA_gen(2048);
    ~Key()
    {
        EVP_PKEY_free(key);
    }
    std::string pem() const
    {
        BIO* bio = BIO_new(BIO_s_mem());
        PEM_write_bio_PrivateKey(bio, key, nullptr, nullptr, 0, nullptr, nullptr);
        char* data = nullptr;
        const long size = BIO_get_mem_data(bio, &data);
        std::string out(data, std::size_t(size));
        BIO_free(bio);
        return out;
    }
};

std::string base64UrlDecode(std::string text)
{
    for (auto& c : text)
        c = c == '-' ? '+' : c == '_' ? '/' : c;
    while (text.size() % 4)
        text += '=';
    std::string out(text.size(), '\0');
    const int n = EVP_DecodeBlock(reinterpret_cast<unsigned char*>(out.data()),
                                  reinterpret_cast<const unsigned char*>(text.data()), int(text.size()));
    out.resize(std::size_t(n) - std::count(text.end() - 2, text.end(), '='));
    return out;
}

} // namespace

TEST(GitHubApp, Base64Url)
{
    EXPECT_EQ(base64Url(""), "");
    EXPECT_EQ(base64Url("f"), "Zg");
    EXPECT_EQ(base64Url("fo"), "Zm8");
    EXPECT_EQ(base64Url("foo"), "Zm9v");
    EXPECT_EQ(base64Url("\xfb\xff"), "-_8");
}

TEST(GitHubApp, TheJwtIsSignedWithTheKey)
{
    Key key;
    GitHubApp app("4242", key.pem(), "org", "test", [](const HttpRequest&) {
        return HttpResponse{};
    });
    const std::string jwt = app.jwt(1'700'000'000);
    const auto dot1 = jwt.find('.');
    const auto dot2 = jwt.find('.', dot1 + 1);
    ASSERT_NE(dot2, std::string::npos);
    const Json header = Json::parse(base64UrlDecode(jwt.substr(0, dot1)));
    const Json claims = Json::parse(base64UrlDecode(jwt.substr(dot1 + 1, dot2 - dot1 - 1)));
    EXPECT_EQ(header["alg"], "RS256");
    EXPECT_EQ(claims["iss"], "4242");
    EXPECT_EQ(claims["iat"], 1'700'000'000 - 60);
    EXPECT_EQ(claims["exp"], 1'700'000'000 + 540);

    const std::string signedPart = jwt.substr(0, dot2);
    const std::string signature = base64UrlDecode(jwt.substr(dot2 + 1));
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    ASSERT_EQ(EVP_DigestVerifyInit(context.get(), nullptr, EVP_sha256(), nullptr, key.key), 1);
    EXPECT_EQ(EVP_DigestVerify(context.get(), reinterpret_cast<const unsigned char*>(signature.data()),
                               signature.size(), reinterpret_cast<const unsigned char*>(signedPart.data()),
                               signedPart.size()),
              1);
}

TEST(GitHubApp, ATokenIsFetchedOnceAndRenewedBeforeItEnds)
{
    Key key;
    std::int64_t now = 1'700'000'000;
    std::vector<std::string> calls;
    int tokens = 0;
    GitHubApp app(
        "1", key.pem(), "phosphor-works", "test",
        [&](const HttpRequest& r) {
            calls.push_back(r.method + " " + r.url.substr(22));
            EXPECT_NE(std::find_if(r.headers.begin(), r.headers.end(),
                                   [](const std::string& h) {
                                       return h.starts_with("Authorization: Bearer ey");
                                   }),
                      r.headers.end());
            if (r.url.ends_with("/installation"))
                return HttpResponse{200, {}, R"({"id": 77})", {}};
            ++tokens;
            return HttpResponse{
                201,
                {},
                Json{{"token", "ghs_" + std::to_string(tokens)}, {"expires_at", "2023-11-14T23:13:20Z"}}.dump(),
                {}};
        },
        [&] {
            return now;
        });
    EXPECT_EQ(app.token(), "ghs_1");
    EXPECT_EQ(app.token(), "ghs_1");
    EXPECT_EQ(calls,
              (std::vector<std::string>{"GET /orgs/phosphor-works/installation",
                                        "POST /app/installations/77/access_tokens"}));
    now += 3600 - 9 * 60; // 9 minutes before it ends
    EXPECT_EQ(app.token(), "ghs_2");
    EXPECT_EQ(calls.size(), 3u) << "the installation is known";
}

TEST(GitHubApp, ABadKeyIsRefused)
{
    EXPECT_THROW(GitHubApp("1", "not a key", "org", "test",
                           [](const HttpRequest&) {
                               return HttpResponse{};
                           }),
                 std::runtime_error);
}
