#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/expr/eval-cache.hh"
#include "nix/expr/tests/libexpr.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/store/dummy-store.hh"
#include "nix/store/globals.hh"
#include "nix/util/callback.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/file-system.hh"
#include "nix/util/finally.hh"

namespace nix {

using namespace eval_cache;

/**
 * A store that records every GC-root and validity operation, i.e. the
 * operations that become daemon round-trips with a `RemoteStore`. All
 * paths are valid unless listed in `invalidPaths`.
 */
struct CountingStore : DummyStore
{
    std::vector<StorePath> tempRoots;
    std::vector<StorePath> validityChecks;
    std::vector<StorePathSet> validityBatches;
    StorePathSet invalidPaths;

    CountingStore(ref<const Config> config)
        : Store{*config}
        , DummyStore{config}
    {
    }

    void reset()
    {
        tempRoots.clear();
        validityChecks.clear();
        validityBatches.clear();
    }

    void addTempRoot(const StorePath & path) override
    {
        tempRoots.push_back(path);
    }

    bool isValidPathUncached(const StorePath & path) override
    {
        validityChecks.push_back(path);
        return !invalidPaths.count(path);
    }

    StorePathSet queryValidPaths(const StorePathSet & paths, SubstituteFlag maybeSubstitute) override
    {
        validityBatches.push_back(paths);
        StorePathSet res;
        for (auto & path : paths)
            if (!invalidPaths.count(path))
                res.insert(path);
        return res;
    }

    void queryPathInfoUncached(
        const StorePath & path, Callback<std::shared_ptr<const ValidPathInfo>> callback) noexcept override
    {
        callback(nullptr);
    }

    void queryRealisationUncached(
        const DrvOutput & drvOutput, Callback<std::shared_ptr<const UnkeyedRealisation>> callback) noexcept override
    {
        callback(nullptr);
    }

    std::optional<StorePath> queryPathFromHashPart(const std::string & hashPart) override
    {
        return std::nullopt;
    }

    void addToStore(const ValidPathInfo & info, Source & source, RepairFlag repair, CheckSigsFlag checkSigs) override
    {
        unsupported("addToStore");
    }

    StorePath addToStoreFromDump(
        Source & dump,
        std::string_view name,
        FileSerialisationMethod dumpMethod,
        ContentAddressMethod hashMethod,
        HashAlgorithm hashAlgo,
        const StorePathSet & references,
        RepairFlag repair) override
    {
        unsupported("addToStoreFromDump");
    }

    void registerDrvOutput(const Realisation & output) override
    {
        unsupported("registerDrvOutput");
    }

    std::optional<TrustedFlag> isTrustedClient() override
    {
        return std::nullopt;
    }

    ref<SourceAccessor> getFSAccessor(bool requireValidPath) override
    {
        return makeEmptySourceAccessor();
    }

    std::shared_ptr<SourceAccessor> getFSAccessor(const StorePath & path, bool requireValidPath) override
    {
        return nullptr;
    }
};

class EvalCacheTest : public LibExprTest
{
protected:
    ref<CountingStore> counting;

    EvalCacheTest()
        : EvalCacheTest(make_ref<CountingStore>(make_ref<DummyStoreConfig>(DummyStoreConfig::Params{})))
    {
    }

    EvalCacheTest(ref<CountingStore> counting)
        : LibExprTest(
              counting,
              [](bool & readOnlyMode) {
                  EvalSettings settings{readOnlyMode};
                  settings.nixPath = {};
                  return settings;
              })
        , counting(counting)
    {
        setEnv("XDG_CACHE_HOME", createTempDir().c_str());
    }

    ref<EvalCache> makeCache(const Hash & fingerprint, Value & vRoot)
    {
        return make_ref<EvalCache>(std::cref(fingerprint), state, [&vRoot]() { return &vRoot; });
    }
};

static constexpr std::string_view ctxExpr = R"({
  x = builtins.appendContext "foo" {
    "/nix/store/00000000000000000000000000000001-ctx-a" = { path = true; };
    "/nix/store/00000000000000000000000000000002-ctx-b" = { path = true; };
    "/nix/store/00000000000000000000000000000003-ctx-c" = { path = true; };
  };
})";

