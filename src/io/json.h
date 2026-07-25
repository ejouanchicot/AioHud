// json.h -- tiny dependency-free JSON parser + serializer (C++14, no exceptions).
//
// Built for the AioHUD layout descriptor round-trip (load + save). Handles the JSON
// subset our exporter emits: objects, arrays, strings (UTF-8 passthrough + escapes),
// numbers (double), booleans, null. OBJECT KEY ORDER IS PRESERVED so saves are stable
// and diff-friendly. No throwing: parse() returns false on malformed input.
#pragma once
#include <string>
#include <vector>
#include <utility>
#include <stdlib.h>   // strtod
#include <stdio.h>    // sprintf
#include <string.h>   // strlen

namespace aio { namespace json {

struct Value {
    enum Type { Null, Bool, Num, Str, Arr, Obj };
    Type   type;
    bool   b;
    double num;
    std::string str;
    std::vector<Value> arr;
    std::vector<std::pair<std::string, Value>> obj;   // ordered object

    Value() : type(Null), b(false), num(0) {}

    bool is_null() const { return type == Null; }
    bool is_bool() const { return type == Bool; }
    bool is_num()  const { return type == Num; }
    bool is_str()  const { return type == Str; }
    bool is_arr()  const { return type == Arr; }
    bool is_obj()  const { return type == Obj; }

    bool has(const char* key) const {
        for (size_t i = 0; i < obj.size(); ++i) if (obj[i].first == key) return true;
        return false;
    }
    // object member access -> a static Null Value if absent (never null pointer).
    const Value& operator[](const char* key) const {
        for (size_t i = 0; i < obj.size(); ++i) if (obj[i].first == key) return obj[i].second;
        return null_ref();
    }

    double      as_num (double d = 0.0)        const { return type == Num  ? num : d; }
    bool        as_bool(bool d = false)        const { return type == Bool ? b   : d; }
    std::string as_str (const char* d = "")    const { return type == Str  ? str : std::string(d); }

