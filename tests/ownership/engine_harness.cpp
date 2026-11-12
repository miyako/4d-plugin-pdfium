/*
 * Synthetic 4D engine boundary for ownership regressions.
 *
 * Compiles the real plugin entry point (4DPlugin-pdfium.cpp, included below
 * so its static helpers are reachable) and links the real
 * support/4DPlugin-JSON.cpp, C_TEXT.cpp and the bundled, unmodified
 * 4D Plugin API (4DPluginAPI.c). Only gCall4D is replaced, and PDFium is
 * stubbed (no PDF is ever parsed). Every engine resource (Unistrings,
 * pictures, objects/collections, handles) is a real heap block tracked in a
 * live set, so leaks are detected exactly and a dispose of something the
 * engine no longer owns (double free) is reported by name instead of being
 * freed a second time.
 *
 * Engine semantics modelled (per the SDK contract):
 *  - PA_CreateVariable(eVK_Unistring/eVK_Picture) allocates an empty value.
 *  - PA_GetStringVariable returns a view borrowed from the variable.
 *  - PA_SetObjectProperty/PA_SetCollectionElement deep-copy strings and
 *    pictures and retain objects/collections; the caller still owns its
 *    variable.
 *  - PA_GetObjectProperty returns an owned copy (or a retained
 *    object/collection) that must be cleared; on error the result is left
 *    uninitialised (poisoned here, see g_fail_key) and must not be cleared.
 *  - PA_ExecuteCommandByID returns an owned result (New Collection, 4D.File).
 */

/* Resolved through -I <source-root>, so the same harness tests baseline and fixed trees. */
#include "4DPlugin-pdfium.cpp"

#include "PrivateTypes.h"
#include "EntryPoints.h"

#include <map>
#include <set>
#include <vector>

extern "C" Call4DProcPtr gCall4D;

