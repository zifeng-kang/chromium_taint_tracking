// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.ntp;

import android.content.Context;
import android.content.res.Resources;
import android.util.AttributeSet;
import android.view.View;
import android.widget.LinearLayout;

import org.chromium.chrome.R;

/**
 * Layout for the new tab page. This positions the page elements in the correct vertical positions.
 * There are no separate phone and tablet UIs; this layout adapts based on the available space.
 */
public class NewTabPageLayout extends LinearLayout {

    // Space permitting, the spacers will grow from 0dp to the heights given below. If there is
    // additional space, it will be distributed evenly between the top and bottom spacers.
    private static final float TOP_SPACER_HEIGHT_DP = 44f;
    private static final float MIDDLE_SPACER_HEIGHT_DP = 24f;
    private static final float BOTTOM_SPACER_HEIGHT_DP = 44f;
    private static final float TOTAL_SPACER_HEIGHT_DP = TOP_SPACER_HEIGHT_DP
            + MIDDLE_SPACER_HEIGHT_DP + BOTTOM_SPACER_HEIGHT_DP;

    private final int mTopSpacerIdealHeight;
    private final int mMiddleSpacerIdealHeight;
    private final int mBottomSpacerIdealHeight;
    private final int mTotalSpacerIdealHeight;
    private final int mMostVisitedLayoutBleed;
    private final int mPeekingCardHeight;
    private final int mTabStripHeight;

    private int mParentViewportHeight;
    private int mSearchboxViewShadowWidth;
    private boolean mHasSpaceForPeekingCard;

    private boolean mCardsUiEnabled;
    private View mTopSpacer;  // Spacer above search logo.
    private View mMiddleSpacer;  // Spacer between toolbar and Most Likely.
    private View mBottomSpacer;  // Spacer below Most Likely.

    // Separate spacer below Most Likely to add enough space so the user can scroll with Most Likely
    // at the top of the screen.
    private View mScrollCompensationSpacer;

