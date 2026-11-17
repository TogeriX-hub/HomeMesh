#include <gtest/gtest.h>

#include "../../examples/companion_radio/ui-homemesh/Warnings.h"

using namespace homemesh;

TEST(HomeMeshWarnings, ChannelName) {
  EXPECT_TRUE(isWarningChannel("warnings"));
  EXPECT_TRUE(isWarningChannel("#warnings"));
  EXPECT_TRUE(isWarningChannel("#Warnings"));
  EXPECT_FALSE(isWarningChannel("#warning"));
  EXPECT_FALSE(isWarningChannel("#warnings2"));
  EXPECT_FALSE(isWarningChannel("boeblingen"));
}

TEST(HomeMeshWarnings, RealAlertIsShortened) {
  char out[161];
  ASSERT_TRUE(extractWarning("[!] DWD | Sturmböen | Landkreis | /details", out, sizeof(out)));
  EXPECT_STREQ("DWD · Sturmböen · Landkreis", out);
  ASSERT_TRUE(extractWarning("[!] PEGEL | Würm 2,1 m", out, sizeof(out)));
  EXPECT_STREQ("PEGEL · Würm 2,1 m", out);
}

TEST(HomeMeshWarnings, StatusAndTestsAreIgnored) {
  char out[161];
  EXPECT_FALSE(extractWarning("DAB+:OK/18dB NINA:OK 5m Mesh:OK Up:3d Warn:1", out, sizeof(out)));
  EXPECT_FALSE(extractWarning("[TEST] DWD | Probe", out, sizeof(out)));
  EXPECT_FALSE(extractWarning("[SW] Update", out, sizeof(out)));
  EXPECT_FALSE(extractWarning("/warnings", out, sizeof(out)));
  EXPECT_FALSE(extractWarning("[!]", out, sizeof(out)));
  EXPECT_FALSE(extractWarning("[!] | /details", out, sizeof(out)));
  EXPECT_FALSE(extractWarning(nullptr, out, sizeof(out)));
}

TEST(HomeMeshWarnings, SmallBufferKeepsValidUtf8) {
  char out[10];
  ASSERT_TRUE(extractWarning("[!] DWD | Sturmböen", out, sizeof(out)));
  EXPECT_STREQ("DWD · St", out);
}