namespace {

typedef std::basic_string<PA_Unichar> u16;

struct Obj {
    int rc;
    bool coll;
    std::map<u16, PA_Variable> props;
    std::vector<PA_Variable> items;
};

struct Handle { char *data; size_t size; };

std::set<void *> g_strings;
std::set<void *> g_pictures;
std::set<Obj *> g_objects;
std::set<void *> g_handles;
std::vector<std::string> g_violations;
int g_file_objects = 0;

/* PA_GetObjectProperty for this key fails: eb.fError is set and the result
   is left uninitialised. The poison is readable (tests fail by assertion,
   not by crashing) but never engine-owned, so clearing it is counted. */
std::string g_fail_key;
const char kPoisonMark = 0x5A;
int g_poison_clears = 0;
PA_Unichar kPoisonBuf[] = { 'B', 'A', 'D', '!', 0 };
PA_Unichar *const kPoison = kPoisonBuf;

void violation(const std::string &what) {
    fprintf(stderr, "  ENGINE CONTRACT: %s\n", what.c_str());
    g_violations.push_back(what);
}

[[noreturn]] void die(const char *msg) {
    fprintf(stderr, "HARNESS ERROR: %s\n", msg);
    fflush(stderr);
    abort();
}

std::string narrow(const u16 &s) { std::string r; for(PA_Unichar c : s) r.push_back((char)c); return r; }
u16 widen(const std::string &s) { u16 r; for(char c : s) r.push_back((PA_Unichar)(unsigned char)c); return r; }

PA_Unistring new_string(const PA_Unichar *src, size_t len) {
    PA_Unichar *buf = (PA_Unichar *)malloc((len + 1) * sizeof(PA_Unichar));
    if(len) memcpy(buf, src, len * sizeof(PA_Unichar));
    buf[len] = 0;
    g_strings.insert(buf);
    PA_Unistring s;
    s.fLength = (PA_long32)len;
    s.fString = buf;
    s.fReserved1 = 0;
    s.fReserved2 = 0;
    return s;
}

void free_string(PA_Unistring *s) {
    if(!s->fString) return;
    if(s->fString == kPoison) {
        g_poison_clears++;
        fprintf(stderr, "  cleared the uninitialised result of a failed PA_GetObjectProperty\n");
    } else if(!g_strings.erase(s->fString)) {
        violation("double_dispose_string");
    } else {
        free(s->fString);
    }
    s->fString = 0;
    s->fLength = 0;
}

void *new_picture() {
    void *p = malloc(16);
    g_pictures.insert(p);
    return p;
}

void free_picture(void *p) {
    if(!p) return;
    if(!g_pictures.erase(p)) { violation("double_dispose_picture"); return; }
    free(p);
}

Handle *new_handle(size_t size) {
    Handle *h = (Handle *)malloc(sizeof(Handle));
    h->size = size;
    h->data = (char *)malloc(size ? size : 1);
    g_handles.insert(h);
    return h;
}

void free_handle(Handle *h) {
    if(!h) return;
    if(!g_handles.erase(h)) { violation("double_dispose_handle"); return; }
    free(h->data);
    free(h);
}

Handle *live_handle(void *p) {
    if(!g_handles.count(p)) die("used an unknown handle");
    return (Handle *)p;
}

void release_object(Obj *o);

void clear_var(PA_Variable *v) {
    if(v->fFiller == kPoisonMark) {
        g_poison_clears++;
        fprintf(stderr, "  cleared the uninitialised result of a failed PA_GetObjectProperty\n");
        v->fType = eVK_Undefined;
        v->fFiller = 0;
        return;
    }
    switch(v->fType) {
        case eVK_Unistring: free_string(&v->uValue.fString); break;
        case eVK_Picture: free_picture(v->uValue.fPicture); break;
        case eVK_Blob: free_handle((Handle *)v->uValue.fBlob.fHandle); break;
        case eVK_Object: if(v->uValue.fObject) release_object((Obj *)v->uValue.fObject); break;
        case eVK_Collection: if(v->uValue.fCollection) release_object((Obj *)v->uValue.fCollection); break;
        default: break;
    }
    v->fType = eVK_Undefined;
}

Obj *live(void *p) {
    if(!g_objects.count((Obj *)p)) die("used a dead object/collection");
    return (Obj *)p;
}

PA_Variable undefined_var() { PA_Variable v; memset(&v, 0, sizeof(v)); v.fType = eVK_Undefined; return v; }

PA_Variable copy_var(const PA_Variable &src) {
    PA_Variable dst = src;
    switch(src.fType) {
        case eVK_Unistring:
            dst.uValue.fString = new_string(src.uValue.fString.fString, src.uValue.fString.fLength);
            break;
        case eVK_Picture:
            if(!g_pictures.count(PA_Picture(src.uValue.fPicture))) die("stored a dead picture");
            dst.uValue.fPicture = new_picture();
            break;
        case eVK_Object:
            if(src.uValue.fObject) live(src.uValue.fObject)->rc++;
            break;
        case eVK_Collection:
            if(src.uValue.fCollection) live(src.uValue.fCollection)->rc++;
            break;
        default: break;
    }
    return dst;
}

Obj *new_object(bool coll = false) {
    Obj *o = new Obj();
    o->rc = 1;
    o->coll = coll;
    g_objects.insert(o);
    return o;
}

void release_object(Obj *o) {
    if(!g_objects.count(o)) { violation("double_release_object"); return; }
    if(--o->rc == 0) {
        for(auto &kv : o->props) clear_var(&kv.second);
        for(auto &v : o->items) clear_var(&v);
        g_objects.erase(o);
        delete o;
    }
}

u16 key_of(const PA_Unistring &k) { return u16(k.fString, k.fLength); }

PA_Variable object_result(Obj *o) {
    PA_Variable v = undefined_var();
    v.fType = o->coll ? eVK_Collection : eVK_Object;
    if(o->coll) v.uValue.fCollection = (PA_CollectionRef)o; else v.uValue.fObject = (PA_ObjectRef)o;
    return v;
}

void execute_command(EngineBlock *eb) {
    PA_Variable **params = (PA_Variable **)eb->fPtr1;
    PA_Variable *ret = (PA_Variable *)eb->fPtr2;
    switch(eb->fParam1) {
        case 1230: { /* OB Get type */
            Obj *o = live(params[0]->uValue.fObject);
            auto it = o->props.find(key_of(params[1]->uValue.fString));
            PA_long32 kind = it == o->props.end() ? eVK_Undefined : it->second.fType;
            if(kind == eVK_Unistring) kind = eVK_Text;
            ret->fType = eVK_Longint;
            ret->uValue.fLongint = kind;
            break;
        }
        case 1231: { /* OB Is defined */
            Obj *o = live(params[0]->uValue.fObject);
            ret->fType = eVK_Boolean;
            ret->uValue.fBoolean = o->props.count(key_of(params[1]->uValue.fString)) ? 1 : 0;
            break;
        }
        case 1472: *ret = object_result(new_object(true)); break; /* New Collection */
        case 1566: { /* 4D.File(platformPath; fk platform path) */
            if(eb->fParam2 != 2 || params[0]->fType != eVK_Unistring || params[1]->fType != eVK_Longint)
                die("unexpected 4D.File parameters");
            if(params[0]->uValue.fString.fString == kPoison) { violation("file_from_uninitialised_path"); *ret = undefined_var(); break; }
            if(!g_strings.count(params[0]->uValue.fString.fString)) die("4D.File got a dead path string");
            Obj *f = new_object(false);
            g_file_objects++;
            PA_Variable path = undefined_var();
            path.fType = eVK_Unistring;
            path.uValue.fString = new_string(params[0]->uValue.fString.fString, params[0]->uValue.fString.fLength);
            f->props[widen("path")] = path;
            *ret = object_result(f);
            break;
        }
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unexpected PA_ExecuteCommandByID %d", (int)eb->fParam1);
            die(msg);
        }
    }
}

