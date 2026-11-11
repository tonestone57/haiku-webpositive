# WebPositive Code Audit: Bugs, Errors, Improvements and Tasks

## 1. Overview
This document contains the complete audit and analysis of `src/apps/webpositive`, covering bugs, resource leaks, concurrency hazards, performance bottlenecks, code quality issues, and an actionable task list.

---

## 2. Identified Bugs & Errors

### Bug 1: Background Tab URL Bar Overwrite
- **File:** `src/apps/webpositive/BrowserWindow.cpp:1548`
- **Component:** `BrowserWindow::LoadNegotiating`
- **Severity:** High
- **Description:** In `BrowserWindow::LoadNegotiating()`, while updating the background tab's user data is gated behind `if (view != CurrentWebView())`, the call `fURLInputGroup->SetText(url.String())` is executed unconditionally. If a background tab loads a new URL or redirection, it immediately replaces the URL displayed in the user's active window/tab.
- **Fix:** Restrict `fURLInputGroup->SetText(url.String())` to only execute when `view == CurrentWebView()`.

### Bug 2: Bookmark Name Collision Accumulator Loop
- **File:** `src/apps/webpositive/BrowserWindow.cpp:1986-1996`
- **Component:** `BrowserWindow::_CreateBookmark`
- **Severity:** Medium
- **Description:** When generating a unique filename for duplicate bookmarks, the code loops:
  ```cpp
  int32 tries = 1;
  while (entry.Exists()) {
      fileName << " " << tries++;
      entryPath = path;
      status = entryPath.Append(fileName);
      ...
  }
  ```
  `fileName` is never reset to the base title. On consecutive iterations, it appends each attempt number, yielding names such as `"Title 1"`, `"Title 1 2"`, `"Title 1 2 3"`.
- **Fix:** Preserve the base filename before the loop and format `fileName` as `baseName + " " + tries++`.

### Bug 3: UTF-8 Character Count vs Byte Indexing in Smart URL Parser
- **File:** `src/apps/webpositive/BrowserWindow.cpp:2706-2716`
- **Component:** `BrowserWindow::_SmartURLHandler`
- **Severity:** Medium
- **Description:** The loop bound is evaluated with `url.CountChars()` (number of UTF-8 characters), but array indexing `url[i]` accesses raw byte offsets:
  ```cpp
  for (int32 i = 0; i < url.CountChars(); i++) {
      if (url[i] == '.') ...
  ```
  For internationalized domain names (IDN) or URLs with multi-byte characters, `CountChars() < Length()`, causing the loop to exit prematurely before scanning the full domain or path.
- **Fix:** Use byte length `url.Length()` for byte indexing or decode with `BUnicodeChar`.

### Bug 4: Autocompleter Operator Precedence Logic Flaw
- **File:** `src/apps/webpositive/autocompletion/AutoCompleterDefaultImpl.cpp:186-187`
- **Component:** `BDefaultCompletionStyle::EditViewStateChanged`
- **Severity:** Medium
- **Description:**
  ```cpp
  if (fChoiceModel->CountChoices() > 1 || (fChoiceModel->CountChoices() == 1
          && pattern.ICompare(fChoiceModel->ChoiceAt(0)->Text())) != 0) {
  ```
  The parentheses wrap the entire expression `(fChoiceModel->CountChoices() == 1 && pattern.ICompare(...)) != 0`, comparing the boolean conjunction to `!= 0` rather than checking `pattern.ICompare(...) != 0`.
- **Fix:** Adjust parentheses to `(fChoiceModel->CountChoices() == 1 && pattern.ICompare(...) != 0)`.

### Bug 5: `baseURL()` Substring Bounds & Crashes
- **File:** `src/apps/webpositive/support/BaseURL.cpp:10-16`
- **Component:** `baseURL`
- **Severity:** Medium
- **Description:**
  ```cpp
  int32 baseURLStart = string.FindFirst("://") + 3;
  int32 baseURLEnd = string.FindFirst("/", baseURLStart + 1);
  BString result;
  result.SetTo(string.String() + baseURLStart, baseURLEnd - baseURLStart);
  ```
  If `"://"` is missing, `FindFirst` returns `-1`, setting `baseURLStart = 2`. If there is no trailing `/`, `baseURLEnd = -1`, resulting in a negative length passed to `SetTo()` and undefined behavior or memory crash.
- **Fix:** Check for `baseURLStart >= 3` and handle `baseURLEnd == B_ERROR` by using the remaining string length.

### Bug 6: Strict Weak Ordering Violation in History Item Comparison
- **File:** `src/apps/webpositive/BrowsingHistory.cpp:67-73`
- **Component:** `BrowsingHistoryItem::operator<`
- **Severity:** High
- **Description:**
  ```cpp
  return fDateTime < other.fDateTime || fURL < other.fURL;
  ```
  Evaluating `(A.time < B.time || A.url < B.url)` violates strict weak ordering. When `A.time < B.time` and `A.url > B.url`, `A < B` is true, but `B < A` can simultaneously evaluate to true. In standard sort algorithms, this causes non-deterministic ordering, infinite loops, or crashes.
- **Fix:** Implement hierarchical comparison: compare `fDateTime` first, and if equal, compare `fURL`.

