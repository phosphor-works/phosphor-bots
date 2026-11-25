// SPDX-FileCopyrightText: 2026 Phosphor Works Team
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AccessJwt.h"
#include "GitHubApp.h"

#include <gtest/gtest.h>

#include <memory>

#include <nlohmann/json.hpp>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

using namespace Bots;
using Json = nlohmann::json;

namespace {

constexpr std::int64_t Now = 1'800'000'000;

struct Signer
{
    EVP_PKEY* key = EVP_RSA_gen(2048);
    ~Signer()
    {
        EVP_PKEY_free(key);
    }

    std::string bignum(const char* name) const
    {
        BIGNUM* value = nullptr;
        EVP_PKEY_get_bn_param(key, name, &value);
        std::string out(std::size_t(BN_num_bytes(value)), '\0');
        BN_bn2bin(value, reinterpret_cast<unsigned char*>(out.data()));
        BN_free(value);
        return out;
    }

    std::string jwks(const std::string& kid) const
    {
        return Json{{"keys",
                     Json::array({{{"kid", kid},
                                   {"kty", "RSA"},
                                   {"alg", "RS256"},
                                   {"n", base64Url(bignum(OSSL_PKEY_PARAM_RSA_N))},
                                   {"e", base64Url(bignum(OSSL_PKEY_PARAM_RSA_E))}}})}}
            .dump();
    }

    std::string token(const Json& claims, const std::string& kid = "k1") const
    {
        const std::string part =
            base64Url(Json{{"alg", "RS256"}, {"kid", kid}, {"typ", "JWT"}}.dump()) + "." + base64Url(claims.dump());
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        std::size_t length = 0;
        EVP_DigestSignInit(context.get(), nullptr, EVP_sha256(), nullptr, key);
        EVP_DigestSign(context.get(), nullptr, &length, reinterpret_cast<const unsigned char*>(part.data()),
                       part.size());
        std::string signature(length, '\0');
        EVP_DigestSign(context.get(), reinterpret_cast<unsigned char*>(signature.data()), &length,
                       reinterpret_cast<const unsigned char*>(part.data()), part.size());
        signature.resize(length);
        return part + "." + base64Url(signature);
    }
};

Json claims()
{
    return {{"aud", Json::array({"AUD1"})},
            {"email", "staff@example.org"},
            {"exp", Now + 600},
            {"iat", Now - 10},
            {"nbf", Now - 10},
            {"iss", "https://phosphor-works.cloudflareaccess.com"}};
}

struct Fixture
{
    Signer signer;
    int fetches = 0;
    AccessJwt access{"phosphor-works", "AUD1",
                     [this](const HttpRequest& r) {
                         ++fetches;
                         EXPECT_EQ(r.url, "https://phosphor-works.cloudflareaccess.com/cdn-cgi/access/certs");
                         return HttpResponse{200, {}, signer.jwks("k1"), {}};
                     },
                     [] {
                         return Now;
                     }};
};

} // namespace

TEST(AccessJwt, AGoodTokenNamesItsPerson)
{
    Fixture f;
    EXPECT_EQ(f.access.verify(f.signer.token(claims())), "staff@example.org");
    EXPECT_EQ(f.access.verify(f.signer.token(claims())), "staff@example.org");
    EXPECT_EQ(f.fetches, 1) << "the keys are fetched once";
}

TEST(AccessJwt, BadTokensAreRefused)
{
    Fixture f;
    std::string why;
    auto c = claims();
    c["exp"] = Now - 3600;
    EXPECT_FALSE(f.access.verify(f.signer.token(c), &why));
    EXPECT_EQ(why, "expired");
    c = claims();
    c["aud"] = "OTHER";
    EXPECT_FALSE(f.access.verify(f.signer.token(c), &why));
    EXPECT_EQ(why, "another application");
    c = claims();
    c["iss"] = "https://evil.cloudflareaccess.com";
    EXPECT_FALSE(f.access.verify(f.signer.token(c), &why));
    c = claims();
    c.erase("email");
    EXPECT_FALSE(f.access.verify(f.signer.token(c), &why)) << "a service token is no person";

    Signer other;
    EXPECT_FALSE(f.access.verify(other.token(claims()), &why));
    EXPECT_EQ(why, "a bad signature");

    std::string token = f.signer.token(claims());
    const auto dot = token.find('.');
    token.replace(dot + 1, token.find('.', dot + 1) - dot - 1,
                  base64Url(Json{{"aud", "AUD1"},
                                 {"email", "evil@example.org"},
                                 {"exp", Now + 600},
                                 {"iss", "https://phosphor-works.cloudflareaccess.com"}}
                                .dump()));
    EXPECT_FALSE(f.access.verify(token, &why)) << "changed claims";
    EXPECT_FALSE(f.access.verify("not.a.jwt", &why));
    EXPECT_FALSE(f.access.verify("", &why));
}

TEST(AccessJwt, AnUnknownKeyFetchesTheKeysAgainButNotOften)
{
    Fixture f;
    EXPECT_FALSE(f.access.verify(f.signer.token(claims(), "k2")));
    EXPECT_FALSE(f.access.verify(f.signer.token(claims(), "k3")));
    EXPECT_EQ(f.fetches, 1);
}

TEST(AccessJwt, Base64UrlDecodes)
{
    EXPECT_EQ(base64UrlDecode("Zm9v"), "foo");
    EXPECT_EQ(base64UrlDecode("Zg"), "f");
    EXPECT_EQ(base64UrlDecode("-_8"), "\xfb\xff");
    EXPECT_FALSE(base64UrlDecode("a"));
    EXPECT_FALSE(base64UrlDecode("$$$$"));
}
