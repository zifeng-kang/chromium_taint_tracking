// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_NTP_SNIPPETS_BOOKMARKS_BOOKMARK_LAST_VISIT_UTILS_H_
#define COMPONENTS_NTP_SNIPPETS_BOOKMARKS_BOOKMARK_LAST_VISIT_UTILS_H_

#include <vector>

class GURL;

namespace base {
class Time;
}  // namespace base

namespace bookmarks {
class BookmarkModel;
class BookmarkNode;
}  // namespace bookmarks

namespace ntp_snippets {

// If there is a bookmark for |url|, this function updates its last visit date
// to now. If there are multiple bookmarks for a given URL, it updates all of
// them.
void UpdateBookmarkOnURLVisitedInMainFrame(
    bookmarks::BookmarkModel* bookmark_model,
    const GURL& url);

// Gets the last visit date for a given bookmark |node|. If the bookmark lacks
// this info, it returns it creation date.
base::Time GetLastVisitDateForBookmark(const bookmarks::BookmarkNode* node);

// Returns the list of most recently visited bookmarks. For each bookmarked URL,
// it returns the most recently created bookmark. The result is ordered by visit
// time (the most recent first). Only bookmarks visited after
// |min_visit_time| are considered, at most |max_count| bookmarks are returned.
std::vector<const bookmarks::BookmarkNode*> GetRecentlyVisitedBookmarks(
    bookmarks::BookmarkModel* bookmark_model,
    int max_count,
    const base::Time& min_visit_time);

}  // namespace ntp_snippets

#endif  // COMPONENTS_NTP_SNIPPETS_BOOKMARKS_BOOKMARK_LAST_VISIT_UTILS_H_
