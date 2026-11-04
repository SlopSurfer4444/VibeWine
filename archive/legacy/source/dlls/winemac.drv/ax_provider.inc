/* Real MSAA provider. Included by dllmain.c; COM never runs on the Cocoa thread.
 * One worker only: native leases/cache expiry bound native responsiveness, not
 * arbitrary provider COM. An action already entered in COM cannot be cancelled. */
#include "oleacc.h"
#include "ax_protocol.h"

static const IID ax_iid_accessible = {0x618736e0,0x3c3d,0x11cf,{0x81,0x0c,0x00,0xaa,0x00,0x38,0x9b,0x71}};
static const IID ax_iid_unknown = {0,0,0,{0xc0,0,0,0,0,0,0,0x46}};
static HRESULT (WINAPI *ax_object_from_result)(LRESULT,REFIID,WPARAM,void **);
static HRESULT (WINAPI *ax_standard_object)(HWND,LONG,REFIID,void **);
static HRESULT (WINAPI *ax_create_guid)(GUID *);
static HRESULT (WINAPI *ax_children)(IAccessible *,LONG,LONG,VARIANT *,LONG *);
static HRESULT (WINAPI *ax_window_from_object)(IAccessible *,HWND *);
static HRESULT (WINAPI *ax_co_initialize)(void *,DWORD);
static void (WINAPI *ax_co_uninitialize)(void);
static void (WINAPI *ax_free_string)(BSTR);
static BSTR (WINAPI *ax_alloc_string)(const OLECHAR *);
static HRESULT (WINAPI *ax_clear_variant)(VARIANT *);

struct ax_binding {
    IAccessible *object;
    IUnknown *identity;
    VARIANT child;
    uint32_t role, cookie;
    HWND standard_hwnd;
    uint64_t token;
};
static WCHAR ax_cookie_property[64];
static uint32_t ax_next_cookie;
static uint64_t ax_next_token;
static BOOL ax_provider_abandoned;
struct ax_cache {
    uint32_t hwnd, count;
    uint64_t generation, revision;
    ULONGLONG timestamp;
    struct ax_binding bindings[MACDRV_AX_MAX_NODES];
};
static struct ax_cache ax_caches[MACDRV_AX_MAX_WINDOWS];

static void ax_cache_clear(struct ax_cache *cache)
{
    unsigned int i;
    for (i = 0; i < cache->count; ++i)
    {
        IAccessible_Release(cache->bindings[i].object);
        IUnknown_Release(cache->bindings[i].identity);
    }
    memset(cache, 0, sizeof(*cache));
}

static struct ax_cache *ax_cache_get(uint32_t hwnd)
{
    struct ax_cache *oldest = ax_caches;
    unsigned int i;
    for (i = 0; i < MACDRV_AX_MAX_WINDOWS; ++i)
    {
        if (ax_caches[i].hwnd == hwnd) return ax_caches + i;
        if (!ax_caches[i].hwnd) oldest = ax_caches + i;
        else if (oldest->hwnd && ax_caches[i].timestamp < oldest->timestamp) oldest = ax_caches + i;
    }
    ax_cache_clear(oldest);
    return oldest;
}

static BOOL ax_window_owned(uint32_t hwnd)
{
    DWORD pid = 0;
    return NtUserGetWindowThread(UlongToHandle(hwnd), &pid) && pid == GetCurrentProcessId();
}

/* A GUID property belongs to this worker instance. The HWND property disappears
 * on destruction, unlike a process-local HWND lookup. The 32-bit cookie is only
 * an identity key: wire tokens are independent and never resurrect after absence. */
static uint32_t ax_window_cookie(HWND hwnd)
{
    ULONG_PTR cookie;
    if (!ax_window_owned(HandleToUlong(hwnd))) return 0;
    cookie = (ULONG_PTR)GetPropW(hwnd, ax_cookie_property);
    if (cookie) return cookie <= UINT32_MAX ? cookie : 0;
    if (ax_next_cookie == UINT32_MAX) return 0;
    cookie = ++ax_next_cookie;
    if (!SetPropW(hwnd, ax_cookie_property, (HANDLE)cookie)) return 0;
    return (ULONG_PTR)GetPropW(hwnd, ax_cookie_property) == cookie ? cookie : 0;
}