TEST_F(EvalCacheTest, cachedStringContextValidationIsBatchedAndMemoized)
{
    auto fp = hashString(HashAlgorithm::SHA256, "eval-cache-test-1");
    Value vRoot = eval(std::string(ctxExpr));

    auto cache1 = makeCache(fp, vRoot);
    cache1->getRoot()->getAttr("x")->getStringWithContext();
    cache1->commit();

    counting->reset();
    auto cache2 = makeCache(fp, vRoot);
    auto s = cache2->getRoot()->getAttr("x")->getStringWithContext();
    EXPECT_EQ(s.first, "foo");
    EXPECT_EQ(s.second.size(), 3);
    EXPECT_EQ(counting->tempRoots.size(), 3) << "each context path is rooted exactly once";
    EXPECT_EQ(counting->validityBatches.size(), 1) << "validity is checked with one batched queryValidPaths";
    EXPECT_EQ(counting->validityChecks.size(), 0) << "no per-path validity round-trips";

    counting->reset();
    auto s2 = cache2->getRoot()->getAttr("x")->getStringWithContext();
    EXPECT_EQ(s2.first, "foo");
    EXPECT_EQ(counting->tempRoots.size(), 0) << "already-rooted paths are not re-rooted";
    EXPECT_EQ(counting->validityBatches.size(), 0);
    EXPECT_EQ(counting->validityChecks.size(), 0);
}

TEST_F(EvalCacheTest, invalidContextPathStillForcesRegeneration)
{
    auto fp = hashString(HashAlgorithm::SHA256, "eval-cache-test-2");
    Value vRoot = eval(std::string(ctxExpr));

    auto cache1 = makeCache(fp, vRoot);
    cache1->getRoot()->getAttr("x")->getStringWithContext();
    cache1->commit();

    StorePath invalid{"00000000000000000000000000000002-ctx-b"};
    counting->invalidPaths.insert(invalid);

    counting->reset();
    auto cache2 = makeCache(fp, vRoot);
    auto s = cache2->getRoot()->getAttr("x")->getStringWithContext();
    EXPECT_EQ(s.first, "foo");
    EXPECT_EQ(s.second.size(), 3);

    counting->reset();
    auto s2 = cache2->getRoot()->getAttr("x")->getStringWithContext();
    EXPECT_EQ(s2.first, "foo");
    bool rechecked = false;
    for (auto & batch : counting->validityBatches)
        rechecked |= batch.count(invalid) > 0;
    for (auto & path : counting->validityChecks)
        rechecked |= path == invalid;
    EXPECT_TRUE(rechecked) << "a path that failed validation must not be memoized as valid";
}

TEST_F(EvalCacheTest, forceDerivationRootsAndValidatesOnlyOnce)
{
    bool oldReadOnly = settings.readOnlyMode;
    settings.readOnlyMode = false;
    Finally restore([&]() { settings.readOnlyMode = oldReadOnly; });

    Value vRoot = eval(R"({ drvPath = "/nix/store/00000000000000000000000000000004-foo.drv"; })");
    auto cache = make_ref<EvalCache>(std::nullopt, state, [&]() { return &vRoot; });

    counting->reset();
    cache->getRoot()->forceDerivation();
    EXPECT_EQ(counting->tempRoots.size(), 1);
    EXPECT_EQ(counting->validityChecks.size() + counting->validityBatches.size(), 1);

    counting->reset();
    cache->getRoot()->forceDerivation();
    EXPECT_EQ(counting->tempRoots.size(), 0) << "second forceDerivation reuses the memoized root";
    EXPECT_EQ(counting->validityChecks.size(), 0);
    EXPECT_EQ(counting->validityBatches.size(), 0);
}

TEST_F(EvalCacheTest, writeDerivationSeedsRootedPathMemo)
{
    bool oldReadOnly = settings.readOnlyMode;
    settings.readOnlyMode = false;
    Finally restore([&]() { settings.readOnlyMode = oldReadOnly; });

    Value vRoot = eval(R"(
      let d = builtins.derivationStrict { name = "t"; system = "x86_64-linux"; builder = "/bin/sh"; };
      in { drvPath = d.drvPath; }
    )");
    auto cache = make_ref<EvalCache>(std::nullopt, state, [&]() { return &vRoot; });

    counting->reset();
    auto drvPath = cache->getRoot()->forceDerivation();
    EXPECT_TRUE(drvPath.isDerivation());
    EXPECT_EQ(counting->tempRoots.size(), 1) << "writeDerivation roots the path; forceDerivation must reuse that root";
    EXPECT_EQ(counting->validityChecks.size() + counting->validityBatches.size(), 1);
}

} // namespace nix