void engine(short selector, EngineBlock *eb) {
    switch(selector) {
        case EX_CREATE_UNISTRING: {
            const PA_Unichar *src = (const PA_Unichar *)eb->fHandle;
            size_t len = 0;
            while(src[len]) len++;
            eb->fUniString1 = new_string(src, len);
            break;
        }
        case EX_DISPOSE_UNISTRING:
            free_string(&eb->fUniString1);
            break;
        case EX_VARIABLE_TO_STRING: {
            PA_Variable *v = (PA_Variable *)eb->fHandle;
            if(v->fType == eVK_Unistring) eb->fUniString1 = v->uValue.fString; /* borrowed */
            break;
        }
        case EX_CREATE_PICTURE:
            eb->fPicture = new_picture();
            break;
        case EX_DISPOSE_PICTURE:
            free_picture(eb->fPicture);
            break;
        case EX_CLEAR_VARIABLE:
            clear_var((PA_Variable *)eb->fPtr1);
            break;
        case EX_YIELD_ABSOLUTE:
            break;
        case EX_SET_OBJ_VALUE: {
            Obj *o = live(eb->fPtr1);
            PA_Variable value = copy_var(*(PA_Variable *)eb->fPtr2);
            if(eb->fShort == eVK_Collection) {
                if(!o->coll || eb->fLongint < 0) die("bad collection set");
                size_t i = (size_t)eb->fLongint;
                if(i >= o->items.size()) o->items.resize(i + 1, undefined_var());
                clear_var(&o->items[i]);
                o->items[i] = value;
            } else {
                u16 k = key_of(eb->fUniString1);
                auto it = o->props.find(k);
                if(it != o->props.end()) clear_var(&it->second);
                o->props[k] = value;
            }
            break;
        }
        case EX_GET_OBJ_VALUE: {
            if(!eb->fPtr1) { violation("property_of_null_object"); *(PA_Variable *)eb->fPtr2 = undefined_var(); eb->fError = -1; break; }
            Obj *o = live(eb->fPtr1);
            PA_Variable *out = (PA_Variable *)eb->fPtr2;
            if(eb->fShort == eVK_Collection) {
                if(!o->coll) die("collection get on an object");
                *out = undefined_var();
                if(eb->fLongint == -1) {
                    out->fType = eVK_Longint;
                    out->uValue.fLongint = (PA_long32)o->items.size();
                } else if(eb->fLongint >= 0 && (size_t)eb->fLongint < o->items.size()) {
                    *out = copy_var(o->items[(size_t)eb->fLongint]);
                }
                break;
            }
            u16 k = key_of(eb->fUniString1);
            if(!g_fail_key.empty() && narrow(k) == g_fail_key) {
                *out = undefined_var();
                out->fFiller = kPoisonMark;
                out->fType = eVK_Unistring;
                out->uValue.fString.fString = kPoison;
                out->uValue.fString.fLength = 4;
                eb->fError = -1;
                break;
            }
            auto it = o->props.find(k);
            *out = it == o->props.end() ? undefined_var() : copy_var(it->second);
            break;
        }
        case EX_EXECUTE_COMMAND_BY_ID:
            execute_command(eb);
            break;
        case EX_HANDLE_MANAGER: {
            switch(eb->fParam1) {
                case 1: eb->fHandle = (PA_Handle)new_handle((size_t)eb->fParam2); break;
                case 2: free_handle((Handle *)eb->fHandle); break;
                case 5: eb->fParam3 = (sLONG_PTR)live_handle(eb->fHandle)->data; break;
                case 6: break;
                default: die("unexpected handle manager op");
            }
            break;
        }
        default: {
            char msg[64];
            snprintf(msg, sizeof(msg), "unmodelled engine selector %d", selector);
            die(msg);
        }
    }
}

