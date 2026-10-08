#include <gtest/gtest.h>

#include "nix/expr/tests/libexpr.hh"
#include "nix/store/globals.hh"
#include "nix/store/store-open.hh"
#include "nix/store/tests/forwarding-store.hh"
#include "nix/util/finally.hh"

namespace nix {

struct FailingBuildStore : ForwardingStore
{
    unsigned buildAttempts = 0;

    using ForwardingStore::ForwardingStore;

    void
    buildPaths(const std::vector<DerivedPath> & paths, BuildMode buildMode, std::shared_ptr<Store> evalStore) override
    {
        buildAttempts++;
        throw Error("the build failed");
    }
};

class ImportFromDerivationTest : public LibExprTest
{
protected:
    ref<FailingBuildStore> failing;

    ImportFromDerivationTest()
        : ImportFromDerivationTest(make_ref<FailingBuildStore>(openStore("dummy://?read-only=false")))
    {
    }

    ImportFromDerivationTest(ref<FailingBuildStore> failing)
        : LibExprTest(
              failing,
              [](bool & readOnlyMode) {
                  EvalSettings settings{readOnlyMode};
                  settings.nixPath = {};
                  return settings;
              })
        , failing(failing)
    {
    }
};

TEST_F(ImportFromDerivationTest, failedRealisationIsReplayed)
{
    bool oldReadOnly = settings.readOnlyMode;
    settings.readOnlyMode = false;
    Finally restore([&]() { settings.readOnlyMode = oldReadOnly; });

    Value v = eval(R"(
      let d = derivation { name = "broken"; system = "x86_64-linux"; builder = "/bin/sh"; };
      in { a = import d; b = import d; }
    )");
    auto * a = v.attrs()->get(createSymbol("a"))->value;
    auto * b = v.attrs()->get(createSymbol("b"))->value;

    EXPECT_THROW(state.forceValue(*a, noPos), Error);
    EXPECT_EQ(failing->buildAttempts, 1);

    EXPECT_THROW(state.forceValue(*a, noPos), Error);
    EXPECT_THROW(state.forceValue(*b, noPos), Error);
    EXPECT_EQ(failing->buildAttempts, 1) << "a failed realisation is replayed, not attempted again";
}

} // namespace nix
