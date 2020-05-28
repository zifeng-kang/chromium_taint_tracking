// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/browsing_data/browsing_data_counter_utils.h"

#include "base/command_line.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browsing_data/cache_counter.h"
#include "chrome/browser/browsing_data/media_licenses_counter.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/common/pref_names.h"
#include "chrome/grit/generated_resources.h"
#include "components/browsing_data/core/counters/autofill_counter.h"
#include "components/browsing_data/core/counters/history_counter.h"
#include "components/browsing_data/core/counters/passwords_counter.h"
#include "components/browsing_data/core/pref_names.h"
#include "components/prefs/pref_service.h"
#include "ui/base/l10n/l10n_util.h"
#include "ui/base/text/bytes_formatting.h"

#if defined(ENABLE_EXTENSIONS)
#include "base/numerics/safe_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/browsing_data/hosted_apps_counter.h"
#endif

bool AreCountersEnabled() {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kEnableClearBrowsingDataCounters)) {
    return true;
  }

  if (base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableClearBrowsingDataCounters)) {
    return false;
  }

  // Enabled by default.
  return true;
}

// A helper function to display the size of cache in units of MB or higher.
// We need this, as 1 MB is the lowest nonzero cache size displayed by the
// counter.
base::string16 FormatBytesMBOrHigher(
    browsing_data::BrowsingDataCounter::ResultInt bytes) {
  if (ui::GetByteDisplayUnits(bytes) >= ui::DataUnits::DATA_UNITS_MEBIBYTE)
    return ui::FormatBytes(bytes);

  return ui::FormatBytesWithUnits(
      bytes, ui::DataUnits::DATA_UNITS_MEBIBYTE, true);
}

