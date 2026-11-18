#include <gtest/gtest.h>

#include "../../examples/companion_radio/ui-homemesh/ShareLink.h"

using namespace homemesh;

TEST(HomeMeshShareLink, MatchesTheDocumentedExample) {
  const uint8_t key[32] = {
    0x9c, 0xd8, 0xfc, 0xf2, 0x2a, 0x47, 0x33, 0x3b, 0x59, 0x1d, 0x96, 0xa2, 0xb8, 0x48, 0xb7, 0x3f,
    0x45, 0x7b, 0x1b, 0xb1, 0xa3, 0xea, 0x24, 0x53, 0xa8, 0x85, 0xf9, 0xe5, 0x78, 0x77, 0x65, 0xb1 };
  char out[220];
  ASSERT_TRUE(buildContactLink("Example Contact", key, CONTACT_TYPE_COMPANION, out, sizeof(out)));
  EXPECT_STREQ("meshcore://contact/add?name=Example+Contact&public_key="
               "9cd8fcf22a47333b591d96a2b848b73f457b1bb1a3ea2453a885f9e5787765b1&type=1", out);
}

TEST(HomeMeshShareLink, EncodesSpecialCharacters) {
  char out[64];
  urlEncodeName("Küche & Flur #2", out, sizeof(out));
  EXPECT_STREQ("K%C3%BCche+%26+Flur+%232", out);
}

TEST(HomeMeshShareLink, LongestNameFits) {
  uint8_t key[32] = {};
  char name[32];
  memset(name, 0xC3, 30);   // worst case: every byte encoded
  name[30] = 'x';
  name[31] = 0;
  char out[220];
  EXPECT_TRUE(buildContactLink(name, key, CONTACT_TYPE_COMPANION, out, sizeof(out)));
  char small[100];
  EXPECT_FALSE(buildContactLink(name, key, CONTACT_TYPE_COMPANION, small, sizeof(small)));
}

TEST(HomeMeshShareLink, EncodingStopsAtBufferEnd) {
  char out[6];
  urlEncodeName("aä", out, sizeof(out));   // "a%C3" fits, "%A4" does not
  EXPECT_STREQ("a%C3", out);
}