static BOOL ax_lease_valid(const struct macdrv_ax_request *request);

static BOOL ax_window_ready(const struct macdrv_ax_request *request, HWND hwnd)
{
    DWORD_PTR result = 0;
    /* A native window can be registered before its GUI thread enters the message
     * loop. A late WM_NULL has no MSAA marshaling resource, so failed readiness
     * checks can be retried without risking repeated abandoned WM_GETOBJECTs. */
    if (!SendMessageTimeoutW(hwnd, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &result))
    {
        TRACE("MSAA phase=readiness_retry tick=%llu hwnd=%p error=%lu\n",
              (unsigned long long)GetTickCount64(), hwnd, (unsigned long)GetLastError());
        return FALSE;
    }
    return ax_lease_valid(request);
}

static BOOL ax_acquire_client(const struct macdrv_ax_request *request, HWND hwnd, IAccessible **object,
                              HWND *standard_hwnd, uint32_t *cookie)
{
    DWORD_PTR result = 0;
    HRESULT hr;
    *object = NULL;
    *standard_hwnd = NULL;
    *cookie = 0;
    if (!ax_window_owned(HandleToUlong(hwnd))) return FALSE;
    if (!ax_window_ready(request, hwnd)) return FALSE;
    if (!SendMessageTimeoutW(hwnd, WM_GETOBJECT, (WPARAM)0xffffffff, OBJID_CLIENT,
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &result))
    {
        /* A timed-out WM_GETOBJECT may later create a marshaled result which
         * cannot be reclaimed here. Never retry uncertain work: leave the native
         * lease outstanding for quarantine and permanently abandon this worker. */
        ax_provider_abandoned = TRUE;
        WARN("MSAA phase=getobject_abandoned tick=%llu hwnd=%p error=%lu\n",
             (unsigned long long)GetTickCount64(), hwnd, (unsigned long)GetLastError());
        return FALSE;
    }
    /* Timeout/error is never evidence of a standard provider. Only an explicit
     * zero response permits the real Wine standard MSAA provider fallback. */
    if (result) hr = ax_object_from_result(result, &ax_iid_accessible, 0, (void **)object);
    else
    {
        hr = ax_standard_object(hwnd, OBJID_CLIENT, &ax_iid_accessible, (void **)object);
        if (SUCCEEDED(hr) && *object)
        {
            *cookie = ax_window_cookie(hwnd);
            if (!*cookie) { IAccessible_Release(*object); *object = NULL; return FALSE; }
            *standard_hwnd = hwnd;
        }
    }
    TRACE("MSAA phase=client_result tick=%llu custom=%u hr=%#lx object=%u\n",
          (unsigned long long)GetTickCount64(), !!result, (unsigned long)hr, !!*object);
    return SUCCEEDED(hr) && *object;
}

/* Native owns the outstanding serial and deadline; never substitute a PE clock
 * for validation, including after a potentially slow provider getter. */
static BOOL ax_lease_valid(const struct macdrv_ax_request *request)
{
    struct macdrv_ax_request validation = *request;
    BOOL valid;
    validation.operation = MACDRV_AX_VALIDATE;
    validation.result = MACDRV_AX_STALE;
    valid = !ax_provider_abandoned && ax_window_owned(request->hwnd) && !MACDRV_CALL(ax_poll, &validation) &&
            validation.result == MACDRV_AX_OK;
    if (!valid) TRACE("MSAA phase=lease_rejected tick=%llu operation=%u result=%u serial=%llu\n",
                      (unsigned long long)GetTickCount64(), request->operation, validation.result,
                      (unsigned long long)request->serial);
    return valid;
}

static void ax_copy_text(uint16_t *destination, BSTR source)
{
    unsigned int i;
    if (!source) return;
    for (i = 0; i < MACDRV_AX_TEXT - 1 && source[i]; ++i) destination[i] = source[i];
    /* Do not split a UTF-16 surrogate pair at the bounded wire-string edge. */
    if (i && destination[i - 1] >= 0xd800 && destination[i - 1] <= 0xdbff) --i;
    destination[i] = 0;
}