/* ---------- PDFium stub: a document of g_pages pages of 20x10 pt ---------- */

std::string g_loaded_path;
int g_pages = 0;
int g_open_docs = 0, g_open_pages = 0, g_open_bitmaps = 0;

struct Bitmap { int w, h; std::vector<unsigned char> px; };

} // namespace

extern "C" {
FPDF_DOCUMENT FPDF_LoadDocument(FPDF_STRING file_path, FPDF_BYTESTRING) {
    g_loaded_path = file_path;
    if(g_pages <= 0) return nullptr;
    g_open_docs++;
    return (FPDF_DOCUMENT)&g_pages;
}
int FPDF_GetPageCount(FPDF_DOCUMENT) { return g_pages; }
FPDF_PAGE FPDF_LoadPage(FPDF_DOCUMENT, int) { g_open_pages++; return (FPDF_PAGE)&g_open_pages; }
float FPDF_GetPageWidthF(FPDF_PAGE) { return 20.0f; }
float FPDF_GetPageHeightF(FPDF_PAGE) { return 10.0f; }
FPDF_BITMAP FPDFBitmap_Create(int w, int h, int) {
    g_open_bitmaps++;
    Bitmap *b = new Bitmap{w, h, std::vector<unsigned char>((size_t)w * h * 4, 0x80)};
    return (FPDF_BITMAP)b;
}
FPDF_BOOL FPDFBitmap_FillRect(FPDF_BITMAP, int, int, int, int, FPDF_DWORD) { return 1; }
void FPDF_RenderPageBitmap(FPDF_BITMAP, FPDF_PAGE, int, int, int, int, int, int) {}
void *FPDFBitmap_GetBuffer(FPDF_BITMAP b) { return ((Bitmap *)b)->px.data(); }
int FPDFBitmap_GetStride(FPDF_BITMAP b) { return ((Bitmap *)b)->w * 4; }
void FPDFBitmap_Destroy(FPDF_BITMAP b) { g_open_bitmaps--; delete (Bitmap *)b; }
void FPDF_ClosePage(FPDF_PAGE) { g_open_pages--; }
void FPDF_CloseDocument(FPDF_DOCUMENT) { g_open_docs--; }
void FPDF_InitLibrary() {}
void FPDF_DestroyLibrary() {}
}

