/*
 * QuickJS libuv bindings
 *
 * Copyright (c) 2019-present Saúl Ibarra Corretgé <s@saghul.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "curl-utils.h"
#include "private.h"
#include "quv.h"

#include <string.h>


#ifdef QUV_HAVE_CURL

JSModuleDef *quv__load_http(JSContext *ctx, const char *url) {
    JSModuleDef *m;
    DynBuf dbuf;
    CURLcode res;

    dbuf_init(&dbuf);

    res = quv_curl_load_http(&dbuf, url);

    if (res != CURLE_OK) {
        m = NULL;
        goto end;
    }

    /* compile the module */
    JSValue func_val = JS_Eval(ctx, (char *) dbuf.buf, dbuf.size, url, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    if (JS_IsException(func_val)) {
        JS_FreeValue(ctx, func_val);
        m = NULL;
        goto end;
    }

    /* XXX: could propagate the exception */
    js_module_set_import_meta(ctx, func_val, FALSE, FALSE);
    /* the module is already referenced, so we must free it */
    m = JS_VALUE_GET_PTR(func_val);
    JS_FreeValue(ctx, func_val);

end:
    /* free the memory we allocated */
    dbuf_free(&dbuf);

    return m;
}

#endif

JSModuleDef *quv_module_loader(JSContext *ctx, const char *module_name, void *opaque) {
    static const char http[] = "http://";
    static const char https[] = "https://";

    JSModuleDef *m;
    JSValue func_val;
    int r;
    DynBuf dbuf;

    if (strncmp(http, module_name, strlen(http)) == 0 || strncmp(https, module_name, strlen(https)) == 0) {
#ifdef QUV_HAVE_CURL
        return quv__load_http(ctx, module_name);
#else
        JS_ThrowReferenceError(ctx, "could not load '%s', libcurl support not enabled", module_name);
        return NULL;
#endif
    }

    dbuf_init(&dbuf);
    r = quv__load_file(ctx, &dbuf, module_name);
    if (r != 0) {
        dbuf_free(&dbuf);
        JS_ThrowReferenceError(ctx, "could not load '%s'", module_name);
        return NULL;
    }

    /* compile the module */
    func_val = JS_Eval(ctx, (char *) dbuf.buf, dbuf.size, module_name, JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY);
    dbuf_free(&dbuf);
    if (JS_IsException(func_val)) {
        JS_FreeValue(ctx, func_val);
        return NULL;
    }

    /* XXX: could propagate the exception */
    js_module_set_import_meta(ctx, func_val, TRUE, FALSE);
    /* the module is already referenced, so we must free it */
    m = JS_VALUE_GET_PTR(func_val);
    JS_FreeValue(ctx, func_val);

    return m;
}

int js_module_set_import_meta(JSContext *ctx, JSValueConst func_val, JS_BOOL use_realpath, JS_BOOL is_main) {
    JSModuleDef *m;
    char buf[PATH_MAX + 16];
    int r;
    JSValue meta_obj;
    JSAtom module_name_atom;
    const char *module_name;

    CHECK_EQ(JS_VALUE_GET_TAG(func_val), JS_TAG_MODULE);
    m = JS_VALUE_GET_PTR(func_val);

    module_name_atom = JS_GetModuleName(ctx, m);
    module_name = JS_AtomToCString(ctx, module_name_atom);
#if 0
    fprintf(stdout, "XXX loaded module: %s\n", module_name);
#endif
    JS_FreeAtom(ctx, module_name_atom);
    if (!module_name)
        return -1;
    if (!strchr(module_name, ':')) {
        pstrcpy(buf, sizeof(buf), "file://");
        /* realpath() cannot be used with modules compiled with qjsc
           because the corresponding module source code is not
           necessarily present */
        if (use_realpath) {
            uv_fs_t req;
            r = uv_fs_realpath(NULL, &req, module_name, NULL);
            if (r != 0) {
                uv_fs_req_cleanup(&req);
                JS_ThrowTypeError(ctx, "realpath failure");
                JS_FreeCString(ctx, module_name);
                return -1;
            }
            pstrcat(buf, sizeof(buf), req.ptr);
            uv_fs_req_cleanup(&req);
        } else {
            pstrcat(buf, sizeof(buf), module_name);
        }
    } else {
        pstrcpy(buf, sizeof(buf), module_name);
    }
    JS_FreeCString(ctx, module_name);

    meta_obj = JS_GetImportMeta(ctx, m);
    if (JS_IsException(meta_obj))
        return -1;
    JS_DefinePropertyValueStr(ctx, meta_obj, "url", JS_NewString(ctx, buf), JS_PROP_C_W_E);
    JS_DefinePropertyValueStr(ctx, meta_obj, "main", JS_NewBool(ctx, is_main), JS_PROP_C_W_E);
    JS_FreeValue(ctx, meta_obj);
    return 0;
}