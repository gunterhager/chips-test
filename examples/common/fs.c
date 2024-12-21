#include "sokol_fetch.h"
#include "sokol_app.h"
#include "sokol_log.h"
#include "chips/chips_common.h"
#include "fs.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdalign.h>
#include <stdarg.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif
#if defined(WIN32)
#include <windows.h>
#endif

#define FS_EXT_SIZE (16)
#define FS_PATH_SIZE (256)
#define FS_MAX_SIZE (2024 * 1024)

typedef struct {
    char cstr[FS_PATH_SIZE];
    bool clamped;
} fs_path_t;

typedef struct {
    size_t snapshot_index;
    fs_snapshot_load_callback_t callback;
} fs_snapshot_load_context_t;

typedef struct {
    fs_path_t path;
    fs_result_t result;
    uint8_t* ptr;
    size_t size;
    alignas(64) uint8_t buf[FS_MAX_SIZE + 1];
} fs_channel_state_t;

typedef struct {
    bool valid;
    fs_channel_state_t channels[FS_CHANNEL_NUM];
} fs_state_t;
static fs_state_t state;

void fs_init(void) {
    memset(&state, 0, sizeof(state));
    state.valid = true;
    sfetch_setup(&(sfetch_desc_t){
        .max_requests = 128,
        .num_channels = FS_CHANNEL_NUM,
        .num_lanes = 1,
        .logger.func = slog_func,
    });
}

void fs_dowork(void) {
    assert(state.valid);
    sfetch_dowork();
}

static void fs_path_reset(fs_path_t* path) {
    memset(path->cstr, 0, sizeof(path->cstr));
    path->clamped = false;
}

