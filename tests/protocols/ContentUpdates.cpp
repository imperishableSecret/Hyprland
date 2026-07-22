#include <protocols/types/ContentUpdate.hpp>

#include <gtest/gtest.h>

class CContentUpdateTestAccess {
  public:
    static void applyState(CContentUpdate& update) {
        update.applyState();
    }
};

TEST(ContentUpdates, extensionStateIsCapturedPerUpdate) {
    SSurfaceState  state;
    CContentUpdate first{state, {}};
    CContentUpdate second{state, {}};
    int            pending = 1;
    int            active  = 0;

    first.addActivation([&active, value = pending] { active = value; });
    pending = 2;
    second.addActivation([&active, value = pending] { active = value; });

    CContentUpdateTestAccess::applyState(first);
    EXPECT_EQ(active, 1);

    CContentUpdateTestAccess::applyState(second);
    EXPECT_EQ(active, 2);
}

TEST(ContentUpdates, activationsRunInRegistrationOrder) {
    SSurfaceState  state;
    CContentUpdate update{state, {}};
    int            active = 1;

    update.addActivation([&active] { active += 2; });
    update.addActivation([&active] { active *= 3; });

    CContentUpdateTestAccess::applyState(update);
    EXPECT_EQ(active, 9);
}
