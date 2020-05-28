// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp.cards;

import android.content.Context;
import android.content.res.Resources;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.util.AttributeSet;
import android.view.GestureDetector;
import android.view.MotionEvent;
import android.view.View;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

import org.chromium.base.Log;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.ntp.NewTabPageLayout;
import org.chromium.chrome.browser.ntp.snippets.SnippetHeaderViewHolder;

/**
 * Simple wrapper on top of a RecyclerView that will acquire focus when tapped.  Ensures the
 * New Tab page receives focus when clicked.
 */
public class NewTabPageRecyclerView extends RecyclerView {
    private static final String TAG = "NtpCards";

    private final GestureDetector mGestureDetector;
    private final LinearLayoutManager mLayoutManager;
    private final int mToolbarHeight;
    private final int mMinBottomSpacing;

    /**
     * Total height of the items being dismissed.  Tracked to allow the bottom space to compensate
     * for their removal animation and avoid moving the scroll position.
     */
    private int mCompensationHeight;

    /** View used to calculate the position of the cards' snap point. */
    private View mAboveTheFoldView;

    /**
     * Constructor needed to inflate from XML.
     */
    public NewTabPageRecyclerView(Context context, AttributeSet attrs) {
        super(context, attrs);

        mGestureDetector =
                new GestureDetector(getContext(), new GestureDetector.SimpleOnGestureListener() {
                    @Override
                    public boolean onSingleTapUp(MotionEvent e) {
                        boolean retVal = super.onSingleTapUp(e);
                        requestFocus();
                        return retVal;
                    }
                });
        mLayoutManager = new LinearLayoutManager(getContext());
        setLayoutManager(mLayoutManager);

        Resources res = context.getResources();
        mToolbarHeight = res.getDimensionPixelSize(R.dimen.toolbar_height_no_shadow)
                + res.getDimensionPixelSize(R.dimen.toolbar_progress_bar_height);
        mMinBottomSpacing =
                res.getDimensionPixelSize(R.dimen.ntp_min_bottom_spacing_recycler_view);
    }

    public boolean isFirstItemVisible() {
        return mLayoutManager.findFirstVisibleItemPosition() == 0;
    }

    @Override
    public boolean onInterceptTouchEvent(MotionEvent ev) {
        mGestureDetector.onTouchEvent(ev);
        return super.onInterceptTouchEvent(ev);
    }

    @Override
    public boolean onTouchEvent(MotionEvent ev) {
        // Action down would already have been handled in onInterceptTouchEvent
        if (ev.getActionMasked() != MotionEvent.ACTION_DOWN) {
            mGestureDetector.onTouchEvent(ev);
        }
        return super.onTouchEvent(ev);
    }

