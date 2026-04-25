# Coding Best Practices

**Read this file at the start of any coding phase.** These are personal coding conventions that apply to every project, every language, every framework. They override defaults and must be followed unless explicitly told otherwise.

---

## Philosophy

Code is navigated spatially. The folder tree is a mental map — every directory, file, and function name is a landmark. When the structure is clean and names are descriptive, you can find what you need without searching. When it's not, you're lost in your own codebase.

These rules exist to keep code **navigable, scannable, and small enough to hold in your head**.

---

## Universal Rules

These apply to ALL code in ALL languages. No exceptions.

### 1. Descriptive Names — Everything

Every name must tell you what it is at a glance. Files, folders, functions, variables, types, classes, routes, components, database tables, columns — everything.

**The test:** Would you know what this is from the file tree alone, without opening it?

| Bad | Good |
|-----|------|
| `utils.ts` | `dateFormatUtils.ts` |
| `helpers.ts` | `pricingCalculationHelpers.ts` |
| `data.ts` | `userSessionData.ts` |
| `handler()` | `handleFileUploadComplete()` |
| `process()` | `processIncomingWebhookPayload()` |
| `calc()` | `calculateMonthlySubscriptionCost()` |
| `Item` | `InventoryLineItem` |
| `manage()` | `manageConnectionPool()` |
| `Service` | `NotificationDeliveryService` |
| `tmp` | `pendingUploadBuffer` |

Prefer longer, descriptive names over short, ambiguous ones. Abbreviations are acceptable only when universally understood (`id`, `url`, `http`, `db`, `api`, `auth`).

### 2. Small Functions — ~100 Lines of Code Max

The ~100-line limit applies to the **function body only** — the executable logic. Documentation comments above the function do NOT count toward this limit.

- If a function body is growing past ~100 lines, look for logical extraction points
- Each function should do one thing and do it well
- Aim for concise, precise code — fewer lines that do more, not sprawling logic
- Use one blank line to separate logical chunks within a function for readability — but no more than one
- Don't add unnecessary blank lines just for spacing — simplify the logic instead

### 3. Small Files — Under 1,000 Lines of Code

The 1,000-line limit applies to **code only**. Documentation comments, file headers, and inline documentation do NOT count toward this limit. This limit exists to enforce efficient, concise code — not to punish well-documented code.

- Prefer more files with fewer functions each
- Every function in a file must logically belong there — don't put camera rotation functions in a text editor file
- Split where it makes semantic sense, not just to hit a number
- The folder nesting structure exists to support these smaller units

### 4. Library-First Design

Write all code as if every function could be extracted into a standalone library. The end goal is maximum reusability across projects.

**Principles:**
- Functions should be self-contained with minimal external dependencies
- Clean, well-defined interfaces — inputs and outputs should be obvious
- Avoid hard-coded project-specific values inside reusable logic — pass them as parameters
- Group related functions that could form a library into their own files/modules
- In any language that supports libraries (C, Rust, JS/TS, Swift, etc.), structure code so extraction into a library requires moving files, not rewriting them

**For C specifically:** This is the primary target. Every utility, algorithm, data structure, and system abstraction should be written as a reusable library from day one. Headers define the public API contract, source files contain the implementation. A well-written C library can be linked into any project.

### 5. No Auto-Save / No Background Persistence

**Every write to a database, file, or storage must trace back to a deliberate user action.** No exceptions unless explicitly requested.

Never implement:
- Auto-saving as the user types
- Debounced save on input change
- Watchers that persist reactive state changes
- Background sync intervals
- Persisting UI state automatically (sidebar position, tab selection, scroll)
- Writing to storage on mount/unmount/navigation

Always require:
- User clicks "Save" → write
- User submits form → persist
- User explicitly toggles setting → save
- User confirms action → execute

**Pattern:** `User Action → Service Call → Write → Confirm Success`

### 6. Folder Nesting as Navigation

Directories are not just organization — they're how you navigate. The tree structure should let you drill down to the right file by following a logical path, not by guessing or searching.

**Principles:**
- Group by **domain/feature**, not by file type (prefer `auth/authService.ts` over `services/auth.ts` when the module is complex enough)
- Each directory should have a clear, single purpose
- Nesting depth should match conceptual depth — don't nest just for nesting, don't flatten when there's real hierarchy
- A directory with 1 file is usually wrong — either the file belongs in the parent or more files belong in the directory

### 7. Error Transparency

- Never swallow errors silently
- Catch at service boundaries where you can show user feedback
- Log technical details (`console.error` with context)
- Show human-readable messages to users — never raw errors, stack traces, or technical details
- If you catch it, either handle it meaningfully or log it. Never empty catch blocks.