static BOOL ax_role_state(IAccessible *object, VARIANT child, uint32_t *role, LONG *state)
{
    VARIANT value;
    HRESULT hr;
    memset(&value, 0, sizeof(value));
    hr = IAccessible_get_accRole(object, child, &value);
    if (hr != S_OK) TRACE("MSAA phase=role_failed hr=%#lx\n", (unsigned long)hr);
    *role = 0;
    if (hr == S_OK && V_VT(&value) == VT_I4)
    {
        switch (V_I4(&value))
        {
        case ROLE_SYSTEM_PUSHBUTTON: *role = MACDRV_AX_ROLE_BUTTON; break;
        case ROLE_SYSTEM_TEXT: *role = MACDRV_AX_ROLE_EDIT; break;
        case ROLE_SYSTEM_CHECKBUTTON: *role = MACDRV_AX_ROLE_CHECKBOX; break;
        case ROLE_SYSTEM_RADIOBUTTON: *role = MACDRV_AX_ROLE_RADIO; break;
        case ROLE_SYSTEM_GROUPING: *role = MACDRV_AX_ROLE_GROUP; break;
        }
    }
    ax_clear_variant(&value);
    memset(&value, 0, sizeof(value));
    hr = IAccessible_get_accState(object, child, &value);
    if (hr != S_OK || V_VT(&value) != VT_I4)
    {
        TRACE("MSAA phase=state_failed hr=%#lx type=%u\n", (unsigned long)hr, V_VT(&value));
        ax_clear_variant(&value);
        return FALSE;
    }
    *state = V_I4(&value);
    ax_clear_variant(&value);
    return TRUE;
}

static BOOL ax_has_default_action(IAccessible *object, VARIANT child)
{
    BSTR text = NULL;
    BOOL result = IAccessible_get_accDefaultAction(object, child, &text) == S_OK && text && text[0];
    ax_free_string(text);
    return result;
}

/* Press invokes the provider's actual default action. Check/radio state is
 * read from MSAA; the bridge never synthesizes BM_CLICK or assigns check state. */
static BOOL ax_press_role(uint32_t role)
{
    return role == MACDRV_AX_ROLE_BUTTON || role == MACDRV_AX_ROLE_CHECKBOX || role == MACDRV_AX_ROLE_RADIO;
}

static uint32_t ax_node_flags(IAccessible *object, VARIANT child, uint32_t role, LONG state)
{
    uint32_t flags = 0;
    if (!(state & STATE_SYSTEM_UNAVAILABLE)) flags |= MACDRV_AX_ENABLED;
    if (state & STATE_SYSTEM_FOCUSED) flags |= MACDRV_AX_FOCUSED;
    if (state & STATE_SYSTEM_READONLY) flags |= MACDRV_AX_READONLY;
    if (state & STATE_SYSTEM_PROTECTED) flags |= MACDRV_AX_PASSWORD;
    if (role == MACDRV_AX_ROLE_CHECKBOX || role == MACDRV_AX_ROLE_RADIO)
    {
        /* Mixed takes precedence over checked if a custom provider sends both;
         * wire values remain unambiguous and never describe a fabricated toggle. */
        if (state & STATE_SYSTEM_MIXED) flags |= MACDRV_AX_MIXED;
        else if (state & STATE_SYSTEM_CHECKED) flags |= MACDRV_AX_CHECKED;
    }
    if ((flags & MACDRV_AX_ENABLED) && !(flags & (MACDRV_AX_READONLY | MACDRV_AX_PASSWORD)))
    {
        if (ax_press_role(role) && ax_has_default_action(object, child)) flags |= MACDRV_AX_PRESSABLE;
        if (role == MACDRV_AX_ROLE_EDIT) flags |= MACDRV_AX_SETTABLE;
    }
    return flags;
}

struct ax_walk {
    const struct macdrv_ax_request *request;
    struct macdrv_ax_snapshot *snapshot;
    struct ax_cache *cache, *previous;
    IAccessible *ancestors[9];
    HWND resolved_hwnds[MACDRV_AX_MAX_NODES];
    unsigned int resolved_count;
    unsigned int visited;
    BOOL expired;
};

