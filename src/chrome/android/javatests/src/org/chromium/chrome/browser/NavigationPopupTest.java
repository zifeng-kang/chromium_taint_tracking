// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.graphics.Bitmap;
import android.test.suitebuilder.annotation.MediumTest;
import android.test.suitebuilder.annotation.SmallTest;

import org.chromium.base.ThreadUtils;
import org.chromium.base.test.util.Feature;
import org.chromium.base.test.util.UrlUtils;
import org.chromium.chrome.browser.navigation.NavigationHandler;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.test.ChromeActivityTestCaseBase;
import org.chromium.content.browser.test.util.Criteria;
import org.chromium.content.browser.test.util.CriteriaHelper;
import org.chromium.content_public.browser.LoadUrlParams;
import org.chromium.content_public.browser.NavigationEntry;
import org.chromium.content_public.browser.NavigationHistory;

/**
 * Tests for the navigation popup.
 */
public class NavigationPopupTest extends ChromeActivityTestCaseBase<ChromeActivity> {

    private static final int INVALID_NAVIGATION_INDEX = -1;

    private Profile mProfile;

    public NavigationPopupTest() {
        super(ChromeActivity.class);
    }

    @Override
    protected void setUp() throws Exception {
        super.setUp();
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                mProfile = Profile.getLastUsedProfile();
            }
        });
    }

    @Override
    public void startMainActivity() throws InterruptedException {
        startMainActivityOnBlankPage();
    }

    // Exists solely to expose protected methods to this test.
    private static class TestNavigationHistory extends NavigationHistory {
        @Override
        public void addEntry(NavigationEntry entry) {
            super.addEntry(entry);
        }
    }

    // Exists solely to expose protected methods to this test.
    private static class TestNavigationEntry extends NavigationEntry {
        public TestNavigationEntry(int index, String url, String virtualUrl, String originalUrl,
                String title, Bitmap favicon, int transition) {
            super(index, url, virtualUrl, originalUrl, title, favicon, transition);
        }
    }

    private static class TestNavigationHandler implements NavigationHandler {
        private final TestNavigationHistory mHistory;
        private int mNavigatedIndex = INVALID_NAVIGATION_INDEX;

        public TestNavigationHandler() {
            mHistory = new TestNavigationHistory();
            mHistory.addEntry(new TestNavigationEntry(
                    1, "about:blank", null, null, "About Blank", null, 0));
            mHistory.addEntry(new TestNavigationEntry(
                    5, UrlUtils.encodeHtmlDataUri("<html>1</html>"), null, null, null, null, 0));
        }

        @Override
        public boolean canGoBack() {
            return false;
        }

        @Override
        public boolean canGoForward() {
            return false;
        }

        @Override
        public boolean canGoToOffset(int offset) {
            return false;
        }

        @Override
        public void goToOffset(int offset) {
        }

        @Override
        public void goBack() {
        }

        @Override
        public void goForward() {
        }

        @Override
        public boolean isInitialNavigation() {
            return false;
        }

        @Override
        public void loadIfNecessary() {
        }

        @Override
        public void requestRestoreLoad() {
        }

        @Override
        public void reload(boolean checkForRepost) {
        }

        @Override
        public void reloadToRefreshContent(boolean checkForRepost) {
        }

        @Override
        public void reloadBypassingCache(boolean checkForRepost) {
        }

        @Override
        public void reloadDisableLoFi(boolean checkForRepost) {
        }

        @Override
        public void cancelPendingReload() {
        }

        @Override
        public void continuePendingReload() {
        }

        @Override
        public void loadUrl(LoadUrlParams params) {
        }

        @Override
        public String getOriginalUrlForVisibleNavigationEntry() {
            return null;
        }

        @Override
        public void clearSslPreferences() {
        }

        @Override
        public boolean getUseDesktopUserAgent() {
            return false;
        }

        @Override
        public void setUseDesktopUserAgent(boolean override, boolean reloadOnChange) {
        }

        @Override
        public NavigationEntry getEntryAtIndex(int index) {
            return null;
        }

        @Override
        public NavigationHistory getDirectedNavigationHistory(boolean isForward, int itemLimit) {
            return mHistory;
        }

        @Override
        public void goToNavigationIndex(int index) {
            mNavigatedIndex = index;
        }

        @Override
        public int getLastCommittedEntryIndex() {
            return -1;
        }

        @Override
        public boolean removeEntryAtIndex(int index) {
            return false;
        }
    }

    @MediumTest
    @Feature({"Navigation"})
    public void testFaviconFetching() throws InterruptedException {
        final TestNavigationHandler controller = new TestNavigationHandler();
        final NavigationPopup popup = new NavigationPopup(
                mProfile, getActivity(), controller, true);
        popup.setWidth(300);
        popup.setHeight(300);
        popup.setAnchorView(getActivity().getCurrentContentViewCore().getContainerView());
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                popup.show();
            }
        });

        CriteriaHelper.pollUiThread(new Criteria("All favicons did not get updated.") {
            @Override
            public boolean isSatisfied() {
                NavigationHistory history = controller.mHistory;
                for (int i = 0; i < history.getEntryCount(); i++) {
                    if (history.getEntryAtIndex(i).getFavicon() == null) {
                        return false;
                    }
                }
                return true;
            }
        });

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                popup.dismiss();
            }
        });
    }

    @SmallTest
    @Feature({"Navigation"})
    public void testItemSelection() {
        final TestNavigationHandler controller = new TestNavigationHandler();
        final NavigationPopup popup =
                new NavigationPopup(mProfile, getActivity(), controller, true);
        popup.setWidth(300);
        popup.setHeight(300);
        popup.setAnchorView(getActivity().getCurrentContentViewCore().getContainerView());
        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                popup.show();
            }
        });

        ThreadUtils.runOnUiThreadBlocking(new Runnable() {
            @Override
            public void run() {
                popup.performItemClick(1);
            }
        });

        assertFalse("Popup did not hide as expected.", popup.isShowing());
        assertEquals("Popup attempted to navigate to the wrong index", 5,
                controller.mNavigatedIndex);
    }

}