### 8. Delete Dead Code

Don't comment out code. Don't leave unused functions, imports, or variables. If it's not being used, delete it. Git has the history if you ever need it back.

---

## Inline Code Documentation

Every source file carries its own documentation. This gives both the developer and Claude immediate context the moment a file is opened — no hunting through separate docs.

### File Header — Always Present

Every source file MUST have a header block at the top with three sections: **status**, **issues**, and **todo**. All three sections are always present, even when empty. The empty slots serve as a reminder that documentation can go there — if the sections aren't present, they get forgotten.

**Template (JS/TS/Vue):**

```typescript
/**
 * @file DescriptiveFileName
 * @description What this file is for and what it does (1-3 concise lines)
 *
 * @status None
 * @issues None
 * @todo None
 */
```

**Template (Rust):**

```rust
//! # DescriptiveModuleName
//! What this module is for and what it does (1-3 concise lines)
//!
//! ## Status
//! None
//!
//! ## Issues
//! None
//!
//! ## Todo
//! None
```

**Template (C — header files):**

```c
/**
 * @file descriptive_name.h
 * @brief What this header defines and its purpose (1-3 concise lines)
 *
 * @status None
 * @issues None
 * @todo None
 */
```

**Template (C — source files):**

```c
/**
 * @file descriptive_name.c
 * @brief Implementation of [what this file implements]
 *
 * @status None
 * @issues None
 * @todo None
 */
```

**Template (Swift):**

```swift
/// # DescriptiveFileName
/// What this file is for and what it does (1-3 concise lines)
///
/// ## Status
/// None
///
/// ## Issues
/// None
///
/// ## Todo
/// None
```

**When there IS something to document:**

```typescript
/**
 * @file AvatarUploadService
 * @description Handles user avatar upload, validation, and storage.
 *   Supports JPEG, PNG, and WebP formats up to 10MB.
 *
 * @status Avatar upload works but no progress indicator yet
 * @issues Timeout on large files (>10MB) — needs chunked upload
 * @todo
 *   - [ ] Add upload progress bar (blocked on chunked upload)
 *   - [ ] Validate image dimensions before upload
 */
```

**What each section means:**

| Section | Purpose | Example |
|---------|---------|---------|
| `@status` | Current state of the code — what works, what's partial | "Core CRUD works, batch operations not yet implemented" |
| `@issues` | Known bugs, performance problems, fragile areas | "Race condition when two users edit simultaneously" |
| `@todo` | Incomplete work left during a session — breadcrumbs | "- [ ] Add input validation for email field" |

### File Header Cleanup Rule

**When working in any file, always check the header first.** If there are completed TODOs, resolved issues, or outdated status notes — clean them up. Reset sections to "None" when everything is addressed. This prevents documentation rot.

### File-Level TODOs Are Breadcrumbs

The `@todo` section in file headers is NOT the project plan. The real task tracking lives in plan files and `.teamide/tasks/`. File-level TODOs exist for the "mid-session, had to step away" situation:

- You're deep in a function, a priority comes in — drop a TODO so you don't lose your place
- You notice something needs doing but it's not the current task — note it so it doesn't get forgotten
- If you come back to that file a month later, the TODO catches what fell through the cracks

**Cleanup context:** When a file-level TODO is completed, remove it from the header. When all TODOs are done, reset to "None". If a TODO turns into a real task, move it to the project plan and remove it from the header.

### Function Documentation

Document every public/exported function. Internal helper functions need docs only when the logic isn't obvious from the name and signature.

**JS/TS pattern:**

```typescript
/**
 * Calculates the prorated subscription cost for a partial billing period.
 * Uses daily rate × remaining days, rounded to 2 decimal places.
 *
 * @param planMonthlyRate - The full monthly rate in dollars
 * @param startDate - When the subscription started (mid-cycle)
 * @param billingCycleEnd - End date of the current billing cycle
 * @returns Prorated cost in dollars (e.g., 14.52)
 */
function calculateProratedSubscriptionCost(
  planMonthlyRate: number,
  startDate: Date,
  billingCycleEnd: Date
): number {
  // function body...
}
```

**C header pattern (API contract):**

```c
/**
 * Allocates a block from the thread-local memory pool.
 * Falls back to system malloc if pool is exhausted.
 *
 * @param size  Bytes to allocate (rounded up to pool alignment)
 * @return      Pointer to allocated block, or NULL on failure
 *
 * Thread-safe: yes (per-thread pools, no locks needed)
 * Reentrant: no (modifies pool state)
 */
void *pool_alloc(size_t size);
```