static uint64_t ax_bind_token(struct ax_walk *walk, struct ax_binding *binding)
{
    unsigned int i;
    struct ax_binding *old;
    for (i = 0; i < walk->cache->count; ++i)
    {
        old = walk->cache->bindings + i;
        if (old->role != binding->role || V_I4(&old->child) != V_I4(&binding->child)) continue;
        if ((binding->standard_hwnd && old->standard_hwnd == binding->standard_hwnd && old->cookie == binding->cookie) ||
            (!binding->standard_hwnd && !old->standard_hwnd && old->identity == binding->identity)) return 0;
    }
    for (i = 0; i < walk->previous->count; ++i)
    {
        old = walk->previous->bindings + i;
        if (old->role != binding->role || V_I4(&old->child) != V_I4(&binding->child)) continue;
        if (binding->standard_hwnd)
        {
            if (old->standard_hwnd == binding->standard_hwnd && old->cookie == binding->cookie) return old->token;
        }
        else if (!old->standard_hwnd && old->identity == binding->identity) return old->token;
    }
    /* Tokens are process-wide and never recycled, even when a cached window or
     * node disappears. Exhaustion stops exposure rather than retargeting refs. */
    if (ax_next_token == UINT64_MAX) return 0;
    return ++ax_next_token;
}