### Bug 7: CookieWindow Domain List Memory Leak
- **File:** `src/apps/webpositive/CookieWindow.cpp:214-217`
- **Component:** `CookieWindow::_BuildDomainList`
- **Severity:** Medium
- **Description:**
  ```cpp
  for (int i = fDomains->FullListCountItems() - 1; i >= 1; i--) {
      delete fDomains->FullListItemAt(i);
  }
  fDomains->MakeEmpty();
  ```
  The loop stops at `i >= 1`, leaving index `0` undeleted. Every refresh cycle leaks the first domain item.
- **Fix:** Change loop condition to `i >= 0`.

### Bug 8: History Submenu Inaccurate Substring Match
- **File:** `src/apps/webpositive/BrowserWindow.cpp:2260`
- **Component:** `addItemToMenuOrSubmenu`
- **Severity:** Low
- **Description:** `label.FindFirst(baseURLLabel) >= 0` searches for the base URL substring anywhere within `label` rather than matching only the prefix/domain.
- **Fix:** Verify match at the start of the host/URL.

### Bug 9: BookmarkBar Incomplete Removal in `kDeleteMsg`
- **File:** `src/apps/webpositive/BookmarkBar.cpp:256-263`
- **Component:** `BookmarkBar::MessageReceived`
- **Severity:** Low
- **Description:** In `kDeleteMsg`, `RemoveItem(fSelectedItemIndex)` removes the visual menu item but does not delete the `IconMenuItem` object nor remove its inode from `fItemsMap`, relying on an asynchronous node monitor notification that may fail or lag.
- **Fix:** Properly free or coordinate deletion with `fItemsMap`.

---

## 3. Architecture, Resource & Concurrency Improvements

### 1. Auxiliary Window Lifecycle Cleanup
- **File:** `src/apps/webpositive/BrowserApp.cpp:106-114, 452-468`
- **Issue:** Windows `fDownloadWindow`, `fSettingsWindow`, `fConsoleWindow`, and `fCookieWindow` intercept `QuitRequested()` with `Hide(); return false;`. On application exit in `BrowserApp::QuitRequested()`, these loopers are never asked to `Quit()`, leaving dangling threads and unclosed windows until process kill.
- **Improvement:** In `BrowserApp::QuitRequested()`, explicitly lock and call `Quit()` on each auxiliary window.

### 2. Lock Acquisition in `CredentialsStorage::~CredentialsStorage`
- **File:** `src/apps/webpositive/CredentialsStorage.cpp:66-70, 219-240`
- **Issue:** `_SaveSettings()` is called from the destructor and iterates over `fCredentialMap` without locking `BLocker`.
- **Improvement:** Guard access in `_SaveSettings()` with `BAutolock _(this)`.

### 3. FontSelectionView vs SettingsWindow View Hierarchy Ownership
- **File:** `src/apps/webpositive/SettingsWindow.cpp:145-156` & `support/FontSelectionView.cpp:78-88`
- **Issue:** `SettingsWindow` deletes `fStandardFontView` (a `BHandler`) in its destructor, while `FontSelectionView` conditionally deletes controls based on whether `fPreviewTextView->Window()` is null. Because `BWindow` deallocates child `BView`s upon teardown, this creates subtle timing and double-deletion risks.
- **Improvement:** Cleanly unify ownership using standard Haiku view hierarchy management.

### 4. TabBar Redundant Tooltip Resets & Boolean Cast
- **File:** `src/apps/webpositive/tabview/TabManager.cpp:605, 649`
- **Issue:**
  - `fController->SetToolTip(...)` is dispatched on every minor `MouseMoved` event over a tab even when the tooltip string has not changed.
  - Line 649 assigns a boolean condition to a `float`: `float isFront = ContainerView()->SelectedTab() == static_cast<TabView*>(this);`.
- **Improvement:** Cache current tooltip and assign `bool isFront`.

---

## 4. Prioritized Actionable Task List

| Priority | ID | Target File | Task Description |
| :--- | :--- | :--- | :--- |
| **P0** | TASK-01 | `BrowserWindow.cpp:1548` | Fix active URL bar text overwrite on background tab `LoadNegotiating` (Resolved) |
| **P0** | TASK-02 | `BrowsingHistory.cpp:67-73` | Fix strict weak ordering violation in `BrowsingHistoryItem::operator<` (Resolved) |
| **P1** | TASK-03 | `CookieWindow.cpp:214-217` | Fix memory leak of domain item at index 0 in `_BuildDomainList()` |
| **P1** | TASK-04 | `BrowserWindow.cpp:1986-1996` | Fix bookmark filename collision iteration string concatenation |
| **P1** | TASK-05 | `BaseURL.cpp:10-16` | Add safety bounds checks for missing `"://"` and trailing `/` in `baseURL()` |
| **P1** | TASK-06 | `BrowserWindow.cpp:2706-2716` | Replace `url.CountChars()` with `url.Length()` in `_SmartURLHandler()` |
| **P2** | TASK-07 | `AutoCompleterDefaultImpl.cpp:186` | Fix operator precedence in autocompletion choice visibility check |
| **P2** | TASK-08 | `BrowserApp.cpp:452-470` | Cleanly terminate auxiliary windows (`fDownloadWindow`, `fSettingsWindow`, etc.) on exit |
| **P2** | TASK-09 | `CredentialsStorage.cpp:219` | Add locker protection in `CredentialsStorage::_SaveSettings()` |
| **P3** | TASK-10 | `BrowserWindow.cpp:2260` | Replace `FindFirst` with prefix match in `addItemToMenuOrSubmenu` |
| **P3** | TASK-11 | `TabManager.cpp:605, 649` | Avoid redundant tooltip updates on tab mouse move; fix `bool isFront` type |