**Rust pattern:**

```rust
/// Calculates the prorated subscription cost for a partial billing period.
/// Uses daily rate × remaining days, rounded to 2 decimal places.
///
/// # Arguments
/// * `plan_monthly_rate` - The full monthly rate in dollars
/// * `start_date` - When the subscription started (mid-cycle)
/// * `billing_cycle_end` - End date of the current billing cycle
///
/// # Returns
/// Prorated cost in dollars (e.g., 14.52)
fn calculate_prorated_subscription_cost(
    plan_monthly_rate: f64,
    start_date: NaiveDate,
    billing_cycle_end: NaiveDate,
) -> f64 {
    // function body...
}
```

### Inline Comments

Use inline comments to explain **why**, not **what**. The code shows what it does — comments explain the reasoning that isn't obvious.

```typescript
// Offset by 1 because the API uses 1-based page indexing
const apiPage = displayPage + 1;

// Must check auth before loading data — the API returns 200 with empty
// results for unauthorized users instead of 401
await verifyAuthStatus();
const results = await loadDashboardData();
```

Don't write comments that just restate the code:

```typescript
// BAD — restates the code
const total = price * quantity; // multiply price by quantity

// GOOD — explains the business rule
const total = price * quantity; // pre-tax total, tax applied at checkout
```

---

## Folder Structure Patterns by Language

The core principle is the same everywhere: **categorical nesting by domain**, with descriptive names, keeping files small and focused. Here's how that maps to each language ecosystem.

### JavaScript / TypeScript (Vue, Quasar, Node)

```
src/
├── modules/                    # Feature modules (self-contained)
│   ├── auth/
│   │   ├── index.ts            # Module entry, re-exports
│   │   ├── store.ts            # Pinia store (composition API)
│   │   ├── api.ts              # API calls for this module
│   │   ├── types.ts            # Types/interfaces for this module
│   │   ├── AuthLogin.vue       # Components named descriptively
│   │   ├── AuthRegister.vue
│   │   └── components/         # Sub-components if needed
│   │       └── AuthFormField.vue
│   └── dashboard/
│       ├── index.ts
│       ├── store.ts
│       ├── api.ts
│       └── DashboardOverview.vue
├── services/                   # Shared services (API client, DB, etc.)
│   ├── apiClient.ts            # Centralized fetch wrapper
│   ├── db/
│   │   ├── client.ts
│   │   └── tables/
│   │       ├── userSettings.ts
│   │       └── sessionHistory.ts
│   └── fileWatcher.ts
├── stores/                     # Global stores (app-wide state)
│   └── globalAppState.ts
├── components/                 # Shared components (used across modules)
│   ├── AppHeader.vue
│   └── ConfirmDialog.vue
├── composables/                # Shared composables
│   └── useWindowResize.ts
├── types/                      # Global types
│   └── appTypes.ts
├── layouts/                    # Page layouts
├── pages/                      # Route pages
├── router/                     # Vue Router config
├── boot/                       # Startup/init files
├── css/                        # Global styles
└── utils/                      # Platform detection, formatters
    └── platformDetection.ts
```

**Key conventions:**
- Each feature module has `index.ts`, `store.ts`, `api.ts`, `types.ts`
- Components are PascalCase: `UserProfileCard.vue`
- Everything else is camelCase: `apiClient.ts`, `useAuth.ts`
- Path aliases: `@/` for `src/`, `@modules/`, `@components/`, `@stores/`

### Rust

Apply the same categorical nesting using Rust's module system:

```
src/
├── main.rs                         # Entry point, minimal — just init and run
├── app.rs                          # Application setup, config, state
├── lib.rs                          # Library entry (if crate is also a lib)
├── commands/                       # Tauri commands or CLI handlers
│   ├── mod.rs                      # Re-exports all command modules
│   ├── file_operations.rs          # File read/write/watch commands
│   ├── window_management.rs        # Window creation/positioning
│   └── backend_lifecycle.rs        # Start/stop/health-check backend
├── services/                       # Business logic, reusable across commands
│   ├── mod.rs
│   ├── process_manager.rs          # Spawn/kill/monitor child processes
│   ├── port_allocator.rs           # Find available ports
│   └── log_writer.rs               # Write structured logs to files
├── models/                         # Data structures, enums, types
│   ├── mod.rs
│   ├── app_config.rs               # Config structs
│   └── backend_state.rs            # Backend process state
├── platform/                       # OS-specific code
│   ├── mod.rs                      # Conditional compilation re-exports
│   ├── macos.rs                    # macOS-specific (menus, notifications)
│   ├── windows.rs
│   └── linux.rs
└── utils/                          # Small, focused helpers
    ├── mod.rs
    └── path_resolver.rs            # Resolve app paths, home dir, etc.
```