static void ax_visit(struct ax_walk *walk, IAccessible *object, VARIANT child, uint32_t parent, unsigned int depth, HWND standard_hwnd, uint32_t cookie)
{
    struct macdrv_ax_node *node;
    struct ax_binding *binding;
    VARIANT children[MACDRV_AX_MAX_NODES];
    LONG state, count = 0, obtained = 0, i, x = 0, y = 0, width = 0, height = 0;
    uint32_t role;
    BSTR text = NULL;
    unsigned int j;
    BOOL state_valid;

    if (depth > 8 || walk->visited++ >= MACDRV_AX_MAX_NODES || walk->expired) return;
    if (!(walk->expired = !ax_lease_valid(walk->request)))
    {
        /* Detect object cycles on the current path, while permitting distinct
         * simple child IDs of the same accessible object. */
        if (V_I4(&child) == CHILDID_SELF)
            for (j = 0; j < depth; ++j) if (walk->ancestors[j] == object) return;
        walk->ancestors[depth] = V_I4(&child) == CHILDID_SELF ? object : NULL;
    }
    else return;

    state_valid = ax_role_state(object, child, &role, &state);
    if (!state_valid && V_I4(&child) == CHILDID_SELF && depth)
    {
        HWND hwnd = NULL;
        IAccessible *client = NULL;
        HWND client_standard_hwnd;
        uint32_t client_cookie;
        /* Wine's standard client's enumeration produces OBJID_WINDOW wrappers,
         * whose role/state methods are unimplemented. Query their real client
         * provider rather than manufacturing a role from a class or caption. */
        if (SUCCEEDED(ax_window_from_object(object, &hwnd)) && hwnd &&
            ax_window_owned(HandleToUlong(hwnd)) && IsChild(UlongToHandle(walk->request->hwnd), hwnd))
        {
            for (j = 0; j < walk->resolved_count; ++j) if (walk->resolved_hwnds[j] == hwnd) break;
            if (j == walk->resolved_count && walk->resolved_count < MACDRV_AX_MAX_NODES && ax_lease_valid(walk->request))
            {
                walk->resolved_hwnds[walk->resolved_count++] = hwnd;
                if (ax_acquire_client(walk->request, hwnd, &client, &client_standard_hwnd, &client_cookie))
                {
                    /* Wrapper replacement does not add a tree depth. */
                    walk->ancestors[depth] = NULL;
                    ax_visit(walk, client, child, parent, depth, client_standard_hwnd, client_cookie);
                    IAccessible_Release(client);
                    return;
                }
                if (ax_provider_abandoned) { walk->expired = TRUE; return; }
            }
        }
    }
    if (state_valid && role &&
        !(state & (STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN)))
    {
        binding = walk->cache->bindings + walk->snapshot->count;
        memset(binding, 0, sizeof(*binding));
        if (FAILED(IAccessible_QueryInterface(object, &ax_iid_unknown, (void **)&binding->identity))) return;
        binding->child = child;
        binding->role = role;
        binding->standard_hwnd = standard_hwnd;
        binding->cookie = cookie;
        binding->token = ax_bind_token(walk, binding);
        if (!binding->token) { IUnknown_Release(binding->identity); return; }
        node = walk->snapshot->nodes + walk->snapshot->count;
        node->token = binding->token;
        node->id = walk->snapshot->count + 1;
        node->parent = parent;
        node->role = role;
        node->flags = ax_node_flags(object, child, role, state);
        IAccessible_accLocation(object, &x, &y, &width, &height, child);
        node->x = x;
        node->y = y;
        node->width = width;
        node->height = height;
        if (SUCCEEDED(IAccessible_get_accName(object, child, &text))) ax_copy_text(node->name, text);
        ax_free_string(text);
        text = NULL;
        /* A protected object never has its value queried, even read-only. */
        if (role == MACDRV_AX_ROLE_EDIT && !(state & STATE_SYSTEM_PROTECTED))
        {
            if (SUCCEEDED(IAccessible_get_accValue(object, child, &text))) ax_copy_text(node->value, text);
            ax_free_string(text);
        }
        binding->object = object;
        binding->child = child; /* Traversal admits only VT_I4 here. */
        binding->role = role;
        IAccessible_AddRef(object);
        ++walk->snapshot->count;
        walk->cache->count = walk->snapshot->count;
        parent = node->id;
    }
    if (depth == 8 || V_I4(&child) != CHILDID_SELF || walk->visited >= MACDRV_AX_MAX_NODES) return;
    if (!(walk->expired = !ax_lease_valid(walk->request)) &&
        SUCCEEDED(IAccessible_get_accChildCount(object, &count)) && count > 0)
    {
        count = min(count, MACDRV_AX_MAX_NODES - walk->visited);
        memset(children, 0, sizeof(children));
        if (SUCCEEDED(ax_children(object, 0, count, children, &obtained)))
        {
            obtained = max(0, min(obtained, count));
            for (i = 0; i < obtained && !walk->expired; ++i)
            {
                if (V_VT(children + i) == VT_DISPATCH && V_DISPATCH(children + i))
                {
                    IAccessible *next;
                    VARIANT self;
                    memset(&self, 0, sizeof(self));
                    V_VT(&self) = VT_I4;
                    V_I4(&self) = CHILDID_SELF;
                    if (SUCCEEDED(IDispatch_QueryInterface(V_DISPATCH(children + i), &ax_iid_accessible, (void **)&next)))
                    {
                        ax_visit(walk, next, self, parent, depth + 1, NULL, 0);
                        IAccessible_Release(next);
                    }
                }
                else if (V_VT(children + i) == VT_I4 && V_I4(children + i) > 0)
                    ax_visit(walk, object, children[i], parent, depth + 1, standard_hwnd, cookie);
            }
        }
        for (i = 0; i < count; ++i) ax_clear_variant(children + i);
    }
}

static void ax_snapshot(const struct macdrv_ax_request *request, struct macdrv_ax_snapshot *snapshot)
{
    struct ax_cache *cache = ax_cache_get(request->hwnd), previous;
    struct ax_walk walk;
    IAccessible *root = NULL;
    HWND standard_hwnd;
    uint32_t cookie;
    VARIANT self;
    /* Keep the previous references alive during matching, preventing canonical
     * COM pointer address reuse from impersonating a destroyed provider. */
    if (cache->generation != request->generation) ax_cache_clear(cache);
    previous = *cache;
    memset(cache, 0, sizeof(*cache));
    if (!ax_lease_valid(request)) { snapshot->result = MACDRV_AX_STALE; goto done; }
    if (!ax_acquire_client(request, UlongToHandle(request->hwnd), &root, &standard_hwnd, &cookie)) goto done;
    cache->hwnd = request->hwnd;
    cache->generation = request->generation;
    cache->revision = request->revision;
    cache->timestamp = GetTickCount64();
    memset(&walk, 0, sizeof(walk));
    memset(&self, 0, sizeof(self));
    V_VT(&self) = VT_I4;
    V_I4(&self) = CHILDID_SELF;
    walk.request = request;
    walk.snapshot = snapshot;
    walk.cache = cache;
    walk.previous = &previous;
    walk.resolved_hwnds[walk.resolved_count++] = UlongToHandle(request->hwnd);
    ax_visit(&walk, root, self, 0, 0, standard_hwnd, cookie);
    IAccessible_Release(root);
    if (walk.expired || !ax_lease_valid(request))
    {
        snapshot->count = 0;
        snapshot->result = MACDRV_AX_STALE;
        ax_cache_clear(cache);
    }
    else snapshot->result = MACDRV_AX_OK;
 done:
    ax_cache_clear(&previous);
}

