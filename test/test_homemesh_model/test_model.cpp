#include <gtest/gtest.h>

#include "../../examples/companion_radio/ui-homemesh/Model.h"

using namespace homemesh;

TEST(HomeMeshModel, UnreadCountsIncomingOnly) {
  static MessageLog log;
  log.clear();
  log.add(1, false, "a");
  log.add(2, true, "b");
  log.add(3, false, "c");
  EXPECT_EQ(3, log.count);
  EXPECT_EQ(2, log.unread);
  EXPECT_EQ(Delivery::Pending, log.msgs[1].status);
  log.markRead();
  EXPECT_EQ(0, log.unread);
  EXPECT_EQ(3, log.seen);
}

TEST(HomeMeshModel, FullLogDropsOldestAndKeepsDivider) {
  static MessageLog log;
  log.clear();
  for (int i = 0; i < MAX_LOG_MESSAGES; i++) log.add(i, false, "x");
  log.markRead();
  log.add(100, false, "newest");
  EXPECT_EQ(MAX_LOG_MESSAGES, log.count);
  EXPECT_EQ(1u, log.msgs[0].time);                    // message 0 dropped
  EXPECT_STREQ("newest", log.last()->text);
  EXPECT_EQ(MAX_LOG_MESSAGES - 1, log.seen);          // divider still before "newest"
  EXPECT_EQ(1, log.unread);
}

TEST(HomeMeshModel, LongTextIsCutOnCharacterBoundary) {
  static MessageLog log;
  log.clear();
  std::string s(TEXT_LEN - 2, 'a');
  s += "ü";   // 2 bytes, crosses the limit
  log.add(1, false, s.c_str());
  EXPECT_EQ((size_t)(TEXT_LEN - 2), strlen(log.msgs[0].text));
}

TEST(HomeMeshModel, FavouritesAndUnreadSplit) {
  static Model m;
  m.clear();
  m.conv_count = 3;
  m.convs[0].favourite = true;
  m.convs[2].favourite = true;
  m.convs[0].log.add(1, false, "x");
  m.convs[1].log.add(1, false, "y");
  m.convs[1].log.add(2, false, "z");
  int f[MAX_FAVOURITES];
  EXPECT_EQ(2, m.favourites(f));
  EXPECT_EQ(0, f[0]);
  EXPECT_EQ(2, f[1]);
  EXPECT_EQ(1, m.unreadFrom(true));
  EXPECT_EQ(2, m.unreadFrom(false));
}