    private LogoView mSearchProviderLogoView;
    private View mSearchBoxView;
    private MostVisitedLayout mMostVisitedLayout;
    /**
     * Constructor for inflating from XML.
     */
    public NewTabPageLayout(Context context, AttributeSet attrs) {
        super(context, attrs);
        Resources res = getResources();
        float density = res.getDisplayMetrics().density;
        mTopSpacerIdealHeight = Math.round(density * TOP_SPACER_HEIGHT_DP);
        mMiddleSpacerIdealHeight = Math.round(density * MIDDLE_SPACER_HEIGHT_DP);
        mBottomSpacerIdealHeight = Math.round(density * BOTTOM_SPACER_HEIGHT_DP);
        mTotalSpacerIdealHeight = Math.round(density * TOTAL_SPACER_HEIGHT_DP);
        mMostVisitedLayoutBleed = res.getDimensionPixelSize(R.dimen.most_visited_layout_bleed);
        mPeekingCardHeight =
                res.getDimensionPixelSize(R.dimen.snippets_padding_and_peeking_card_height);
        mTabStripHeight = res.getDimensionPixelSize(R.dimen.tab_strip_height);
    }

    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();
        mTopSpacer = findViewById(R.id.ntp_top_spacer);
        mMiddleSpacer = findViewById(R.id.ntp_middle_spacer);
        mBottomSpacer = findViewById(R.id.ntp_bottom_spacer);
        mScrollCompensationSpacer = findViewById(R.id.ntp_scroll_spacer);
        mSearchProviderLogoView = (LogoView) findViewById(R.id.search_provider_logo);
        mSearchBoxView = findViewById(R.id.search_box);
        mMostVisitedLayout = (MostVisitedLayout) findViewById(R.id.most_visited_layout);
        setSearchBoxStyle();
    }

    /**
     * Specifies the height of the parent's viewport for the container view of this View.
     *
     * As this is required in onMeasure, we can not rely on the parent having the proper
     * size set yet and thus must be told explicitly of this size.
     *
     * This View takes into account the presence of the tab strip height for tablets.
     */
    public void setParentViewportHeight(int height) {
        mParentViewportHeight = height;
    }

    /**
     * Sets whether the cards UI is enabled.
     */
    public void setUseCardsUiEnabled(boolean useCardsUi) {
        mCardsUiEnabled = useCardsUi;
    }

    /**
     * @return Whether the cards UI has space for a peeking card to display.
     */
    public boolean hasSpaceForPeekingCard() {
        return mHasSpaceForPeekingCard;
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        // Remove the scroll spacer from the layout so the weighted children can be measured
        // correctly.
        mScrollCompensationSpacer.setVisibility(View.GONE);
        super.onMeasure(widthMeasureSpec, heightMeasureSpec);

        if (getMeasuredHeight() > mParentViewportHeight) {
            mHasSpaceForPeekingCard = false;
            // No scroll spacing needed when using the cards ui.
            if (!mCardsUiEnabled) {
                // This layout is bigger than its parent's viewport, so the user will need to scroll
                // to see all of it. Extra spacing should be added at the bottom so the user can
                // scroll until Most Visited is at the top.

                // The top, middle, and bottom spacers should have a measured height of 0 at this
                // point since they use weights to set height, and there was no extra space.
                assert mTopSpacer.getMeasuredHeight() == 0;
                assert mMiddleSpacer.getMeasuredHeight() == 0;
                assert mBottomSpacer.getMeasuredHeight() == 0;

                final int topOfMostVisited = calculateTopOfMostVisited();
                final int belowTheFoldHeight = getMeasuredHeight() - mParentViewportHeight;
                if (belowTheFoldHeight < topOfMostVisited) {
                    // Include the scroll spacer in the layout and call super.onMeasure again so it
                    // is measured.
                    mScrollCompensationSpacer.getLayoutParams().height =
                            topOfMostVisited - belowTheFoldHeight;

                    mScrollCompensationSpacer.setVisibility(View.INVISIBLE);
                    super.onMeasure(widthMeasureSpec, heightMeasureSpec);
                }
            }
        } else {
            mHasSpaceForPeekingCard = true;
            // This layout is smaller than or equal to its parent viewport. Redistribute any
            // weighted space.
            if (mCardsUiEnabled) {
                // Call super.onMeasure with mode EXACTLY and the target height to allow the top
                // spacer (which has a weight of 1) to grow and take up the remaining space.
                int targetHeight = Math.max(getMeasuredHeight(),
                        mParentViewportHeight - mPeekingCardHeight - mTabStripHeight);
                heightMeasureSpec = MeasureSpec.makeMeasureSpec(targetHeight, MeasureSpec.EXACTLY);
                super.onMeasure(widthMeasureSpec, heightMeasureSpec);
            }
            distributeExtraSpace(mTopSpacer.getMeasuredHeight());
        }

        // Make the search box and logo as wide as the most visited items.
        if (mMostVisitedLayout.getVisibility() != GONE) {
            final int width = mMostVisitedLayout.getMeasuredWidth() - mMostVisitedLayoutBleed;
            measureExactly(mSearchBoxView, width + mSearchboxViewShadowWidth,
                    mSearchBoxView.getMeasuredHeight());
            measureExactly(
                    mSearchProviderLogoView, width, mSearchProviderLogoView.getMeasuredHeight());
        }
    }

    /**
     * Calculate the vertical position of Most Visited.
     * This method does not use mMostVisitedLayout.getTop(), so can be called in onMeasure.
     */
    private int calculateTopOfMostVisited() {
        // Manually add the heights (and margins) of all children above Most Visited.
        int top = 0;
        int mostVisitedIndex = indexOfChild(mMostVisitedLayout);
        for (int i = 0; i < mostVisitedIndex; i++) {
            View child = getChildAt(i);
            MarginLayoutParams params = (MarginLayoutParams) child.getLayoutParams();

            if (child.getVisibility() != View.GONE) {
                top += params.topMargin + child.getMeasuredHeight() + params.bottomMargin;
            }
        }
        top += ((MarginLayoutParams) mMostVisitedLayout.getLayoutParams()).topMargin;
        return top;
    }

    /**
     * Set the search box style, adding a shadow if required.
     */
    private void setSearchBoxStyle() {
        if (!NtpColorUtils.shouldUseMaterialColors()) return;

        Resources resources = getContext().getResources();

        // Adjust the margins to account for the bigger size due to the shadow.
        MarginLayoutParams layoutParams = (MarginLayoutParams) mSearchBoxView.getLayoutParams();
        layoutParams.setMargins(
                resources.getDimensionPixelSize(R.dimen.ntp_search_box_material_margin_left),
                resources.getDimensionPixelSize(R.dimen.ntp_search_box_material_margin_top),
                resources.getDimensionPixelSize(R.dimen.ntp_search_box_material_margin_right),
                resources.getDimensionPixelSize(R.dimen.ntp_search_box_material_margin_bottom));
        layoutParams.height = resources
                .getDimensionPixelSize(R.dimen.ntp_search_box_material_height);
        // Width will be adjusted in onMeasure();
        mSearchboxViewShadowWidth = resources
                .getDimensionPixelOffset(R.dimen.ntp_search_box_material_extra_width);

        mSearchBoxView.setBackgroundResource(R.drawable.textbox);
        mSearchBoxView.setPadding(
                resources.getDimensionPixelSize(R.dimen.ntp_search_box_material_padding_left),
                resources.getDimensionPixelSize(R.dimen.ntp_search_box_material_padding_top),
                resources.getDimensionPixelSize(R.dimen.ntp_search_box_material_padding_right),
                resources.getDimensionPixelSize(R.dimen.ntp_search_box_material_padding_bottom));
    }

    /**
     * Distribute extra vertical space between the three spacer views. Doing this here allows for
     * more sophisticated constraints than in xml.
     * @param extraHeight The amount of extra space, in pixels.
     */
    private void distributeExtraSpace(int extraHeight) {
        int topSpacerHeight;
        int middleSpacerHeight;
        int bottomSpacerHeight;

        if (extraHeight < mTotalSpacerIdealHeight) {
            // The spacers will be less than their ideal height, shrink them proportionally.
            topSpacerHeight =
                    Math.round(extraHeight * (TOP_SPACER_HEIGHT_DP / TOTAL_SPACER_HEIGHT_DP));
            middleSpacerHeight =
                    Math.round(extraHeight * (MIDDLE_SPACER_HEIGHT_DP / TOTAL_SPACER_HEIGHT_DP));
            bottomSpacerHeight = extraHeight - topSpacerHeight - middleSpacerHeight;
        } else {
            // Distribute remaining space evenly between the top and bottom spacers.
            extraHeight -= mTotalSpacerIdealHeight;
            topSpacerHeight = mTopSpacerIdealHeight + extraHeight / 2;
            middleSpacerHeight = mMiddleSpacerIdealHeight;
            bottomSpacerHeight = mBottomSpacerIdealHeight + extraHeight / 2;
        }

        measureExactly(mTopSpacer, 0, topSpacerHeight);
        measureExactly(mMiddleSpacer, 0, middleSpacerHeight);
        measureExactly(mBottomSpacer, 0, bottomSpacerHeight);
    }

    /**
     * Convenience method to call measure() on the given View with MeasureSpecs converted from the
     * given dimensions (in pixels) with MeasureSpec.EXACTLY.
     */
    private static void measureExactly(View view, int widthPx, int heightPx) {
        view.measure(MeasureSpec.makeMeasureSpec(widthPx, MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(heightPx, MeasureSpec.EXACTLY));
    }
}