static uint32_t ax_action(const struct macdrv_ax_request *request)
{
    struct ax_cache *cache = NULL;
    struct ax_binding *binding;
    uint32_t role;
    LONG state;
    unsigned int i;
    HRESULT hr;
    BSTR value = NULL;
    DWORD_PTR provider_result;
    for (i = 0; i < MACDRV_AX_MAX_WINDOWS; ++i)
        if (ax_caches[i].hwnd == request->hwnd) { cache = ax_caches + i; break; }
    if (!cache || !request->node_id || request->node_id > cache->count ||
        cache->generation != request->generation || cache->revision != request->revision ||
        GetTickCount64() - cache->timestamp > MACDRV_AX_CACHE_MS || !ax_lease_valid(request)) return MACDRV_AX_STALE;
    binding = cache->bindings + request->node_id - 1;
    if (!request->token || binding->token != request->token) return MACDRV_AX_STALE;
    role = binding->role;
    if (request->operation == MACDRV_AX_PRESS)
    {
        if (!ax_press_role(role))
            return MACDRV_AX_DENIED;
    }
    else if (request->operation == MACDRV_AX_SET_VALUE)
    {
        if (role != MACDRV_AX_ROLE_EDIT || request->value[MACDRV_AX_TEXT - 1]) return MACDRV_AX_DENIED;
        if (!(value = ax_alloc_string((const WCHAR *)request->value))) return MACDRV_AX_UNAVAILABLE;
    }
    else return MACDRV_AX_DENIED;
    /* A standard wrapper is retained only while its actual HWND lifetime cookie
     * still matches. Arbitrary custom providers never receive HWND cookies. */
    if (binding->standard_hwnd &&
        (!ax_window_owned(HandleToUlong(binding->standard_hwnd)) ||
         (binding->standard_hwnd != UlongToHandle(request->hwnd) &&
          !IsChild(UlongToHandle(request->hwnd), binding->standard_hwnd)) ||
         (ULONG_PTR)GetPropW(binding->standard_hwnd, ax_cookie_property) != binding->cookie))
    { ax_free_string(value); return MACDRV_AX_STALE; }
    if (binding->standard_hwnd)
    {
        IAccessible *replacement = NULL;
        if (!ax_window_ready(request, binding->standard_hwnd))
        { ax_free_string(value); return MACDRV_AX_UNAVAILABLE; }
        if (!SendMessageTimeoutW(binding->standard_hwnd, WM_GETOBJECT, (WPARAM)0xffffffff, OBJID_CLIENT,
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &provider_result))
        {
            ax_provider_abandoned = TRUE;
            WARN("MSAA phase=action_getobject_abandoned tick=%llu error=%lu\n",
                 (unsigned long long)GetTickCount64(), (unsigned long)GetLastError());
            ax_free_string(value);
            return MACDRV_AX_STALE;
        }
        if (provider_result)
        {
            /* Consume the provider's LresultFromObject marshaling resource even
             * though a standard-to-custom transition must deny this action. */
            ax_object_from_result(provider_result, &ax_iid_accessible, 0, (void **)&replacement);
            if (replacement) IAccessible_Release(replacement);
            ax_free_string(value);
            return MACDRV_AX_STALE;
        }
    }
    if (!ax_role_state(binding->object, binding->child, &role, &state) || role != binding->role)
    { ax_free_string(value); return MACDRV_AX_STALE; }
    if (state & (STATE_SYSTEM_UNAVAILABLE | STATE_SYSTEM_READONLY | STATE_SYSTEM_PROTECTED |
                 STATE_SYSTEM_INVISIBLE | STATE_SYSTEM_OFFSCREEN))
    { ax_free_string(value); return MACDRV_AX_DENIED; }
    /* State and role can change since the snapshot. Recheck the actual default
     * action after those getters, before the final lease check and mutation. */
    if (request->operation == MACDRV_AX_PRESS && !ax_has_default_action(binding->object, binding->child))
    { ax_free_string(value); return MACDRV_AX_DENIED; }
    /* A provider getter can destroy, replace or reparent a standard child.
     * Recheck its lifetime after the final COM getter as well as before it. */
    if (binding->standard_hwnd &&
        (!ax_window_owned(HandleToUlong(binding->standard_hwnd)) ||
         (binding->standard_hwnd != UlongToHandle(request->hwnd) &&
          !IsChild(UlongToHandle(request->hwnd), binding->standard_hwnd)) ||
         (ULONG_PTR)GetPropW(binding->standard_hwnd, ax_cookie_property) != binding->cookie))
    { ax_free_string(value); return MACDRV_AX_STALE; }
    /* Last operation before mutation: native generation/revision/lease check.
     * This cannot cancel a mutation once an arbitrary provider COM call starts. */
    if (!ax_lease_valid(request)) { ax_free_string(value); return MACDRV_AX_STALE; }
    if (request->operation == MACDRV_AX_PRESS)
        hr = IAccessible_accDoDefaultAction(binding->object, binding->child);
    else hr = IAccessible_put_accValue(binding->object, binding->child, value);
    ax_free_string(value);
    return hr == S_OK ? MACDRV_AX_OK : MACDRV_AX_UNAVAILABLE;
}