#if defined(__GNUC__)
static void fs_path_printf(fs_path_t* path, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
#endif
static void fs_path_printf(fs_path_t* path, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int res = vsnprintf(path->cstr, sizeof(path->cstr), fmt, args);
    va_end(args);
    path->clamped = res >= (int)sizeof(path->cstr);
}

static void fs_path_extract_extension(fs_path_t* path, char* buf, size_t buf_size) {
    const char* tail = strrchr(path->cstr, '\\');
    if (0 == tail) {
        tail = strrchr(path->cstr, '/');
    }
    if (0 == tail) {
        tail = path->cstr;
    }
    const char* ext = strrchr(tail, '.');
    buf[0] = 0;
    if (ext) {
        size_t i = 0;
        char c = 0;
        while ((c = *++ext) && (i < (buf_size-1))) {
            buf[i] = tolower(c);
            i++;
        }
        buf[i] = 0;
    }
}

// http://web.mit.edu/freebsd/head/contrib/wpa/src/utils/base64.c
static const unsigned char fs_base64_table[65] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static bool fs_base64_decode(fs_channel_state_t* channel, const char* src) {
    int len = (int)strlen(src);

    uint8_t dtable[256];
    memset(dtable, 0x80, sizeof(dtable));
    for (int i = 0; i < (int)sizeof(fs_base64_table)-1; i++) {
        dtable[fs_base64_table[i]] = (uint8_t)i;
    }
    dtable['='] = 0;

    int count = 0;
    for (int i = 0; i < len; i++) {
        if (dtable[(int)src[i]] != 0x80) {
            count++;
        }
    }

    // input length must be multiple of 4
    if ((count == 0) || (count & 3)) {
        return false;
    }

    // output length
    int olen = (count / 4) * 3;
    if (olen >= (int)sizeof(channel->buf)) {
        return false;
    }

    // decode loop
    count = 0;
    int pad = 0;
    uint8_t block[4];
    for (int i = 0; i < len; i++) {
        uint8_t tmp = dtable[(int)src[i]];
        if (tmp == 0x80) {
            continue;
        }
        if (src[i] == '=') {
            pad++;
        }
        block[count] = tmp;
        count++;
        if (count == 4) {
            count = 0;
            channel->buf[channel->size++] = (block[0] << 2) | (block[1] >> 4);
            channel->buf[channel->size++] = (block[1] << 4) | (block[2] >> 2);
            channel->buf[channel->size++] = (block[2] << 6) | block[3];
            if (pad > 0) {
                if (pad <= 2) {
                    channel->size -= pad;
                }
                else {
                    // invalid padding
                    return false;
                }
                break;
            }
        }
    }
    return true;
}

bool fs_ext(fs_channel_t chn, const char* ext) {
    assert(state.valid);
    assert(chn < FS_CHANNEL_NUM);
    char buf[FS_EXT_SIZE];
    fs_path_extract_extension(&state.channels[chn].path, buf, sizeof(buf));
    return 0 == strcmp(ext, buf);
}

const char* fs_filename(fs_channel_t chn) {
    assert(state.valid);
    assert(chn < FS_CHANNEL_NUM);
    return state.channels[chn].path.cstr;
}

void fs_reset(fs_channel_t chn) {
    assert(state.valid);
    assert(chn < FS_CHANNEL_NUM);
    fs_channel_state_t* channel = &state.channels[chn];
    fs_path_reset(&channel->path);
    channel->result = FS_RESULT_IDLE;
    channel->ptr = 0;
    channel->size = 0;
}

bool fs_load_base64(fs_channel_t chn, const char* name, const char* payload) {
    assert(state.valid);
    assert(chn < FS_CHANNEL_NUM);
    fs_reset(chn);
    fs_channel_state_t* channel = &state.channels[chn];
    fs_path_printf(&channel->path, "%s", name);
    if (fs_base64_decode(channel, payload)) {
        channel->result = FS_RESULT_SUCCESS;
        channel->ptr = channel->buf;
        return true;
    }
    else {
        channel->result = FS_RESULT_FAILED;
        return false;
    }
}

static void fs_fetch_callback(const sfetch_response_t* response) {
    assert(state.valid);
    fs_channel_t chn = *(fs_channel_t*)response->user_data;
    assert(chn < FS_CHANNEL_NUM);
    fs_channel_state_t* channel = &state.channels[chn];
    if (response->fetched) {
        channel->result = FS_RESULT_SUCCESS;
        channel->ptr = (uint8_t*)response->data.ptr;
        channel->size = response->data.size;
        assert(channel->size < sizeof(channel->buf));
        // in case it's a text file, zero-terminate the data
        channel->buf[channel->size] = 0;
    }
    else if (response->failed) {
        channel->result = FS_RESULT_FAILED;
    }
}

#if defined(__EMSCRIPTEN__)
static void fs_emsc_dropped_file_callback(const sapp_html5_fetch_response* response) {
    fs_channel_t chn = (fs_channel_t)(uintptr_t)response->user_data;
    assert(chn < FS_CHANNEL_NUM);
    fs_channel_state_t* channel = &state.channels[chn];
    if (response->succeeded) {
        channel->result = FS_RESULT_SUCCESS;
        channel->ptr = (uint8_t*)response->data.ptr;
        channel->size = response->data.size;
        assert(channel->size < sizeof(channel->buf));
        // in case it's a text file, zero-terminate the data
        channel->buf[channel->size] = 0;
    }
    else {
        channel->result = FS_RESULT_FAILED;
    }
}
#endif

void fs_load_file_async(fs_channel_t chn, const char* path) {
    assert(state.valid);
    assert(chn < FS_CHANNEL_NUM);
    fs_reset(chn);
    fs_channel_state_t* channel = &state.channels[chn];
    fs_path_printf(&channel->path, "%s", path);
    channel->result = FS_RESULT_PENDING;
    sfetch_send(&(sfetch_request_t){
        .path = path,
        .channel = chn,
        .callback = fs_fetch_callback,
        .buffer = { .ptr = channel->buf, .size = FS_MAX_SIZE },
        .user_data = { .ptr = &chn, .size = sizeof(chn) },
    });
}

void fs_load_dropped_file_async(fs_channel_t chn) {
    assert(state.valid);
    assert(chn < FS_CHANNEL_NUM);
    fs_reset(chn);
    fs_channel_state_t* channel = &state.channels[chn];
    const char* path = sapp_get_dropped_file_path(0);
    fs_path_printf(&channel->path, "%s", path);
    channel->result = FS_RESULT_PENDING;
    #if defined(__EMSCRIPTEN__)
        sapp_html5_fetch_dropped_file(&(sapp_html5_fetch_request){
            .dropped_file_index = 0,
            .callback = fs_emsc_dropped_file_callback,
            .buffer = { .ptr = channel->buf, .size = FS_MAX_SIZE },
            .user_data = (void*)(intptr_t)chn,
        });
    #else
        fs_load_file_async(chn, path);
    #endif
}

fs_result_t fs_result(fs_channel_t chn) {
    assert(state.valid);
    assert(chn < FS_CHANNEL_NUM);
    return state.channels[chn].result;
}

bool fs_success(fs_channel_t chn) {
    return fs_result(chn) == FS_RESULT_SUCCESS;
}

bool fs_failed(fs_channel_t chn) {
    return fs_result(chn) == FS_RESULT_FAILED;
}

bool fs_pending(fs_channel_t chn) {
    return fs_result(chn) == FS_RESULT_PENDING;
}

chips_range_t fs_data(fs_channel_t chn) {
    assert(state.valid);
    assert(chn < FS_CHANNEL_NUM);
    fs_channel_state_t* channel = &state.channels[chn];
    if (channel->result == FS_RESULT_SUCCESS) {
        return (chips_range_t){ .ptr = channel->ptr, .size = channel->size };
    }
    else {
        return (chips_range_t){0};
    }
}

fs_path_t fs_make_snapshot_path(const char* dir, const char* system_name, size_t snapshot_index) {
    fs_path_t path = {0};
    fs_path_printf(&path, "%s/chips_%s_snapshot_%zu", dir, system_name, snapshot_index);
    return path;
}

#if !defined(__EMSCRIPTEN__)
static void fs_snapshot_fetch_callback(const sfetch_response_t* response) {
    const fs_snapshot_load_context_t* ctx = (fs_snapshot_load_context_t*) response->user_data;
    size_t snapshot_index = ctx->snapshot_index;
    fs_snapshot_load_callback_t callback = ctx->callback;
    if (response->fetched) {
        callback(&(fs_snapshot_response_t){
            .snapshot_index = snapshot_index,
            .result = FS_RESULT_SUCCESS,
            .data = {
                .ptr = (uint8_t*)response->data.ptr,
                .size = response->data.size
            }
        });
    }
    else if (response->failed) {
        callback(&(fs_snapshot_response_t){
            .snapshot_index = snapshot_index,
            .result = FS_RESULT_FAILED,
        });
    }
}
#endif

#if defined (WIN32)
fs_path_t fs_win32_make_snapshot_path_utf8(const char* system_name, size_t snapshot_index) {
    WCHAR wc_tmp_path[1024];
    if (0 == GetTempPathW(sizeof(wc_tmp_path), wc_tmp_path)) {
        return (fs_path_t){0};
    }
    char utf8_tmp_path[2048];
    if (0 == WideCharToMultiByte(CP_UTF8, 0, wc_tmp_path, -1, utf8_tmp_path, sizeof(utf8_tmp_path), NULL, NULL)) {
        return (fs_path_t){0};
    }
    return fs_make_snapshot_path(utf8_tmp_path, system_name, snapshot_index);
}

bool fs_win32_make_snapshot_path_wide(const char* system_name, size_t snapshot_index, WCHAR* out_buf, size_t out_buf_size_bytes) {
    const fs_path_t path = fs_win32_make_snapshot_path_utf8(system_name, snapshot_index);
    if ((path.len == 0) || (path.clamped)) {
        return false;
    }
    if (0 == MultiByteToWideChar(CP_UTF8, 0, path.cstr, -1, out_buf, out_buf_size_bytes)) {
        return false;
    }
    return true;
}

bool fs_save_snapshot(const char* system_name, size_t snapshot_index, chips_range_t data) {
    WCHAR wc_path[1024];
    if (!fs_win32_make_snapshot_path_wide(system_name, snapshot_index, wc_path, sizeof(wc_path)/sizeof(WCHAR))) {
        return false;
    }
    HANDLE fp = CreateFileW(wc_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (fp == INVALID_HANDLE_VALUE) {
        return false;
    }
    if (!WriteFile(fp, data.ptr, data.size, NULL, NULL)) {
        CloseHandle(fp);
        return false;
    }
    CloseHandle(fp);
    return true;
}

bool fs_load_snapshot_async(size_t slot_index, const char* system_name, size_t snapshot_index, fs_snapshot_load_callback_t callback) {
    assert(slot_index < FS_NUM_SLOTS);
    assert(system_name && callback);
    fs_path_t path = fs_win32_make_snapshot_path_utf8(system_name, snapshot_index);
    if ((path.len == 0) || path.clamped) {
        return false;
    }
    fs_snapshot_load_context_t context = {
        .snapshot_index = snapshot_index,
        .callback = callback
    };
    fs_slot_t* slot = &state.slots[slot_index];
    sfetch_send(&(sfetch_request_t){
        .path = path.cstr,
        .channel = slot_index,
        .callback = fs_snapshot_fetch_callback,
        .buffer = { .ptr = slot->buf, .size = FS_MAX_SIZE },
        .user_data = { .ptr = &context, .size = sizeof(context) }
    });
    return true;
}

#elif defined(__EMSCRIPTEN__)

EM_JS(void, fs_js_save_snapshot, (const char* system_name_cstr, int snapshot_index, void* bytes, int num_bytes), {
    const db_name = 'chips';
    const db_store_name = 'store';
    const system_name = UTF8ToString(system_name_cstr);
    console.log('fs_js_save_snapshot: called with', system_name, snapshot_index);
    let open_request;
    try {
        open_request = window.indexedDB.open(db_name, 1);
    } catch (e) {
        console.log('fs_js_save_snapshot: failed to open IndexedDB with ' + e);
        return;
    }
    open_request.onupgradeneeded = () => {
        console.log('fs_js_save_snapshot: creating db');
        const db = open_request.result;
        db.createObjectStore(db_store_name);
    };
    open_request.onsuccess = () => {
        console.log('fs_js_save_snapshot: onsuccess');
        const db = open_request.result;
        const transaction = db.transaction([db_store_name], 'readwrite');
        const file = transaction.objectStore(db_store_name);
        const key = system_name + '_' + snapshot_index;
        const blob = HEAPU8.subarray(bytes, bytes + num_bytes);
        const put_request = file.put(blob, key);
        put_request.onsuccess = () => {
            console.log('fs_js_save_snapshot:', key, 'successfully stored')
        };
        put_request.onerror = () => {
            console.log('fs_js_save_snapshot: FAILED to store', key);
        };
        transaction.onerror = () => {
            console.log('fs_js_save_snapshot: transaction onerror');
        };
    };
    open_request.onerror = () => {
        console.log('fs_js_save_snapshot: open_request onerror');
    }
});

EM_JS(void, fs_js_load_snapshot, (const char* system_name_cstr, int snapshot_index, fs_snapshot_load_context_t* context), {
    const db_name = 'chips';
    const db_store_name = 'store';
    const system_name = UTF8ToString(system_name_cstr);
    let open_request;
    try {
        open_request = window.indexedDB.open(db_name, 1);
    } catch (e) {
        console.log('fs_js_load_snapshot: failed to open IndexedDB with ' + e);
    }
    open_request.onupgradeneeded = () => {
        console.log('fs_js_load_snapshot: creating db');
        const db = open_request.result;
        db.createObjectStore(db_store_name);
    };
    open_request.onsuccess = () => {
        const db = open_request.result;
        let transaction;
        try {
            transaction = db.transaction([db_store_name], 'readwrite');
        } catch (e) {
            console.log('fs_js_load_snapshot: db.transaction failed with', e);
            return;
        };
        const file = transaction.objectStore(db_store_name);
        const key = system_name + '_' + snapshot_index;
        const get_request = file.get(key);
        get_request.onsuccess = () => {
            if (get_request.result !== undefined) {
                const num_bytes = get_request.result.length;
                console.log('fs_js_load_snapshot:', key, 'successfully loaded', num_bytes, 'bytes');
                const ptr = _fs_emsc_alloc(num_bytes);
                HEAPU8.set(get_request.result, ptr);
                _fs_emsc_load_snapshot_callback(context, ptr, num_bytes);
            } else {
                _fs_emsc_load_snapshot_callback(context, 0, 0);
            }
        };
        get_request.onerror = () => {
            console.log('fs_js_load_snapshot: FAILED loading', key);
        };
        transaction.onerror = () => {
            console.log('fs_js_load_snapshot: transaction onerror');
        };
    };
    open_request.onerror = () => {
        console.log('fs_js_load_snapshot: open_request onerror');
    }
});

bool fs_save_snapshot(const char* system_name, size_t snapshot_index, chips_range_t data) {
    assert(system_name && data.ptr && data.size > 0);
    fs_js_save_snapshot(system_name, (int)snapshot_index, data.ptr, data.size);
    return true;
}

EMSCRIPTEN_KEEPALIVE void* fs_emsc_alloc(int size) {
    return malloc((size_t)size);
}

EMSCRIPTEN_KEEPALIVE void fs_emsc_load_snapshot_callback(const fs_snapshot_load_context_t* ctx, void* bytes, int num_bytes) {
    size_t snapshot_index = ctx->snapshot_index;
    fs_snapshot_load_callback_t callback = ctx->callback;
    if (bytes) {
        callback(&(fs_snapshot_response_t){
            .snapshot_index = snapshot_index,
            .result = FS_RESULT_SUCCESS,
            .data = {
                .ptr = bytes,
                .size = (size_t)num_bytes
            }
        });
        free(bytes);
    }
    else {
        callback(&(fs_snapshot_response_t){
            .snapshot_index = snapshot_index,
            .result = FS_RESULT_FAILED,
        });
    }
    free((void*)ctx);
}

bool fs_load_snapshot_async(const char* system_name, size_t snapshot_index, fs_snapshot_load_callback_t callback) {
    assert(slot_index < FS_NUM_SLOTS);
    assert(system_name && callback);

    // allocate a 'context' struct which needs to be tunneled through JS to the fs_emsc_load_snapshot_callback() function
    fs_snapshot_load_context_t* context = calloc(1, sizeof(fs_snapshot_load_context_t));
    context->snapshot_index = snapshot_index;
    context->callback = callback;

    fs_js_load_snapshot(system_name, (int)snapshot_index, context);

    return true;
}
#else // Apple or Linux

bool fs_save_snapshot(const char* system_name, size_t snapshot_index, chips_range_t data) {
    assert(system_name && data.ptr && data.size > 0);
    fs_path_t path = fs_make_snapshot_path("/tmp", system_name, snapshot_index);
    if (path.clamped) {
        return false;
    }
    FILE* fp = fopen(path.cstr, "wb");
    if (fp) {
        fwrite(data.ptr, data.size, 1, fp);
        fclose(fp);
        return true;
    }
    else {
        return false;
    }
}

bool fs_load_snapshot_async(const char* system_name, size_t snapshot_index, fs_snapshot_load_callback_t callback) {
    assert(system_name && callback);
    fs_path_t path = fs_make_snapshot_path("/tmp", system_name, snapshot_index);
    if (path.clamped) {
        return false;
    }
    fs_snapshot_load_context_t context = {
        .snapshot_index = snapshot_index,
        .callback = callback
    };
    const fs_channel_t chn = FS_CHANNEL_SNAPSHOTS;
    fs_channel_state_t* channel = &state.channels[chn];
    sfetch_send(&(sfetch_request_t){
        .path = path.cstr,
        .channel = (int)chn,
        .callback = fs_snapshot_fetch_callback,
        .buffer = { .ptr = channel->buf, .size = FS_MAX_SIZE },
        .user_data = { .ptr = &context, .size = sizeof(context) }
    });
    return true;
}
#endif