    @Override
    public void focusableViewAvailable(View v) {
        // To avoid odd jumps during NTP animation transitions, we do not attempt to give focus
        // to child views if this scroll view already has focus.
        if (hasFocus()) return;
        super.focusableViewAvailable(v);
    }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        // Fixes landscape transitions when unfocusing the URL bar: crbug.com/288546
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN;
        return super.onCreateInputConnection(outAttrs);
    }

    @Override
    protected void onLayout(boolean changed, int l, int t, int r, int b) {
        int numberViews = getChildCount();
        for (int i = 0; i < numberViews; ++i) {
            View view = getChildAt(i);
            NewTabPageViewHolder viewHolder = (NewTabPageViewHolder) getChildViewHolder(view);
            if (viewHolder == null) return;
            viewHolder.updateLayoutParams();
        }
        super.onLayout(changed, l, t, r, b);
    }

    public void setAboveTheFoldView(View aboveTheFoldView) {
        mAboveTheFoldView = aboveTheFoldView;
    }

    /** Scroll up from the cards' current position and snap to present the first one. */
    public void scrollToFirstCard() {
        // Offset the target scroll by the height of the omnibox (the top padding).
        final int targetScroll = mAboveTheFoldView.getHeight() - mAboveTheFoldView.getPaddingTop();
        // If (somehow) the peeking card is tapped while midway through the transition,
        // we need to account for how much we have already scrolled.
        smoothScrollBy(0, targetScroll - computeVerticalScrollOffset());
    }

    /**
     * Updates the space added at the end of the list to make sure the above/below the fold
     * distinction can be preserved.
     */
    public void refreshBottomSpacing() {
        ViewHolder bottomSpacingViewHolder = findBottomSpacer();

        // It might not be in the layout yet if it's not visible or ready to be displayed.
        if (bottomSpacingViewHolder == null) return;

        assert bottomSpacingViewHolder.getItemViewType() == NewTabPageListItem.VIEW_TYPE_SPACING;
        bottomSpacingViewHolder.itemView.requestLayout();
    }

    /**
     * Calculates the height of the bottom spacing item, such that there is always enough content
     * below the fold to push the header up to to the top of the screen.
     */
    int calculateBottomSpacing() {
        int firstVisiblePos = mLayoutManager.findFirstVisibleItemPosition();

        // We have enough items to fill the view, since the snap point item is not even visible.
        if (firstVisiblePos > getNewTabPageAdapter().getFirstHeaderPosition()) {
            return mMinBottomSpacing;
        }

        ViewHolder lastContentItem = findLastContentItem();
        ViewHolder firstHeader = findFirstHeader();

        int bottomSpacing = getHeight() - mToolbarHeight;
        if (lastContentItem == null || firstHeader == null) {
            // This can happen in several cases, where some elements are not visible and the
            // RecyclerView didn't already attach them. We handle it by just adding space to make
            // sure that we never run out and force the UI to jump around and get stuck in a
            // position that breaks the animations. The height will be properly adjusted at the
            // next pass. Known cases that make it necessary:
            //  - The card list is refreshed while the NTP is not shown, for example when changing
            //    the sync settings.
            //  - Dismissing a snippet and having the status card coming to take its place.
            //  - Refresh while being below the fold, for example by tapping the status card.

            if (firstHeader != null) bottomSpacing -= firstHeader.itemView.getTop();

            Log.w(TAG, "The RecyclerView items are not attached, can't determine the content "
                            + "height: snap=%s, last=%s. Using full height: %d ",
                    firstHeader, lastContentItem, bottomSpacing);
        } else {
            int contentHeight =
                    lastContentItem.itemView.getBottom() - firstHeader.itemView.getTop();
            bottomSpacing -= contentHeight - mCompensationHeight;
        }

        return Math.max(mMinBottomSpacing, bottomSpacing);
    }

    /**
     * Refresh the peeking state of the first card.
     */
    public void updatePeekingCard() {
        CardViewHolder firstCard = findFirstCard();
        if (firstCard == null) return;

        if (firstCard.itemView.isShown()) {
            if (findAboveTheFoldView() == null) return;
            firstCard.setCanPeek(findAboveTheFoldView().hasSpaceForPeekingCard());
            firstCard.updatePeek();
        }
    }

    /**
     * Show the snippets header when the user scrolls down and snippet articles starts reaching the
     * top of the screen.
     */
    public void updateSnippetsHeaderDisplay() {
        SnippetHeaderViewHolder header = findFirstHeader();
        if (header == null) return;

        if (findAboveTheFoldView() == null) return;
        header.setCanTransition(findAboveTheFoldView().hasSpaceForPeekingCard());
        // Start doing the calculations if the snippet header is currently shown on screen.
        header.updateDisplay();

        // Update the space at the bottom, which needs to know about the height of the header.
        refreshBottomSpacing();
    }

    private NewTabPageAdapter getNewTabPageAdapter() {
        return (NewTabPageAdapter) getAdapter();
    }

    /**
     * Finds the view holder for the first header.
     * @return The {@link ViewHolder} of the header, or null if it is not present.
     */
    private SnippetHeaderViewHolder findFirstHeader() {
        ViewHolder viewHolder =
                findViewHolderForAdapterPosition(getNewTabPageAdapter().getFirstHeaderPosition());
        if (!(viewHolder instanceof SnippetHeaderViewHolder)) return null;

        return (SnippetHeaderViewHolder) viewHolder;
    }

    /**
     * Finds the view holder for the first card.
     * @return The {@link ViewHolder} for the first card, or null if it is not present.
     */
    private CardViewHolder findFirstCard() {
        ViewHolder viewHolder =
                findViewHolderForAdapterPosition(getNewTabPageAdapter().getFirstCardPosition());
        if (!(viewHolder instanceof CardViewHolder)) return null;

        return (CardViewHolder) viewHolder;
    }

    /**
     * Finds the view holder for the last content item: a card or status indicator.
     * @return The {@link ViewHolder} of the last content item, or null if it is not present.
     */
    private ViewHolder findLastContentItem() {
        ViewHolder viewHolder = findViewHolderForAdapterPosition(
                getNewTabPageAdapter().getLastContentItemPosition());
        if (viewHolder instanceof CardViewHolder) return viewHolder;
        if (viewHolder instanceof ProgressViewHolder) return viewHolder;

        return null;
    }

    /**
     * Finds the view holder for the bottom spacer.
     * @return The {@link ViewHolder} of the bottom spacer, or null if it is not present.
     */
    private ViewHolder findBottomSpacer() {
        return findViewHolderForAdapterPosition(getNewTabPageAdapter().getBottomSpacerPosition());
    }

    /**
     * Finds the above the fold view.
     * @return The View for above the fold or null, if it is not present.
     */
    private NewTabPageLayout findAboveTheFoldView() {
        ViewHolder viewHolder =
                findViewHolderForAdapterPosition(getNewTabPageAdapter().getAboveTheFoldPosition());
        if (viewHolder == null) return null;

        View view = viewHolder.itemView;
        if (!(view instanceof NewTabPageLayout)) return null;

        return (NewTabPageLayout) view;
    }

    /** Called when an item is in the process of being removed from the view. */
    public void onItemDismissStarted(View itemView) {
        mCompensationHeight += itemView.getHeight();
        refreshBottomSpacing();
    }

    /** Called when an item has finished being removed from the view. */
    public void onItemDismissFinished(View itemView) {
        mCompensationHeight -= itemView.getHeight();
        assert mCompensationHeight >= 0;
    }

    /**
     * If the RecyclerView is currently scrolled to between regionStart and regionEnd, smooth scroll
     * out of the region. flipPoint is the threshold used to decide which bound of the region to
     * scroll to. It returns whether the view was scrolled.
     */
    private boolean scrollOutOfRegion(int regionStart, int flipPoint, int regionEnd) {
        final int currentScroll = computeVerticalScrollOffset();

        if (currentScroll < regionStart || currentScroll > regionEnd) return false;

        if (currentScroll < flipPoint) {
            smoothScrollBy(0, regionStart - currentScroll);
        } else {
            smoothScrollBy(0, regionEnd - currentScroll);
        }
        return true;
    }

    /**
     * If the RecyclerView is currently scrolled to between regionStart and regionEnd, smooth scroll
     * out of the region to the nearest edge.
     */
    private boolean scrollOutOfRegion(int regionStart, int regionEnd) {
        return scrollOutOfRegion(regionStart, (regionStart + regionEnd) / 2, regionEnd);
    }

    /**
     * Snaps the scroll point of the RecyclerView to prevent the user from scrolling to midway
     * through a transition and to allow peeking card behaviour.
     */
    public void snapScroll(View fakeBox, int parentScrollY, int parentHeight) {
        // Snap scroll to prevent resting in the middle of the omnibox transition.
        final int searchBoxTransitionLength = getResources()
                .getDimensionPixelSize(R.dimen.ntp_search_box_transition_length);
        int fakeBoxUpperBound = fakeBox.getTop() + fakeBox.getPaddingTop();
        if (scrollOutOfRegion(fakeBoxUpperBound - searchBoxTransitionLength, fakeBoxUpperBound)) {
            // The snap scrolling regions should never overlap.
            return;
        }

        // Snap scroll to prevent resting in the middle of the peeking card transition
        // and to allow the peeking card to peek a bit before snapping back.
        CardViewHolder peekingCardViewHolder = findFirstCard();
        if (peekingCardViewHolder != null && isFirstItemVisible()) {

            if (!peekingCardViewHolder.getCanPeek()) return;

            View peekingCardView = peekingCardViewHolder.itemView;
            View headerView = findFirstHeader().itemView;
            final int peekingHeight = getResources().getDimensionPixelSize(
                    R.dimen.snippets_padding_and_peeking_card_height);

            // |A + B - C| gives the offset of the peeking card relative to the Recycler View,
            // so scrolling to this point would put the peeking card at the top of the
            // screen. Remove the |headerView| height which gets dynamically increased with
            // scrolling.
            // |A + B - C - D| will scroll us so that the peeking card is just off the bottom
            // of the screen.
            // Finally, we get |A + B - C - D + E| because the transition starts from the
            // peeking card's resting point, which is |E| from the bottom of the screen.
            int start = peekingCardView.getTop()  // A.
                    + parentScrollY // B.
                    - headerView.getHeight()  // C.
                    - parentHeight  // D.
                    + peekingHeight;  // E.

            // The height of the region in which the the peeking card will snap.
            int snapScrollHeight = (peekingCardView.getHeight() / 2) + headerView.getHeight();

            scrollOutOfRegion(start,
                              start + snapScrollHeight,
                              start + snapScrollHeight);
        }
    }
}