static DWORD WINAPI ax_worker(void *unused)
{
    HMODULE oleacc, ole32, oleaut32;
    struct macdrv_ax_request request;
    struct macdrv_ax_snapshot *snapshot;
    unsigned int i;
    GUID cookie_guid;
    BYTE *guid_bytes = (BYTE *)&cookie_guid;
    static const WCHAR hex[] = L"0123456789abcdef";
    HRESULT hr;
    NTSTATUS status;
    TRACE("MSAA phase=worker_start tick=%llu thread=%lu\n", (unsigned long long)GetTickCount64(),
          (unsigned long)GetCurrentThreadId());
    oleacc = LoadLibraryW(L"oleacc.dll");
    ole32 = LoadLibraryW(L"ole32.dll");
    oleaut32 = LoadLibraryW(L"oleaut32.dll");
    if (!oleacc || !ole32 || !oleaut32)
    { WARN("MSAA phase=load_failed oleacc=%u ole32=%u oleaut32=%u\n", !!oleacc, !!ole32, !!oleaut32); return 0; }
#define AX_LOAD(module, variable, name) do { variable = (void *)GetProcAddress(module, name); \
    if (!variable) { WARN("MSAA phase=import_failed symbol=%s\n", name); return 0; } } while (0)
    AX_LOAD(oleacc, ax_object_from_result, "ObjectFromLresult");
    AX_LOAD(oleacc, ax_standard_object, "CreateStdAccessibleObject");
    AX_LOAD(oleacc, ax_children, "AccessibleChildren");
    AX_LOAD(oleacc, ax_window_from_object, "WindowFromAccessibleObject");
    AX_LOAD(ole32, ax_create_guid, "CoCreateGuid");
    AX_LOAD(ole32, ax_co_initialize, "CoInitializeEx");
    AX_LOAD(ole32, ax_co_uninitialize, "CoUninitialize");
    AX_LOAD(oleaut32, ax_free_string, "SysFreeString");
    AX_LOAD(oleaut32, ax_alloc_string, "SysAllocString");
    AX_LOAD(oleaut32, ax_clear_variant, "VariantClear");
