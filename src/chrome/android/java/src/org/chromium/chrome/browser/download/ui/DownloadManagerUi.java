// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.download.ui;

import android.app.Activity;
import android.content.Context;
import android.content.res.Resources;
import android.graphics.drawable.Drawable;
import android.os.AsyncTask;
import android.os.Environment;
import android.os.StatFs;
import android.support.v4.widget.DrawerLayout;
import android.support.v7.app.ActionBarDrawerToggle;
import android.support.v7.widget.LinearLayoutManager;
import android.support.v7.widget.RecyclerView;
import android.support.v7.widget.Toolbar;
import android.support.v7.widget.Toolbar.OnMenuItemClickListener;
import android.text.format.Formatter;
import android.util.AttributeSet;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.MenuItem;
import android.view.View;
import android.view.ViewGroup;
import android.widget.AdapterView;
import android.widget.BaseAdapter;
import android.widget.ListView;
import android.widget.ProgressBar;
import android.widget.TextView;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.ContextUtils;
import org.chromium.base.Log;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.download.DownloadManagerService;
import org.chromium.chrome.browser.widget.FadingShadow;
import org.chromium.chrome.browser.widget.FadingShadowView;
import org.chromium.chrome.browser.widget.TintedDrawable;
import org.chromium.ui.base.DeviceFormFactor;

import java.io.File;

/**
 * Displays and manages the UI for the download manager.
 */
public class DownloadManagerUi extends DrawerLayout implements OnMenuItemClickListener {
    private static final String TAG = "download_ui";

    /**
     * Icons and labels for each filter in the menu.
     *
     * Changing the ordering of these items requires changing the FILTER_* values in
     * {@link DownloadHistoryAdapter}.
     */
    private static final int[][] FILTER_LIST = new int[][] {
        {R.drawable.ic_get_app_white_24dp, R.string.download_manager_ui_all_downloads},
        {R.drawable.ic_drive_site_white_24dp, R.string.download_manager_ui_pages},
        {R.drawable.ic_play_arrow_white_24dp, R.string.download_manager_ui_video},
        {R.drawable.ic_music_note_white_24dp, R.string.download_manager_ui_audio},
        {R.drawable.ic_image_white_24dp, R.string.download_manager_ui_images},
        {R.drawable.ic_drive_text_white_24dp, R.string.download_manager_ui_documents},
        {R.drawable.ic_drive_file_white_24dp, R.string.download_manager_ui_other}
    };

    /** Responds to events occurring in the DownloadManagerUi. */
    public interface DownloadManagerUiDelegate {
        /** Called when the close button is clicked. */
        void onCloseButtonClicked(DownloadManagerUi ui);
    }

    /** Manages the display of space used by the downloads. */
    private final class SpaceDisplay extends RecyclerView.AdapterDataObserver {
        private final AsyncTask<Void, Void, Long> mFileSystemBytesTask;

        private TextView mSpaceUsedTextView;
        private TextView mSpaceTotalTextView;
        private ProgressBar mSpaceBar;
        private long mFileSystemBytes = Long.MAX_VALUE;

        SpaceDisplay(final ViewGroup parent) {
            mSpaceUsedTextView = (TextView) parent.findViewById(R.id.space_used_display);
            mSpaceTotalTextView = (TextView) parent.findViewById(R.id.space_total_display);
            mSpaceBar = (ProgressBar) parent.findViewById(R.id.space_bar);
            mFileSystemBytesTask = createStorageSizeTask().execute();
        }

        private AsyncTask<Void, Void, Long> createStorageSizeTask() {
            return new AsyncTask<Void, Void, Long>() {
                @Override
                protected Long doInBackground(Void... params) {
                    File downloadDirectory = Environment.getExternalStoragePublicDirectory(
                            Environment.DIRECTORY_DOWNLOADS);

                    // Create the downloads directory, if necessary.
                    if (!downloadDirectory.exists()) {
                        try {
                            // mkdirs() can fail, so we still need to check if the directory exists
                            // later.
                            downloadDirectory.mkdirs();
                        } catch (SecurityException e) {
                            Log.e(TAG, "SecurityException when creating download directory.", e);
                        }
                    }

                    // Determine how much space is available on the storage device where downloads
                    // reside.  If the downloads directory doesn't exist, it is likely that the user
                    // doesn't have an SD card installed.
                    long fileSystemBytes = 0;
                    if (downloadDirectory.exists()) {
                        StatFs statFs = new StatFs(downloadDirectory.getPath());
                        long totalBlocks = ApiCompatibilityUtils.getBlockCount(statFs);
                        long blockSize = ApiCompatibilityUtils.getBlockSize(statFs);
                        fileSystemBytes = totalBlocks * blockSize;
                    } else {
                        Log.e(TAG, "Download directory doesn't exist.");
                    }
                    return fileSystemBytes;
                }
            };
        }

