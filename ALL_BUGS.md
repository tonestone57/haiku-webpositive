# Haiku Codebase Audit: Critical, Major, and Moderate Bugs (Excluding WebPositive)

## 1. Executive Summary
This document presents the extensive code audit and analysis across the `src/` directory and its sub-directories (excluding `src/apps/webpositive`, which is tracked separately in its dedicated audit file). The audit identifies **Critical**, **Major**, and **Moderate** bugs and errors across Haiku's core applications, servers, kits, and libraries.

A total of **8** bugs were identified, analyzed, and categorized. Each entry specifies the subsystem, exact source file and line numbers, severity level, technical root cause, and remediation steps. Section 3 provides an actionable, prioritized task list to guide fixes.

---

## 2. Identified Bugs & Errors

### BUG-01: Invalid 3-Way Comparator Return Contract in CompareMessages
- **Subsystem:** `src/servers/app`
- **File:** `src/servers/app/DelayedMessage.cpp:655-660`
- **Severity:** **Critical**
- **Description:** `CompareMessages(const ScheduledMessage* one, const ScheduledMessage* two)` is passed as the comparator function for `BObjectList<ScheduledMessage>::BinaryInsertWithDuplicates()` and `SortItems()`. The function returns `*one < *two`, which evaluates to only boolean 0 or 1 rather than the standard 3-way comparator contract (< 0 for less, 0 for equal, > 0 for greater). In `BObjectList` binary insertion and sort routines, returning 0 when `*one > *two` breaks ordering, resulting in out-of-order delayed message delivery or dropped/misplaced timeouts.
- **Fix:** Implement strict 3-way comparator logic returning -1 when `*one < *two`, 1 when `*two < *one`, and 0 when equal.

### BUG-02: Strict Weak Ordering Violation in ValCtrlLayoutEntry::operator<
- **Subsystem:** `src/apps/cortex`
- **File:** `src/apps/cortex/ValControl/ValCtrlLayoutEntry.h:108-111`
- **Severity:** **Critical**
- **Description:** `ValCtrlLayoutEntry::operator<` is implemented as `return frame.left < b.frame.left || frame.top < b.frame.top;`. Evaluating `(A.left < B.left || A.top < B.top)` violates the mathematical requirements of strict weak ordering. When element A has a smaller left coordinate but larger top coordinate than B, both `A < B` and `B < A` evaluate to `true`. When sorted with standard C++ sort algorithms or binary search trees, this induces non-deterministic sorting, infinite loops, and segmentation crashes.
- **Fix:** Implement hierarchical coordinate comparison: compare `frame.left` first; only if `frame.left == b.frame.left`, compare `frame.top`.

### BUG-03: Uninitialized Cursor Pointers in CursorManager Constructor
- **Subsystem:** `src/servers/app`
- **File:** `src/servers/app/CursorManager.cpp:33-40`
- **Severity:** **Critical**
- **Description:** Member pointer variables `fCursorSystemDefault`, `fCursorContextMenu`, and associated internal cursor tracking pointers are uninitialized in `CursorManager::CursorManager()`. If `ReleaseCursors()` or `SetCursor()` is invoked before full initialization completes or during early app server startup teardown, uninitialized heap pointer dereferencing occurs, crashing `app_server`.
- **Fix:** Explicitly initialize all cursor pointers (`fCursorSystemDefault`, `fCursorContextMenu`, etc.) to `NULL` in the constructor initializer list.

### BUG-04: Uninitialized Variable sizeFound in Resource Loading Error Path
- **Subsystem:** `src/kits/storage`
- **File:** `src/kits/storage/AppFileInfo.cpp:1095-1105`
- **Severity:** **Major**
- **Description:** In `AppFileInfo::GetResourceInfo()`, `size_t sizeFound;` is declared uninitialized. If `fResources->GetResourceInfo()` fails and modifies `error` without initializing `sizeFound`, subsequent statements evaluating buffer allocation logic risk reading indeterminate stack memory or performing invalid buffer allocations.
- **Fix:** Initialize `size_t sizeFound = 0;` and `int32 idFound = -1;` upon declaration.