base::string16 GetCounterTextFromResult(
    const browsing_data::BrowsingDataCounter::Result* result) {
  base::string16 text;
  std::string pref_name = result->source()->GetPrefName();

  if (!result->Finished()) {
    // The counter is still counting.
    text = l10n_util::GetStringUTF16(IDS_CLEAR_BROWSING_DATA_CALCULATING);

  } else if (pref_name == browsing_data::prefs::kDeletePasswords ||
             pref_name == browsing_data::prefs::kDeleteDownloadHistory) {
    // Counters with trivially formatted result: passwords and downloads.
    browsing_data::BrowsingDataCounter::ResultInt count =
        static_cast<const browsing_data::BrowsingDataCounter::FinishedResult*>(
            result)
            ->Value();
    text = l10n_util::GetPluralStringFUTF16(
        pref_name == browsing_data::prefs::kDeletePasswords
            ? IDS_DEL_PASSWORDS_COUNTER
            : IDS_DEL_DOWNLOADS_COUNTER,
        count);

  } else if (pref_name == browsing_data::prefs::kDeleteCache) {
    // Cache counter.
    browsing_data::BrowsingDataCounter::ResultInt cache_size_bytes =
        static_cast<const browsing_data::BrowsingDataCounter::FinishedResult*>(
            result)
            ->Value();

    PrefService* prefs = result->source()->GetPrefs();
    browsing_data::TimePeriod time_period =
        static_cast<browsing_data::TimePeriod>(
            prefs->GetInteger(browsing_data::prefs::kDeleteTimePeriod));

    // Three cases: Nonzero result for the entire cache, nonzero result for
    // a subset of cache (i.e. a finite time interval), and almost zero (< 1MB).
    static const int kBytesInAMegabyte = 1024 * 1024;
    if (cache_size_bytes >= kBytesInAMegabyte) {
      base::string16 formatted_size = FormatBytesMBOrHigher(cache_size_bytes);
      text = time_period == browsing_data::ALL_TIME
                 ? formatted_size
                 : l10n_util::GetStringFUTF16(
                       IDS_DEL_CACHE_COUNTER_UPPER_ESTIMATE, formatted_size);
    } else {
      text = l10n_util::GetStringUTF16(IDS_DEL_CACHE_COUNTER_ALMOST_EMPTY);
    }

  } else if (pref_name == browsing_data::prefs::kDeleteBrowsingHistory) {
    // History counter.
    const browsing_data::HistoryCounter::HistoryResult* history_result =
        static_cast<const browsing_data::HistoryCounter::HistoryResult*>(
            result);
    browsing_data::BrowsingDataCounter::ResultInt local_item_count =
        history_result->Value();
    bool has_synced_visits = history_result->has_synced_visits();

    text = has_synced_visits
        ? l10n_util::GetPluralStringFUTF16(
              IDS_DEL_BROWSING_HISTORY_COUNTER_SYNCED, local_item_count)
        : l10n_util::GetPluralStringFUTF16(
              IDS_DEL_BROWSING_HISTORY_COUNTER, local_item_count);

  } else if (pref_name == browsing_data::prefs::kDeleteFormData) {
    // Autofill counter.
    const browsing_data::AutofillCounter::AutofillResult* autofill_result =
        static_cast<const browsing_data::AutofillCounter::AutofillResult*>(
            result);
    browsing_data::AutofillCounter::ResultInt num_suggestions =
        autofill_result->Value();
    browsing_data::AutofillCounter::ResultInt num_credit_cards =
        autofill_result->num_credit_cards();
    browsing_data::AutofillCounter::ResultInt num_addresses =
        autofill_result->num_addresses();

    std::vector<base::string16> displayed_strings;

    if (num_credit_cards) {
      displayed_strings.push_back(l10n_util::GetPluralStringFUTF16(
          IDS_DEL_AUTOFILL_COUNTER_CREDIT_CARDS, num_credit_cards));
    }
    if (num_addresses) {
      displayed_strings.push_back(l10n_util::GetPluralStringFUTF16(
          IDS_DEL_AUTOFILL_COUNTER_ADDRESSES, num_addresses));
    }
    if (num_suggestions) {
      // We use a different wording for autocomplete suggestions based on the
      // length of the entire string.
      switch (displayed_strings.size()) {
        case 0:
          displayed_strings.push_back(l10n_util::GetPluralStringFUTF16(
              IDS_DEL_AUTOFILL_COUNTER_SUGGESTIONS, num_suggestions));
          break;
        case 1:
          displayed_strings.push_back(l10n_util::GetPluralStringFUTF16(
              IDS_DEL_AUTOFILL_COUNTER_SUGGESTIONS_LONG, num_suggestions));
          break;
        case 2:
          displayed_strings.push_back(l10n_util::GetPluralStringFUTF16(
              IDS_DEL_AUTOFILL_COUNTER_SUGGESTIONS_SHORT, num_suggestions));
          break;
        default:
          NOTREACHED();
      }
    }

    // Construct the resulting string from the sections in |displayed_strings|.
    switch (displayed_strings.size()) {
      case 0:
        text = l10n_util::GetStringUTF16(IDS_DEL_AUTOFILL_COUNTER_EMPTY);
        break;
      case 1:
        text = displayed_strings[0];
        break;
      case 2:
        text = l10n_util::GetStringFUTF16(IDS_DEL_AUTOFILL_COUNTER_TWO_TYPES,
                                          displayed_strings[0],
                                          displayed_strings[1]);
        break;
      case 3:
        text = l10n_util::GetStringFUTF16(IDS_DEL_AUTOFILL_COUNTER_THREE_TYPES,
                                          displayed_strings[0],
                                          displayed_strings[1],
                                          displayed_strings[2]);
        break;
      default:
        NOTREACHED();
    }

  } else if (pref_name == browsing_data::prefs::kDeleteMediaLicenses) {
    const MediaLicensesCounter::MediaLicenseResult* media_license_result =
        static_cast<const MediaLicensesCounter::MediaLicenseResult*>(result);
    if (media_license_result->Value() > 0) {
      text = l10n_util::GetStringFUTF16(
          IDS_DEL_MEDIA_LICENSES_COUNTER_SITE_COMMENT,
          base::UTF8ToUTF16(media_license_result->GetOneOrigin()));
    } else {
      text = l10n_util::GetStringUTF16(
          IDS_DEL_MEDIA_LICENSES_COUNTER_GENERAL_COMMENT);
    }

#if defined(ENABLE_EXTENSIONS)
  } else if (pref_name == browsing_data::prefs::kDeleteHostedAppsData) {
    // Hosted apps counter.
    const HostedAppsCounter::HostedAppsResult* hosted_apps_result =
        static_cast<const HostedAppsCounter::HostedAppsResult*>(result);
    int hosted_apps_count = hosted_apps_result->Value();

    DCHECK_GE(hosted_apps_result->Value(),
              base::checked_cast<browsing_data::BrowsingDataCounter::ResultInt>(
                  hosted_apps_result->examples().size()));

    std::vector<base::string16> replacements;
    if (hosted_apps_count > 0) {
      replacements.push_back(                                     // App1,
          base::UTF8ToUTF16(hosted_apps_result->examples()[0]));
    }
    if (hosted_apps_count > 1) {
      replacements.push_back(
          base::UTF8ToUTF16(hosted_apps_result->examples()[1]));  // App2,
    }
    if (hosted_apps_count > 2) {
      replacements.push_back(l10n_util::GetPluralStringFUTF16(  // and X-2 more.
          IDS_DEL_HOSTED_APPS_COUNTER_AND_X_MORE,
          hosted_apps_count - 2));
    }

    // The output string has both the number placeholder (#) and substitution
    // placeholders ($1, $2, $3). First fetch the correct plural string first,
    // then substitute the $ placeholders.
    text = base::ReplaceStringPlaceholders(
        l10n_util::GetPluralStringFUTF16(
            IDS_DEL_HOSTED_APPS_COUNTER, hosted_apps_count),
        replacements,
        nullptr);
#endif
  }

  return text;
}

bool GetDeletionPreferenceFromDataType(
    browsing_data::BrowsingDataType data_type,
    std::string* out_pref) {
  switch (data_type) {
    case browsing_data::HISTORY:
      *out_pref = browsing_data::prefs::kDeleteBrowsingHistory;
      return true;
    case browsing_data::CACHE:
      *out_pref = browsing_data::prefs::kDeleteCache;
      return true;
    case browsing_data::COOKIES:
      *out_pref = browsing_data::prefs::kDeleteCookies;
      return true;
    case browsing_data::PASSWORDS:
      *out_pref = browsing_data::prefs::kDeletePasswords;
      return true;
    case browsing_data::FORM_DATA:
      *out_pref = browsing_data::prefs::kDeleteFormData;
      return true;
    case browsing_data::BOOKMARKS:
      // Bookmarks are deleted on the Android side. No corresponding deletion
      // preference.
      return false;
    case browsing_data::NUM_TYPES:
      // This is not an actual type.
      NOTREACHED();
      return false;
  }
  NOTREACHED();
  return false;
}