        @Override
        public void onChanged() {
            Context context = mSpaceUsedTextView.getContext();

            if (mFileSystemBytes == Long.MAX_VALUE) {
                try {
                    mFileSystemBytes = mFileSystemBytesTask.get();
                } catch (Exception e) {
                    Log.e(TAG, "Failed to get file system size.");
                }

                // Display how big the storage is.
                String fileSystemReadable = Formatter.formatFileSize(context, mFileSystemBytes);
                mSpaceTotalTextView.setText(context.getString(
                        R.string.download_manager_ui_space_used, fileSystemReadable));
            }

            // Indicate how much space has been used by downloads.
            long usedBytes = mHistoryAdapter.getTotalDownloadSize();
            int percentage =
                    mFileSystemBytes == 0 ? 0 : (int) (100.0 * usedBytes / mFileSystemBytes);
            mSpaceBar.setProgress(percentage);
            mSpaceUsedTextView.setText(Formatter.formatFileSize(context, usedBytes));
        }
    }

    /** Allows selecting an item from a list displayed in the drawer. */
    private final class FilterAdapter
            extends BaseAdapter implements AdapterView.OnItemClickListener {

        private final DownloadManagerUi mRootLayout;
        private final int mSelectedBackgroundColor;
        private int mSelectedIndex;

        public FilterAdapter(DownloadManagerUi parent) {
            mRootLayout = parent;
            mSelectedBackgroundColor = ApiCompatibilityUtils.getColor(
                    parent.getContext().getResources(), R.color.default_primary_color);
        }

        @Override
        public int getCount() {
            return FILTER_LIST.length;
        }

        @Override
        public Object getItem(int position) {
            return FILTER_LIST[position];
        }

        @Override
        public long getItemId(int position) {
            return position;
        }

        @Override
        public View getView(int position, View convertView, ViewGroup parent) {
            Context context = mRootLayout.getContext();
            Resources resources = context.getResources();

            TextView labelView = null;
            if (convertView instanceof TextView) {
                labelView = (TextView) convertView;
            } else {
                labelView = (TextView) LayoutInflater.from(context).inflate(
                        R.layout.download_manager_ui_drawer_filter, null);
            }

            int iconId = FILTER_LIST[position][0];
            labelView.setText(FILTER_LIST[position][1]);

            Drawable iconDrawable = null;
            if (position == mSelectedIndex) {
                // Highlight the selected item by changing the foreground and background colors.
                labelView.setBackgroundColor(mSelectedBackgroundColor);
                iconDrawable = TintedDrawable.constructTintedDrawable(
                        resources, iconId, R.color.light_active_color);
                labelView.setTextColor(
                        ApiCompatibilityUtils.getColor(resources, R.color.light_active_color));
            } else {
                // Draw the item normally.
                labelView.setBackground(null);
                iconDrawable = TintedDrawable.constructTintedDrawable(
                        resources, iconId, R.color.descriptive_text_color);
                labelView.setTextColor(
                        ApiCompatibilityUtils.getColor(resources, R.color.default_text_color));
            }

            labelView.setCompoundDrawablesWithIntrinsicBounds(iconDrawable, null, null, null);
            return labelView;
        }

        @Override
        public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
            // Update the dataset based on the filter.
            int filter = position;
            mHistoryAdapter.filter(filter);
            mRootLayout.onFilterChanged(filter);

            // Update the sidebar itself.
            mSelectedIndex = position;
            notifyDataSetChanged();
            mRootLayout.closeDrawer(Gravity.START);
        }
    }

    private final DownloadHistoryAdapter mHistoryAdapter;
    private final FilterAdapter mFilterAdapter;

    private DownloadManagerUiDelegate mDelegate;
    private ActionBarDrawerToggle mActionBarDrawerToggle;
    private DownloadManagerToolbar mToolbar;
    private SpaceDisplay mSpaceDisplay;
    private ListView mFilterView;
    private RecyclerView mRecyclerView;

    private boolean mIsDownloadHistoryInitialized;

    public DownloadManagerUi(Context context, AttributeSet attrs) {
        super(context, attrs);
        mHistoryAdapter = new DownloadHistoryAdapter();
        mFilterAdapter = new FilterAdapter(this);
    }

    @Override
    protected void onFinishInflate() {
        super.onFinishInflate();

        mToolbar = (DownloadManagerToolbar) findViewById(R.id.action_bar);
        mToolbar.setOnMenuItemClickListener(this);

        mSpaceDisplay = new SpaceDisplay(this);
        mFilterView = (ListView) findViewById(R.id.section_list);
        mFilterView.setAdapter(mFilterAdapter);
        mFilterView.setOnItemClickListener(mFilterAdapter);

        mRecyclerView = (RecyclerView) findViewById(R.id.recycler_view);
        mRecyclerView.setAdapter(mHistoryAdapter);
        mRecyclerView.setLayoutManager(new LinearLayoutManager(getContext()));
    }

    @Override
    public void onAttachedToWindow() {
        super.onAttachedToWindow();
        getDownloadManagerService().addDownloadHistoryAdapter(mHistoryAdapter);
        mHistoryAdapter.registerAdapterDataObserver(mSpaceDisplay);

        // Get information about all the downloads if it hasn't already been done.
        if (!mIsDownloadHistoryInitialized) {
            mIsDownloadHistoryInitialized = true;
            getDownloadManagerService().getAllDownloads();
        }
    }

    @Override
    public void onDetachedFromWindow() {
        getDownloadManagerService().removeDownloadHistoryAdapter(mHistoryAdapter);
        mHistoryAdapter.unregisterAdapterDataObserver(mSpaceDisplay);
        super.onDetachedFromWindow();
    }

    /**
     * Called when the UI needs to react to the back button being pressed.
     *
     * @return Whether the back button was handled.
     */
    public boolean onBackPressed() {
        if (isDrawerOpen(Gravity.START)) {
            closeDrawer(Gravity.START);
            return true;
        }
        return false;
    }

    /**
     * Initializes the UI for the given Activity.
     *
     * @param activity Activity that is showing the UI.
     */
    public void initialize(DownloadManagerUiDelegate delegate, Activity activity) {
        mDelegate = delegate;

        mActionBarDrawerToggle = new ActionBarDrawerToggle(activity,
                this, (Toolbar) findViewById(R.id.action_bar),
                R.string.accessibility_bookmark_drawer_toggle_btn_open,
                R.string.accessibility_bookmark_drawer_toggle_btn_close);
        addDrawerListener(mActionBarDrawerToggle);
        mActionBarDrawerToggle.syncState();

        FadingShadowView shadow = (FadingShadowView) findViewById(R.id.shadow);
        if (DeviceFormFactor.isLargeTablet(getContext())) {
            shadow.setVisibility(View.GONE);
        } else {
            shadow.init(ApiCompatibilityUtils.getColor(getResources(),
                    R.color.toolbar_shadow_color), FadingShadow.POSITION_TOP);
        }

        mSpaceDisplay.onChanged();
        mToolbar.setTitle(R.string.menu_downloads);
    }

    @Override
    public boolean onMenuItemClick(MenuItem item) {
        if (item.getItemId() == R.id.close_menu_id && mDelegate != null) {
            mDelegate.onCloseButtonClicked(DownloadManagerUi.this);
            return true;
        }
        return false;
    }

    /** Called when the filter has been changed by the user. */
    private void onFilterChanged(int filetype) {
        if (filetype == 0) {
            mToolbar.setTitle(R.string.menu_downloads);
        } else {
            mToolbar.setTitle(FILTER_LIST[filetype][1]);
        }
    }

    private static DownloadManagerService getDownloadManagerService() {
        return DownloadManagerService.getDownloadManagerService(
                ContextUtils.getApplicationContext());
    }

}