### BUG-05: Uninitialized Hash Table Pointers in PathMonitor Ancestor and Entry
- **Subsystem:** `src/kits/storage`
- **File:** `src/kits/storage/PathMonitor.cpp:98, 268`
- **Severity:** **Major**
- **Description:** The nested classes `Ancestor` and `Entry` define hash chain pointer `fHashNext`. Neither class initializes `fHashNext` to `NULL` in their respective constructors. When instances are inserted into or removed from the hash bucket chains in `PathMonitor`, uninitialized pointers cause random pointer corruption and crashes during node monitoring.
- **Fix:** Initialize `fHashNext(NULL)` in both `Ancestor` and `Entry` constructor initializer lists.

### BUG-06: Potential Drag-and-Drop Message Leak in BarView Spring-Loaded Folder
- **Subsystem:** `src/apps/deskbar`
- **File:** `src/apps/deskbar/BarView.cpp:895-905`
- **Severity:** **Major**
- **Description:** In `BarView::MouseMoved()`, spring-loaded folder handling processes incoming drag messages. When comparing messages via `SpringLoadedFolderCompareMessages()`, temporary message allocations or references are retained without being released when drag targets navigate away or abort.
- **Fix:** Ensure drag message tracking state is explicitly cleared and released when drag transitions outside the target bounds.

### BUG-07: Redundant Nested Condition Check in Mime Message Construction
- **Subsystem:** `src/kits/storage`
- **File:** `src/kits/storage/Mime.cpp:55-70`
- **Severity:** **Moderate**
- **Description:** In `Mime.cpp`, nested identical conditionals `if (!err)` are checked repeatedly in succession after an outer `if (!err)` guard block without intermediate operations that could reset `err`, reflecting copy-paste error and potential oversight of intended secondary error checking.
- **Fix:** Refactor error check sequence and verify proper status return handling.

### BUG-08: Lock Acquisition Timeout Drift in DelayedMessageSender
- **Subsystem:** `src/servers/app`
- **File:** `src/servers/app/DelayedMessage.cpp:800-815`
- **Severity:** **Moderate**
- **Description:** In `DelayedMessageSender::_SendDelayedMessages()`, when lock contention occurs (`fLock.LockWithTimeout(30000) != B_OK`), the wakeup timeout is incremented via `atomic_add64(&fScheduledWakeup, DM_MINIMUM_DELAY)`. Under sustained contention, successive lock failures cause monotonic drift of scheduled wakeup times without bounds checking against overdue messages.
- **Fix:** Cap the retry backoff and force high-priority wakeup reconciliation once threshold timeouts expire.

---

## 3. Prioritized Actionable Task List

| Priority | ID | Severity | Subsystem | Target File | Task Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **P0** | BUG-01 | Critical | `src/servers/app` | `src/servers/app/DelayedMessage.cpp:655-660` | Invalid 3-Way Comparator Return Contract in CompareMessages |
| **P0** | BUG-02 | Critical | `src/apps/cortex` | `src/apps/cortex/ValControl/ValCtrlLayoutEntry.h:108-111` | Strict Weak Ordering Violation in ValCtrlLayoutEntry::operator< |
| **P0** | BUG-03 | Critical | `src/servers/app` | `src/servers/app/CursorManager.cpp:33-40` | Uninitialized Cursor Pointers in CursorManager Constructor |
| **P1** | BUG-04 | Major | `src/kits/storage` | `src/kits/storage/AppFileInfo.cpp:1095-1105` | Uninitialized Variable sizeFound in Resource Loading Error Path |
| **P1** | BUG-05 | Major | `src/kits/storage` | `src/kits/storage/PathMonitor.cpp:98, 268` | Uninitialized Hash Table Pointers in PathMonitor Ancestor and Entry |
| **P1** | BUG-06 | Major | `src/apps/deskbar` | `src/apps/deskbar/BarView.cpp:895-905` | Potential Drag-and-Drop Message Leak in BarView Spring-Loaded Folder |
| **P2** | BUG-07 | Moderate | `src/kits/storage` | `src/kits/storage/Mime.cpp:55-70` | Redundant Nested Condition Check in Mime Message Construction |
| **P2** | BUG-08 | Moderate | `src/servers/app` | `src/servers/app/DelayedMessage.cpp:800-815` | Lock Acquisition Timeout Drift in DelayedMessageSender |