namespace {

/* ---------- test helpers ---------- */

PA_Unistring U(const char *ascii) {
    u16 s = widen(ascii);
    return new_string(s.c_str(), s.size());
}

void put(Obj *o, const char *key, PA_Variable v) {
    PA_Unistring k = U(key);
    PA_SetObjectProperty((PA_ObjectRef)o, &k, v);
    PA_DisposeUnistring(&k);
}

void put_text(Obj *o, const char *key, const char *value) {
    PA_Variable v = undefined_var();
    PA_Unistring u = U(value);
    PA_SetStringVariable(&v, &u);
    put(o, key, v);
    PA_ClearVariable(&v);
}

void put_real(Obj *o, const char *key, double d) {
    PA_Variable v = undefined_var();
    PA_SetRealVariable(&v, d);
    put(o, key, v);
}

Obj *put_collection(Obj *o, const char *key) {
    Obj *c = new_object(true);
    PA_Variable v = object_result(c);
    put(o, key, v);
    PA_ClearVariable(&v);
    return c;
}

bool check(bool ok, const char *what) {
    if(!ok) fprintf(stderr, "  check failed: %s\n", what);
    return ok;
}

bool no_leaks() {
    bool ok = true;
    for(void *p : g_strings) {
        fprintf(stderr, "  LEAK: engine string \"%s\"\n", narrow(u16((PA_Unichar *)p)).c_str());
        ok = false;
    }
    if(!g_pictures.empty()) { fprintf(stderr, "  LEAK: %zu engine picture(s)\n", g_pictures.size()); ok = false; }
    if(!g_objects.empty()) { fprintf(stderr, "  LEAK: %zu engine object(s)/collection(s)\n", g_objects.size()); ok = false; }
    if(!g_handles.empty()) { fprintf(stderr, "  LEAK: %zu engine handle(s)\n", g_handles.size()); ok = false; }
    if(g_poison_clears) { fprintf(stderr, "  %d failed result(s) cleared\n", g_poison_clears); ok = false; }
    if(!g_violations.empty()) ok = false;
    return ok;
}

/* A 4D.File-like input object: { platformPath: <path> }. */
Obj *file_object(const char *platformPath) {
    Obj *f = new_object(false);
    put_text(f, "platformPath", platformPath);
    return f;
}

/* Calls the real `pdf to image` command entry point; returns the result collection. */
Obj *call_pdf_to_image(Obj *file, Obj *options) {
    PA_ObjectRef p1 = (PA_ObjectRef)file, p2 = (PA_ObjectRef)options;
    PA_ObjectRef *args[2] = { &p1, &p2 };
    PA_CollectionRef result = NULL;
    PluginBlock pb;
    pb.fParameters = args;
    pb.fResult = &result;
    pb.fData = NULL;
    PluginMain(1, &pb);
    return (Obj *)result;
}

size_t live_pictures_in(Obj *c) {
    size_t n = 0;
    for(auto &v : c->items) if(v.fType == eVK_Picture && g_pictures.count(PA_Picture(v.uValue.fPicture))) n++;
    return n;
}

bool docs_closed() {
    return check(g_open_docs == 0 && g_open_pages == 0 && g_open_bitmaps == 0, "PDFium handles all closed");
}

size_t count_violations(const char *what) {
    size_t n = 0;
    for(auto &v : g_violations) if(v == what) n++;
    return n;
}

bool string_left(const char *s) {
    for(void *p : g_strings) if(narrow(u16((PA_Unichar *)p)) == s) return true;
    return false;
}

/* ---------- tests ---------- */

/* LIVE: `pdf to image` with a file. p's platformPath string was shared with
   cbparams[0] and disposed twice; the "platformPath" key, the empty string
   from PA_CreateVariable(eVK_Unistring) and the 4D.File result leaked. */
bool defect_cmd_pdf_to_image_file() {
    Obj *file = file_object("/tmp/pdfium-ownership.pdf");
    Obj *images = call_pdf_to_image(file, NULL);
    bool ok = check(images && images->coll && images->items.empty(), "empty collection for unreadable document");
    ok &= check(g_loaded_path == "/tmp/pdfium-ownership.pdf", "resolved path passed to FPDF_LoadDocument");
    ok &= check(g_file_objects == 1, "one 4D.File constructed");
    release_object(file);
    if(images) release_object(images);
    return no_leaks() && ok;
}

bool defect_object_to_path_double_dispose() {
    Obj *file = file_object("/tmp/a.pdf");
    std::string path;
    bool r = file_object_to_path((PA_ObjectRef)file, path);
    release_object(file);
    bool ok = check(r && path == "/tmp/a.pdf", "path resolved");
    ok &= check(count_violations("double_dispose_string") == 0, "platformPath string disposed exactly once");
    return ok;
}

bool defect_object_to_path_key_leak() {
    Obj *file = file_object("/tmp/a.pdf");
    std::string path;
    file_object_to_path((PA_ObjectRef)file, path);
    release_object(file);
    return check(!string_left("platformPath"), "\"platformPath\" key disposed");
}

bool defect_object_to_path_key_leak_non_text() {
    /* platformPath is not text: the helper bails out early. */
    Obj *file = new_object(false);
    put_real(file, "platformPath", 1.0);
    std::string path;
    bool r = file_object_to_path((PA_ObjectRef)file, path);
    release_object(file);
    bool ok = check(!r && path.empty(), "non-text platformPath rejected");
    return no_leaks() && ok;
}

bool defect_object_to_path_empty_arg_leak() {
    Obj *file = file_object("/tmp/a.pdf");
    std::string path;
    file_object_to_path((PA_ObjectRef)file, path);
    release_object(file);
    return check(!string_left(""), "no empty string left over from PA_CreateVariable(eVK_Unistring)");
}

bool defect_object_to_path_folder_leak() {
    Obj *file = file_object("/tmp/a.pdf");
    std::string path;
    file_object_to_path((PA_ObjectRef)file, path);
    release_object(file);
    return check(g_objects.empty(), "4D.File constructor result released");
}

/* LIVE: each rendered page created an empty picture via
   PA_CreateVariable(eVK_Picture) and overwrote it. */
bool defect_cmd_pdf_to_image_page_picture_leak() {
    g_pages = 3;
    Obj *file = file_object("/tmp/three.pdf");
    Obj *images = call_pdf_to_image(file, NULL);
    bool ok = check(images && images->items.size() == 3, "three images");
    ok &= check(images && live_pictures_in(images) == 3, "three live pictures in the result");
    ok &= check(g_pictures.size() == 3, "no picture other than the three results");
    ok &= docs_closed();
    release_object(file);
    if(images) release_object(images);
    return check(g_pictures.empty(), "no picture left after releasing the result") && ok;
}

/* LIVE: ob_get_a("background") never cleared the copied string. */
bool defect_cmd_pdf_to_image_background_option() {
    g_pages = 1;
    Obj *file = file_object("/tmp/one.pdf");
    Obj *options = new_object(false);
    put_text(options, "background", "none");
    put_real(options, "dpi", 144.0);
    Obj *images = call_pdf_to_image(file, options);
    bool ok = check(images && images->items.size() == 1 && live_pictures_in(images) == 1, "one image");
    ok &= docs_closed();
    release_object(file);
    release_object(options);
    if(images) release_object(images);
    return no_leaks() && ok;
}

bool defect_ob_get_a_result_not_cleared() {
    Obj *o = new_object(false);
    put_text(o, "background", "none");
    CUTF8String s;
    bool r = ob_get_a((PA_ObjectRef)o, L"background", &s);
    release_object(o);
    bool ok = check(r && s == (const uint8_t *)"none", "value read");
    return no_leaks() && ok;
}

/* LIVE: dpi given as text: the copy was never cleared. */
bool defect_ob_get_n_wrong_kind_not_cleared() {
    Obj *o = new_object(false);
    put_text(o, "dpi", "high");
    double d = ob_get_n((PA_ObjectRef)o, L"dpi");
    release_object(o);
    bool ok = check(d == 0.0, "wrong kind yields 0");
    return no_leaks() && ok;
}

/* DORMANT (built, no caller). */
bool defect_dormant_ob_get_b_wrong_kind_not_cleared() {
    Obj *o = new_object(false);
    put_text(o, "flag", "yes");
    bool b = ob_get_b((PA_ObjectRef)o, L"flag");
    release_object(o);
    bool ok = check(!b, "wrong kind yields false");
    return no_leaks() && ok;
}

/* DORMANT: the retained collection reference was never released, so the
   collection outlived its owner. */
bool defect_dormant_ob_get_c_reference_not_released() {
    Obj *o = new_object(false);
    Obj *c = put_collection(o, "items");
    PA_CollectionRef r = ob_get_c((PA_ObjectRef)o, L"items");
    bool ok = check(r == (PA_CollectionRef)c, "collection returned");
    release_object(o);
    return no_leaks() && ok;
}

/* DORMANT: ob_set_* overwrote the value from PA_CreateVariable(eVK_Unistring/eVK_Picture). */
bool defect_dormant_ob_set_empty_owner_leaks() {
    Obj *o = new_object(false);
    ob_set_s((PA_ObjectRef)o, "k1", "v1");
    ob_set_s((PA_ObjectRef)o, L"k2", "v2");
    ob_set_a((PA_ObjectRef)o, L"k3", L"v3");
    CUTF16String v4 = widen("v4");
    ob_set_a((PA_ObjectRef)o, L"k4", &v4);
    PA_Picture pic = PA_CreatePicture(NULL, 0);
    // ob_set_p consumes `pic` (shallow set, deep-copying set, then clear), so
    // the caller must not dispose it again.
    ob_set_p((PA_ObjectRef)o, L"k5", pic);
    bool ok = check(o->props.size() == 5, "five properties set");
    release_object(o);
    return no_leaks() && ok;
}

/* HARDENING: a failed PA_GetObjectProperty leaves p uninitialised; it must
   be neither read nor cleared. */
bool hardening_object_to_path_platform_path_error() {
    Obj *file = file_object("/tmp/a.pdf");
    g_fail_key = "platformPath";
    std::string path;
    bool r = file_object_to_path((PA_ObjectRef)file, path);
    g_fail_key.clear();
    release_object(file);
    bool ok = check(!r && path.empty(), "failure reported, no path");
    ok &= check(g_file_objects == 0, "no 4D.File constructed from garbage");
    return no_leaks() && ok;
}

/* HARDENING: on error PA_GetObjectProperty leaves its result uninitialised
   (no pre-init in the SDK); ob_get_a/ob_get_n must not read it. */
bool hardening_ob_get_error_result_untouched() {
    Obj *o = new_object(false);
    put_text(o, "a", "x");
    put_real(o, "n", 1.0);
    g_fail_key = "a";
    CUTF8String s;
    ob_get_a((PA_ObjectRef)o, L"a", &s);
    g_fail_key = "n";
    double d = ob_get_n((PA_ObjectRef)o, L"n");
    g_fail_key.clear();
    release_object(o);
    bool ok = check(s.empty() && d == 0.0, "failed reads yield nothing");
    return no_leaks() && ok;
}

/* ---------- controls (pass on both trees) ---------- */

bool control_ob_get_n_value() {
    Obj *o = new_object(false);
    put_real(o, "dpi", 300.0);
    bool ok = check(ob_is_defined((PA_ObjectRef)o, L"dpi"), "dpi defined");
    ok &= check(!ob_is_defined((PA_ObjectRef)o, L"none"), "missing key undefined");
    ok &= check(ob_get_n((PA_ObjectRef)o, L"dpi") == 300.0, "dpi read");
    release_object(o);
    return no_leaks() && ok;
}

bool control_pdf_to_image_no_file() {
    Obj *images = call_pdf_to_image(NULL, NULL);
    bool ok = check(images && images->coll && images->items.empty(), "empty collection");
    ok &= check(g_loaded_path.empty(), "PDFium not called");
    if(images) release_object(images);
    return no_leaks() && ok;
}

bool control_ob_set_n_b_round_trip() {
    Obj *o = new_object(false);
    ob_set_n((PA_ObjectRef)o, L"n", 2.5);
    ob_set_b((PA_ObjectRef)o, L"b", true);
    bool ok = check(o->props.size() == 2, "two properties set");
    release_object(o);
    return no_leaks() && ok;
}

struct Test { const char *name; bool (*fn)(); };
const Test kTests[] = {
    { "defect_cmd_pdf_to_image_file", defect_cmd_pdf_to_image_file },
    { "defect_object_to_path_double_dispose", defect_object_to_path_double_dispose },
    { "defect_object_to_path_key_leak", defect_object_to_path_key_leak },
    { "defect_object_to_path_key_leak_non_text", defect_object_to_path_key_leak_non_text },
    { "defect_object_to_path_empty_arg_leak", defect_object_to_path_empty_arg_leak },
    { "defect_object_to_path_folder_leak", defect_object_to_path_folder_leak },
    { "defect_cmd_pdf_to_image_page_picture_leak", defect_cmd_pdf_to_image_page_picture_leak },
    { "defect_cmd_pdf_to_image_background_option", defect_cmd_pdf_to_image_background_option },
    { "defect_ob_get_a_result_not_cleared", defect_ob_get_a_result_not_cleared },
    { "defect_ob_get_n_wrong_kind_not_cleared", defect_ob_get_n_wrong_kind_not_cleared },
    { "defect_dormant_ob_get_b_wrong_kind_not_cleared", defect_dormant_ob_get_b_wrong_kind_not_cleared },
    { "defect_dormant_ob_get_c_reference_not_released", defect_dormant_ob_get_c_reference_not_released },
    { "defect_dormant_ob_set_empty_owner_leaks", defect_dormant_ob_set_empty_owner_leaks },
    { "hardening_object_to_path_platform_path_error", hardening_object_to_path_platform_path_error },
    { "hardening_ob_get_error_result_untouched", hardening_ob_get_error_result_untouched },
    { "control_ob_get_n_value", control_ob_get_n_value },
    { "control_pdf_to_image_no_file", control_pdf_to_image_no_file },
    { "control_ob_set_n_b_round_trip", control_ob_set_n_b_round_trip },
};

} // namespace

int main(int argc, char **argv) {
    gCall4D = engine;
    if(argc == 2 && !strcmp(argv[1], "--list")) {
        for(const Test &t : kTests) printf("%s\n", t.name);
        return 0;
    }
    if(argc != 2) { fprintf(stderr, "usage: %s --list | <test>\n", argv[0]); return 2; }
    for(const Test &t : kTests) {
        if(!strcmp(argv[1], t.name)) {
            bool ok = t.fn();
            printf("%s: %s\n", t.name, ok ? "PASS" : "FAIL");
            return ok ? 0 : 1;
        }
    }
    fprintf(stderr, "unknown test %s\n", argv[1]);
    return 2;
}