**Key conventions:**
- `mod.rs` in each directory for re-exports (like `index.ts`)
- snake_case for everything: files, functions, variables, modules
- Descriptive module names: `backend_lifecycle.rs` not `backend.rs`
- Keep `main.rs` thin — delegate to `app.rs` or modules immediately
- Group by responsibility, not by Rust convention alone
- Include `lib.rs` when the crate should be usable as a library

### C

Follow categorical nesting for headers, with source files mirroring the structure. Every module should be structured for library extraction from day one.

```
include/                            # Public headers (categorical nesting)
├── core/                           # Core functionality
│   ├── memory_allocator.h
│   ├── string_operations.h
│   └── error_codes.h
├── arch/                           # Architecture-specific
│   ├── arm64.h
│   ├── x86_64.h
│   └── riscv32.h
├── os/                             # OS-specific abstractions
│   ├── linux_syscalls.h
│   ├── darwin_syscalls.h
│   └── windows_syscalls.h
├── fmt/                            # File format parsers
│   ├── elf_parser.h
│   ├── macho_parser.h
│   └── pe_parser.h
└── sys/                            # System-level interfaces
    └── thread_pool.h

src/                                # Implementation files (mirror include/ structure)
├── core/
│   ├── memory_allocator.c
│   ├── string_operations.c
│   └── error_codes.c
├── arch/
│   ├── arm64.c
│   └── x86_64.c
└── os/
    ├── linux_syscalls.c
    └── darwin_syscalls.c

tests/                              # Test files (mirror src/ structure)
├── unit/
│   ├── test_memory_allocator.c
│   └── test_string_operations.c
└── integration/
    └── test_full_pipeline.c
```

**Key conventions:**
- `include/` and `src/` mirror each other's structure
- Category directories by domain: `core/`, `arch/`, `os/`, `fmt/`, `sys/`
- snake_case for all file names
- Header names describe what's inside: `memory_allocator.h` not `mem.h`
- One header + one source per logical unit
- Headers define the full API contract (see Function Documentation section)
- Each category directory is a potential standalone library

### Swift (macOS / iOS)

```
Sources/
├── App/                            # App entry, lifecycle
│   ├── AppMain.swift
│   └── AppDelegate.swift
├── Features/                       # Feature modules
│   ├── Camera/
│   │   ├── CameraManager.swift
│   │   ├── CameraPreviewView.swift
│   │   └── CameraPermissions.swift
│   └── Recording/
│       ├── RecordingSession.swift
│       ├── RecordingOverlay.swift
│       └── RecordingStorage.swift
├── Services/                       # Shared services
│   ├── FileStorageService.swift
│   └── NotificationService.swift
├── Models/                         # Data models
│   ├── RecordingMetadata.swift
│   └── UserPreferences.swift
├── Views/                          # Shared UI components
│   ├── PermissionRequestView.swift
│   └── StatusIndicator.swift
├── Extensions/                     # Swift extensions
│   └── DateFormattingExtensions.swift
└── Utilities/                      # Small helpers
    └── DeviceCapabilities.swift
```

**Key conventions:**
- PascalCase for files (Swift convention), but still descriptive
- Feature-based grouping under `Features/`
- `CameraPermissions.swift` not `Permissions.swift` — qualify the name

### General Pattern (Any Language)

When you encounter a new language, apply this structure:

```
src/ (or equivalent)
├── [entry point]               # Minimal — just init and delegate
├── features/ or modules/       # Self-contained feature units
│   └── [feature_name]/
│       ├── [entry/index]       # Re-exports or module definition
│       ├── [types/models]      # Data structures for this feature
│       ├── [api/handlers]      # External interface (API calls, commands)
│       └── [service/logic]     # Business logic
├── services/                   # Shared, cross-cutting services
├── models/ or types/           # Global data structures
├── platform/                   # OS/environment-specific code
└── utils/                      # Small, focused helpers
```

---

## Code Style Conventions

### Import Organization (JS/TS)

Organize imports in 3 groups, separated by blank lines:

```typescript
// 1. Framework & language imports
import { ref, computed, watch, onMounted } from 'vue';
import { defineStore } from 'pinia';

// 2. Third-party libraries
import { format } from 'date-fns';

// 3. Local imports (project code)
import { useGlobalStore } from '@/stores/globalAppState';
import { request } from '@/services/apiClient';
import type { UserProfile } from './types';
```