#undef AX_LOAD
    hr = ax_co_initialize(NULL, COINIT_MULTITHREADED);
    TRACE("MSAA phase=com_init tick=%llu hr=%#lx\n", (unsigned long long)GetTickCount64(), (unsigned long)hr);
    if (FAILED(hr)) return 0;
    hr = ax_create_guid(&cookie_guid);
    if (FAILED(hr)) { WARN("MSAA phase=guid_failed hr=%#lx\n", (unsigned long)hr); ax_co_uninitialize(); return 0; }
    lstrcpyW(ax_cookie_property, L"Wine.macdrv.MSAA.");
    for (i = 0; i < sizeof(cookie_guid); ++i)
    {
        ax_cookie_property[17 + i * 2] = hex[guid_bytes[i] >> 4];
        ax_cookie_property[18 + i * 2] = hex[guid_bytes[i] & 15];
    }
    ax_cookie_property[49] = 0;
    if (!(snapshot = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*snapshot))))
    { ax_co_uninitialize(); return 0; }
    for (;;)
    {
        memset(&request, 0, sizeof(request));
        request.version = MACDRV_AX_VERSION;
        request.size = sizeof(request);
        status = MACDRV_CALL(ax_poll, &request);
        if (status) { WARN("MSAA phase=poll_failed status=%#lx\n", (unsigned long)status); break; }
        if (request.version != MACDRV_AX_VERSION || request.size != sizeof(request))
        { WARN("MSAA phase=protocol_failed version=%u size=%u\n", request.version, request.size); break; }
        if (!request.operation)
        {
            for (i = 0; i < MACDRV_AX_MAX_WINDOWS; ++i)
                if (ax_caches[i].hwnd && (GetTickCount64() - ax_caches[i].timestamp > MACDRV_AX_CACHE_MS ||
                                         !ax_window_owned(ax_caches[i].hwnd))) ax_cache_clear(ax_caches + i);
            Sleep(50);
            continue;
        }
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->version = MACDRV_AX_VERSION;
        snapshot->size = sizeof(*snapshot);
        snapshot->operation = request.operation;
        snapshot->hwnd = request.hwnd;
        snapshot->generation = request.generation;
        snapshot->revision = request.revision;
        snapshot->serial = request.serial;
        snapshot->result = MACDRV_AX_UNAVAILABLE;
        TRACE("MSAA phase=request tick=%llu operation=%u serial=%llu generation=%llu revision=%llu\n",
              (unsigned long long)GetTickCount64(), request.operation, (unsigned long long)request.serial,
              (unsigned long long)request.generation, (unsigned long long)request.revision);
        if (request.operation == MACDRV_AX_SNAPSHOT) ax_snapshot(&request, snapshot);
        else snapshot->result = ax_action(&request);
        /* No completion publish on uncertain send failure: native owns timeout
         * quarantine. No replacement worker or subsequent request is started. */
        if (ax_provider_abandoned)
        { WARN("MSAA phase=worker_quarantined serial=%llu\n", (unsigned long long)request.serial); break; }
        status = MACDRV_CALL(ax_publish, snapshot);
        TRACE("MSAA phase=publish tick=%llu operation=%u serial=%llu result=%u count=%u status=%#lx\n",
              (unsigned long long)GetTickCount64(), request.operation, (unsigned long long)request.serial,
              snapshot->result, snapshot->count, (unsigned long)status);
    }
    for (i = 0; i < MACDRV_AX_MAX_WINDOWS; ++i) ax_cache_clear(ax_caches + i);
    HeapFree(GetProcessHeap(), 0, snapshot);
    ax_co_uninitialize();
    TRACE("MSAA phase=worker_exit tick=%llu abandoned=%u\n", (unsigned long long)GetTickCount64(), ax_provider_abandoned);
    return 0;
}

static void macdrv_ax_provider_start(void)
{
    WCHAR enabled[2];
    HANDLE thread;
    if (GetEnvironmentVariableW(L"WINE_MACDRV_AX_BRIDGE", enabled, ARRAY_SIZE(enabled)) != 1 || enabled[0] != '1') return;
    thread = CreateThread(NULL, 0, ax_worker, NULL, 0, NULL);
    /* DllMain only creates the thread. Windows starts its body after loader
     * serialization; never wait here and never load COM inside DllMain. */
    if (thread) CloseHandle(thread);
    else WARN("MSAA provider worker could not start\n");
}