    static const Value& null_ref() { static Value v; return v; }
};

// ---- builders ----
inline Value mkNull()                  { Value v; return v; }
inline Value mkBool(bool x)            { Value v; v.type = Value::Bool; v.b = x; return v; }
inline Value mkNum(double x)           { Value v; v.type = Value::Num;  v.num = x; return v; }
inline Value mkStr(const std::string& s){ Value v; v.type = Value::Str; v.str = s; return v; }
inline Value mkArr()                   { Value v; v.type = Value::Arr; return v; }
inline Value mkObj()                   { Value v; v.type = Value::Obj; return v; }

// append (or replace) a key in an object value.
inline void set(Value& o, const std::string& k, const Value& val) {
    for (size_t i = 0; i < o.obj.size(); ++i) if (o.obj[i].first == k) { o.obj[i].second = val; return; }
    o.obj.push_back(std::make_pair(k, val));
}

// ======================= parser =======================
namespace detail {

// `depth` bounds the parse_value <-> parse_object/parse_array recursion. layout.json is FILE input : a
// truncated / corrupt / hand-edited file of "[[[[[..." would otherwise recurse until the stack is gone, and
// load_layout runs at startup OUTSIDE any __try -> a silent crash with no diagnostic. 64 is ~8x the deepest
// nesting a real layout uses (root > widgets > widget > config > object).
static const int MAX_DEPTH = 64;
struct P { const char* s; const char* e; bool ok; int depth; };

inline void skip_ws(P& p) {
    while (p.s < p.e) { char c = *p.s; if (c==' '||c=='\t'||c=='\n'||c=='\r') ++p.s; else break; }
}

inline bool parse_string(P& p, std::string& out) {
    if (p.s >= p.e || *p.s != '"') { p.ok = false; return false; }
    ++p.s;
    while (p.s < p.e) {
        char c = *p.s++;
        if (c == '"') return true;
        if (c != '\\') { out += c; continue; }           // raw byte (UTF-8 passes through)
        if (p.s >= p.e) break;
        char e = *p.s++;
        switch (e) {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'u': {                                   // \uXXXX -> UTF-8 (BMP)
                if (p.e - p.s < 4) { p.ok = false; return false; }
                unsigned cp = 0;
                for (int i = 0; i < 4; ++i) {
                    char h = *p.s++; cp <<= 4;
                    if (h>='0'&&h<='9') cp |= (unsigned)(h-'0');
                    else if (h>='a'&&h<='f') cp |= (unsigned)(h-'a'+10);
                    else if (h>='A'&&h<='F') cp |= (unsigned)(h-'A'+10);
                    else { p.ok = false; return false; }
                }
                if (cp < 0x80) out += (char)cp;
                else if (cp < 0x800) { out += (char)(0xC0|(cp>>6)); out += (char)(0x80|(cp&0x3F)); }
                else { out += (char)(0xE0|(cp>>12)); out += (char)(0x80|((cp>>6)&0x3F)); out += (char)(0x80|(cp&0x3F)); }
                break;
            }
            default: out += e; break;
        }
    }
    p.ok = false; return false;                           // unterminated
}

bool parse_value(P& p, Value& out);

inline bool parse_object(P& p, Value& out) {
    out.type = Value::Obj; ++p.s; skip_ws(p);
    if (p.s < p.e && *p.s == '}') { ++p.s; return true; }
    while (p.s < p.e) {
        skip_ws(p);
        std::string key;
        if (!parse_string(p, key)) { p.ok = false; return false; }
        skip_ws(p);
        if (p.s >= p.e || *p.s != ':') { p.ok = false; return false; }
        ++p.s;
        Value val;
        if (!parse_value(p, val)) return false;
        out.obj.push_back(std::make_pair(key, val));
        skip_ws(p);
        if (p.s < p.e && *p.s == ',') { ++p.s; continue; }
        if (p.s < p.e && *p.s == '}') { ++p.s; return true; }
        p.ok = false; return false;
    }
    p.ok = false; return false;
}

inline bool parse_array(P& p, Value& out) {
    out.type = Value::Arr; ++p.s; skip_ws(p);
    if (p.s < p.e && *p.s == ']') { ++p.s; return true; }
    while (p.s < p.e) {
        Value val;
        if (!parse_value(p, val)) return false;
        out.arr.push_back(val);
        skip_ws(p);
        if (p.s < p.e && *p.s == ',') { ++p.s; continue; }
        if (p.s < p.e && *p.s == ']') { ++p.s; return true; }
        p.ok = false; return false;
    }
    p.ok = false; return false;
}

inline bool parse_value(P& p, Value& out) {
    skip_ws(p);
    if (p.s >= p.e) { p.ok = false; return false; }
    char c = *p.s;
    if (c == '"') { out.type = Value::Str; return parse_string(p, out.str); }
    if (c == '{' || c == '[') {                           // the ONE descent point -> bound it here
        if (p.depth >= MAX_DEPTH) { p.ok = false; return false; }
        ++p.depth;
        const bool r = (c == '{') ? parse_object(p, out) : parse_array(p, out);
        --p.depth;
        return r;
    }
    if (c == 't') { if (p.e-p.s>=4 && p.s[1]=='r'&&p.s[2]=='u'&&p.s[3]=='e')             { p.s+=4; out.type=Value::Bool; out.b=true;  return true; } p.ok=false; return false; }
    if (c == 'f') { if (p.e-p.s>=5 && p.s[1]=='a'&&p.s[2]=='l'&&p.s[3]=='s'&&p.s[4]=='e'){ p.s+=5; out.type=Value::Bool; out.b=false; return true; } p.ok=false; return false; }
    if (c == 'n') { if (p.e-p.s>=4 && p.s[1]=='u'&&p.s[2]=='l'&&p.s[3]=='l')             { p.s+=4; out.type=Value::Null; return true; }              p.ok=false; return false; }
    char* end = 0;
    double d = strtod(p.s, &end);                         // string is NUL-terminated (c_str) -> safe
    if (end == p.s) { p.ok = false; return false; }
    p.s = end; out.type = Value::Num; out.num = d;
    return true;
}

// ======================= serializer =======================
inline void esc(const std::string& s, std::string& out) {
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if ((unsigned char)c < 0x20) { char t[8]; sprintf(t, "\\u%04x", (unsigned char)c); out += t; }
                else out += c;
        }
    }
}
inline void indent(std::string& out, int n) { for (int i = 0; i < n; ++i) out += "  "; }
inline void num_str(double d, std::string& out) {
    // The +-1e15 window used to guard ONLY the integer fast path, so an out-of-range double fell through to
    // "%.4f" -- and 1e300 formats to 301 digits + ".0000" = 306 bytes into a char[40]. Every number read from
    // layout.json round-trips through here on the next save, so a corrupt file smashed the stack. Clamp FIRST
    // (this also maps NaN to 0, since every comparison against NaN is false) and size the buffer for the
    // clamped worst case ("-1000000000000000.0000" = 22 chars).
    if (!(d > -1e15 && d < 1e15)) d = (d > 0.0) ? 1e15 : ((d < 0.0) ? -1e15 : 0.0);
    long long ll = (long long)d;
    if ((double)ll == d) { char t[32]; sprintf(t, "%lld", ll); out += t; return; }
    char t[64]; _snprintf(t, sizeof(t), "%.4f", d); t[sizeof(t) - 1] = 0;
    int n = (int)strlen(t);                          // trim trailing zeros (36.5000 -> 36.5)
    while (n > 0 && t[n-1] == '0') t[--n] = 0;
    if (n > 0 && t[n-1] == '.') t[--n] = 0;
    out += t;
}
inline void dump_v(const Value& v, std::string& out, int ind) {
    switch (v.type) {
        case Value::Null: out += "null"; break;
        case Value::Bool: out += v.b ? "true" : "false"; break;
        case Value::Num:  num_str(v.num, out); break;
        case Value::Str:  out += '"'; esc(v.str, out); out += '"'; break;
        case Value::Arr:
            if (v.arr.empty()) { out += "[]"; break; }
            out += "[\n";
            for (size_t i = 0; i < v.arr.size(); ++i) { indent(out, ind+1); dump_v(v.arr[i], out, ind+1); if (i+1 < v.arr.size()) out += ","; out += "\n"; }
            indent(out, ind); out += "]"; break;
        case Value::Obj:
            if (v.obj.empty()) { out += "{}"; break; }
            out += "{\n";
            for (size_t i = 0; i < v.obj.size(); ++i) { indent(out, ind+1); out += '"'; esc(v.obj[i].first, out); out += "\": "; dump_v(v.obj[i].second, out, ind+1); if (i+1 < v.obj.size()) out += ","; out += "\n"; }
            indent(out, ind); out += "}"; break;
    }
}

} // namespace detail

// ---- public API ----
inline bool parse(const std::string& text, Value& out) {
    detail::P p; p.s = text.c_str(); p.e = p.s + text.size(); p.ok = true; p.depth = 0;
    if (!detail::parse_value(p, out)) return false;
    return p.ok;
}
inline std::string dump(const Value& v) { std::string out; detail::dump_v(v, out, 0); out += "\n"; return out; }

}} // namespace aio::json