### Vue Component Structure

Always this order in single-file components:

```vue
<template>
  <!-- Template first — the visual output -->
</template>

<script setup lang="ts">
// Script second — the logic
</script>

<style scoped lang="scss">
// Style last — scoped to this component
</style>
```

Within `<script setup>`:
1. Imports
2. Store references
3. Props & emits
4. Reactive state (`ref`, `reactive`)
5. Computed properties
6. Methods / handlers
7. Watchers
8. Lifecycle hooks (`onMounted`, etc.)

### Pinia Store Pattern

Always use composition API style:

```typescript
export const useFeatureStore = defineStore('feature-name', () => {
  // State
  const items = ref<Item[]>([]);
  const selectedItem = ref<Item | null>(null);
  const isInitialized = ref(false);

  // Init guard — prevent double initialization
  async function initialize() {
    if (isInitialized.value) return;
    items.value = await featureApi.loadItems();
    isInitialized.value = true;
  }

  // Actions
  function selectItem(item: Item) {
    selectedItem.value = item;
  }

  // Return everything that should be accessible
  return { items, selectedItem, isInitialized, initialize, selectItem };
});
```

### API Layer Pattern

Centralized request function + module-level API objects:

```typescript
// services/apiClient.ts — single fetch wrapper, typed
export async function request<T>(path: string, options?: RequestInit): Promise<T> {
  const response = await fetch(`${API_BASE}${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options
  });
  if (!response.ok) {
    const error = await response.json().catch(() => ({ error: 'Request failed' }));
    throw new Error(error.error || 'Request failed');
  }
  return response.json();
}

// modules/inventory/api.ts — module-specific API
export const inventoryApi = {
  listProducts: (warehouseId: string): Promise<Product[]> =>
    request(`/warehouses/${warehouseId}/products`),

  updateStock: (productId: string, quantity: number): Promise<void> =>
    request(`/products/${productId}/stock`, {
      method: 'PATCH',
      body: JSON.stringify({ quantity })
    })
};
```

### TypeScript Conventions

- **Strict mode** always on
- **No `any`** — use `unknown` with type narrowing instead
- **Discriminated unions** with type guard functions:

```typescript
type EditorTab =
  | { type: 'file'; path: string; content: string }
  | { type: 'terminal'; sessionId: string }
  | { type: 'diff'; leftPath: string; rightPath: string };

function isFileTab(tab: EditorTab): tab is EditorTab & { type: 'file' } {
  return tab.type === 'file';
}
```

- **Interfaces for object shapes**, types for unions and aliases
- **PascalCase** for types/interfaces, no `I` prefix (`User` not `IUser`)

### CSS / Styling

- Scoped styles in components: `<style scoped lang="scss">`
- CSS custom properties for theming: `var(--app-surface-color)`
- Dark theme by default
- kebab-case for CSS classes: `.file-tree-node`, `.panel-header`
- SCSS variables for design tokens (colors, spacing)

### Formatting (JS/TS/Vue)

```
Semicolons:       required
Quotes:           single quotes
Trailing commas:  none
Line width:       100 characters
Indentation:      2 spaces
```

### General Code Style

- **`const` by default** — only use `let` when reassignment is needed, never `var`
- **Arrow functions** for most cases: `const handleClick = () => { ... }`
- **Nullish coalescing**: `value ?? fallback` over `value || fallback`
- **Optional chaining**: `obj?.property?.method?.()` over nested null checks
- **No magic numbers/strings** — use named constants
- **Colocate related code** — don't scatter logic across files unnecessarily
- **One blank line** between logical chunks for readability — no more than one

---

## Naming Reference

| Thing | Convention | Example |
|-------|-----------|---------|
| Vue components | PascalCase | `UserProfileCard.vue` |
| TS/JS files | camelCase | `userProfileService.ts` |
| Rust files | snake_case | `process_manager.rs` |
| C files | snake_case | `memory_allocator.c` |
| Swift files | PascalCase | `CameraManager.swift` |
| Config files | kebab-case | `quasar.config.js` |
| Directories | camelCase (JS/TS), snake_case (Rust/C) | `userAuth/`, `user_auth/` |
| Variables/functions | camelCase (JS/TS), snake_case (Rust/C), camelCase (Swift) | |
| Constants | UPPER_SNAKE_CASE | `MAX_RETRY_ATTEMPTS` |
| Types/Interfaces | PascalCase | `UserSession`, `FileTreeNode` |
| CSS classes | kebab-case | `.sidebar-panel-header` |
| Database tables | snake_case | `user_sessions` |
| Database columns | snake_case | `created_at`, `session_token` |
